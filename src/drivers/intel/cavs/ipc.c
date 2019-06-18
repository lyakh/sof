// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2016 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//         Keyon Jie <yang.jie@linux.intel.com>
//         Rander Wang <rander.wang@intel.com>

#include <sof/debug.h>
#include <sof/timer.h>
#include <sof/interrupt.h>
#include <sof/ipc.h>
#include <sof/mailbox.h>
#include <sof/sof.h>
#include <sof/stream.h>
#include <sof/dai.h>
#include <sof/dma.h>
#include <sof/alloc.h>
#include <sof/wait.h>
#include <sof/trace.h>
#include <sof/ssp.h>
#include <platform/interrupt.h>
#include <platform/mailbox.h>
#include <platform/shim.h>
#include <platform/dma.h>
#include <platform/platform.h>
#include <sof/audio/component.h>
#include <sof/audio/pipeline.h>
#include <ipc/header.h>
#include <platform/pm_runtime.h>
#include <cavs/version.h>

extern struct ipc *_ipc;

/* No private data for IPC */

#if CONFIG_DEBUG_IPC_COUNTERS
static inline void increment_ipc_received_counter(void)
{
	static uint32_t ipc_received_counter;

	mailbox_sw_reg_write(SRAM_REG_FW_IPC_RECEIVED_COUNT,
			     ipc_received_counter++);
}

static inline void increment_ipc_processed_counter(void)
{
	static uint32_t ipc_processed_counter;

	mailbox_sw_reg_write(SRAM_REG_FW_IPC_PROCESSED_COUNT,
			     ipc_processed_counter++);
}
#endif

/* test code to check working IRQ */
static void ipc_irq_handler(void *arg)
{
	uint32_t dipcctl;

#if CAVS_VERSION == CAVS_VERSION_1_5
	uint32_t dipct;
	uint32_t dipcie;

	dipct = ipc_read(IPC_DIPCT);
	dipcie = ipc_read(IPC_DIPCIE);
	dipcctl = ipc_read(IPC_DIPCCTL);

	tracev_ipc("ipc: irq dipct 0x%x dipcie 0x%x dipcctl 0x%x", dipct,
		   dipcie, dipcctl);
#else
	uint32_t dipctdr;
	uint32_t dipcida;

	dipctdr = ipc_read(IPC_DIPCTDR);
	dipcida = ipc_read(IPC_DIPCIDA);
	dipcctl = ipc_read(IPC_DIPCCTL);

	tracev_ipc("ipc: irq dipctdr 0x%x dipcida 0x%x dipcctl 0x%x", dipctdr,
		   dipcida, dipcctl);
#endif

	/* new message from host */
#if CAVS_VERSION == CAVS_VERSION_1_5
	if (dipct & IPC_DIPCT_BUSY && dipcctl & IPC_DIPCCTL_IPCTBIE)
#else
	if (dipctdr & IPC_DIPCTDR_BUSY && dipcctl & IPC_DIPCCTL_IPCTBIE)
#endif
	{
		/* mask Busy interrupt */
		ipc_write(IPC_DIPCCTL, dipcctl & ~IPC_DIPCCTL_IPCTBIE);

#if CONFIG_DEBUG_IPC_COUNTERS
		increment_ipc_received_counter();
#endif

		/* TODO: place message in Q and process later */
		/* It's not Q ATM, may overwrite */
		if (_ipc->host_pending) {
			trace_ipc_error("ipc: dropping msg");
#if CAVS_VERSION == CAVS_VERSION_1_5
			trace_ipc_error(" dipct 0x%x dipcie 0x%x dipcctl 0x%x",
					dipct, dipcie, ipc_read(IPC_DIPCCTL));
#else
			trace_ipc_error(" dipctdr 0x%x dipcida 0x%x "
					"dipcctl 0x%x", dipctdr, dipcida,
					ipc_read(IPC_DIPCCTL));
#endif
		} else {
			_ipc->host_pending = 1;
			ipc_schedule_process(_ipc);
		}
	}

	/* reply message(done) from host */
#if CAVS_VERSION == CAVS_VERSION_1_5
	if (dipcie & IPC_DIPCIE_DONE && dipcctl & IPC_DIPCCTL_IPCIDIE)
#else
	if (dipcida & IPC_DIPCIDA_DONE)
#endif
	{
		/* mask Done interrupt */
		ipc_write(IPC_DIPCCTL,
			  ipc_read(IPC_DIPCCTL) & ~IPC_DIPCCTL_IPCIDIE);

		/* clear DONE bit - tell host we have completed the operation */
#if CAVS_VERSION == CAVS_VERSION_1_5
		ipc_write(IPC_DIPCIE,
			  ipc_read(IPC_DIPCIE) | IPC_DIPCIE_DONE);
#else
		ipc_write(IPC_DIPCIDA,
			  ipc_read(IPC_DIPCIDA) | IPC_DIPCIDA_DONE);
#endif

		/* unmask Done interrupt */
		ipc_write(IPC_DIPCCTL,
			  ipc_read(IPC_DIPCCTL) | IPC_DIPCCTL_IPCIDIE);

		/* send next message to host */
		ipc_process_msg_queue();
	}
}

