/**
 * @file    edf.c
 * @brief   EDF（最早截止时间优先）调度器实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 实现 EDF 实时调度策略：
 *          - 基于绝对截止时间的有序链表管理就绪任务
 *          - O(n) 入队（插入排序），O(1) 选下一个
 *          - 周期性任务的作业释放/完成管理
 *          - Liu & Layland 可调度性测试
 *          - WCET 预留和截止时间违约检测
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SC-003, SC-005~006
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "edf.h"
#include "scheduler.h"
#include "thread.h"

#include <kernel/barrier.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <stdint.h>
#include <string.h>

/* HAL 接口 */
extern uint32_t hal_get_cpu_id(void);
extern tick_t hal_get_tick_count(void);

/* ========================================================================
 * EDF 就绪队列实例
 * ======================================================================== */

/** @brief 每 CPU 的 EDF 就绪队列 */
static edf_ready_queue_t s_edf_queues[CONFIG_MAX_CPUS]
    __attribute__((aligned(64U)));

/** @brief 每 CPU 每 EDF 参数（与线程表索引对应） */
static edf_params_t s_edf_params[CONFIG_MAX_CPUS][CONFIG_MAX_THREADS];

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 获取当前 CPU 的 EDF 队列
 *
 * @return EDF 就绪队列指针
 */
static edf_ready_queue_t *edf_get_queue(void)
{
    uint32_t cpu = hal_get_cpu_id();
    if (cpu >= CONFIG_MAX_CPUS)
    {
        cpu = 0U;
    }
    return &s_edf_queues[cpu];
}

/**
 * @brief 获取线程对应的 EDF 参数
 *
 * @param tid 线程 ID
 *
 * @return EDF 参数指针
 */
static edf_params_t *edf_get_thread_params(thread_id_t tid)
{
    uint32_t cpu = hal_get_cpu_id();
    if (cpu >= CONFIG_MAX_CPUS)
    {
        cpu = 0U;
    }
    if (tid >= (thread_id_t)CONFIG_MAX_THREADS)
    {
        return NULL;
    }
    return &s_edf_params[cpu][tid];
}

/* ========================================================================
 * 初始化
 * ======================================================================== */

kernel_status_t edf_init(void)
{
    uint32_t cpu;
    uint32_t i;

    (void)memset(s_edf_queues, 0, sizeof(s_edf_queues));
    (void)memset(s_edf_params, 0, sizeof(s_edf_params));

    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        INIT_LIST_HEAD(&s_edf_queues[cpu].tasks);
        s_edf_queues[cpu].count = 0U;
        s_edf_queues[cpu].lock = 0U;
        s_edf_queues[cpu].total_utilization = 0ULL;

        for (i = 0U; i < CONFIG_MAX_THREADS; i++)
        {
            s_edf_params[cpu][i].wcet = EDF_DEFAULT_WCET;
            s_edf_params[cpu][i].period = EDF_DEFAULT_PERIOD;
            s_edf_params[cpu][i].relative_deadline = EDF_DEFAULT_PERIOD;
            s_edf_params[cpu][i].runtime = 0ULL;
            s_edf_params[cpu][i].absolute_deadline = EDF_DEADLINE_INVALID;
            s_edf_params[cpu][i].next_release = 0ULL;
            s_edf_params[cpu][i].overrun_count = 0U;
            s_edf_params[cpu][i].is_periodic = false;
        }
    }

    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 参数设置/获取
 * ======================================================================== */

