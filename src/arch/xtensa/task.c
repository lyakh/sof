// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2017 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>

/**
 * \file arch/xtensa/task.c
 * \brief Arch task implementation file
 * \authors Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

#include <sof/alloc.h>
#include <sof/debug.h>
#include <sof/interrupt.h>
#include <sof/schedule.h>
#include <platform/platform.h>
#include <arch/task.h>

/**
 * \brief Retrieves task IRQ level.
 * \param[in,out] task Task data.
 * \return IRQ level.
 */
static uint32_t task_get_irq(struct task *task)
{
	uint32_t irq;

	switch (task->priority) {
	case SOF_TASK_PRI_MED + 1 ... SOF_TASK_PRI_LOW:
		irq = PLATFORM_IRQ_TASK_LOW;
		break;
	case SOF_TASK_PRI_HIGH ... SOF_TASK_PRI_MED - 1:
		irq = PLATFORM_IRQ_TASK_HIGH;
		break;
	case SOF_TASK_PRI_MED:
	default:
		irq = PLATFORM_IRQ_TASK_MED;
		break;
	}

	return irq;
}

/**
 * \brief Adds task to the list per IRQ level.
 * \param[in,out] task Task data.
 */
static int task_set_data(struct task *task)
{
	struct list_item *dst = NULL;
	struct irq_task *irq_task;
	uint32_t flags;

	switch (task->priority) {
#ifdef CONFIG_TASK_HAVE_PRIORITY_MEDIUM
	case SOF_TASK_PRI_MED + 1 ... SOF_TASK_PRI_LOW:
		irq_task = *task_irq_low_get();
		break;
	case SOF_TASK_PRI_HIGH ... SOF_TASK_PRI_MED - 1:
		irq_task = *task_irq_high_get();
		break;
	case SOF_TASK_PRI_MED:
		irq_task = *task_irq_med_get();
		break;
#elif CONFIG_TASK_HAVE_PRIORITY_LOW
	case  SOF_TASK_PRI_MED ... SOF_TASK_PRI_LOW:
		irq_task = *task_irq_low_get();
		break;
	case SOF_TASK_PRI_HIGH ... SOF_TASK_PRI_MED - 1:
		irq_task = *task_irq_high_get();
		break;
#else
	case SOF_TASK_PRI_HIGH ... SOF_TASK_PRI_LOW:
		irq_task = *task_irq_high_get();
		break;
#endif
	default:
		trace_error(TRACE_CLASS_IRQ,
			    "task_set_data() error: task priority %d",
			    task->priority);
		return -EINVAL;
	}

	dst = &irq_task->list;
	spin_lock_irq(&irq_task->lock, flags);
	list_item_append(&task->irq_list, dst);
	spin_unlock_irq(&irq_task->lock, flags);

	return 0;
}

/**
 * \brief Interrupt handler for the IRQ task.
 * \param[in,out] arg IRQ task data.
 */
static void _irq_task(void *arg)
{
	struct irq_task *irq_task = *(struct irq_task **)arg;
	struct list_item *tlist;
	struct list_item *clist;
	struct task *task;
	uint32_t flags;
	int run_task = 0;

	spin_lock_irq(&irq_task->lock, flags);

	interrupt_clear(irq_task->irq);

	list_for_item_safe(clist, tlist, &irq_task->list) {
		task = container_of(clist, struct task, irq_list);
		list_item_del(clist);

		if (task->func &&
		    task->state == SOF_TASK_STATE_PENDING) {
			schedule_task_running(task);
			run_task = 1;
		} else {
			run_task = 0;
		}

		/* run task without holding task lock */
		spin_unlock_irq(&irq_task->lock, flags);

		if (run_task)
			task->func(task->data);

		spin_lock_irq(&irq_task->lock, flags);
		schedule_task_complete(task);
	}

	spin_unlock_irq(&irq_task->lock, flags);
}

