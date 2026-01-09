# LLD-003: Synchronization and Communication Low-Level Design

**Document ID**: LLD-003
**Version**: 1.0
**Date**: 2025-01-09
**Author**: AISafe64 Team
**Status**: Draft
**Parent**: HLD-003 (Kernel Module Design)

---

## 1. Module Overview

### 1.1 Purpose
The Synchronization and Communication module provides thread-safe primitives for task coordination, including mutexes (with priority inheritance), semaphores, spinlocks, event flags, and message queues. Designed for safety-critical systems with bounded execution times and deterministic behavior.

### 1.2 Scope
This document describes the low-level design of:
- Mutex locks with priority inheritance protocol
- Counting semaphores
- Ticket spinlocks (fair SMP locks)
- Event flag groups
- Message queues
- Condition variables

### 1.3 References
- POSIX 1003.1-2017 (PSE52)
- MISRA-C:2012 Guidelines
- HLD-003: Kernel Module Design
- plan.md Section 4.2 (Data Structures)

---

## 2. Data Structure Design

### 2.1 Mutex Structure

```c
/**
 * @brief Mutex Lock (with priority inheritance)
 * @note MISRA-C:2012 compliant
 * @note 64-byte cache-line aligned for SMP
 */
typedef struct __attribute__((aligned(64))) Mutex
{
    atomic_uint_fast32_t lock;        /**< Lock state (0=unlocked, 1=locked) */
    TCB_t             *owner;         /**< Current owner task */
    uint8_t             ceiling;       /**< Priority ceiling (0-255) */
    uint8_t             original_prio; /**< Owner's original priority */
    TaskList_t          wait_list;     /**< Blocked tasks */
    uint32_t            nest_count;    /**< Nesting count (for recursive) */
    uint32_t            flags;         /**< Flags (see MUTEX_*) */

} Mutex_t;

/* Mutex flags */
#define MUTEX_PRIORITY_INHERITANCE  (1U << 0)  /**< Enable PI protocol */
#define MUTEX_PRIORITY_CEILING      (1U << 1)  /**< Enable priority ceiling */
#define MUTEX_RECURSIVE             (1U << 2)  /**< Allow recursive locking */

/* Compile-time validation */
STATIC_ASSERT(sizeof(Mutex_t) <= 64U, Mutex_size_exceeds_cache_line);
```

### 2.2 Semaphore Structure

```c
/**
 * @brief Counting Semaphore
 * @note POSIX sem_t compatible
 */
typedef struct
{
    atomic_uint_fast32_t count;       /**< Semaphore count */
    atomic_uint_fast32_t max_count;   /**< Maximum count */
    TaskList_t          wait_list;    /**< Blocked tasks */
    uint32_t            flags;        /**< Flags (see SEM_*) */

} Semaphore_t;

/* Semaphore flags */
#define SEM_FIFO               (1U << 0)  /**< FIFO ordering (default: priority) */
#define SEM_BINARY             (1U << 1)  /**< Binary semaphore (max=1) */
```

### 2.3 Ticket Spinlock Structure

```c
/**
 * @brief Ticket Spinlock (fair FIFO lock)
 * @note ARM64 optimized with WFE instruction
 * @note Safe for SMP usage
 */
typedef struct
{
    atomic_uint_fast16_t next_ticket;    /**< Next ticket to serve */
    atomic_uint_fast16_t serving_ticket; /**< Currently serving ticket */

} TicketLock_t;

/* Compile-time validation */
STATIC_ASSERT(sizeof(TicketLock_t) == 4U, TicketLock_size_mismatch);
```

### 2.4 Event Flag Group Structure

```c
/**
 * @brief Event Flag Group
 * @note 32-bit event flags (32 independent events)
 */
typedef struct
{
    atomic_uint_fast32_t flags;         /**< Event flags (bitmask) */
    TaskList_t          wait_list;      /**< Blocked tasks */

} EventGroup_t;

/* Event wait types */
#define EV_WAIT_ANY            (1U << 0)  /**< Wait for any flag */
#define EV_WAIT_ALL            (1U << 1)  /**< Wait for all flags */
#define EV_AUTO_CLEAR         (1U << 2)  /**< Auto-clear on exit */
```

### 2.5 Message Queue Structure

