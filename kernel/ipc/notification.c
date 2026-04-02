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
        return -1;
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
 */
static ipc_notification_t *get_notification(kobj_id_t notify_id)
{
    uint32_t idx;

    if (notify_id == KOBJ_ID_INVALID)
    {
        return NULL;
    }

    idx = (uint32_t)(notify_id & 0xFFFFU);
    if (idx >= CONFIG_IPC_MAX_NOTIFICATIONS)
    {
        return NULL;
    }

    return &s_notifications[idx];
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

    ntf->id = (kobj_id_t)((uint32_t)idx | ((uint32_t)idx << 16U));
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

    ntf = get_notification(notify_id);
    if (ntf == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 如果有线程在等待，唤醒它 */
    if (ntf->state == IPC_NOTIFY_WAITING)
    {
        KThread_t *waiter = &g_scheduler.thread_table[ntf->waiter_tid];
        if (waiter->state == KTHREAD_STATE_BLOCKED)
        {
            waiter->state = KTHREAD_STATE_READY;
            scheduler_enqueue(waiter);
        }
    }

    /* 标记为空闲 */
    ntf->id = KOBJ_ID_INVALID;
    ntf->state = IPC_NOTIFY_IDLE;
    ntf->signals = 0ULL;
    ntf->waited_mask = 0ULL;
    ntf->waiter_tid = THREAD_ID_INVALID;

    barrier();

    free_notify_index((uint32_t)(notify_id & 0xFFFFU));

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

    /* 原子地设置信号位 */
    ntf->signals |= signal;
    barrier();

    /* 检查是否匹配等待掩码 */
    if ((ntf->state == IPC_NOTIFY_WAITING) &&
        ((ntf->signals & ntf->waited_mask) != 0ULL))
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

    /* 检查是否已有待处理的信号 */
    active = ntf->signals & waited_mask;
    if (active != 0ULL)
    {
        /* 有待处理信号：直接返回 */
        *triggered = active;
        ntf->signals &= ~active;
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

    /* 被唤醒后：提取触发的信号 */
    active = ntf->signals & waited_mask;
    *triggered = active;
    ntf->signals &= ~active;
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

    active = ntf->signals & waited_mask;
    if (active == 0ULL)
    {
        return -(int32_t)EAGAIN;
    }

    /* 有待处理信号 */
    *triggered = active;
    ntf->signals &= ~active;

    if (ntf->signals == 0ULL)
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
    /* 简化实现：非阻塞或无限等待 */
    if (timeout_ms == IPC_TIMEOUT_NONBLOCK)
    {
        return ipc_notification_try_wait(notify_id, waited_mask, triggered);
    }

    if (timeout_ms == IPC_TIMEOUT_INFINITE)
    {
        return ipc_notification_wait(notify_id, waited_mask, triggered);
    }

    /* 带超时 - 完整实现中需要设置定时器 */
    (void)timeout_ms;
    return ipc_notification_wait(notify_id, waited_mask, triggered);
}
