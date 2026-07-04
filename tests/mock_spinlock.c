/**
 * @file    mock_spinlock.c
 * @brief   Ticket Lock 宿主机测试桩实现
 * @author  AISafe64 Team
 * @date    2026-07-04
 *
 * @details 为链接真实内核源码（如 kernel/mm/kmalloc.c）的宿主机单元测试
 *          提供 ticket_lock_* 函数的定义。真实内核中这些函数定义在
 *          kernel/sched/spinlock.c（依赖 HAL 层），宿主机测试无法链接，
 *          故此处提供不依赖 HAL 的等价实现。
 *
 *          使用 GCC __atomic 内建函数，逻辑与内核 spinlock.c 一致：
 *          - acquire: 原子取票后自旋等待服务号
 *          - release: 原子递增服务号
 *          不再做 cpu_id 递归检测（与内核 spinlock.c 同步移除）。
 *
 * @note MISRA-C:2012 合规
 */

#include <kernel/spinlock.h>
#include <stddef.h>
#include <stdint.h>

void ticket_lock_init(TicketLock_t *lock)
{
    if (lock == NULL)
    {
        return;
    }

    lock->next_ticket = 0U;
    lock->serving_ticket = 0U;
}

void ticket_lock_acquire(TicketLock_t *lock)
{
    uint32_t my_ticket;
    uint32_t serving;

    if (lock == NULL)
    {
        return;
    }

    my_ticket = __atomic_fetch_add(&lock->next_ticket, 1U, __ATOMIC_RELAXED);

    do
    {
        serving = __atomic_load_n(&lock->serving_ticket, __ATOMIC_ACQUIRE);
    } while (serving != my_ticket);
}

void ticket_lock_release(TicketLock_t *lock)
{
    if (lock == NULL)
    {
        return;
    }

    __atomic_store_n(&lock->serving_ticket,
                     lock->serving_ticket + 1U,
                     __ATOMIC_RELEASE);
}

bool ticket_lock_try_acquire(TicketLock_t *lock)
{
    uint32_t expected;

    if (lock == NULL)
    {
        return false;
    }

    expected = __atomic_load_n(&lock->next_ticket, __ATOMIC_RELAXED);
    if (__atomic_load_n(&lock->serving_ticket, __ATOMIC_RELAXED) != expected)
    {
        return false;
    }

    return __atomic_compare_exchange_n(&lock->next_ticket,
                                        &expected,
                                        expected + 1U,
                                        false,
                                        __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED);
}

bool ticket_lock_is_held(const TicketLock_t *lock)
{
    if (lock == NULL)
    {
        return false;
    }

    return (lock->next_ticket != lock->serving_ticket) ? true : false;
}

uint32_t ticket_lock_acquire_irqsave(TicketLock_t *lock)
{
    /* 宿主机测试无真实中断概念，仅获取锁并返回占位状态 */
    ticket_lock_acquire(lock);
    return 0U;
}

void ticket_lock_release_irqrestore(TicketLock_t *lock, uint32_t irq_state)
{
    (void)irq_state;
    ticket_lock_release(lock);
}
