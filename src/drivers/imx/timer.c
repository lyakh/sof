// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright 2019 NXP
//
// Author: Daniel Baluta <daniel.baluta@nxp.com>

#include <platform/timer.h>
#include <platform/interrupt.h>
#include <sof/debug.h>
#include <sof/audio/component.h>
#include <sof/drivers/timer.h>
#include <stdint.h>

void platform_timer_start(struct timer *timer)
{
	arch_timer_enable(timer);
}

void platform_timer_stop(struct timer *timer)
{
	arch_timer_disable(timer);
}

int platform_timer_set(struct timer *timer, uint64_t ticks)
{
	return arch_timer_set(timer, ticks);
}

void platform_timer_clear(struct timer *timer)
{
	arch_timer_clear(timer);
}

uint64_t platform_timer_get(struct timer *timer)
{
	return arch_timer_get_system(timer);
}

/* get timestamp for host stream DMA position */
void platform_host_timestamp(struct comp_dev *host,
			     struct sof_ipc_stream_posn *posn)
{
	int err;

	/* get host position */
	err = comp_position(host, posn);
	if (err == 0)
		posn->flags |= SOF_TIME_HOST_VALID | SOF_TIME_HOST_64;
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
	posn->wallclock = timer_get_system(platform_timer) - posn->wallclock;
	posn->flags |= SOF_TIME_WALL_VALID | SOF_TIME_WALL_64;
}

/* get current wallclock for componnent */
void platform_dai_wallclock(struct comp_dev *dai, uint64_t *wallclock)
{
	/* only 1 wallclock on imx8 */
	*wallclock = timer_get_system(platform_timer);
}

int timer_register(struct timer *timer, void(*handler)(void *arg), void *arg)
{
	switch (timer->id) {
	case TIMER0:
	case TIMER1:
		timer->irq_arg = arg;
		return arch_timer_register(timer, handler, arg);
	default:
		return -EINVAL;
	}
}

void timer_unregister(struct timer *timer)
{
	interrupt_unregister(timer->irq, timer->irq_arg);
}

void timer_enable(struct timer *timer)
{
	interrupt_enable(timer->irq, timer->irq_arg);
}

void timer_disable(struct timer *timer)
{
	interrupt_disable(timer->irq, timer->irq_arg);
}
