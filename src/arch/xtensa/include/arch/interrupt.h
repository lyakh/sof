/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

#ifndef __ARCH_INTERRUPT_H
#define __ARCH_INTERRUPT_H

#include <xtensa/xtruntime.h>
#include <xtensa/hal.h>
#include <stdint.h>
#include <stdlib.h>

extern char irq_name_level2[];
extern char irq_name_level3[];
extern char irq_name_level4[];
extern char irq_name_level5[];

static inline int arch_interrupt_register(int irq,
	void (*handler)(void *arg), void *arg)
{
	xthal_set_intclear(0x1 << irq);
	_xtos_set_interrupt_handler_arg(irq, handler, arg);
	return 0;
}

static inline void arch_interrupt_unregister(int irq)
{
	_xtos_set_interrupt_handler_arg(irq, NULL, NULL);
}

/* returns previous mask */
#define arch_interrupt_enable_mask(mask) \
	_xtos_ints_on(mask)

/* returns previous mask */
#define arch_interrupt_disable_mask(mask) \
	_xtos_ints_off(mask)

static inline uint32_t arch_interrupt_get_level(void)
{
	uint32_t level;

	__asm__ __volatile__(
		"       rsr.ps %0\n"
		"       extui  %0, %0, 0, 4\n"
		: "=&a" (level) :: "memory");

	return level;
}

static inline void arch_interrupt_set(int irq)
{
	xthal_set_intset(0x1 << irq);
}

static inline void arch_interrupt_clear(int irq)
{
	xthal_set_intclear(0x1 << irq);
}

static inline uint32_t arch_interrupt_get_enabled(void)
{
	return xthal_get_intenable();
}

static inline uint32_t arch_interrupt_get_status(void)
{
	return xthal_get_interrupt();
}

static inline uint32_t arch_interrupt_global_disable(void)
{
	uint32_t flags;

	asm volatile("rsil	%0, 5"
		     : "=a" (flags) :: "memory");
	return flags;
}

static inline void arch_interrupt_global_enable(uint32_t flags)
{
	asm volatile("wsr %0, ps; rsync"
		     :: "a" (flags) : "memory");
}

#endif
