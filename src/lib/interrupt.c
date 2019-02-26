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

struct irq_desc *interrupt_get_parent(uint32_t irq)
{
	struct irq_cascade_desc *cascade;
	struct irq_desc *parent = NULL;
	unsigned long flags;

	if (irq < PLATFORM_IRQ_CHILDREN)
		return NULL;

	spin_lock_irq(&cascade_lock, flags);

	dcache_invalidate_region(&cascade_root, sizeof(cascade_root));

	for (cascade = cascade_root.list; cascade; cascade = cascade->next)
		if (irq >= cascade->irq_base &&
		    irq < cascade->irq_base + PLATFORM_IRQ_CHILDREN) {
			parent = &cascade->desc;
			break;
		}

	spin_unlock_irq(&cascade_lock, flags);

	return parent;
}

void interrupt_init(void)
{
	cascade_root.last_irq = PLATFORM_IRQ_CHILDREN - 1;
	dcache_writeback_region(&cascade_root, sizeof(cascade_root));
	spinlock_init(&cascade_lock);
}

static int irq_register_child(struct irq_desc *parent, int irq, int unmask,
			      void (*handler)(void *arg), void *arg)
{
	int hw_irq, ret = 0;
	struct irq_desc *child;
	struct irq_cascade_desc *cascade;
	struct list_item *list, *head;

	if (parent == NULL)
		return -EINVAL;

	cascade = container_of(parent, struct irq_cascade_desc, desc);

	spin_lock(&cascade->lock);

	hw_irq = irq - cascade->irq_base;

	if (hw_irq < 0 || cascade->irq_base + PLATFORM_IRQ_CHILDREN <= irq) {
		ret = -EINVAL;
		goto finish;
	}

	head = &cascade->child[hw_irq].list;

	list_for_item (list, head) {
		child = container_of(list, struct irq_desc, irq_list);

		if (child->handler_arg == arg) {
			trace_error(TRACE_CLASS_IRQ,
				    "error: IRQ 0x%x handler argument re-used!",
				    irq);
			ret = -EEXIST;
			goto finish;
		}

		if (child->unmask != unmask) {
			trace_error(TRACE_CLASS_IRQ,
				    "error: IRQ 0x%x flags differ!", irq);
			ret = -EINVAL;
			goto finish;
		}
	}

	/* init child from run-time, may be registered and unregistered
	 * many times at run-time
	 */
	child = rzalloc(RZONE_SYS_RUNTIME | RZONE_FLAG_UNCACHED,
			SOF_MEM_CAPS_RAM, sizeof(struct irq_desc));
	if (!child) {
		ret = -ENOMEM;
		goto finish;
	}

	child->handler = handler;
	child->handler_arg = arg;
	child->unmask = unmask;
	child->irq = irq;

	list_item_append(&child->irq_list, head);

	/* do we need to register parent ? */
	if (!cascade->num_children)
		ret = interrupt_register(parent->irq, IRQ_AUTO_UNMASK,
					 parent->handler, parent);

	/* increment number of children */
	if (!ret)
		cascade->num_children++;

finish:
	spin_unlock(&cascade->lock);
	return ret;
}

static void irq_unregister_child(struct irq_desc *parent, int irq,
				 const void *arg)
{
	struct irq_desc *child;
	struct irq_cascade_desc *cascade = container_of(parent,
						struct irq_cascade_desc, desc);
	int hw_irq = irq - cascade->irq_base;
	struct list_item *list, *head = &cascade->child[hw_irq].list;

	spin_lock(&cascade->lock);

	list_for_item (list, head) {
		child = container_of(list, struct irq_desc, irq_list);

		if (child->handler_arg == arg) {
			list_item_del(&child->irq_list);
			cascade->num_children--;
			rfree(child);

			/*
			 * unregister the root interrupt if this l2 is the last
			 * registered child.
			 */
			if (!cascade->num_children)
				interrupt_unregister(parent->irq, parent);

			break;
		}
	}

	spin_unlock(&cascade->lock);
}

static uint32_t irq_enable_child(struct irq_desc *parent, int irq, void *arg)
{
	struct irq_cascade_desc *cascade = container_of(parent,
						struct irq_cascade_desc, desc);
	struct irq_child *child;
	unsigned int cpu = cpu_get_id();
	struct list_item *list;

	spin_lock(&cascade->lock);

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
			interrupt_enable(parent->irq, parent->handler_arg);

		/* enable the child interrupt */
		interrupt_unmask(irq, cpu);
	}

	spin_unlock(&cascade->lock);

	return 0;
}

static uint32_t irq_disable_child(struct irq_desc *parent, int irq, void *arg)
{
	struct irq_cascade_desc *cascade = container_of(parent,
						struct irq_cascade_desc, desc);
	struct irq_child *child;
	unsigned int cpu = cpu_get_id();
	struct list_item *list;

	spin_lock(&cascade->lock);

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
			interrupt_disable(parent->irq, parent->handler_arg);
	}

	spin_unlock(&cascade->lock);

	return 0;
}

int interrupt_register(uint32_t irq, int unmask, void (*handler)(void *arg),
		       void *arg)
{
	struct irq_desc *parent;

	/* no parent means we are registering DSP internal IRQ */
	parent = interrupt_get_parent(irq);
	if (parent == NULL)
		return arch_interrupt_register(irq, handler, arg);
	else
		return irq_register_child(parent, irq, unmask, handler, arg);
}

void interrupt_unregister(uint32_t irq, const void *arg)
{
	struct irq_desc *parent;

	/* no parent means we are unregistering DSP internal IRQ */
	parent = interrupt_get_parent(irq);
	if (parent == NULL)
		arch_interrupt_unregister(irq);
	else
		irq_unregister_child(parent, irq, arg);
}

uint32_t interrupt_enable(uint32_t irq, void *arg)
{
	struct irq_desc *parent;

	/* no parent means we are enabling DSP internal IRQ */
	parent = interrupt_get_parent(irq);
	if (parent == NULL)
		return arch_interrupt_enable_mask(1 << irq);
	else
		return irq_enable_child(parent, irq, arg);
}

uint32_t interrupt_disable(uint32_t irq, void *arg)
{
	struct irq_desc *parent;

	/* no parent means we are disabling DSP internal IRQ */
	parent = interrupt_get_parent(irq);
	if (parent == NULL)
		return arch_interrupt_disable_mask(1 << irq);
	else
		return irq_disable_child(parent, irq, arg);
}
