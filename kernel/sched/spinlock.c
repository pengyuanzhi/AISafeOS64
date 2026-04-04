/**
 * @file    spinlock.c
 * @brief   Ticket Lock 公平自旋锁实现
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 实现所有 spinlock.h 中声明的函数：
 *          - ticket_lock_init:           初始化锁
 *          - ticket_lock_acquire:        获取锁（自旋等待）
 *          - ticket_lock_release:        释放锁
 *          - ticket_lock_try_acquire:    非阻塞尝试获取
 *          - ticket_lock_is_held:        检查锁状态
 *          - ticket_lock_acquire_irqsave:   保存中断状态并获取
 *          - ticket_lock_release_irqrestore: 释放并恢复中断状态
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-004（内核同步原语）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/spinlock.h>
#include <kernel/barrier.h>
#include <kernel/compiler.h>
#include <kernel/types.h>
#include <stdint.h>
#include "hal.h"

/* HAL 接口 */
/* ========================================================================
 * 初始化
 * ======================================================================== */

void ticket_lock_init(TicketLock_t *lock)
{
    if (lock == NULL)
    {
        return;
    }

    lock->next_ticket = 0U;
    lock->serving_ticket = 0U;
    lock->cpu_id = 0xFFFFFFFFU;
    lock->nest_count = 0U;
    barrier();
}

/* ========================================================================
 * 获取锁（阻塞自旋）
 * ======================================================================== */

void ticket_lock_acquire(TicketLock_t *lock)
{
    uint32_t my_ticket;

    if (lock == NULL)
    {
        return;
    }

    /* 原子地获取一个票号 */
    my_ticket = atomic_inc_u32(&lock->next_ticket);

    /* 自旋等待直到轮到自己 */
    for (;;)
    {
        uint32_t serving = atomic_load_acquire_u32(&lock->serving_ticket);
        if (serving == my_ticket)
        {
            break;
        }

        /* 使用 WFE 降低功耗，等待 SEV 唤醒 */
        WFE();
    }

    /* 获取锁后的内存屏障 */
    barrier();

    lock->cpu_id = hal_get_cpu_id();
    lock->nest_count++;
}

/* ========================================================================
 * 释放锁
 * ======================================================================== */

void ticket_lock_release(TicketLock_t *lock)
{
    if (lock == NULL)
    {
        return;
    }

    lock->nest_count--;
    lock->cpu_id = 0xFFFFFFFFU;

    /* 释放锁前的内存屏障 */
    barrier();

    /* 递增 serving_ticket，唤醒下一个等待者 */
    atomic_store_release_u32(&lock->serving_ticket,
                             lock->serving_ticket + 1U);

    /* 广播事件，唤醒所有在 WFE 等待的核心 */
    SEV();
}

/* ========================================================================
 * 尝试获取锁（非阻塞）
 * ======================================================================== */

bool ticket_lock_try_acquire(TicketLock_t *lock)
{
    uint32_t expected;
    bool success;

    if (lock == NULL)
    {
        return false;
    }

    /* 读取当前票号 */
    expected = atomic_load_acquire_u32(&lock->next_ticket);

    /* 检查锁是否空闲 */
    if (atomic_load_acquire_u32(&lock->serving_ticket) != expected)
    {
        return false;
    }

    /* 尝试原子地递增 next_ticket */
    success = atomic_cas_u32(&lock->next_ticket, expected, expected + 1U);
    if (success)
    {
        barrier();
        lock->cpu_id = hal_get_cpu_id();
        lock->nest_count++;
        return true;
    }

    return false;
}

/* ========================================================================
 * 检查锁是否被持有
 * ======================================================================== */

bool ticket_lock_is_held(const TicketLock_t *lock)
{
    if (lock == NULL)
    {
        return false;
    }

    return (lock->next_ticket != lock->serving_ticket) ? true : false;
}

/* ========================================================================
 * IRQ 安全的锁操作
 * ======================================================================== */

uint32_t ticket_lock_acquire_irqsave(TicketLock_t *lock)
{
    uint32_t irq_state;

    /* 先保存并禁用中断 */
    irq_state = hal_irq_saved_state();
    hal_irq_disable();

    /* 再获取自旋锁 */
    ticket_lock_acquire(lock);

    return irq_state;
}

void ticket_lock_release_irqrestore(TicketLock_t *lock, uint32_t irq_state)
{
    /* 先释放自旋锁 */
    ticket_lock_release(lock);

    /* 再恢复中断状态 */
    hal_irq_restore(irq_state);
}
