/**
 * @file    test_perf_sync_simple.c
 * @brief   同步机制性能测试（简化版）
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <stdatomic.h>
#include <stdbool.h>

/* ========================================================================
 * 时间测量辅助函数
 * ======================================================================== */

static uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t ns_to_us(uint64_t ns)
{
    return ns / 1000ULL;
}

/* ========================================================================
 * TicketLock 性能测试
 * ======================================================================== */

typedef struct
{
    atomic_uint next_ticket;
    atomic_uint serving_ticket;
} TicketLock_t;

static void ticket_lock_init(TicketLock_t *lock)
{
    atomic_init(&lock->next_ticket, 0U);
    atomic_init(&lock->serving_ticket, 0U);
}

static void ticket_lock_acquire(TicketLock_t *lock)
{
    uint32_t my_ticket = atomic_fetch_add_explicit(&lock->next_ticket, 1U, memory_order_relaxed);
    while (atomic_load_explicit(&lock->serving_ticket, memory_order_acquire) != my_ticket)
    {
        /* 自旋等待 */
    }
}

static void ticket_lock_release(TicketLock_t *lock)
{
    atomic_fetch_add_explicit(&lock->serving_ticket, 1U, memory_order_release);
}

static void benchmark_ticket_lock(void)
{
    TicketLock_t lock;
    ticket_lock_init(&lock);

    uint64_t total = 0ULL;
    uint32_t i;

    printf("\n========== TicketLock 性能测试 ==========\n");

    for (i = 0U; i < 1000000U; i++)
    {
        uint64_t start = get_time_ns();

        ticket_lock_acquire(&lock);
        volatile uint64_t tmp = i * 123456789ULL;
        (void)tmp;
        ticket_lock_release(&lock);

        uint64_t end = get_time_ns();
        total += (end - start);
    }

    uint64_t avg_us = ns_to_us(total / 1000000U);

    printf("  迭代次数: 1000000\n");
    printf("  平均获取+释放时间: %llu us\n", avg_us);
    printf("  吞吐量: %.2f 百万次/秒\n",
           1000000.0 * 1000000.0 / (double)(total / 1000ULL));
}

/* ========================================================================
 * Mutex 性能测试
 * ======================================================================== */

typedef struct
{
    atomic_int locked;
} Mutex_t;

static void mutex_init(Mutex_t *mutex)
{
    atomic_init(&mutex->locked, 0);
}

static bool mutex_trylock(Mutex_t *mutex)
{
    int expected = 0;
    return atomic_compare_exchange_strong_explicit(&mutex->locked, &expected, 1,
                                                   memory_order_acquire,
                                                   memory_order_relaxed);
}

static void mutex_unlock(Mutex_t *mutex)
{
    atomic_store_explicit(&mutex->locked, 0, memory_order_release);
}

static void benchmark_mutex(void)
{
    Mutex_t mutex;
    mutex_init(&mutex);

    uint64_t total = 0ULL;
    uint32_t i;

    printf("\n========== Mutex 性能测试 ==========\n");

    for (i = 0U; i < 1000000U; i++)
    {
        uint64_t start = get_time_ns();

        while (!mutex_trylock(&mutex))
        {
            /* 自旋等待 */
        }
        volatile uint64_t tmp = i * 987654321ULL;
        (void)tmp;
        mutex_unlock(&mutex);

        uint64_t end = get_time_ns();
        total += (end - start);
    }

    uint64_t avg_us = ns_to_us(total / 1000000U);

    printf("  迭代次数: 1000000\n");
    printf("  平均获取+释放时间: %llu us\n", avg_us);
    printf("  吞吐量: %.2f 百万次/秒\n",
           1000000.0 * 1000000.0 / (double)(total / 1000ULL));
}

/* ========================================================================
 * Semaphore 性能测试
 * ======================================================================== */

typedef struct
{
    atomic_int count;
} Semaphore_t;

static void sem_init(Semaphore_t *sem, int count)
{
    atomic_init(&sem->count, count);
}

static void sem_wait(Semaphore_t *sem)
{
    while (atomic_load_explicit(&sem->count, memory_order_acquire) <= 0)
    {
        /* 自旋等待 */
    }
    atomic_fetch_sub_explicit(&sem->count, 1, memory_order_relaxed);
}

static void sem_signal(Semaphore_t *sem)
{
    atomic_fetch_add_explicit(&sem->count, 1, memory_order_release);
}

static void benchmark_semaphore(void)
{
    Semaphore_t sem;
    sem_init(&sem, 1000000);

    uint64_t total = 0ULL;
    uint32_t i;

    printf("\n========== Semaphore 性能测试 ==========\n");

    for (i = 0U; i < 1000000U; i++)
    {
        uint64_t start = get_time_ns();

        sem_wait(&sem);
        volatile uint64_t tmp = i * 543210987ULL;
        (void)tmp;
        sem_signal(&sem);

        uint64_t end = get_time_ns();
        total += (end - start);
    }

    uint64_t avg_us = ns_to_us(total / 1000000U);

    printf("  迭代次数: 1000000\n");
    printf("  平均 P/V 操作时间: %llu us\n", avg_us);
    printf("  吞吐量: %.2f 百万次/秒\n",
           1000000.0 * 1000000.0 / (double)(total / 1000ULL));
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("========================================\n");
    printf("AISafe64 - 同步机制性能测试（简化版）\n");
    printf("========================================\n");

    benchmark_ticket_lock();
    benchmark_mutex();
    benchmark_semaphore();

    printf("\n========================================\n");
    printf("性能测试完成\n");
    printf("========================================\n");

    return 0;
}