kernel_status_t edf_set_params(thread_id_t tid, const edf_params_t *params)
{
    edf_params_t *dest;
    KThread_t *thread;

    if (params == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (tid >= (thread_id_t)CONFIG_MAX_THREADS)
    {
        return -(int32_t)ESRCH;
    }

    thread = &g_scheduler.thread_table[tid];
    if (thread->state == KTHREAD_STATE_DEAD)
    {
        return -(int32_t)ESRCH;
    }

    /* WCET 不能大于周期（如果指定了周期） */
    if ((params->period > 0ULL) && (params->wcet > params->period))
    {
        return -(int32_t)EINVAL;
    }

    /* 相对截止时间不能小于 WCET */
    if (params->relative_deadline < params->wcet)
    {
        return -(int32_t)EINVAL;
    }

    dest = edf_get_thread_params(tid);
    if (dest == NULL)
    {
        return -(int32_t)ESRCH;
    }

    (void)memcpy(dest, params, sizeof(edf_params_t));

    /* 设置初始绝对截止时间 */
    if (dest->absolute_deadline == EDF_DEADLINE_INVALID)
    {
        tick_t now = hal_get_tick_count();
        dest->absolute_deadline = now + dest->relative_deadline;
        dest->next_release = now + dest->period;
    }

    /* 更新线程调度策略为 EDF */
    thread->policy = (KThreadPolicy_t)2U; /* SCHED_EDF */

    return KERNEL_OK;
}

kernel_status_t edf_get_params(thread_id_t tid, edf_params_t *params)
{
    edf_params_t *src;

    if (params == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (tid >= (thread_id_t)CONFIG_MAX_THREADS)
    {
        return -(int32_t)ESRCH;
    }

    if (g_scheduler.thread_table[tid].state == KTHREAD_STATE_DEAD)
    {
        return -(int32_t)ESRCH;
    }

    src = edf_get_thread_params(tid);
    if (src == NULL)
    {
        return -(int32_t)ESRCH;
    }

    (void)memcpy(params, src, sizeof(edf_params_t));

    return KERNEL_OK;
}

/* ========================================================================
 * 入队（按截止时间插入排序）
 * ======================================================================== */

void edf_enqueue(KThread_t *thread)
{
    edf_ready_queue_t *queue;
    struct list_head *pos;
    KThread_t *entry;
    edf_params_t *thread_params;
    edf_params_t *entry_params;

    if (thread == NULL)
    {
        return;
    }

    queue = edf_get_queue();
    thread_params = edf_get_thread_params(thread->tid);

    if (thread_params == NULL)
    {
        return;
    }

    /* 按绝对截止时间升序插入 */
    pos = queue->tasks.next;
    while (pos != &queue->tasks)
    {
        entry = container_of(pos, KThread_t, rq_list);
        entry_params = edf_get_thread_params(entry->tid);

        if (entry_params != NULL)
        {
            if (thread_params->absolute_deadline < entry_params->absolute_deadline)
            {
                break;
            }
        }

        pos = pos->next;
    }

    /* 在 pos 之前插入 */
    list_add_tail(&thread->rq_list, pos);
    queue->count++;

    barrier();
}

/* ========================================================================
 * 选下一个（最早截止时间）
 * ======================================================================== */

KThread_t *edf_pick_next(void)
{
    edf_ready_queue_t *queue;
    KThread_t *next;

    queue = edf_get_queue();

    if (list_empty(&queue->tasks) != 0)
    {
        return NULL;
    }

    next = list_first_entry(&queue->tasks, KThread_t, rq_list);
    return next;
}

/* ========================================================================
 * 时钟滴答处理
 * ======================================================================== */

void edf_tick(void)
{
    KThread_t *current;
    edf_params_t *params;
    tick_t now;

    current = kthread_get_current();
    if (current == NULL)
    {
        return;
    }

    /* 仅处理 EDF 策略线程 */
    if (current->policy != (KThreadPolicy_t)2U)
    {
        return;
    }

    params = edf_get_thread_params(current->tid);
    if (params == NULL)
    {
        return;
    }

    now = hal_get_tick_count();

    /* 更新运行时间 */
    params->runtime++;

    /* 检查是否超过 WCET */
    if (params->runtime >= params->wcet)
    {
        /* WCET 耗尽，让出 CPU */
        schedule();
        return;
    }

    /* 检查截止时间违约 */
    if (now >= params->absolute_deadline)
    {
        params->overrun_count++;
        /* 截止时间到达，重新调度 */
        schedule();
        return;
    }
}

/* ========================================================================
 * 可调度性分析
 * ======================================================================== */

bool edf_check_schedulability(void)
{
    uint32_t cpu;
    uint64_t total_util = 0ULL;
    edf_ready_queue_t *queue;

    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        queue = &s_edf_queues[cpu];
        total_util += queue->total_utilization;
    }

    /* Liu & Layland 条件：U <= 1.0（这里用千分比，<= 1000） */
    if (total_util <= 1000ULL)
    {
        return true;
    }

    return false;
}

/* ========================================================================
 * 作业释放/完成
 * ======================================================================== */

void edf_job_release(KThread_t *thread)
{
    edf_params_t *params;
    tick_t now;

    if (thread == NULL)
    {
        return;
    }

    params = edf_get_thread_params(thread->tid);
    if (params == NULL)
    {
        return;
    }

    now = hal_get_tick_count();

    /* 设置新周期的截止时间 */
    params->absolute_deadline = now + params->relative_deadline;
    params->runtime = 0ULL;

    /* 更新下次释放时间 */
    if (params->is_periodic)
    {
        params->next_release = now + params->period;
    }

    /* 将线程加入就绪队列 */
    thread->state = KTHREAD_STATE_READY;
    edf_enqueue(thread);
}

void edf_job_complete(KThread_t *thread)
{
    edf_params_t *params;

    if (thread == NULL)
    {
        return;
    }

    params = edf_get_thread_params(thread->tid);
    if (params == NULL)
    {
        return;
    }

    if (params->is_periodic)
    {
        /* 周期性任务：等待下一个周期释放 */
        thread->state = KTHREAD_STATE_BLOCKED;
    }
    else
    {
        /* 偶发任务：完成即可 */
        thread->state = KTHREAD_STATE_READY;
    }
}
