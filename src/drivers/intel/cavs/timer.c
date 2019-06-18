// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//         Keyon Jie <yang.jie@linux.intel.com>
//         Rander Wang <rander.wang@intel.com>
//         Janusz Jankowski <janusz.jankowski@linux.intel.com>

#include <platform/timer.h>
#include <platform/shim.h>
#include <sof/debug.h>
#include <sof/audio/component.h>
#include <sof/clk.h>
#include <sof/drivers/timer.h>
#include <stdint.h>

void platform_timer_start(struct timer *timer)
{
	/* run timer */
	shim_write64(SHIM_DSPWCT0C, 0);
	shim_write(SHIM_DSPWCTCS,
		   shim_read(SHIM_DSPWCTCS) | SHIM_DSPWCTCS_T0A);
}

void platform_timer_stop(struct timer *timer)
{
	/* stop timer */
	shim_write64(SHIM_DSPWCT0C, 0);
	shim_write(SHIM_DSPWCTCS,
		   shim_read(SHIM_DSPWCTCS) & ~SHIM_DSPWCTCS_T0A);
}

int platform_timer_set(struct timer *timer, uint64_t ticks)
{
	/* a tick value of 0 will not generate an IRQ */
	if (ticks == 0)
		ticks = 1;

	/* set new value and run */
	shim_write64(SHIM_DSPWCT0C, ticks);
	shim_write(SHIM_DSPWCTCS, SHIM_DSPWCTCS_T0A);

	return 0;
}

void platform_timer_clear(struct timer *timer)
{
	/* write 1 to clear the timer interrupt */
	shim_write(SHIM_DSPWCTCS, SHIM_DSPWCTCS_T0T);
}

uint64_t platform_timer_get(struct timer *timer)
{
//	return arch_timer_get_system(timer);
	return (uint64_t)shim_read64(SHIM_DSPWC);
}

/* get timestamp for host stream DMA position */
void platform_host_timestamp(struct comp_dev *host,
			     struct sof_ipc_stream_posn *posn)
{
	int err;

	/* get host position */
	err = comp_position(host, posn);
	if (err == 0)
		posn->flags |= SOF_TIME_HOST_VALID;
}

/* get timestamp for DAI stream DMA position */
void platform_dai_timestamp(struct comp_dev *dai,
			    struct sof_ipc_stream_posn *posn)
{
	int err;

	/* get DAI position */
	err = comp_position(dai, posn);
	if (err == 0)
		posn->flags |= SOF_TIME_DAI_VALID;

	/* get SSP wallclock - DAI sets this to stream start value */
	posn->wallclock = shim_read64(SHIM_DSPWC) - posn->wallclock;
	posn->wallclock_hz = clock_get_freq(PLATFORM_DEFAULT_CLOCK);
	posn->flags |= SOF_TIME_WALL_VALID;
}

/* get current wallclock for componnent */
void platform_dai_wallclock(struct comp_dev *dai, uint64_t *wallclock)
{
	*wallclock = shim_read64(SHIM_DSPWC);
}

static int platform_timer_register(struct timer *timer,
				   void (*handler)(void *arg), void *arg)
{
	int err;

	/* register timer interrupt */
	err = interrupt_register(timer->irq, IRQ_MANUAL_UNMASK, handler, arg);
	if (err < 0)
		return err;

	timer->irq_arg = arg;

	/* enable timer interrupt */
	interrupt_enable(timer->irq);

	/* disable timer interrupt on core level */
	timer_disable(timer);

	return err;
}

int timer_register(struct timer *timer, void(*handler)(void *arg), void *arg)
{
	int ret;

	switch (timer->id) {
	case TIMER0:
	case TIMER1:
	case TIMER2:
		ret = arch_timer_register(timer, handler, arg);
		/*
		 * Actually this isn't needed for arch_interrupt_register(),
		 * since arch_interrupt_unregister() doesn't support interrupt
		 * sharing and thus doesn't need the handler argument to locate
		 * the handler, do it just for uniformity
		 */
		if (!ret)
			timer->irq_arg = arg;
		return ret;
	case TIMER3:
		return platform_timer_register(timer, handler, arg);
	default:
		return -EINVAL;
	}
}

static void platform_timer_unregister(struct timer *timer)
{
	/* disable timer interrupt */
	interrupt_disable(timer->irq);

	/* unregister timer interrupt */
	interrupt_unregister(timer->irq, timer->irq_arg);
}

void timer_unregister(struct timer *timer)
{
	switch (timer->id) {
	case TIMER0:
	case TIMER1:
	case TIMER2:
		interrupt_unregister(timer->irq, timer->irq_arg);
		break;
	case TIMER3:
		platform_timer_unregister(timer);
		break;
	}
}

void timer_enable(struct timer *timer)
{
	switch (timer->id) {
	case TIMER0:
	case TIMER1:
	case TIMER2:
		interrupt_enable(timer->irq);
		break;
	case TIMER3:
		platform_interrupt_unmask(timer->irq);
		break;
	}
}

void timer_disable(struct timer *timer)
{
	switch (timer->id) {
	case TIMER0:
	case TIMER1:
	case TIMER2:
		interrupt_disable(timer->irq);
		break;
	case TIMER3:
		platform_interrupt_mask(timer->irq);
		break;
	}
}
