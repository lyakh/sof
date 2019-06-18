// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2017 Intel Corporation. All rights reserved.
//
// Author: Keyon Jie <yang.jie@linux.intel.com>
//         Liam Girdwood <liam.r.girdwood@linux.intel.com>

#include <sof/interrupt.h>
#include <sof/interrupt-map.h>
#include <sof/alloc.h>
#include <arch/interrupt.h>
#include <platform/interrupt.h>
#include <stdint.h>
#include <stdlib.h>

static int irq_register_child(struct irq_desc *parent, int irq, int unmask,
			      void (*handler)(void *arg), void *arg)
{
	int ret = 0;
	struct irq_desc *child;
	struct irq_cascade_desc *cascade;

	if (parent == NULL)
		return -EINVAL;

	cascade = container_of(parent, struct irq_cascade_desc, desc);

	spin_lock(&cascade->lock);

	/* init child from run-time, may be registered and unregistered
	 * many times at run-time
	 */
	child = rzalloc(RZONE_SYS_RUNTIME, SOF_MEM_CAPS_RAM,
			sizeof(struct irq_desc));
	if (!child) {
		ret = -ENOMEM;
		goto finish;
	}

	child->enabled_count = 0;
	child->handler = handler;
	child->handler_arg = arg;
	child->id = SOF_IRQ_ID(irq);
	child->unmask = unmask;

	list_item_append(&child->irq_list, &cascade->child[SOF_IRQ_BIT(irq)]);

	/* do we need to register parent ? */
	if (cascade->num_children == 0) {
		ret = arch_interrupt_register(parent->irq,
					      parent->handler, parent);
	}

	/* increment number of children */
	cascade->num_children++;

finish:
	spin_unlock(&cascade->lock);
	return ret;
}

static void irq_unregister_child(struct irq_desc *parent, int irq)
{
	struct irq_desc *child;
	struct list_item *clist;
	struct list_item *tlist;
	struct irq_cascade_desc *cascade = container_of(parent,
						struct irq_cascade_desc, desc);

	spin_lock(&cascade->lock);

	/* does child already exist ? */
	if (list_is_empty(&cascade->child[SOF_IRQ_BIT(irq)]))
		goto finish;

	list_for_item_safe(clist, tlist, &cascade->child[SOF_IRQ_BIT(irq)]) {
		child = container_of(clist, struct irq_desc, irq_list);

		if (SOF_IRQ_ID(irq) == child->id) {
			list_item_del(&child->irq_list);
			cascade->num_children--;
			rfree(child);
		}
	}

	/*
	 * unregister the root interrupt if the this l2 is
	 * the last registered one.
	 */
	if (cascade->num_children == 0)
		arch_interrupt_unregister(parent->irq);

finish:
	spin_unlock(&cascade->lock);
}

static uint32_t irq_enable_child(struct irq_desc *parent, int irq)
{
	struct irq_desc *child;
	struct list_item *clist;
	struct irq_cascade_desc *cascade = container_of(parent,
						struct irq_cascade_desc, desc);

	spin_lock(&cascade->lock);

	/* enable the parent interrupt */
	if (parent->enabled_count == 0)
		arch_interrupt_enable_mask(1 << SOF_IRQ_NUMBER(irq));

	list_for_item(clist, &cascade->child[SOF_IRQ_BIT(irq)]) {
		child = container_of(clist, struct irq_desc, irq_list);

		if ((SOF_IRQ_ID(irq) == child->id) &&
		    !child->enabled_count) {
			child->enabled_count = 1;
			parent->enabled_count++;

			/* enable the child interrupt */
			platform_interrupt_unmask(irq, 0);
		}
	}

	spin_unlock(&cascade->lock);
	return 0;

}

static uint32_t irq_disable_child(struct irq_desc *parent, int irq)
{
	struct irq_desc *child;
	struct list_item *clist;
	struct irq_cascade_desc *cascade = container_of(parent,
						struct irq_cascade_desc, desc);

	spin_lock(&cascade->lock);

	list_for_item(clist, &cascade->child[SOF_IRQ_BIT(irq)]) {
		child = container_of(clist, struct irq_desc, irq_list);

		if ((SOF_IRQ_ID(irq) == child->id) &&
		    child->enabled_count) {
			child->enabled_count = 0;
			parent->enabled_count--;

			/* disable the child interrupt */
			platform_interrupt_mask(irq, 0);
		}
	}

	if (parent->enabled_count == 0)
		arch_interrupt_disable_mask(1 << SOF_IRQ_NUMBER(irq));

	spin_unlock(&cascade->lock);
	return 0;
}

int interrupt_register(uint32_t irq, int unmask, void (*handler)(void *arg),
		       void *arg)
{
	struct irq_desc *parent;

	/* no parent means we are registering DSP internal IRQ */
	parent = platform_irq_get_parent(irq);
	if (parent == NULL)
		return arch_interrupt_register(irq, handler, arg);
	else
		return irq_register_child(parent, irq, unmask, handler, arg);
}

void interrupt_unregister(uint32_t irq)
{
	struct irq_desc *parent;

	/* no parent means we are unregistering DSP internal IRQ */
	parent = platform_irq_get_parent(irq);
	if (parent == NULL)
		arch_interrupt_unregister(irq);
	else
		irq_unregister_child(parent, irq);
}

uint32_t interrupt_enable(uint32_t irq)
{
	struct irq_desc *parent;

	/* no parent means we are enabling DSP internal IRQ */
	parent = platform_irq_get_parent(irq);
	if (parent == NULL)
		return arch_interrupt_enable_mask(1 << irq);
	else
		return irq_enable_child(parent, irq);
}

uint32_t interrupt_disable(uint32_t irq)
{
	struct irq_desc *parent;

	/* no parent means we are disabling DSP internal IRQ */
	parent = platform_irq_get_parent(irq);
	if (parent == NULL)
		return arch_interrupt_disable_mask(1 << irq);
	else
		return irq_disable_child(parent, irq);
}
