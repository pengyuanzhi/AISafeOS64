/**
 * @file    user_timer.c
 * @brief   用户态定时器实现
 * @author  AISafe64 Team
 * @date    2026-07-05
 * @version 1.0
 *
 * @details 基于 per-CPU tick 计数的用户定时器：
 *          - 活跃定时器按 expire_tick 有序排列在链表中
 *          - user_timer_tick 在每个 tick 检查链表头部
 *          - 到期投递 notification + 重载（周期）或移除（单次）
 *
 * @note MISRA-C:2012 合规
 *
 * @revision history
 * v1.0 2026-07-05 初始版本
 */

#include <kernel/user_timer.h>
#include <kernel/config.h>
#include <kernel/errno.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <kernel/ipc_notification.h>
#include "hal.h"
#include "timer.h"
#include "../../sched/thread.h"
#include <stdint.h>

#ifndef CONFIG_MAX_USER_TIMERS
#define CONFIG_MAX_USER_TIMERS 32U
#endif

/** @brief 用户定时器静态池 */
static user_timer_t s_timers[CONFIG_MAX_USER_TIMERS];

/** @brief 活跃定时器链表头（按 expire_tick 有序） */
static struct list_head s_active_list;

/** @brief 下一个定时器 ID */
static uint32_t s_next_timer_id = 1U;

/** @brief 子系统锁 */
static TicketLock_t s_timer_lock;

/** @brief 初始化标志 */
static bool s_initialized = false;

/**
 * @brief 系统 tick 计数（从 timer.c 引用）
 */
extern tick_t s_system_ticks;

/* ========================================================================
 * 内部辅助
 * ======================================================================== */

/**
 * @brief 按序插入活跃链表
 *
 * @details 按 expire_tick 升序插入，保证链表头部是最早到期的。
 *          调用者持锁。
 *
 * @param timer 要插入的定时器
 */
static void timer_insert_ordered(user_timer_t *timer)
{
    struct list_head *pos;
    user_timer_t *entry;

    list_for_each(pos, &s_active_list)
    {
        entry = list_entry(pos, user_timer_t, node);
        if (entry->expire_tick > timer->expire_tick)
        {
            break;
        }
    }

    list_add_tail(&timer->node, pos);
}

/**
 * @brief 按 ID 查找定时器
 *
 * @param timer_id 定时器 ID
 * @return 定时器指针，未找到返回 NULL
 */
static user_timer_t *timer_find(uint32_t timer_id)
{
    uint32_t i;

    for (i = 0U; i < CONFIG_MAX_USER_TIMERS; i++)
    {
        if (s_timers[i].timer_id == timer_id)
        {
            return &s_timers[i];
        }
    }
    return NULL;
}

/* ========================================================================
 * 公共 API
 * ======================================================================== */

kernel_status_t user_timer_subsys_init(void)
{
    uint32_t i;

    for (i = 0U; i < CONFIG_MAX_USER_TIMERS; i++)
    {
        s_timers[i].timer_id = 0U;
        s_timers[i].owner_tid = 0U;
        s_timers[i].notify_ep = 0U;
        s_timers[i].expire_tick = 0U;
        s_timers[i].interval = 0U;
        s_timers[i].active = false;
        INIT_LIST_HEAD(&s_timers[i].node);
    }

    INIT_LIST_HEAD(&s_active_list);
    ticket_lock_init(&s_timer_lock);
    s_next_timer_id = 1U;
    s_initialized = true;

    return KERNEL_OK;
}

kernel_status_t user_timer_create(uint32_t owner_tid, uint32_t notify_ep,
                                   uint32_t *out_id)
{
    uint32_t i;

    if ((out_id == NULL) || !s_initialized)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&s_timer_lock);

    for (i = 0U; i < CONFIG_MAX_USER_TIMERS; i++)
    {
        if (s_timers[i].timer_id == 0U)
        {
            s_timers[i].timer_id = s_next_timer_id;
            s_next_timer_id++;
            s_timers[i].owner_tid = owner_tid;
            s_timers[i].notify_ep = notify_ep;
            s_timers[i].expire_tick = 0U;
            s_timers[i].interval = 0U;
            s_timers[i].active = false;
            INIT_LIST_HEAD(&s_timers[i].node);

            *out_id = s_timers[i].timer_id;

            ticket_lock_release(&s_timer_lock);
            return KERNEL_OK;
        }
    }

    ticket_lock_release(&s_timer_lock);
    return -(int32_t)ENOMEM;
}

kernel_status_t user_timer_settime(uint32_t timer_id, uint32_t ms,
                                    uint32_t interval_ms)
{
    user_timer_t *timer;
    tick_t ticks;

    if (!s_initialized)
    {
        return -(int32_t)EINVAL;
    }

    timer = timer_find(timer_id);
    if (timer == NULL)
    {
        return -(int32_t)ENOENT;
    }

    ticks = (tick_t)((uint64_t)ms * (uint64_t)CONFIG_TICK_RATE_HZ / 1000U);
    if (ticks == 0U)
    {
        ticks = 1U;
    }

    ticket_lock_acquire(&s_timer_lock);

    if (timer->active)
    {
        list_del_init(&timer->node);
    }

    timer->expire_tick = s_system_ticks + ticks;
    timer->interval = (interval_ms > 0U)
        ? (tick_t)((uint64_t)interval_ms * (uint64_t)CONFIG_TICK_RATE_HZ / 1000U)
        : 0U;
    timer->active = true;

    timer_insert_ordered(timer);

    ticket_lock_release(&s_timer_lock);

    return KERNEL_OK;
}

kernel_status_t user_timer_delete(uint32_t timer_id)
{
    user_timer_t *timer;

    if (!s_initialized)
    {
        return -(int32_t)EINVAL;
    }

    timer = timer_find(timer_id);
    if (timer == NULL)
    {
        return -(int32_t)ENOENT;
    }

    ticket_lock_acquire(&s_timer_lock);

    if (timer->active)
    {
        list_del_init(&timer->node);
    }

    timer->timer_id = 0U;
    timer->active = false;

    ticket_lock_release(&s_timer_lock);

    return KERNEL_OK;
}

uint64_t user_clock_gettime(void)
{
    uint64_t count;
    uint64_t freq;

    count = hal_timer_get_count();
    freq = hal_timer_get_freq();

    if (freq == 0U)
    {
        return 0ULL;
    }

    return (count * 1000000000ULL) / freq;
}

kernel_status_t user_nanosleep(uint64_t ns)
{
    uint64_t ticks;

    if (ns == 0U)
    {
        return KERNEL_OK;
    }

    ticks = (ns * (uint64_t)CONFIG_TICK_RATE_HZ) / 1000000000ULL;
    if (ticks == 0U)
    {
        ticks = 1U;
    }

    kthread_sleep_ticks((tick_t)ticks);

    return KERNEL_OK;
}

void user_timer_tick(void)
{
    if (!s_initialized)
    {
        return;
    }

    ticket_lock_acquire(&s_timer_lock);

    while (!list_empty(&s_active_list))
    {
        user_timer_t *timer;
        struct list_head *first;

        first = s_active_list.next;
        timer = list_entry(first, user_timer_t, node);

        if (timer->expire_tick > s_system_ticks)
        {
            break;
        }

        list_del_init(&timer->node);

        if (timer->notify_ep != 0U)
        {
            ipc_notification_signal(timer->notify_ep, 1ULL);
        }

        if (timer->interval > 0U)
        {
            timer->expire_tick += timer->interval;
            timer_insert_ordered(timer);
        }
        else
        {
            timer->active = false;
        }
    }

    ticket_lock_release(&s_timer_lock);
}