```c
/**
 * @brief Message Queue (fixed-size messages)
 * @note Ring buffer implementation
 */
typedef struct
{
    void               *buffer;        /**< Ring buffer */
    uint32_t            capacity;      /**< Queue capacity (messages) */
    uint32_t            msg_size;      /**< Message size (bytes) */
    atomic_uint_fast32_t head;         /**< Write index */
    atomic_uint_fast32_t tail;         /**< Read index */
    atomic_uint_fast32_t count;        /**< Current message count */
    atomic_uint_fast32_t lock;         /**< Queue lock */

    TaskList_t          tx_wait_list;  /**< Blocked senders */
    TaskList_t          rx_wait_list;  /**< Blocked receivers */

} MessageQueue_t;

/* Compile-time validation */
STATIC_ASSERT(sizeof(MessageQueue_t) <= 128U, MessageQueue_too_large);
```

### 2.6 Condition Variable Structure

```c
/**
 * @brief Condition Variable (POSIX pthread_cond_t)
 * @note Must be used with mutex
 */
typedef struct
{
    TaskList_t          wait_list;     /**< Blocked tasks */
    uint32_t            wait_count;    /**< Number of waiting tasks */

} ConditionVariable_t;
```

---

## 3. API Interface Definition

### 3.1 Mutex API

```c
/**
 * @brief Initialize mutex
 * @param mutex Mutex to initialize
 * @param flags Flags (MUTEX_PRIORITY_INHERITANCE, MUTEX_RECURSIVE, ...)
 * @return 0 on success, negative error code on failure
 *
 * @note Must be called before first use
 */
int32_t mutex_init(Mutex_t *mutex, uint32_t flags);

/**
 * @brief Lock mutex (blocking)
 * @param mutex Mutex to lock
 * @return 0 on success, negative error code on failure
 *
 * @note Blocks if mutex already locked
 * @note Supports recursive locking if MUTEX_RECURSIVE set
 * @warning Do not call from interrupt context
 */
int32_t mutex_lock(Mutex_t *mutex);

/**
 * @brief Try to lock mutex (non-blocking)
 * @param mutex Mutex to lock
 * @return 0 on success, -EBUSY if mutex locked
 */
int32_t mutex_trylock(Mutex_t *mutex);

/**
 * @brief Unlock mutex
 * @param mutex Mutex to unlock
 * @return 0 on success, negative error code on failure
 *
 * @note Wakes highest-priority waiting task
 * @warning Must be called by owner task
 */
int32_t mutex_unlock(Mutex_t *mutex);

/**
 * @brief Destroy mutex
 * @param mutex Mutex to destroy
 * @return 0 on success, negative error code on failure
 *
 * @warning No operations allowed after destroy
 */
int32_t mutex_destroy(Mutex_t *mutex);
```

### 3.2 Semaphore API

```c
/**
 * @brief Initialize semaphore
 * @param sem Semaphore to initialize
 * @param initial_count Initial count
 * @param max_count Maximum count
 * @param flags Flags (SEM_FIFO, SEM_BINARY, ...)
 * @return 0 on success, negative error code on failure
 */
int32_t sem_init(Semaphore_t *sem,
                 uint32_t initial_count,
                 uint32_t max_count,
                 uint32_t flags);

/**
 * @brief Wait on semaphore (P operation)
 * @param sem Semaphore
 * @param timeout_ms Timeout in milliseconds (0 = infinite)
 * @return 0 on success, -ETIMEDOUT on timeout
 *
 * @note Decrements count if > 0, else blocks
 * @warning Do not call from interrupt context
 */
int32_t sem_wait(Semaphore_t *sem, uint32_t timeout_ms);

/**
 * @brief Signal semaphore (V operation)
 * @param sem Semaphore
 * @return 0 on success, negative error code on failure
 *
 * @note Increments count, wakes waiting task
 * @note Safe to call from interrupt context
 */
int32_t sem_post(Semaphore_t *sem);

/**
 * @brief Get current semaphore count
 * @param sem Semaphore
 * @return Current count
 */
uint32_t sem_get_count(Semaphore_t *sem);

/**
 * @brief Destroy semaphore
 * @param sem Semaphore
 * @return 0 on success, negative error code on failure
 */
int32_t sem_destroy(Semaphore_t *sem);
```

### 3.3 Spinlock API

