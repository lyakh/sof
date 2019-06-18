/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

#ifndef __ARCH_TIMER_H_
#define __ARCH_TIMER_H_

#include <arch/interrupt.h>
#include <stdint.h>
#include <errno.h>

struct timer {
	uint32_t id;
	int irq;
	int logical_irq;
	const char *irq_name;
	void *irq_arg;
	void *timer_data;	/* used by core */
	uint32_t hitime;	/* high end of 64bit timer */
	uint32_t hitimeout;
	uint32_t lowtimeout;
	uint64_t delta;
};

/* internal API calls */
int timer64_register(struct timer *timer, void (*handler)(void *arg),
		     void *arg);
void timer_64_handler(void *arg);

static inline int arch_timer_register(struct timer *timer,
	void (*handler)(void *arg), void *arg)
{
	uint32_t flags;
	int ret;

	flags = arch_interrupt_global_disable();
	timer64_register(timer, handler, arg);
	ret = arch_interrupt_register(timer->id, timer_64_handler, timer);
	arch_interrupt_global_enable(flags);

	return ret;
}

static inline void arch_timer_unregister(struct timer *timer)
{
	arch_interrupt_unregister(timer->id);
}

static inline void arch_timer_enable(struct timer *timer)
{
	arch_interrupt_enable_mask(1 << timer->irq);
}

static inline void arch_timer_disable(struct timer *timer)
{
	arch_interrupt_disable_mask(1 << timer->irq);
}

uint64_t arch_timer_get_system(struct timer *timer);

int arch_timer_set(struct timer *timer, uint64_t ticks);

static inline void arch_timer_clear(struct timer *timer)
{
	arch_interrupt_clear(timer->irq);
}

#endif
