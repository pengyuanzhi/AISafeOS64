/**
 * @file    ticket_lock_user.c
 * @brief   用户态 Ticket Lock 实现（无 HAL 依赖）
 * @author  AISafe64 Team
 * @date    2026-06-24
 *
 * @details 提供 ticket_lock_init/acquire/release/try_acquire 等函数的
 *          用户态实现，使用 GCC __atomic 内建函数，不依赖 HAL 层。
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
    uint32_t next;
    uint32_t serving;

    if (lock == NULL)
    {
        return false;
    }

    next = __atomic_load_n(&lock->next_ticket, __ATOMIC_RELAXED);
    serving = __atomic_load_n(&lock->serving_ticket, __ATOMIC_RELAXED);

    if (next == serving)
    {
        if (__atomic_compare_exchange_n(&lock->next_ticket, &next, next + 1U,
                                         false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        {
            return true;
        }
    }

    return false;
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
    ticket_lock_acquire(lock);
    return 0U;
}

void ticket_lock_release_irqrestore(TicketLock_t *lock, uint32_t saved_state)
{
    (void)saved_state;
    ticket_lock_release(lock);
}