```c
/**
 * @brief Acquire ticket spinlock
 * @param lock Spinlock
 *
 * @note Blocks until ticket matches serving number
 * @note Uses WFE instruction to reduce power
 * @note Safe for SMP usage
 * @warning Do not sleep while holding spinlock
 */
static inline void spin_lock(TicketLock_t *lock)
{
    uint16_t my_ticket = atomic_fetch_add_explicit(
        &lock->next_ticket, 1U, memory_order_acquire);

    while (atomic_load_explicit(&lock->serving_ticket,
                                memory_order_acquire) != my_ticket)
    {
        /* Wait for event (low-power) */
        __asm__ volatile("wfe");
    }

    /* Acquire barrier */
    atomic_thread_fence(memory_order_acquire);
}

/**
 * @brief Release ticket spinlock
 * @param lock Spinlock
 */
static inline void spin_unlock(TicketLock_t *lock)
{
    /* Release barrier */
    atomic_thread_fence(memory_order_release);

    atomic_fetch_add_explicit(&lock->serving_ticket, 1U,
                             memory_order_release);

    /* Send event to waiting CPUs */
    __asm__ volatile("sev");
}

/**
 * @brief Try to acquire spinlock (non-blocking)
 * @param lock Spinlock
 * @return 0 on success, -EBUSY if lock held
 */
static inline int32_t spin_trylock(TicketLock_t *lock)
{
    uint16_t my_ticket = atomic_load_explicit(&lock->next_ticket,
                                              memory_order_acquire);
    uint16_t serving = atomic_load_explicit(&lock->serving_ticket,
                                            memory_order_acquire);

    if (my_ticket != serving)
    {
        return -EBUSY;
    }

    /* Try to claim ticket */
    uint16_t expected = my_ticket;
    if (!atomic_compare_exchange_strong_explicit(
            &lock->next_ticket, &expected, my_ticket + 1U,
            memory_order_acquire, memory_order_acquire))
    {
        return -EBUSY;
    }

    return 0;
}
```

### 3.4 Event Flag API

```c
/**
 * @brief Create event flag group
 * @return Event group handle, or NULL on failure
 */
EventGroup_t* event_create(void);

/**
 * @brief Wait for event flags
 * @param group Event group
 * @param flags Flags to wait for (bitmask)
 * @param wait_type EV_WAIT_ANY or EV_WAIT_ALL
 * @param timeout_ms Timeout (0 = infinite)
 * @return 0 on success, -ETIMEDOUT on timeout
 *
 * @note Clears flags if EV_AUTO_CLEAR set
 */
int32_t event_wait(EventGroup_t *group,
                   uint32_t flags,
                   uint32_t wait_type,
                   uint32_t timeout_ms);

/**
 * @brief Set event flags
 * @param group Event group
 * @param flags Flags to set (bitmask)
 * @return Previous flags value
 *
 * @note Wakes all tasks whose wait condition is satisfied
 * @note Safe to call from interrupt context
 */
uint32_t event_set(EventGroup_t *group, uint32_t flags);

/**
 * @brief Clear event flags
 * @param group Event group
 * @param flags Flags to clear (bitmask)
 * @return Previous flags value
 */
uint32_t event_clear(EventGroup_t *group, uint32_t flags);

/**
 * @brief Destroy event flag group
 * @param group Event group
 * @return 0 on success, negative error code on failure
 */
int32_t event_destroy(EventGroup_t *group);
```

### 3.5 Message Queue API

```c
/**
 * @brief Create message queue
 * @param capacity Queue capacity (messages)
 * @param msg_size Message size (bytes)
 * @return Queue handle, or NULL on failure
 *
 * @note Allocates ring buffer
 */
MessageQueue_t* msgq_create(uint32_t capacity, uint32_t msg_size);

/**
 * @brief Send message to queue
 * @param queue Message queue
 * @param msg Message buffer
 * @param timeout_ms Timeout (0 = infinite)
 * @return 0 on success, -ETIMEDOUT on timeout, -EAGAIN if queue full
 *
 * @note Blocks if queue full
 * @warning Do not call from interrupt context
 */
int32_t msgq_send(MessageQueue_t *queue,
                  const void *msg,
                  uint32_t timeout_ms);

/**
 * @brief Receive message from queue
 * @param queue Message queue
 * @param msg Message buffer
 * @param timeout_ms Timeout (0 = infinite)
 * @return 0 on success, -ETIMEDOUT on timeout
 *
 * @note Blocks if queue empty
 * @warning Do not call from interrupt context
 */
int32_t msgq_receive(MessageQueue_t *queue,
                     void *msg,
                     uint32_t timeout_ms);

/**
 * @brief Send message (non-blocking)
 * @param queue Message queue
 * @param msg Message buffer
 * @return 0 on success, -EAGAIN if queue full
 *
 * @note Safe to call from interrupt context
 */
int32_t msgq_try_send(MessageQueue_t *queue, const void *msg);

/**
 * @brief Receive message (non-blocking)
 * @param queue Message queue
 * @param msg Message buffer
 * @return 0 on success, -EAGAIN if queue empty
 *
 * @note Safe to call from interrupt context
 */
int32_t msgq_try_receive(MessageQueue_t *queue, void *msg);

/**
 * @brief Destroy message queue
 * @param queue Message queue
 * @return 0 on success, negative error code on failure
 *
 * @warning Wakes all blocked tasks with error
 */
int32_t msgq_destroy(MessageQueue_t *queue);
```

