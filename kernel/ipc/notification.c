/**
 * @file    notification.c
 * @brief   IPC 通知（Notification）管理实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 实现所有 ipc_notification.h 中声明的函数：
 *          - ipc_notification_subsys_init: 通知子系统初始化
 *          - ipc_notification_create:      创建通知对象
 *          - ipc_notification_destroy:     销毁通知对象
 *          - ipc_notification_signal:      触发信号
 *          - ipc_notification_wait:        等待信号（阻塞）
 *          - ipc_notification_try_wait:    非阻塞等待
 *          - ipc_notification_wait_timeout: 带超时等待
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-006（异步通知，延迟 < 500ns）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/ipc_notification.h>
#include <kernel/ipc_types.h>
#include <kernel/spinlock.h>
#include <kernel/barrier.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <kernel/compiler.h>
#include "thread.h"
#include "scheduler.h"
#include <stdint.h>
#include "hal.h"

/* ========================================================================
 * 全局通知表
 * ======================================================================== */

/**
 * @brief 全局通知对象表（静态分配）
 */
static ipc_notification_t s_notifications[CONFIG_IPC_MAX_NOTIFICATIONS];

/**
 * @brief 空闲通知栈（索引栈）
 */
static uint32_t s_free_notify_stack[CONFIG_IPC_MAX_NOTIFICATIONS];

/**
 * @brief 空闲通知计数
 */
static uint32_t s_free_notify_count;

/**
 * @brief 通知子系统全局锁
 */
static TicketLock_t s_notify_subsys_lock;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 分配空闲通知对象
 *
 * @return 通知索引，无空闲返回 -1
 */
static int32_t alloc_notify_index(void)
{
    int32_t idx;

    ticket_lock_acquire(&s_notify_subsys_lock);

    if (s_free_notify_count == 0U)
    {
        ticket_lock_release(&s_notify_subsys_lock);
        return -(int32_t)EINVAL;
    }

    s_free_notify_count--;
    idx = (int32_t)s_free_notify_stack[s_free_notify_count];

    ticket_lock_release(&s_notify_subsys_lock);

    return idx;
}

/**
 * @brief 释放通知索引
 */
static void free_notify_index(uint32_t idx)
{
    ticket_lock_acquire(&s_notify_subsys_lock);
    s_free_notify_stack[s_free_notify_count] = idx;
    s_free_notify_count++;
    ticket_lock_release(&s_notify_subsys_lock);
}

/**
 * @brief 通过 ID 获取通知对象指针
 *
 * @details 验证 notify_id 中的 generation 与通知结构体中的 generation 匹配，
 *          防止通知对象销毁后索引复用导致旧引用复活（use-after-free）。
 *          ID 编码：高 16 位为 generation，低 16 位为索引（与 endpoint 一致）。
 */
static ipc_notification_t *get_notification(kobj_id_t notify_id)
{
    uint32_t idx;
    uint16_t gen;
    ipc_notification_t *ntf;

    if (notify_id == KOBJ_ID_INVALID)
    {
        return NULL;
    }

    /* ID 的低 16 位为索引 */
    idx = (uint32_t)(notify_id & 0xFFFFU);
    if (idx >= CONFIG_IPC_MAX_NOTIFICATIONS)
    {
        return NULL;
    }

    /* ID 的高 16 位为 generation */
    gen = (uint16_t)((notify_id >> 16U) & 0xFFFFU);

    ntf = &s_notifications[idx];

    /* 验证 generation 匹配 */
    if (ntf->generation != gen)
    {
        return NULL;
    }

    return ntf;
}

/* ========================================================================
 * 通知子系统初始化
 * ======================================================================== */

