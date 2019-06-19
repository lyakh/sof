/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *         Keyon Jie <yang.jie@linux.intel.com>
 */

#ifndef __INCLUDE_LIB_PLATFORM_PLATFORM_H__
#define __INCLUDE_LIB_PLATFORM_PLATFORM_H__

#include <platform/shim.h>
#include <platform/interrupt.h>
#include <stdio.h>
#include <stdlib.h>

/*! \def PLATFORM_DEFAULT_CLOCK
 *  \brief clock source for audio pipeline
 *
 *  There are two types of clock: cpu clock which is a internal clock in
 *  xtensa core, and ssp clock which is provided by external HW IP.
 *  The choice depends on HW features on different platform
 */
#define PLATFORM_DEFAULT_CLOCK CLK_CPU(0)

/*! \def PLATFORM_WORKQ_DEFAULT_TIMEOUT
 *  \brief work queue default timeout in microseconds
 */
#define PLATFORM_WORKQ_DEFAULT_TIMEOUT	1000

/* Host page size */
#define HOST_PAGE_SIZE		4096

/* Platform stream capabilities */
#define PLATFORM_MAX_CHANNELS	8
#define PLATFORM_MAX_STREAMS	16

/* DMA channel drain timeout in microseconds */
#define PLATFORM_DMA_TIMEOUT	1333

/* IPC page data copy timeout */
#define PLATFORM_IPC_DMA_TIMEOUT 2000

/* DSP default delay in cycles */
#define PLATFORM_DEFAULT_DELAY	12

/* DSP number of cores */
#define PLATFORM_CORE_COUNT	1

static inline void platform_panic(uint32_t p) {}

extern struct timer *platform_timer;

#endif