### 3.6 Condition Variable API

```c
/**
 * @brief Initialize condition variable
 * @param cond Condition variable
 * @return 0 on success, negative error code on failure
 */
int32_t cond_init(ConditionVariable_t *cond);

/**
 * @brief Wait on condition variable
 * @param cond Condition variable
 * @param mutex Mutex (must be locked)
 * @return 0 on success, negative error code on failure
 *
 * @note Atomically unlocks mutex and blocks
 * @note Re-acquires mutex before returning
 * @warning Must be called with mutex locked
 */
int32_t cond_wait(ConditionVariable_t *cond, Mutex_t *mutex);

/**
 * @brief Signal condition variable (wake one task)
 * @param cond Condition variable
 * @return 0 on success, negative error code on failure
 *
 * @note Wakes highest-priority waiting task
 */
int32_t cond_signal(ConditionVariable_t *cond);

/**
 * @brief Broadcast condition variable (wake all tasks)
 * @param cond Condition variable
 * @return 0 on success, negative error code on failure
 *
 * @note Wakes all waiting tasks
 */
int32_t cond_broadcast(ConditionVariable_t *cond);

/**
 * @brief Destroy condition variable
 * @param cond Condition variable
 * @return 0 on success, negative error code on failure
 */
int32_t cond_destroy(ConditionVariable_t *cond);
```

---

## 4. Algorithm Implementation Details

### 4.1 Mutex Lock with Priority Inheritance

```c
/**
 * @brief Lock mutex (with priority inheritance)
 * @param mutex Mutex
 * @return 0 on success, negative error code on failure
 */
int32_t mutex_lock(Mutex_t *mutex)
{
    TCB_t *current = get_current_task();
    uint32_t cpu_id = get_cpu_id();

    /* Try to acquire lock */
    if (atomic_compare_exchange_strong_explicit(
            &mutex->lock, &(uint32_t){0U}, 1U,
            memory_order_acquire, memory_order_acquire))
    {
        /* Lock acquired */
        mutex->owner = current;
        mutex->nest_count++;

        /* Enable priority inheritance if needed */
        if ((mutex->flags & MUTEX_PRIORITY_INHERITANCE) != 0U)
        {
            mutex->original_prio = current->priority;
        }

        return 0;
    }

    /* Lock held by another task, check for recursive lock */
    if ((mutex->flags & MUTEX_RECURSIVE) != 0U)
    {
        if (mutex->owner == current)
        {
            /* Recursive lock */
            mutex->nest_count++;
            return 0;
        }
    }

    /* Priority inheritance: boost owner's priority */
    if ((mutex->flags & MUTEX_PRIORITY_INHERITANCE) != 0U)
    {
        TCB_t *owner = mutex->owner;

        /* Boost owner's priority if current has higher priority */
        if (current->priority < owner->priority)
        {
            scheduler_lock();

            /* Save owner's original priority (first inheritance) */
            if (owner->priority == mutex->original_prio)
            {
                mutex->original_prio = owner->priority;
            }

            /* Boost owner to current's priority */
            task_set_priority(owner->task_id, current->priority);

            scheduler_unlock();
        }
    }

    /* Add to wait list (priority-ordered) */
    atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
    task_list_insert_priority(&mutex->wait_list, current);
    atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);

    /* Block current task */
    current->state = TASK_BLOCKED;
    schedule();

    return 0;
}

/**
 * @brief Unlock mutex (with priority inheritance restoration)
 * @param mutex Mutex
 * @return 0 on success, negative error code on failure
 */
int32_t mutex_unlock(Mutex_t *mutex)
{
    TCB_t *current = get_current_task();

    /* Verify ownership */
    if (mutex->owner != current)
    {
        return -EPERM;
    }

    /* Handle recursive lock */
    if (mutex->nest_count > 1U)
    {
        mutex->nest_count--;
        return 0;
    }

    /* Restore original priority (priority inheritance) */
    if ((mutex->flags & MUTEX_PRIORITY_INHERITANCE) != 0U)
    {
        if (current->priority != mutex->original_prio)
        {
            task_set_priority(current->task_id, mutex->original_prio);
        }
    }

    /* Release lock */
    mutex->nest_count = 0U;
    mutex->owner = NULL;
    atomic_store_explicit(&mutex->lock, 0U, memory_order_release);

    /* Wake highest-priority waiting task */
    TCB_t *next = task_list_pop_highest(&mutex->wait_list);
    if (next != NULL)
    {
        uint32_t cpu_id = next->cpu_affinity;
        scheduler_lock();

        next->state = TASK_READY;
        atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
        task_enqueue(next);
        atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);

        scheduler_unlock();
    }

    return 0;
}
```