kernel_status_t ipc_notification_subsys_init(void)
{
    uint32_t i;

    for (i = 0U; i < CONFIG_IPC_MAX_NOTIFICATIONS; i++)
    {
        s_free_notify_stack[i] = (CONFIG_IPC_MAX_NOTIFICATIONS - 1U) - i;
        s_free_notify_count = i + 1U;

        s_notifications[i].id = KOBJ_ID_INVALID;
        s_notifications[i].state = IPC_NOTIFY_IDLE;
        s_notifications[i].signals = 0ULL;
        s_notifications[i].waited_mask = 0ULL;
        s_notifications[i].waiter_tid = THREAD_ID_INVALID;
        s_notifications[i].node.next = &s_notifications[i].node;
        s_notifications[i].node.prev = &s_notifications[i].node;
        s_notifications[i].generation = 0U;
    }

    ticket_lock_init(&s_notify_subsys_lock);

    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 创建通知对象
 * ======================================================================== */

kernel_status_t ipc_notification_create(thread_id_t owner_tid,
                                         kobj_id_t *notify_id)
{
    int32_t idx;
    ipc_notification_t *ntf;

    if (notify_id == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (owner_tid >= CONFIG_MAX_THREADS)
    {
        return -(int32_t)EINVAL;
    }

    idx = alloc_notify_index();
    if (idx < 0)
    {
        return -(int32_t)ENOMEM;
    }

    ntf = &s_notifications[(uint32_t)idx];

    /* 递增 generation（跳过 0 以避免与初始化值冲突），使旧 ID 失效 */
    ntf->generation++;
    if (ntf->generation == 0U)
    {
        ntf->generation = 1U;
    }

    /* 生成通知 ID：高 16 位为 generation，低 16 位为索引（与 endpoint 一致） */
    ntf->id = (kobj_id_t)(((uint32_t)ntf->generation << 16U) | (uint32_t)idx);
    ntf->state = IPC_NOTIFY_IDLE;
    ntf->signals = 0ULL;
    ntf->waited_mask = 0ULL;
    ntf->waiter_tid = THREAD_ID_INVALID;

    barrier();

    *notify_id = ntf->id;

    return KERNEL_OK;
}

/* ========================================================================
 * 销毁通知对象
 * ======================================================================== */

kernel_status_t ipc_notification_destroy(kobj_id_t notify_id)
{
    ipc_notification_t *ntf;
    KThread_t *wake_waiter = NULL;
    uint32_t idx;

    /*
     * 销毁必须在锁保护下完成，与 signal/wait 路径串行化，避免：
     *  - 与 signal 竞争唤醒已失效的 waiter_tid（数组越界）
     *  - 与 wait 竞争复用 waiter_tid 字段
     * 延迟唤醒：持锁期间仅把等待者标记为 READY，释放锁后再
     * scheduler_enqueue（遵循 A3 延迟唤醒模式，避免持子系统锁 →
     * 调度队列锁的锁升级）。
     */
    ticket_lock_acquire(&s_notify_subsys_lock);

    ntf = get_notification(notify_id);
    if (ntf == NULL)
    {
        ticket_lock_release(&s_notify_subsys_lock);
        return -(int32_t)EINVAL;
    }

    /* 如果有线程在等待，唤醒它（先做边界检查，防止 waiter_tid 越界） */
    if (ntf->state == IPC_NOTIFY_WAITING)
    {
        if ((ntf->waiter_tid != THREAD_ID_INVALID) &&
            (ntf->waiter_tid < CONFIG_MAX_THREADS))
        {
            KThread_t *waiter = &g_scheduler.thread_table[ntf->waiter_tid];
            if (waiter->state == KTHREAD_STATE_BLOCKED)
            {
                waiter->state = KTHREAD_STATE_READY;
                wake_waiter = waiter;
            }
        }
    }

    /* 标记为空闲 */
    ntf->id = KOBJ_ID_INVALID;
    ntf->state = IPC_NOTIFY_IDLE;
    ntf->signals = 0ULL;
    ntf->waited_mask = 0ULL;
    ntf->waiter_tid = THREAD_ID_INVALID;

    /* 递增 generation 使旧 ID 失效（防止索引复用导致 use-after-free） */
    ntf->generation++;
    if (ntf->generation == 0U)
    {
        ntf->generation = 1U;
    }

    idx = (uint32_t)(notify_id & 0xFFFFU);

    barrier();

    ticket_lock_release(&s_notify_subsys_lock);

    /* 锁已释放，此时安全获取调度队列锁唤醒等待线程 */
    if (wake_waiter != NULL)
    {
        scheduler_enqueue(wake_waiter);
    }

    /* free_notify_index 内部获取 s_notify_subsys_lock（不可重入，故在释放后调用） */
    free_notify_index(idx);

    return KERNEL_OK;
}

/* ========================================================================
 * 触发通知信号
 * ======================================================================== */

kernel_status_t ipc_notification_signal(kobj_id_t notify_id,
                                         uint64_t signal)
{
    ipc_notification_t *ntf;

    if (signal == 0ULL)
    {
        return -(int32_t)EINVAL;
    }

    ntf = get_notification(notify_id);
    if (ntf == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /*
     * P1-16 安全修复：signals 字段改为原子按位或。
     *
     * 此函数可在中断上下文（irq_dispatch → ipc_notification_signal）
     * 调用，与线程上下文的 wait/try_wait 并发消费 signals（&= ~active）。
     * 原先 ntf->signals |= signal 为非原子读-改-写，会丢失中断中置位的
     * 信号位或破坏线程中清位的副作用。改为 atomic_or_u64 后由
     * LDXR/STXR 独占访问保证原子性，中断与多核均安全。
     */
    (void)atomic_or_u64(&ntf->signals, signal);

    /* 原子读取消费前的快照，用于匹配等待掩码 */
    uint64_t cur_signals = atomic_load_acquire_u64(&ntf->signals);

    /* 检查是否匹配等待掩码 */
    if ((ntf->state == IPC_NOTIFY_WAITING) &&
        ((cur_signals & ntf->waited_mask) != 0ULL))
    {
        /* 唤醒等待线程 */
        if (ntf->waiter_tid < CONFIG_MAX_THREADS)
        {
            KThread_t *waiter = &g_scheduler.thread_table[ntf->waiter_tid];
            if (waiter->state == KTHREAD_STATE_BLOCKED)
            {
                waiter->state = KTHREAD_STATE_READY;
                scheduler_enqueue(waiter);
            }
        }

        ntf->state = IPC_NOTIFY_PENDING;
    }
    else
    {
        ntf->state = IPC_NOTIFY_PENDING;
    }

    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 等待通知信号（阻塞）
 * ======================================================================== */

kernel_status_t ipc_notification_wait(kobj_id_t notify_id,
                                       uint64_t waited_mask,
                                       uint64_t *triggered)
{
    ipc_notification_t *ntf;
    KThread_t *current;
    uint64_t active;

    if ((waited_mask == 0ULL) || (triggered == NULL))
    {
        return -(int32_t)EINVAL;
    }

    ntf = get_notification(notify_id);
    if (ntf == NULL)
    {
        return -(int32_t)EINVAL;
    }

    current = kthread_get_current();
    if (current == NULL)
    {
        return -(int32_t)ESRCH;
    }

    /* 检查是否已有待处理的信号（原子读取，避免与中断 signal 竞态） */
    active = atomic_load_acquire_u64(&ntf->signals) & waited_mask;
    if (active != 0ULL)
    {
        /* 有待处理信号：直接返回，原子清除已消费的位 */
        *triggered = active;
        (void)atomic_and_u64(&ntf->signals, ~active);
        ntf->state = IPC_NOTIFY_IDLE;
        barrier();
        return KERNEL_OK;
    }

    /* 无待处理信号：阻塞等待 */
    ntf->state = IPC_NOTIFY_WAITING;
    ntf->waited_mask = waited_mask;
    ntf->waiter_tid = current->tid;
    current->state = KTHREAD_STATE_BLOCKED;
    barrier();

    schedule();

    /* 被唤醒后：提取触发的信号，原子清除已消费的位 */
    active = atomic_load_acquire_u64(&ntf->signals) & waited_mask;
    *triggered = active;
    (void)atomic_and_u64(&ntf->signals, ~active);
    ntf->waited_mask = 0ULL;
    ntf->waiter_tid = THREAD_ID_INVALID;
    ntf->state = IPC_NOTIFY_IDLE;
    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 非阻塞等待
 * ======================================================================== */

kernel_status_t ipc_notification_try_wait(kobj_id_t notify_id,
                                           uint64_t waited_mask,
                                           uint64_t *triggered)
{
    ipc_notification_t *ntf;
    uint64_t active;

    if ((waited_mask == 0ULL) || (triggered == NULL))
    {
        return -(int32_t)EINVAL;
    }

    ntf = get_notification(notify_id);
    if (ntf == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 原子读取待处理信号，避免与中断 signal 竞态 */
    active = atomic_load_acquire_u64(&ntf->signals) & waited_mask;
    if (active == 0ULL)
    {
        return -(int32_t)EAGAIN;
    }

    /* 有待处理信号：原子清除已消费的位 */
    *triggered = active;
    uint64_t remaining = atomic_and_u64(&ntf->signals, ~active) & ~active;

    if (remaining == 0ULL)
    {
        ntf->state = IPC_NOTIFY_IDLE;
    }

    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 带超时等待
 * ======================================================================== */

kernel_status_t ipc_notification_wait_timeout(kobj_id_t notify_id,
                                               uint64_t waited_mask,
                                               uint64_t *triggered,
                                               uint32_t timeout_ms)
{
    /*
     * 实时性安全的超时语义（与 ipc_msg_send_timeout 一致）：
     *  - IPC_TIMEOUT_NONBLOCK (0)：非阻塞 try_wait，无信号时返回 -EAGAIN，
     *    绝不阻塞调用线程（修复"函数名带 timeout 却永久阻塞"的 P1 缺陷）。
     *  - IPC_TIMEOUT_INFINITE：永久阻塞，与 ipc_notification_wait 一致。
     *  - 其他有限超时值：当前阶段仍调用阻塞 wait（避免引入端点超时
     *    扫描的复杂度），后续可扩展为基于定时器中断的真正超时唤醒。
     */
    if (timeout_ms == IPC_TIMEOUT_NONBLOCK)
    {
        return ipc_notification_try_wait(notify_id, waited_mask, triggered);
    }

    /* 阻塞模式（无限等待或有限超时暂统一走阻塞等待） */
    return ipc_notification_wait(notify_id, waited_mask, triggered);
}
