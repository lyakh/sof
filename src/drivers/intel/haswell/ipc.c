// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2016 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>

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
#include <sof/panic.h>
#include <ipc/header.h>

extern struct ipc *_ipc;

/* private data for IPC */
struct ipc_data {
	struct ipc_data_host_buffer dh_buffer;
};

static void do_notify(void)
{
	uint32_t flags;
	struct ipc_msg *msg;

	spin_lock_irq(&_ipc->lock, flags);
	msg = _ipc->shared_ctx->dsp_msg;
	if (!msg)
		goto out;

	tracev_ipc("ipc: not rx -> 0x%x", msg->header);

	/* copy the data returned from DSP */
	if (msg->rx_size && msg->rx_size < SOF_IPC_MSG_MAX_SIZE)
		mailbox_dspbox_read(msg->rx_data, SOF_IPC_MSG_MAX_SIZE,
				    0, msg->rx_size);

	/* any callback ? */
	if (msg->cb)
		msg->cb(msg->cb_data, msg->rx_data);

	list_item_append(&msg->list, &_ipc->shared_ctx->empty_list);

out:
	spin_unlock_irq(&_ipc->lock, flags);

	/* clear DONE bit - tell Host we have completed */
	shim_write(SHIM_IPCD, 0);

	/* unmask Done interrupt */
	shim_write(SHIM_IMRD, shim_read(SHIM_IMRD) & ~SHIM_IMRD_DONE);
}

static void irq_handler(void *arg)
{
	uint32_t isr, imrd;

	/* Interrupt arrived, check src */
	isr = shim_read(SHIM_ISRD);
	imrd = shim_read(SHIM_IMRD);

	tracev_ipc("ipc: irq isr 0x%x", isr);

	if (isr & SHIM_ISRD_DONE) {

		/* Mask Done interrupt before return */
		shim_write(SHIM_IMRD, shim_read(SHIM_IMRD) | SHIM_IMRD_DONE);
		do_notify();
	}

	if (isr & SHIM_ISRD_BUSY && !(imrd & SHIM_IMRD_BUSY)) {

		/* Mask Busy interrupt before return */
		shim_write(SHIM_IMRD, shim_read(SHIM_IMRD) | SHIM_IMRD_BUSY);

		/* TODO: place message in Q and process later */
		/* It's not Q ATM, may overwrite */
		if (_ipc->host_pending) {
			trace_ipc_error("ipc: dropping msg");
			trace_ipc_error(" isr 0x%x imrd 0x%x ipcx 0x%x",
					isr, shim_read(SHIM_IMRD),
					shim_read(SHIM_IPCX));
		} else {
			_ipc->host_pending = 1;
			ipc_schedule_process(_ipc);
		}
	}
}

void ipc_platform_do_cmd(struct ipc *ipc)
{
	/* Use struct ipc_data *iipc = ipc_get_drvdata(ipc); if needed */
	struct sof_ipc_reply reply;
	int32_t err;

	/* perform command and return any error */
	err = ipc_cmd();
	if (err > 0) {
		goto done; /* reply created and copied by cmd() */
	} else {
		/* send std error reply */
		reply.error = err;
	}

	/* send std error/ok reply */
	reply.hdr.cmd = SOF_IPC_GLB_REPLY;
	reply.hdr.size = sizeof(reply);
	mailbox_hostbox_write(0, &reply, sizeof(reply));

done:
	ipc->host_pending = 0;

	/* clear BUSY bit and set DONE bit - accept new messages */
	shim_write(SHIM_IPCX, SHIM_IPCX_DONE);

	/* unmask busy interrupt */
	shim_write(SHIM_IMRD, shim_read(SHIM_IMRD) & ~SHIM_IMRD_BUSY);

	// TODO: signal audio work to enter D3 in normal context
	/* are we about to enter D3 ? */
	if (ipc->pm_prepare_D3) {
		while (1)
			wait_for_interrupt(0);
	}
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

	/* can't send nofication when one is in progress */
	if (shim_read(SHIM_IPCD) & (SHIM_IPCD_BUSY | SHIM_IPCD_DONE))
		goto out;

	/* now send the message */
	msg = list_first_item(&ipc->shared_ctx->msg_list, struct ipc_msg,
			      list);
	mailbox_dspbox_write(0, msg->tx_data, msg->tx_size);
	list_item_del(&msg->list);
	ipc->shared_ctx->dsp_msg = msg;
	tracev_ipc("ipc: msg tx -> 0x%x", msg->header);

	/* now interrupt host to tell it we have message sent */
	shim_write(SHIM_IPCD, SHIM_IPCD_BUSY);

	list_item_append(&msg->list, &ipc->shared_ctx->empty_list);

out:
	spin_unlock_irq(&ipc->lock, flags);
}

struct ipc_data_host_buffer *ipc_platform_get_host_buffer(struct ipc *ipc)
{
	struct ipc_data *iipc = ipc_get_drvdata(ipc);

	return &iipc->dh_buffer;
}

int platform_ipc_init(struct ipc *ipc)
{
	struct ipc_data *iipc;
	uint32_t imrd, dir, caps, dev;

	_ipc = ipc;

	/* init ipc data */
	iipc = rzalloc(RZONE_SYS, SOF_MEM_CAPS_RAM,
		       sizeof(struct ipc_data));
	ipc_set_drvdata(_ipc, iipc);

	/* schedule */
	schedule_task_init(&_ipc->ipc_task, SOF_SCHEDULE_EDF, SOF_TASK_PRI_IPC,
			   ipc_process_task, _ipc, 0, 0);

#ifdef CONFIG_HOST_PTABLE
	/* allocate page table buffer */
	iipc->dh_buffer.page_table = rzalloc(RZONE_SYS, SOF_MEM_CAPS_RAM,
					     PLATFORM_PAGE_TABLE_SIZE);
	if (iipc->dh_buffer.page_table)
		bzero(iipc->dh_buffer.page_table, PLATFORM_PAGE_TABLE_SIZE);
#endif

	/* request GP DMA with shared access privilege */
	caps = 0;
	dir = DMA_DIR_HMEM_TO_LMEM;
	dev = DMA_DEV_HOST;
	iipc->dh_buffer.dmac = dma_get(dir, caps, dev, DMA_ACCESS_SHARED);

	/* configure interrupt */
	interrupt_register(PLATFORM_IPC_INTERRUPT, IRQ_AUTO_UNMASK,
			   irq_handler, ipc);
	interrupt_enable(PLATFORM_IPC_INTERRUPT, ipc);

	/* Unmask Busy and Done interrupts */
	imrd = shim_read(SHIM_IMRD);
	imrd &= ~(SHIM_IMRD_BUSY | SHIM_IMRD_DONE);
	shim_write(SHIM_IMRD, imrd);

	return 0;
}