### 4.2 Semaphore Wait/Post

```c
/**
 * @brief Semaphore wait (P operation)
 * @param sem Semaphore
 * @param timeout_ms Timeout
 * @return 0 on success, -ETIMEDOUT on timeout
 */
int32_t sem_wait(Semaphore_t *sem, uint32_t timeout_ms)
{
    TCB_t *current = get_current_task();

    /* Try to decrement count */
    uint32_t old_count = atomic_load_explicit(&sem->count,
                                              memory_order_acquire);
    while (old_count > 0U)
    {
        if (atomic_compare_exchange_weak_explicit(
                &sem->count, &old_count, old_count - 1U,
                memory_order_acquire, memory_order_acquire))
        {
            /* Successfully decremented */
            return 0;
        }
    }

    /* Count is 0, block */
    if (timeout_ms == 0U)
    {
        /* Infinite wait */
        atomic_lock(&g_scheduler.ready_queues[get_cpu_id()].lock);

        if ((sem->flags & SEM_FIFO) != 0U)
        {
            task_list_push_tail(&sem->wait_list, current);
        }
        else
        {
            task_list_insert_priority(&sem->wait_list, current);
        }

        current->state = TASK_BLOCKED;
        schedule();

        atomic_unlock(&g_scheduler.ready_queues[get_cpu_id()].lock);

        return 0;
    }
    else
    {
        /* Timeout wait */
        uint64_t deadline = get_system_time_ns() +
                            (uint64_t)timeout_ms * 1000000ULL;

        if ((sem->flags & SEM_FIFO) != 0U)
        {
            task_list_push_tail(&sem->wait_list, current);
        }
        else
        {
            task_list_insert_priority(&sem->wait_list, current);
        }

        current->state = TASK_BLOCKED;
        task_delay_until(deadline);

        /* Check if we were signaled or timed out */
        if (current->state == TASK_BLOCKED)
        {
            /* Timeout: remove from wait list */
            task_list_remove(&sem->wait_list, current);
            return -ETIMEDOUT;
        }

        return 0;
    }
}

/**
 * @brief Semaphore post (V operation)
 * @param sem Semaphore
 * @return 0 on success, negative error code on failure
 *
 * @note Safe to call from interrupt context
 */
int32_t sem_post(Semaphore_t *sem)
{
    /* Wake one waiting task */
    TCB_t *task = task_list_pop_highest(&sem->wait_list);
    if (task != NULL)
    {
        uint32_t cpu_id = task->cpu_affinity;

        task->state = TASK_READY;
        atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
        task_enqueue(task);
        atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);

        return 0;
    }

    /* No waiters, increment count */
    uint32_t old_count = atomic_load_explicit(&sem->count,
                                              memory_order_acquire);
    uint32_t max_count = atomic_load_explicit(&sem->max_count,
                                              memory_order_acquire);

    while (old_count < max_count)
    {
        if (atomic_compare_exchange_weak_explicit(
                &sem->count, &old_count, old_count + 1U,
                memory_order_release, memory_order_acquire))
        {
            return 0;
        }
    }

    /* Count would overflow */
    return -EOVERFLOW;
}
```

### 4.3 Event Flag Wait

