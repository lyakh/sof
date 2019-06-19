// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2017 Intel Corporation. All rights reserved.
//
// Author: Keyon Jie <yang.jie@linux.intel.com>
//         Liam Girdwood <liam.r.girdwood@linux.intel.com>

#include <sof/interrupt.h>
#include <sof/alloc.h>
#include <sof/lock.h>
#include <sof/list.h>
#include <arch/interrupt.h>
#include <platform/interrupt.h>
#include <stdint.h>
#include <stdlib.h>

static spinlock_t cascade_lock;
static union {
	struct {
		struct irq_cascade_desc *list;
		int last_irq;
	} __attribute__((aligned(PLATFORM_DCACHE_ALIGN)));
	uint8_t bytes[PLATFORM_DCACHE_ALIGN];
} cascade_root;

static int interrupt_register_internal(uint32_t irq, int unmask,
				       void (*handler)(void *arg),
				       void *arg, struct irq_desc *desc);
static void interrupt_unregister_internal(uint32_t irq, const void *arg,
					  struct irq_desc *desc);

int interrupt_cascade_register(const struct irq_cascade_tmpl *tmpl)
{
	struct irq_cascade_desc **cascade;
	unsigned long flags;
	unsigned int i;
	int ret;

	if (!tmpl->name || !tmpl->ops)
		return -EINVAL;

	spin_lock_irq(&cascade_lock, flags);

	dcache_invalidate_region(&cascade_root, sizeof(cascade_root));

	for (cascade = &cascade_root.list; *cascade;
	     cascade = &(*cascade)->next) {
		if (!rstrcmp((*cascade)->name, tmpl->name)) {
			ret = -EEXIST;
			trace_error(TRACE_CLASS_IRQ,
				    "error: cascading IRQ controller name duplication!");
			goto unlock;
		}
	}

	*cascade = rmalloc(RZONE_SYS | RZONE_FLAG_UNCACHED, SOF_MEM_CAPS_RAM,
			   sizeof(**cascade));

	spinlock_init(&(*cascade)->lock);
	for (i = 0; i < PLATFORM_IRQ_CHILDREN; i++)
		list_init(&(*cascade)->child[i].list);

	(*cascade)->name = tmpl->name;
	(*cascade)->ops = tmpl->ops;
	(*cascade)->desc.irq = tmpl->irq;
	(*cascade)->desc.handler = tmpl->handler;
	(*cascade)->desc.handler_arg = &(*cascade)->desc;
	(*cascade)->irq_base = cascade_root.last_irq + 1;

	cascade_root.last_irq += ARRAY_SIZE((*cascade)->child);
	dcache_writeback_region(&cascade_root, sizeof(cascade_root));

	ret = 0;

unlock:
	spin_unlock_irq(&cascade_lock, flags);

	return ret;
}

int interrupt_get_irq(unsigned int irq, const char *name)
{
	struct irq_cascade_desc *cascade;
	unsigned long flags;
	int ret = -ENODEV;

	if (!name || name[0] == '\0')
		return irq;

	/* If a name is specified, irq must be <= PLATFORM_IRQ_CHILDREN */
	if (irq >= PLATFORM_IRQ_CHILDREN) {
		trace_error(TRACE_CLASS_IRQ,
			    "error: IRQ %d invalid as a child interrupt!");
		return -EINVAL;
	}

	spin_lock_irq(&cascade_lock, flags);

	dcache_invalidate_region(&cascade_root, sizeof(cascade_root));

	for (cascade = cascade_root.list; cascade; cascade = cascade->next)
		/* .name is non-volatile */
		if (!rstrcmp(name, cascade->name)) {
			ret = cascade->irq_base + irq;
			break;
		}

	spin_unlock_irq(&cascade_lock, flags);

	return ret;
}

struct irq_cascade_desc *interrupt_get_parent(uint32_t irq)
{
	struct irq_cascade_desc *cascade, *c = NULL;
	unsigned long flags;

	if (irq < PLATFORM_IRQ_CHILDREN)
		return NULL;