int arch_run_task(struct task *task)
{
	uint32_t irq;
	int ret;

	ret = task_set_data(task);

	if (ret < 0)
		return ret;

	irq = task_get_irq(task);
	interrupt_set(irq);

	return 0;
}

int arch_allocate_tasks(void)
{
#ifdef CONFIG_TASK_HAVE_PRIORITY_LOW
	/* irq low */
	struct irq_task **low = task_irq_low_get();
	*low = rzalloc(RZONE_SYS, SOF_MEM_CAPS_RAM, sizeof(**low));

	list_init(&((*low)->list));
	spinlock_init(&((*low)->lock));
	(*low)->irq = PLATFORM_IRQ_TASK_LOW;
#endif

#ifdef CONFIG_TASK_HAVE_PRIORITY_MEDIUM
	/* irq medium */
	struct irq_task **med = task_irq_med_get();
	*med = rzalloc(RZONE_SYS, SOF_MEM_CAPS_RAM, sizeof(**med));

	list_init(&((*med)->list));
	spinlock_init(&((*med)->lock));
	(*med)->irq = PLATFORM_IRQ_TASK_MED;
#endif

	/* irq high */
	struct irq_task **high = task_irq_high_get();
	*high = rzalloc(RZONE_SYS, SOF_MEM_CAPS_RAM, sizeof(**high));

	list_init(&((*high)->list));
	spinlock_init(&((*high)->lock));
	(*high)->irq = PLATFORM_IRQ_TASK_HIGH;

	return 0;
}

void arch_free_tasks(void)
{
	uint32_t flags;
/* TODO: do not want to free the tasks, just the entire heap */

#ifdef CONFIG_TASK_HAVE_PRIORITY_LOW
	/* free IRQ low task */
	struct irq_task **low = task_irq_low_get();

	spin_lock_irq(&(*low)->lock, flags);
	interrupt_disable(PLATFORM_IRQ_TASK_LOW);
	interrupt_unregister(PLATFORM_IRQ_TASK_LOW, task_irq_low_get());
	list_item_del(&(*low)->list);
	spin_unlock_irq(&(*low)->lock, flags);
#endif

#ifdef CONFIG_TASK_HAVE_PRIORITY_MEDIUM
	/* free IRQ medium task */
	struct irq_task **med = task_irq_med_get();

	spin_lock_irq(&(*med)->lock, flags);
	interrupt_disable(PLATFORM_IRQ_TASK_MED);
	interrupt_unregister(PLATFORM_IRQ_TASK_MED, task_irq_med_get());
	list_item_del(&(*med)->list);
	spin_unlock_irq(&(*med)->lock, flags);
#endif

	/* free IRQ high task */
	struct irq_task **high = task_irq_high_get();

	spin_lock_irq(&(*high)->lock, flags);
	interrupt_disable(PLATFORM_IRQ_TASK_HIGH);
	interrupt_unregister(PLATFORM_IRQ_TASK_HIGH, task_irq_high_get());
	list_item_del(&(*high)->list);
	spin_unlock_irq(&(*high)->lock, flags);
}

int arch_assign_tasks(void)
{
#ifdef CONFIG_TASK_HAVE_PRIORITY_LOW
	/* irq low */
	interrupt_register(PLATFORM_IRQ_TASK_LOW, IRQ_AUTO_UNMASK, _irq_task,
			   task_irq_low_get());
	interrupt_enable(PLATFORM_IRQ_TASK_LOW);
#endif

#ifdef CONFIG_TASK_HAVE_PRIORITY_MEDIUM
	/* irq medium */
	interrupt_register(PLATFORM_IRQ_TASK_MED, IRQ_AUTO_UNMASK, _irq_task,
			   task_irq_med_get());
	interrupt_enable(PLATFORM_IRQ_TASK_MED);
#endif

	/* irq high */
	interrupt_register(PLATFORM_IRQ_TASK_HIGH, IRQ_AUTO_UNMASK, _irq_task,
			   task_irq_high_get());
	interrupt_enable(PLATFORM_IRQ_TASK_HIGH);

	return 0;
}