```c
/**
 * @brief Wait for event flags
 * @param group Event group
 * @param flags Flags to wait for
 * @param wait_type EV_WAIT_ANY or EV_WAIT_ALL
 * @param timeout_ms Timeout
 * @return 0 on success, -ETIMEDOUT on timeout
 */
int32_t event_wait(EventGroup_t *group,
                   uint32_t flags,
                   uint32_t wait_type,
                   uint32_t timeout_ms)
{
    TCB_t *current = get_current_task();
    uint32_t current_flags;
    uint32_t matched;

    /* Check if condition already satisfied */
    current_flags = atomic_load_explicit(&group->flags,
                                         memory_order_acquire);

    if ((wait_type & EV_WAIT_ALL) != 0U)
    {
        matched = ((current_flags & flags) == flags) ? 1U : 0U;
    }
    else
    {
        matched = ((current_flags & flags) != 0U) ? 1U : 0U;
    }

    if (matched != 0U)
    {
        /* Condition satisfied, clear flags if auto-clear */
        if ((wait_type & EV_AUTO_CLEAR) != 0U)
        {
            atomic_fetch_and_explicit(&group->flags, ~flags,
                                     memory_order_release);
        }
        return 0;
    }

    /* Add to wait list */
    atomic_lock(&g_scheduler.ready_queues[get_cpu_id()].lock);
    task_list_insert_priority(&group->wait_list, current);
    atomic_unlock(&g_scheduler.ready_queues[get_cpu_id()].lock);

    /* Block */
    current->state = TASK_BLOCKED;

    if (timeout_ms == 0U)
    {
        schedule();
    }
    else
    {
        uint64_t deadline = get_system_time_ns() +
                            (uint64_t)timeout_ms * 1000000ULL;
        task_delay_until(deadline);
    }

    /* Check if we timed out */
    if (current->state == TASK_BLOCKED)
    {
        task_list_remove(&group->wait_list, current);
        return -ETIMEDOUT;
    }

    return 0;
}

/**
 * @brief Set event flags
 * @param group Event group
 * @param flags Flags to set
 * @return Previous flags value
 */
uint32_t event_set(EventGroup_t *group, uint32_t flags)
{
    uint32_t old_flags;
    uint32_t new_flags;
    TCB_t *task;
    TCB_t *next;

    /* Set flags */
    old_flags = atomic_fetch_or_explicit(&group->flags, flags,
                                         memory_order_release);

    /* Wake tasks whose wait condition is satisfied */
    task = group->wait_list.head;
    while (task != NULL)
    {
        next = task->next;

        /* Check wait condition */
        new_flags = atomic_load_explicit(&group->flags,
                                         memory_order_acquire);
        uint32_t matched = 0U;

        if ((task->wait_flags & EV_WAIT_ALL) != 0U)
        {
            matched = ((new_flags & task->event_mask) == task->event_mask) ?
                      1U : 0U;
        }
        else
        {
            matched = ((new_flags & task->event_mask) != 0U) ? 1U : 0U;
        }

        if (matched != 0U)
        {
            /* Remove from wait list */
            task_list_remove(&group->wait_list, task);

            /* Wake task */
            uint32_t cpu_id = task->cpu_affinity;
            task->state = TASK_READY;
            atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
            task_enqueue(task);
            atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);
        }

        task = next;
    }

    return old_flags;
}
```

### 4.4 Message Queue Implementation

```c
/**
 * @brief Send message to queue
 * @param queue Message queue
 * @param msg Message buffer
 * @param timeout_ms Timeout
 * @return 0 on success, -ETIMEDOUT on timeout, -EAGAIN if queue full
 */
int32_t msgq_send(MessageQueue_t *queue,
                  const void *msg,
                  uint32_t timeout_ms)
{
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint8_t *buffer;

    if ((queue == NULL) || (msg == NULL))
    {
        return -EINVAL;
    }

    /* Try to send (non-blocking) */
    atomic_lock(&queue->lock);

    count = atomic_load_explicit(&queue->count, memory_order_acquire);

    if (count >= queue->capacity)
    {
        atomic_unlock(&queue->lock);

        if (timeout_ms == 0U)
        {
            return -EAGAIN;
        }

        /* Block until space available */
        /* ... (similar to sem_wait) ... */
    }

    /* Calculate next head position */
    head = atomic_load_explicit(&queue->head, memory_order_acquire);
    buffer = (uint8_t *)queue->buffer + (head * queue->msg_size);

    /* Copy message to buffer */
    memcpy(buffer, msg, queue->msg_size);

    /* Update head and count */
    atomic_store_explicit(&queue->head,
                         (head + 1U) % queue->capacity,
                         memory_order_release);
    atomic_fetch_add_explicit(&queue->count, 1U, memory_order_release);

    atomic_unlock(&queue->lock);

    /* Wake waiting receiver */
    TCB_t *receiver = task_list_pop_highest(&queue->rx_wait_list);
    if (receiver != NULL)
    {
        uint32_t cpu_id = receiver->cpu_affinity;
        receiver->state = TASK_READY;
        atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
        task_enqueue(receiver);
        atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);
    }

    return 0;
}

/**
 * @brief Receive message from queue
 * @param queue Message queue
 * @param msg Message buffer
 * @param timeout_ms Timeout
 * @return 0 on success, -ETIMEDOUT on timeout
 */
int32_t msgq_receive(MessageQueue_t *queue,
                     void *msg,
                     uint32_t timeout_ms)
{
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint8_t *buffer;

    if ((queue == NULL) || (msg == NULL))
    {
        return -EINVAL;
    }

    /* Try to receive (non-blocking) */
    atomic_lock(&queue->lock);

    count = atomic_load_explicit(&queue->count, memory_order_acquire);

    if (count == 0U)
    {
        atomic_unlock(&queue->lock);

        if (timeout_ms == 0U)
        {
            return -EAGAIN;
        }

        /* Block until message available */
        /* ... (similar to sem_wait) ... */
    }

    /* Calculate next tail position */
    tail = atomic_load_explicit(&queue->tail, memory_order_acquire);
    buffer = (uint8_t *)queue->buffer + (tail * queue->msg_size);

    /* Copy message from buffer */
    memcpy(msg, buffer, queue->msg_size);

    /* Update tail and count */
    atomic_store_explicit(&queue->tail,
                         (tail + 1U) % queue->capacity,
                         memory_order_release);
    atomic_fetch_sub_explicit(&queue->count, 1U, memory_order_release);

    atomic_unlock(&queue->lock);

    /* Wake waiting sender */
    TCB_t *sender = task_list_pop_highest(&queue->tx_wait_list);
    if (sender != NULL)
    {
        uint32_t cpu_id = sender->cpu_affinity;
        sender->state = TASK_READY;
        atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
        task_enqueue(sender);
        atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);
    }

    return 0;
}
```

