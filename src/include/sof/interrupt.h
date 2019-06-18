/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

#ifndef __INCLUDE_INTERRUPT__
#define __INCLUDE_INTERRUPT__

#include <stdint.h>
#include <arch/interrupt.h>
#include <platform/interrupt.h>
#include <sof/drivers/interrupt.h>
#include <sof/trace.h>
#include <sof/debug.h>
#include <sof/lock.h>
#include <sof/list.h>

#define trace_irq(__e)	trace_event(TRACE_CLASS_IRQ, __e)
#define trace_irq_error(__e, ...) \
	trace_error(TRACE_CLASS_IRQ,  __e, ##__VA_ARGS__)

#define IRQ_MANUAL_UNMASK	0
#define IRQ_AUTO_UNMASK		1

struct irq_desc {
	/* irq must be first for constructor */
	int irq;        /* logical IRQ number */

	/* handler is optional for constructor */
	void (*handler)(void *arg);
	void *handler_arg;

	/* whether irq should be automatically unmasked */
	int unmask;

	/* to identify interrupt with the same IRQ */
	int id;
	uint32_t enabled_count;

	/* to link to other irq_desc */
	struct list_item irq_list;
};

/* A descriptor for cascading interrupt controllers */
struct irq_cascade_desc {
	const char *name;

	/* the interrupt, that this controller is generating */
	struct irq_desc desc;

	/* to link to the global list of interrupt controllers */
	struct irq_cascade_desc *next;

	/* protect child lists in the below array */
	spinlock_t lock;
	uint32_t num_children;
	struct list_item child[PLATFORM_IRQ_CHILDREN];
};

/* A descriptor for cascading interrupt controller template */
struct irq_cascade_tmpl {
	const char *name;
	int irq;
	void (*handler)(void *arg);
};

int interrupt_register(uint32_t irq, int unmask, void(*handler)(void *arg),
		       void *arg);
void interrupt_unregister(uint32_t irq, const void *arg);
uint32_t interrupt_enable(uint32_t irq);
uint32_t interrupt_disable(uint32_t irq);

/*
 * On platforms, supporting cascading interrupts cascaded interrupt numbers
 * have SOF_IRQ_LEVEL(irq) != 0.
 */
#define interrupt_is_dsp_direct(irq) (!SOF_IRQ_LEVEL(irq))

void interrupt_init(void);
int interrupt_cascade_register(const struct irq_cascade_tmpl *tmpl);
struct irq_desc *interrupt_get_parent(uint32_t irq);

static inline void interrupt_set(int irq)
{
	platform_interrupt_set(irq);
}

static inline void interrupt_clear_mask(int irq, uint32_t mask)
{
	platform_interrupt_clear(irq, mask);
}

static inline void interrupt_clear(int irq)
{
	interrupt_clear_mask(irq, 1);
}

static inline uint32_t interrupt_global_disable(void)
{
	return arch_interrupt_global_disable();
}

static inline void interrupt_global_enable(uint32_t flags)
{
	arch_interrupt_global_enable(flags);
}

#endif
