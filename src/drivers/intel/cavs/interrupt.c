/*
 * Copyright (c) 2018, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Intel Corporation nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Keyon Jie <yang.jie@linux.intel.com>
 *         Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *         Rander Wang <rander.wang@intel.com>
 *         Janusz Jankowski <janusz.jankowski@linux.intel.com>
 *
 */

#include <sof/sof.h>
#include <sof/interrupt.h>
#include <sof/interrupt-map.h>
#include <sof/cpu.h>
#include <sof/cavs-version.h>
#include <arch/interrupt.h>
#include <platform/interrupt.h>
#include <platform/platcfg.h>
#include <platform/shim.h>
#include <stdint.h>
#include <stdlib.h>

static inline void irq_lvl2_handler(void *data, int level, uint32_t ilxsd,
				    uint32_t ilxmsd, uint32_t ilxmcd)
{
	struct irq_desc *parent = (struct irq_desc *)data;
	struct irq_cascade_desc *cascade = container_of(parent,
						struct irq_cascade_desc, desc);
	struct irq_desc *child = NULL;
	int core = cpu_get_id();
	struct list_item *clist;
	uint32_t status;
	uint32_t i = 0;
	uint32_t unmask = 0;

	/* mask the parent IRQ */
	arch_interrupt_disable_mask(1 << level);

	/* mask all child interrupts */
	status = irq_read(ilxsd);
	irq_write(ilxmsd, status);

	/* handle each child */
	while (status) {
		/* any IRQ for this child bit ? */
		if ((status & 0x1) == 0)
			goto next;

		/* get child if any and run handler */
		list_for_item(clist, &cascade->child[i]) {
			child = container_of(clist, struct irq_desc, irq_list);

			if (child && child->handler &&
			    (child->cpu_mask & 1 << core)) {
				child->handler(child->handler_arg);
				unmask = child->unmask;
			} else {
				/* nobody cared ? */
				trace_irq_error("irq_lvl2_handler() error: "
						"nbc");
			}
		}

		/* unmask this bit i interrupt */
		if (unmask)
			irq_write(ilxmcd, 0x1 << i);

next:
		status >>= 1;
		i++;
	}

	/* clear parent and unmask */
	arch_interrupt_clear(level);
	arch_interrupt_enable_mask(1 << level);
}

#define IRQ_LVL2_HANDLER(n) int core = cpu_get_id(); \
				irq_lvl2_handler(data, \
				 IRQ_NUM_EXT_LEVEL##n, \
				 REG_IRQ_IL##n##SD(core), \
				 REG_IRQ_IL##n##MSD(core), \
				 REG_IRQ_IL##n##MCD(core))

static void irq_lvl2_level2_handler(void *data)
{
	IRQ_LVL2_HANDLER(2);
}

static void irq_lvl2_level3_handler(void *data)
{
	IRQ_LVL2_HANDLER(3);
}

static void irq_lvl2_level4_handler(void *data)
{
	IRQ_LVL2_HANDLER(4);
}

static void irq_lvl2_level5_handler(void *data)
{
	IRQ_LVL2_HANDLER(5);
}

static void irq_mask(struct irq_desc *desc, uint32_t irq)
{
	int core = SOF_IRQ_CPU(irq);

	/* mask external interrupt bit */
	switch (desc->irq) {
	case IRQ_NUM_EXT_LEVEL5:
		irq_write(REG_IRQ_IL5MSD(core), 1 << SOF_IRQ_BIT(irq));
		break;
	case IRQ_NUM_EXT_LEVEL4:
		irq_write(REG_IRQ_IL4MSD(core), 1 << SOF_IRQ_BIT(irq));
		break;
	case IRQ_NUM_EXT_LEVEL3:
		irq_write(REG_IRQ_IL3MSD(core), 1 << SOF_IRQ_BIT(irq));
		break;
	case IRQ_NUM_EXT_LEVEL2:
		irq_write(REG_IRQ_IL2MSD(core), 1 << SOF_IRQ_BIT(irq));
	}
}

static void irq_unmask(struct irq_desc *desc, uint32_t irq)
{
	int core = SOF_IRQ_CPU(irq);

	/* unmask external interrupt bit */
	switch (desc->irq) {
	case IRQ_NUM_EXT_LEVEL5:
		irq_write(REG_IRQ_IL5MCD(core), 1 << SOF_IRQ_BIT(irq));
		break;
	case IRQ_NUM_EXT_LEVEL4:
		irq_write(REG_IRQ_IL4MCD(core), 1 << SOF_IRQ_BIT(irq));
		break;
	case IRQ_NUM_EXT_LEVEL3:
		irq_write(REG_IRQ_IL3MCD(core), 1 << SOF_IRQ_BIT(irq));
		break;
	case IRQ_NUM_EXT_LEVEL2:
		irq_write(REG_IRQ_IL2MCD(core), 1 << SOF_IRQ_BIT(irq));
	}
}

static const struct irq_cascade_ops irq_ops = {
	.mask = irq_mask,
	.unmask = irq_unmask,
};

/* DSP internal interrupts */
static struct irq_cascade_desc dsp_irq[] = {
	{.name = "level2", .ops = &irq_ops,
	 .desc = {IRQ_NUM_EXT_LEVEL2, irq_lvl2_level2_handler, },},
	{.name = "level3", .ops = &irq_ops,
	 .desc = {IRQ_NUM_EXT_LEVEL3, irq_lvl2_level3_handler, },},
	{.name = "level4", .ops = &irq_ops,
	 .desc = {IRQ_NUM_EXT_LEVEL4, irq_lvl2_level4_handler, },},
	{.name = "level5", .ops = &irq_ops,
	 .desc = {IRQ_NUM_EXT_LEVEL5, irq_lvl2_level5_handler, },},
};

uint32_t platform_interrupt_get_enabled(void)
{
	return 0;
}

void platform_interrupt_set(uint32_t irq)
{
	if (!interrupt_get_parent(irq))
		arch_interrupt_set(SOF_IRQ_NUMBER(irq));
}

void platform_interrupt_clear(uint32_t irq, uint32_t mask)
{
	if (!interrupt_get_parent(irq))
		arch_interrupt_clear(SOF_IRQ_NUMBER(irq));
}

void platform_interrupt_init(void)
{
	int i, j;
	int core = cpu_get_id();

	/* mask all external IRQs by default */
	irq_write(REG_IRQ_IL2MSD(core), REG_IRQ_IL2MD_ALL);
	irq_write(REG_IRQ_IL3MSD(core), REG_IRQ_IL3MD_ALL);
	irq_write(REG_IRQ_IL4MSD(core), REG_IRQ_IL4MD_ALL);
	irq_write(REG_IRQ_IL5MSD(core), REG_IRQ_IL5MD_ALL);

	if (core != PLATFORM_MASTER_CORE_ID)
		return;

	for (i = 0; i < ARRAY_SIZE(dsp_irq); i++) {
		spinlock_init(&dsp_irq[i].lock);
		for (j = 0; j < PLATFORM_IRQ_CHILDREN; j++)
			list_init(&dsp_irq[i].child[j]);

		interrupt_cascade_register(&dsp_irq[i]);
	}
}