	spin_lock_irq(&cascade_lock, flags);

	dcache_invalidate_region(&cascade_root, sizeof(cascade_root));

	for (cascade = cascade_root.list; cascade; cascade = cascade->next)
		if (irq >= cascade->irq_base &&
		    irq < cascade->irq_base + PLATFORM_IRQ_CHILDREN) {
			c = cascade;
			break;
		}

	spin_unlock_irq(&cascade_lock, flags);

	return c;
}

void interrupt_init(void)
{
	cascade_root.last_irq = PLATFORM_IRQ_CHILDREN - 1;
	dcache_writeback_region(&cascade_root, sizeof(cascade_root));
	spinlock_init(&cascade_lock);
}

static int irq_register_child(struct irq_cascade_desc *cascade, int irq,
			      int unmask, void (*handler)(void *arg), void *arg,
			      struct irq_desc *desc)
{
	int hw_irq, ret = 0;
	struct irq_desc *child, *parent = &cascade->desc;
	struct list_item *list, *head;

	hw_irq = irq - cascade->irq_base;

	if (hw_irq < 0 || cascade->irq_base + PLATFORM_IRQ_CHILDREN <= irq)
		return -EINVAL;

	head = &cascade->child[hw_irq].list;

	list_for_item (list, head) {
		child = container_of(list, struct irq_desc, irq_list);

		if (child->handler_arg == arg) {
			trace_error(TRACE_CLASS_IRQ,
				    "error: IRQ 0x%x handler argument re-used!",
				    irq);
			return -EEXIST;
		}

		if (child->unmask != unmask) {
			trace_error(TRACE_CLASS_IRQ,
				    "error: IRQ 0x%x flags differ!", irq);
			return -EINVAL;
		}
	}

	if (!desc) {
		/* init child from run-time, may be registered and unregistered
		 * many times at run-time
		 */
		child = rzalloc(RZONE_SYS_RUNTIME | RZONE_FLAG_UNCACHED,
				SOF_MEM_CAPS_RAM, sizeof(struct irq_desc));
		if (!child)
			return -ENOMEM;

		child->handler = handler;
		child->handler_arg = arg;
		child->irq = irq;
	} else {
		child = desc;
		child->cpu_mask = 0;
	}

	child->unmask = unmask;

	list_item_append(&child->irq_list, head);

	/* do we need to register parent ? */
	if (!cascade->num_children)
		ret = interrupt_register_internal(parent->irq, IRQ_AUTO_UNMASK,
					parent->handler, parent, parent);

	/* increment number of children */
	if (!ret)
		cascade->num_children++;

	return ret;
}

static void irq_unregister_child(struct irq_cascade_desc *cascade, int irq,
				 const void *arg, struct irq_desc *desc)
{
	struct irq_desc *child, *parent = &cascade->desc;
	int hw_irq = irq - cascade->irq_base;
	struct list_item *list, *head = &cascade->child[hw_irq].list;

	list_for_item (list, head) {
		child = container_of(list, struct irq_desc, irq_list);

		if (child->handler_arg == arg) {
			list_item_del(&child->irq_list);
			cascade->num_children--;
			if (!desc)
				rfree(child);

			/*
			 * unregister the root interrupt if this l2 is the last
			 * registered child.
			 */
			if (!cascade->num_children)
				interrupt_unregister_internal(parent->irq,
							      parent, parent);

			break;
		}
	}
}

static uint32_t irq_enable_child(struct irq_cascade_desc *cascade, int irq,
				 void *arg)
{
	struct irq_child *child;
	unsigned int cpu = cpu_get_id();
	struct list_item *list;
	unsigned long flags;

	/*
	 * Locking is child to parent: when called recursively we are already
	 * holding the child's lock and then also taking the parent's lock. The
	 * same holds for the interrupt_(un)register() paths.
	 */
	spin_lock_irq(&cascade->lock, flags);

	child = cascade->child + irq - cascade->irq_base;

	list_for_item (list, &child->list) {
		struct irq_desc *d = container_of(list,
						struct irq_desc, irq_list);

		if (d->handler_arg == arg) {
			d->cpu_mask |= 1 << cpu;
			break;
		}
	}

	if (!child->enable_count++) {
		/* enable the parent interrupt */
		if (!cascade->enable_count++)
			interrupt_enable(cascade->desc.irq,
					 cascade->desc.handler_arg);

		/* enable the child interrupt */
		interrupt_unmask(irq, cpu);
	}

	spin_unlock_irq(&cascade->lock, flags);

	return 0;
}

