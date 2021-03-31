/*
 * The primary goal is to use P4WQ for IDC. For that we create a work queue for
 * each DSP core.
 * 1 SOF task is 1 Zephyr P4WQ work item
 * From the commit message:
 * <quote>
 * Run from a pool of worker threads that can be allocated efficiently
 * (i.e. you need as many as the number of CPUs plus the number of
 * preempted in-progress items, but no more).
 * </quote>
 * Does that mean, that each task needs a thread?
 * In SOF "tasks" are effectively threads. "Scheduling a task" means making a
 * thread runnable / waking it up.
 *
 * SOF EDF scheduler: registers its own IRQ handler.
 * New EDF tasks are created with schedule_task_init_edf() which creates an XTOS
 * stack frame for the task. The interrupt is triggered from .scheduler_run().
 * The interrupt handler enumerates a list of scheduled tasks. When it finds a
 * task to schedule it sets the CPU context pointer to its stack frame and
 * exits. XTOS then returns from interrupt into that task. Users: KPB, IPC, IDC.
 *
 * Design:
 * - use K_P4WQ_ARRAY_DEFINE() to statically create one queue with one thread
 *	per DSP core.
 * - k_p4wq_submit()
 *	On main CPU (only?)
 *	send tasks to other CPUs.
 */

#include <kernel.h>

#include <sys/p4wq.h>
#include <sof/drivers/idc.h>
#include <sof/init.h>
#include <sof/lib/alloc.h>
#include <ipc/topology.h>

/*
 * Inter-CPU communication is only used in
 * - IPC
 * - Notifier
 * - Power management (IDC_MSG_POWER_UP, IDC_MSG_POWER_DOWN)
 */

#if !CONFIG_MULTICORE

void idc_init_thread(void)
{
}

#else

K_P4WQ_ARRAY_DEFINE(q_zephyr_idc, CONFIG_MAX_CORE_COUNT, SOF_STACK_SIZE);

static void idc_handler(struct k_p4wq_work *work)
{
	struct idc *idc = *idc_get();
	struct idc_msg *msg = work->data;
	int payload = -1;

	SOC_DCACHE_INVALIDATE(msg, sizeof(*msg));

	if (msg->size == sizeof(int))
		memcpy(&payload, msg->payload, msg->size);

	idc->received_msg.core = msg->core;
	idc->received_msg.header = msg->header;
	idc->received_msg.extension = msg->extension;

	switch (msg->header) {
	case IDC_MSG_POWER_UP:
		/* Run the core initialisation? */
		secondary_core_init(sof_get());
		break;
	default:
		idc_cmd(&idc->received_msg);
	}

	rfree(msg);
}

/*
 * Used for *target* CPUs, since the initiator (usually core 0) can launch
 * several IDC messages at once
 */
static struct k_p4wq_work idc_work[CONFIG_MAX_CORE_COUNT];

int idc_send_msg(struct idc_msg *msg, uint32_t mode)
{
	struct idc *idc = *idc_get();
	struct idc_payload *payload = idc_payload_get(idc, msg->core);
	unsigned int target_cpu = msg->core;
	struct idc_msg *msg_cp;
	struct k_p4wq_work *work = idc_work + target_cpu;
	int ret;

	msg_cp = rmalloc(SOF_MEM_ZONE_RUNTIME, 0, SOF_MEM_CAPS_RAM, sizeof(*msg_cp));
	if (!msg_cp)
		return -ENOMEM;

	memcpy(msg_cp, msg, sizeof(*msg_cp));
	work->data = msg_cp;
	/* TODO: verify .priority and .deadline */
	work->priority = K_HIGHEST_THREAD_PRIO + 1;
	work->deadline = 0;
	work->handler = idc_handler;
	work->sync = mode == IDC_BLOCKING;

	if (msg->payload) {
		size_t size = MIN(IDC_MAX_PAYLOAD_SIZE, msg->size);
		memcpy(payload->data, msg->payload, size);
		SOC_DCACHE_FLUSH(payload->data, size);
	}

	/* Temporarily store sender core ID */
	msg_cp->core = cpu_get_id();

	SOC_DCACHE_FLUSH(msg_cp, sizeof(*msg_cp));
	k_p4wq_submit(q_zephyr_idc + target_cpu, work);

	switch (mode) {
	case IDC_BLOCKING:
		ret = k_p4wq_wait(work, K_FOREVER);
		break;
	case IDC_POWER_UP:
	case IDC_NON_BLOCKING:
	default:
		ret = 0;
	}

	return ret;
}

void idc_init_thread(void)
{
	int cpu = cpu_get_id();

	k_mutex_init(&idc_work[cpu].done_mutex);
	k_condvar_init(&idc_work[cpu].done);
	k_p4wq_enable_static_thread(_p4threads_q_zephyr_idc + cpu);
}

#endif /* CONFIG_MULTICORE */
