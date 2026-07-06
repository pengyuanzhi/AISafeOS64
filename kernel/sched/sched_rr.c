/**
 * @file    sched_rr.c
 * @brief   RR/FIFO 调度类实现（封装现有优先级位图 + RR 时间片逻辑）
 * @author  AISafe64 Team
 * @date    2026-07-04
 * @version 3.0
 *
 * @details 本文件将 scheduler.c 中既有的优先级位图调度与 RR 时间片
 *          逻辑封装为 sched_class 实例，供调度类框架（sched_class）
 *          统一调度。不重写算法，仅做封装：
 *          - enqueue  -> scheduler_enqueue（位图置位 + 加入优先级链表尾部）
 *          - dequeue  -> scheduler_dequeue（链表移除 + 清位图位）
 *          - pick_next-> scheduler_pick_next_locked（O(1) 位图查找）
 *          - tick     -> RR 时间片递减（耗尽置 need_resched）
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 *
 * @revision history
 * v1.0 2026-07-05 初始版本
 */

#include "sched_class.h"
#include "scheduler.h"
#include "thread.h"

#include <kernel/config.h>
#include <kernel/barrier.h>

/* ========================================================================
 * sched_rr 调度类方法实现
 * ======================================================================== */

/**
 * @brief 将线程加入 RR 就绪队列
 *
 * @details 封装 scheduler_enqueue：位图置位 + 追加到优先级链表尾部。
 *
 * @param thread 要加入的线程
 */
static void sched_rr_enqueue(struct KThread *thread)
{
    scheduler_enqueue(thread);
}

/**
 * @brief 将线程移出 RR 就绪队列
 *
 * @details 封装 scheduler_dequeue：链表移除 + 清位图位。
 *
 * @param thread 要移除的线程
 */
static void sched_rr_dequeue(struct KThread *thread)
{
    scheduler_dequeue(thread);
}

/**
 * @brief O(1) 选择最高优先级就绪线程
 *
 * @details 封装 scheduler_pick_next_locked：在调用者持锁状态下
 *          执行 256 级优先级位图查找，返回最高优先级链表头部线程。
 *          不返回 idle（idle 回退由 schedule() 处理）。
 *
 * @return 最高优先级就绪线程，无就绪线程返回 NULL
 */
static struct KThread *sched_rr_pick_next(void)
{
    return scheduler_pick_next_locked();
}

/**
 * @brief RR 时钟 tick 处理
 *
 * @details 仅对 RR 策略线程递减时间片，耗尽时重载并请求重调度。
 *          与原 scheduler_tick 内 RR 分支逻辑一致：
 *          时间片归零 -> 重载 time_slice_reload -> 置 need_resched。
 *
 * @param current 当前运行的线程
 */
static void sched_rr_tick(struct KThread *current)
{
    if (current == NULL)
    {
        return;
    }

    /* 仅对 RR 策略的线程处理时间片 */
    if (current->policy == KTHREAD_POLICY_RR)
    {
        if (current->time_slice > 0U)
        {
            current->time_slice--;
            if (current->time_slice == 0U)
            {
                /* 时间片耗尽，重载并请求重调度。
                 * 不在中断中直接调 schedule()，仅置位 need_resched，
                 * 由 IRQ 出口（scheduler_irq_exit_check）或 idle 线程处理。 */
                current->time_slice = current->time_slice_reload;
                scheduler_set_need_resched();
                return;
            }
        }
    }
}

/* ========================================================================
 * sched_rr 调度类实例
 * ======================================================================== */

/**
 * @brief RR 调度类实例
 *
 * @details 基础调度类，封装优先级位图 + RR 时间片。
 *          priority = 100（基础调度类，其他高优先级策略可注册更高的值）。
 */
struct sched_class sched_rr = {
    .name      = "rr",
    .priority  = 100,
    .enqueue   = sched_rr_enqueue,
    .dequeue   = sched_rr_dequeue,
    .pick_next = sched_rr_pick_next,
    .tick      = sched_rr_tick,
    .next      = NULL,
};

/* ========================================================================
 * 注册入口
 * ======================================================================== */

/**
 * @brief 注册 sched_rr 调度类
 *
 * @details 在 scheduler_init 中调用：注册为默认调度类。
 */
void sched_rr_init(void)
{
    sched_class_register(&sched_rr);
    sched_class_set_default(&sched_rr);
}
