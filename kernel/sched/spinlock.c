/**
 * @file    spinlock.c
 * @brief   Ticket Lock 公平自旋锁实现
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 实现所有 spinlock.h 中声明的函数：
 *          - ticket_lock_init:             初始化锁
 *          - ticket_lock_acquire:          获取锁（自旋等待）
 *          - ticket_lock_release:          释放锁
 *          - ticket_lock_try_acquire:      非阻塞尝试获取
 *          - ticket_lock_is_held:          检查锁状态
 *          - ticket_lock_acquire_irqsave:  保存中断状态并获取
 *          - ticket_lock_release_irqrestore: 释放并恢复中断状态
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-004（内核同步原语）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/spinlock.h>
#include <stddef.h>
#include "hal.h"

/* ============================================================================
 * 内部辅助
 * ========================================================================== */

/**
 * @brief 原子获取下一个票号
 * @param lock 锁指针
 * @return 旧票号
 */
static inline uint32_t ticket_lock_fetch_next(volatile uint32_t *lock)
{
    return __atomic_fetch_add(lock, 1U, __ATOMIC_RELAXED);
}

/**
 * @brief 原子读取当前服务票号
 * @param lock 锁指针
 * @return 当前票号
 */
static inline uint32_t ticket_lock_load(const volatile uint32_t *lock)
{
    return __atomic_load_n(lock, __ATOMIC_ACQUIRE);
}

/**
 * @brief 原子写入当前服务票号
 * @param lock 锁指针
 * @param value 要写入的值
 */
static inline void ticket_lock_store_release(volatile uint32_t *lock,
                                             uint32_t value)
{
    __atomic_store_n(lock, value, __ATOMIC_RELEASE);
}

/**
 * @brief 原子比较并交换下一个票号
 * @param lock 锁指针
 * @param expected 期望值
 * @param desired 期望写入值
 * @return true 表示交换成功
 */
static inline bool ticket_lock_cas(volatile uint32_t *lock,
                                   uint32_t *expected,
                                   uint32_t desired)
{
    return __atomic_compare_exchange_n(lock,
                                       expected,
                                       desired,
                                       false,
                                       __ATOMIC_ACQ_REL,
                                       __ATOMIC_ACQUIRE);
}

/* ============================================================================
 * 初始化
 * ========================================================================== */

void ticket_lock_init(TicketLock_t *lock)
{
    if (lock == NULL)
    {
        return;
    }

    lock->next_ticket = 0U;
    lock->serving_ticket = 0U;
}

/* ============================================================================
 * 获取锁（阻塞自旋）
 * ========================================================================== */

void ticket_lock_acquire(TicketLock_t *lock)
{
    uint32_t my_ticket;

    if (lock == NULL)
    {
        return;
    }

    /* 原子地获取一个票号 */
    my_ticket = ticket_lock_fetch_next(&lock->next_ticket);

    /* 自旋等待直到轮到自己 */
    for (;;)
    {
        uint32_t serving;

        serving = ticket_lock_load(&lock->serving_ticket);
        if (serving == my_ticket)
        {
            break;
        }

        /* 使用 HAL 事件等待接口降低功耗 */
        hal_wfe();
    }
}

/* ============================================================================
 * 释放锁
 * ========================================================================== */

void ticket_lock_release(TicketLock_t *lock)
{
    uint32_t serving;

    if (lock == NULL)
    {
        return;
    }

    serving = ticket_lock_load(&lock->serving_ticket);
    if (serving == ticket_lock_load(&lock->next_ticket))
    {
        /* 锁空闲，无可释放（避免误判） */
        return;
    }

    ticket_lock_store_release(&lock->serving_ticket, serving + 1U);

    /* 广播事件，唤醒所有在 WFE 等待的核心 */
    hal_sev();
}

/* ============================================================================
 * 尝试获取锁（非阻塞）
 * ========================================================================== */

bool ticket_lock_try_acquire(TicketLock_t *lock)
{
    uint32_t expected;
    bool success;

    if (lock == NULL)
    {
        return false;
    }

    expected = ticket_lock_load(&lock->next_ticket);
    if (ticket_lock_load(&lock->serving_ticket) != expected)
    {
        return false;
    }

    success = ticket_lock_cas(&lock->next_ticket, &expected, expected + 1U);
    if (success)
    {
        return true;
    }

    return false;
}

/* ============================================================================
 * 检查锁是否被持有
 * ========================================================================== */

bool ticket_lock_is_held(const TicketLock_t *lock)
{
    if (lock == NULL)
    {
        return false;
    }

    return (lock->next_ticket != lock->serving_ticket) ? true : false;
}

/* ============================================================================
 * IRQ 安全的锁操作
 * ========================================================================== */

uint32_t ticket_lock_acquire_irqsave(TicketLock_t *lock)
{
    uint32_t irq_state;

    irq_state = hal_local_irq_saved_state();
    if (lock == NULL)
    {
        return irq_state;
    }

    hal_local_irq_disable();
    ticket_lock_acquire(lock);

    return irq_state;
}

void ticket_lock_release_irqrestore(TicketLock_t *lock, uint32_t irq_state)
{
    if (lock != NULL)
    {
        ticket_lock_release(lock);
    }

    hal_local_irq_restore(irq_state);
}