---

## 5. Performance Requirements

### 5.1 Timing Constraints

| Operation | Maximum Latency |
|-----------|-----------------|
| **Mutex Lock (uncontended)** | 100 ns |
| **Mutex Unlock** | 100 ns |
| **Spin Lock/Unlock** | 50 ns |
| **Sem Post** | 200 ns |
| **Sem Wait (uncontended)** | 200 ns |
| **Event Set** | 300 ns |
| **Msg Send (uncontended)** | 500 ns |
| **Msg Receive (uncontended)** | 500 ns |

### 5.2 Memory Constraints

| Primitive | Memory Overhead |
|-----------|-----------------|
| **Mutex** | 64 bytes |
| **Semaphore** | 32 bytes |
| **Spinlock** | 4 bytes |
| **Event Group** | 16 bytes |
| **Message Queue** | 128 bytes + buffer |

### 5.3 Bounded Execution

- **Maximum Block Time**: Deterministic (configurable timeout)
- **Maximum Waiters**: 256 tasks per primitive
- **Priority Inheritance Depth**: ≤ 8 levels

---

## 6. MISRA-C:2012 Compliance

### 6.1 Critical Rules

| Rule | Requirement |
|------|-------------|
| Rule 10.1 | No implicit integer conversions |
| Rule 10.3 | No assignment in boolean expression |
| Rule 10.4 | Logical operators on boolean operands |
| Rule 10.5 | Bitwise NOT on unsigned only |
| Rule 11.1 | No pointer-integer conversions |

### 6.2 Type Safety

```c
/* ✅ Correct: Explicit boolean comparison */
if (atomic_load(&lock->serving) == my_ticket) {
    /* ... */
}

/* ❌ Wrong: Implicit boolean */
if (atomic_load(&lock->serving)) {
    /* ... */
}
```

### 6.3 Runtime Checks

```c
/* Compile-time assertions */
STATIC_ASSERT(sizeof(Mutex_t) == 64U, Mutex_size_wrong);
STATIC_ASSERT(sizeof(TicketLock_t) == 4U, TicketLock_size_wrong);
STATIC_ASSERT((sizeof(MessageQueue_t) & 0x3FU) == 0U,
              MessageQueue_misaligned);
```

---

## 7. Testing Strategy

### 7.1 Unit Tests

| Test Case | Description |
|-----------|-------------|
| **TC-SYNC-001** | Mutex lock/unlock (basic) |
| **TC-SYNC-002** | Mutex recursive locking |
| **TC-SYNC-003** | Mutex priority inheritance |
| **TC-SYNC-004** | Semaphore wait/post |
| **TC-SYNC-005** | Binary semaphore |
| **TC-SYNC-006** | Spinlock fairness (FIFO) |
| **TC-SYNC-007** | Event flag wait/set/clear |
| **TC-SYNC-008** | Message queue send/receive |
| **TC-SYNC-009** | Condition variable wait/signal |
| **TC-SYNC-010** | Timeout handling |