static uint32_t irq_disable_child(struct irq_cascade_desc *cascade, int irq,
				  void *arg)
{
	struct irq_child *child;
	unsigned int cpu = cpu_get_id();
	struct list_item *list;
	unsigned long flags;

	spin_lock_irq(&cascade->lock, flags);

	child = cascade->child + irq - cascade->irq_base;

	list_for_item (list, &child->list) {
		struct irq_desc *d = container_of(list,
						struct irq_desc, irq_list);

		/* .handler_arg is non-volatile */
		if (d->handler_arg == arg) {
			d->cpu_mask &= ~(1 << cpu);
			break;
		}
	}

	if (!child->enable_count) {
		trace_error(TRACE_CLASS_IRQ,
			    "error: IRQ %x unbalanced interrupt_disable()",
			    irq);
	} else if (!--child->enable_count) {
		/* disable the child interrupt */
		interrupt_mask(irq, cpu);

		/* disable the parent interrupt */
		if (!--cascade->enable_count)
			interrupt_disable(cascade->desc.irq,
					  cascade->desc.handler_arg);
	}

	spin_unlock_irq(&cascade->lock, flags);

	return 0;
}

int interrupt_register(uint32_t irq, int unmask, void (*handler)(void *arg),
		       void *arg)
{
	return interrupt_register_internal(irq, unmask, handler, arg, NULL);
}

static int interrupt_register_internal(uint32_t irq, int unmask,
				       void (*handler)(void *arg),
				       void *arg, struct irq_desc *desc)
{
	struct irq_cascade_desc *cascade;
	/* Avoid a bogus compiler warning */
	unsigned long flags = 0;
	int ret;

	/* no parent means we are registering DSP internal IRQ */
	cascade = interrupt_get_parent(irq);
	if (!cascade)
		return arch_interrupt_register(irq, handler, arg);

	spin_lock_irq(&cascade->lock, flags);
	ret = irq_register_child(cascade, irq, unmask, handler, arg, desc);
	spin_unlock_irq(&cascade->lock, flags);

	return ret;
}

void interrupt_unregister(uint32_t irq, const void *arg)
{
	interrupt_unregister_internal(irq, arg, NULL);
}

static void interrupt_unregister_internal(uint32_t irq, const void *arg,
					  struct irq_desc *desc)
{
	struct irq_cascade_desc *cascade;
	/* Avoid a bogus compiler warning */
	unsigned long flags = 0;

	/* no parent means we are unregistering DSP internal IRQ */
	cascade = interrupt_get_parent(irq);
	if (!cascade) {
		arch_interrupt_unregister(irq);
		return;
	}

	spin_lock_irq(&cascade->lock, flags);
	irq_unregister_child(cascade, irq, arg, desc);
	spin_unlock_irq(&cascade->lock, flags);
}

uint32_t interrupt_enable(uint32_t irq, void *arg)
{
	struct irq_cascade_desc *cascade;

	/* no parent means we are enabling DSP internal IRQ */
	cascade = interrupt_get_parent(irq);
	if (cascade)
		return irq_enable_child(cascade, irq, arg);

	return arch_interrupt_enable_mask(1 << irq);
}

uint32_t interrupt_disable(uint32_t irq, void *arg)
{
	struct irq_cascade_desc *cascade;

	/* no parent means we are disabling DSP internal IRQ */
	cascade = interrupt_get_parent(irq);
	if (cascade)
		return irq_disable_child(cascade, irq, arg);

	return arch_interrupt_disable_mask(1 << irq);
}
