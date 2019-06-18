/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

#ifndef __INCLUDE_PLATFORM_INTERRUPT__
#define __INCLUDE_PLATFORM_INTERRUPT__

#include <stdint.h>
#include <sof/string.h>

/* IRQ numbers */
#define IRQ_NUM_SOFTWARE0	0	/* Level 1 */
#define IRQ_NUM_TIMER1		1	/* Level 1 */
#define IRQ_NUM_SOFTWARE1	2	/* Level 1 */
#define IRQ_NUM_SOFTWARE2	3	/* Level 1 */
#define IRQ_NUM_TIMER2		5	/* Level 2 */
#define IRQ_NUM_SOFTWARE3	6	/* Level 2 */
#define IRQ_NUM_TIMER3		7	/* Level 3 */
#define IRQ_NUM_SOFTWARE4	8	/* Level 3 */
#define IRQ_NUM_SOFTWARE5	9	/* Level 3 */
#define IRQ_NUM_EXT_IA		10	/* Level 4 */
#define IRQ_NUM_EXT_PMC		11	/* Level 4 */
#define IRQ_NUM_SOFTWARE6	12	/* Level 5 */
#define IRQ_NUM_EXT_DMAC0	13	/* Level 5 */
#define IRQ_NUM_EXT_DMAC1	14	/* Level 5 */
#define IRQ_NUM_EXT_TIMER	15	/* Level 5 */
#define IRQ_NUM_EXT_SSP0	16	/* Level 5 */
#define IRQ_NUM_EXT_SSP1	17	/* Level 5 */
#define IRQ_NUM_EXT_SSP2	18	/* Level 5 */
#define IRQ_NUM_EXT_DMAC2	19	/* Level 5 */
#define IRQ_NUM_NMI		20	/* Level 7 */

/* SSP 3,4,5 share PHY IRQs with SSP 0,1,2 respectively but we give them a
 * virtual number in order to differentiate from SSP0, 1 and 2 IRQs
 */
#define IRQ_CHT_SSP_OFFSET	16

/* Level 5 */
#define IRQ_NUM_EXT_SSP3	(IRQ_CHT_SSP_OFFSET + IRQ_NUM_EXT_SSP0)
#define IRQ_NUM_EXT_SSP4	(IRQ_CHT_SSP_OFFSET + IRQ_NUM_EXT_SSP1)
#define IRQ_NUM_EXT_SSP5	(IRQ_CHT_SSP_OFFSET + IRQ_NUM_EXT_SSP2)

/* IRQ Masks */
#define IRQ_MASK_SOFTWARE0	(1 << IRQ_NUM_SOFTWARE0)
#define IRQ_MASK_TIMER1		(1 << IRQ_NUM_TIMER1)
#define IRQ_MASK_SOFTWARE1	(1 << IRQ_NUM_SOFTWARE1)
#define IRQ_MASK_SOFTWARE2	(1 << IRQ_NUM_SOFTWARE2)
#define IRQ_MASK_TIMER2		(1 << IRQ_NUM_TIMER2)
#define IRQ_MASK_SOFTWARE3	(1 << IRQ_NUM_SOFTWARE3)
#define IRQ_MASK_TIMER3		(1 << IRQ_NUM_TIMER3)
#define IRQ_MASK_SOFTWARE4	(1 << IRQ_NUM_SOFTWARE4)
#define IRQ_MASK_SOFTWARE5	(1 << IRQ_NUM_SOFTWARE5)
#define IRQ_MASK_EXT_IA		(1 << IRQ_NUM_EXT_IA)
#define IRQ_MASK_EXT_PMC	(1 << IRQ_NUM_EXT_PMC)
#define IRQ_MASK_SOFTWARE6	(1 << IRQ_NUM_SOFTWARE6)
#define IRQ_MASK_EXT_DMAC0	(1 << IRQ_NUM_EXT_DMAC0)
#define IRQ_MASK_EXT_DMAC1	(1 << IRQ_NUM_EXT_DMAC1)
#define IRQ_MASK_EXT_TIMER	(1 << IRQ_NUM_EXT_TIMER)
#define IRQ_MASK_EXT_SSP0	(1 << IRQ_NUM_EXT_SSP0)
#define IRQ_MASK_EXT_SSP1	(1 << IRQ_NUM_EXT_SSP1)
#define IRQ_MASK_EXT_SSP2	(1 << IRQ_NUM_EXT_SSP2)
#define IRQ_MASK_EXT_DMAC2	(1 << IRQ_NUM_EXT_DMAC2)

/* no nested interrupts */
#define PLATFORM_IRQ_CHILDREN	0

#endif