### 7.2 Integration Tests

| Test Case | Description |
|-----------|-------------|
| **TC-SYNC-INT-001** | Producer-consumer (mutex + condvar) |
| **TC-SYNC-INT-002** | Reader-writer lock (using mutex) |
| **TC-SYNC-INT-003** | Barrier synchronization |
| **TC-SYNC-INT-004** | Rendezvous pattern |

### 7.3 Performance Tests

| Test Case | Metric | Target |
|-----------|--------|--------|
| **TC-SYNC-PERF-001** | Mutex contention latency | < 1 μs |
| **TC-SYNC-PERF-002** | Spinlock acquisition time | < 100 ns |
| **TC-SYNC-PERF-003** | Message queue throughput | > 1M msg/sec |

### 7.4 Coverage Requirements

- **Statement Coverage**: > 95%
- **Branch Coverage**: > 90%
- **MC/DC Coverage**: > 85% (critical functions)

---

## 8. Configuration Options

### 8.1 MenuConfig Options

```kconfig
config SYNCHRONIZATION
    bool "Synchronization Primitives"
    default y

config MUTEX_PRIORITY_INHERITANCE
    bool "Mutex Priority Inheritance"
    default y
    depends on SYNCHRONIZATION

config MUTEX_PRIORITY_CEILING
    bool "Mutex Priority Ceiling"
    default n
    depends on SYNCHRONIZATION

config MAX_MSGQ_SIZE
    int "Maximum message queue size"
    range 1 1024
    default 32
    depends on SYNCHRONIZATION

config MAX_EVENT_FLAGS
    int "Number of event flags"
    range 1 64
    default 32
    depends on SYNCHRONIZATION
```

---

## 9. Error Handling

### 9.1 Error Codes

| Error Code | Description |
|------------|-------------|
| `ERROR_INVALID_MUTEX` | Mutex not initialized |
| `ERROR_NOT_OWNER` | Caller does not own mutex |
| `ERROR_WOULD_BLOCK` | Operation would block (trylock) |
| `ERROR_TIMEOUT` | Operation timed out |
| `ERROR_OVERFLOW` | Semaphore count overflow |
| `ERROR_UNDERFLOW` | Semaphore count underflow |

### 9.2 Error Recovery

- **Mutex Lock Timeout**: Return -ETIMEDOUT (caller handles)
- **Priority Inheritance Failure**: Log error, continue (degraded)
- **Message Queue Overflow**: Return -EAGAIN (caller retry)
- **Deadlock Detection**: Trigger core dump (fatal)

---

## 10. Traceability

### 10.1 Requirements Traceability

| LLD Section | HLD Section | Plan.md Section |
|-------------|-------------|-----------------|
| Mutex with PI | 4.2 Data Structures | 4.2.10 |
| Semaphore | POSIX PSE52 | - |
| Spinlock | 4.9 SMP Sync | 4.9.1 |
| Message Queue | POSIX PSE52 | - |

### 10.2 Test Coverage Traceability

| Test Case | Requirement |
|-----------|-------------|
| TC-SYNC-003 | SYNC-001: Priority inheritance |
| TC-SYNC-PERF-001 | NFR-001: Mutex latency < 1μs |
| TC-SYNC-INT-001 | SYNC-002: Producer-consumer |

---

## Appendix A: Priority Inheritance Protocol

### A.1 Protocol Rules

1. **Donation**: When high-priority task H blocks on mutex held by low-priority task L, L temporarily inherits H's priority.
2. **Propagation**: If L blocks on another mutex held by task M, M inherits L's boosted priority (which is H's priority).
3. **Restoration**: When L releases the mutex, its priority is restored to the maximum of:
   - Its original priority
   - The priorities of all tasks still blocked on mutexes it holds

### A.2 Example

```
Initial state:
  Task H: priority 10 (highest)
  Task M: priority 50 (medium)
  Task L: priority 100 (lowest)

Timeline:
  t0: L locks mutex A
  t1: M locks mutex B (which L needs)
  t2: H tries to lock mutex A (held by L)
      → L's priority boosted to 10
  t3: L tries to lock mutex B (held by M)
      → M's priority boosted to 10 (via L)
  t4: M unlocks mutex B
      → M's priority restored to 50
  t5: L unlocks mutex A
      → L's priority restored to 100
```

---

**Document End**