void ipc_platform_do_cmd(struct ipc *ipc)
{
	struct sof_ipc_reply reply;
	int32_t err;

	/* perform command and return any error */
	err = ipc_cmd();

	/* if err > 0, reply created and copied by cmd() */
	if (err <= 0) {
		/* send std error/ok reply */
		reply.error = err;

		reply.hdr.cmd = SOF_IPC_GLB_REPLY;
		reply.hdr.size = sizeof(reply);
		mailbox_hostbox_write(0, &reply, sizeof(reply));
	}

	ipc->host_pending = 0;

	/* are we about to enter D3 ? */
#if CAVS_VERSION < CAVS_VERSION_2_0
	if (ipc->pm_prepare_D3) {
		/* no return - memory will be powered off and IPC sent */
		platform_pm_runtime_power_off();
	}
#endif

	/* write 1 to clear busy, and trigger interrupt to host*/
#if CAVS_VERSION == CAVS_VERSION_1_5
	ipc_write(IPC_DIPCT, ipc_read(IPC_DIPCT) | IPC_DIPCT_BUSY);
#else
	ipc_write(IPC_DIPCTDR, ipc_read(IPC_DIPCTDR) | IPC_DIPCTDR_BUSY);
	ipc_write(IPC_DIPCTDA, ipc_read(IPC_DIPCTDA) | IPC_DIPCTDA_BUSY);
#endif

#if CONFIG_DEBUG_IPC_COUNTERS
	increment_ipc_processed_counter();
#endif

	/* unmask Busy interrupt */
	ipc_write(IPC_DIPCCTL, ipc_read(IPC_DIPCCTL) | IPC_DIPCCTL_IPCTBIE);

#if CAVS_VERSION == CAVS_VERSION_2_0
	if (ipc->pm_prepare_D3) {
		//TODO: add support for Icelake
		while (1)
			wait_for_interrupt(5);
	}
#endif
}

void ipc_platform_send_msg(struct ipc *ipc)
{
	struct ipc_msg *msg;
	uint32_t flags;

	spin_lock_irq(&ipc->lock, flags);

	/* any messages to send ? */
	if (list_is_empty(&ipc->shared_ctx->msg_list)) {
		ipc->shared_ctx->dsp_pending = 0;
		goto out;
	}

#if CAVS_VERSION == CAVS_VERSION_1_5
	if (ipc_read(IPC_DIPCI) & IPC_DIPCI_BUSY)
#else
	if (ipc_read(IPC_DIPCIDR) & IPC_DIPCIDR_BUSY ||
	    ipc_read(IPC_DIPCIDA) & IPC_DIPCIDA_DONE)
#endif
		goto out;

	/* now send the message */
	msg = list_first_item(&ipc->shared_ctx->msg_list, struct ipc_msg,
			      list);
	mailbox_dspbox_write(0, msg->tx_data, msg->tx_size);
	list_item_del(&msg->list);
	ipc->shared_ctx->dsp_msg = msg;
	tracev_ipc("ipc: msg tx -> 0x%x", msg->header);

	/* now interrupt host to tell it we have message sent */
#if CAVS_VERSION == CAVS_VERSION_1_5
	ipc_write(IPC_DIPCIE, 0);
	ipc_write(IPC_DIPCI, IPC_DIPCI_BUSY | msg->header);
#else
	ipc_write(IPC_DIPCIDD, 0);
	ipc_write(IPC_DIPCIDR, 0x80000000 | msg->header);
#endif

	list_item_append(&msg->list, &ipc->shared_ctx->empty_list);

out:
	spin_unlock_irq(&ipc->lock, flags);
}

int platform_ipc_init(struct ipc *ipc)
{
	_ipc = ipc;

	ipc_set_drvdata(_ipc, NULL);

	/* schedule */
	schedule_task_init(&_ipc->ipc_task, SOF_SCHEDULE_EDF, SOF_TASK_PRI_IPC,
			   ipc_process_task, _ipc, 0, 0);

	/* configure interrupt */
	interrupt_register(PLATFORM_IPC_INTERRUPT, IRQ_AUTO_UNMASK,
			   ipc_irq_handler, ipc);
	interrupt_enable(PLATFORM_IPC_INTERRUPT, ipc);

	/* enable IPC interrupts from host */
	ipc_write(IPC_DIPCCTL, IPC_DIPCCTL_IPCIDIE | IPC_DIPCCTL_IPCTBIE);

	return 0;
}
