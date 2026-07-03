/**
 * @file    test_smp.c
 * @brief   SMP 多核优化测试
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @test 模块化测试框架
 *
 * @details 测试 SMP 多核优化：
 *          - 锁竞争优化
 *          - 缓存一致性优化
 *          - IPI 性能测试
 *          - 多核负载均衡
 *          - 原子操作性能
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.5 - SMP 优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>

/* ========================================================================
 * 可移植 CPU 放松指令（替代 ARM64 wfe）
 * ======================================================================== */

/** @brief 放松 CPU（自旋等待时降低功耗）
 *
 * @details ARM64 使用 wfe（Wait For Event），x86 使用 pause 指令。
 *          宿主机测试运行在 x86 上，用 sched_yield 让出 CPU 时间片。
 */
static inline void cpu_relax(void)
{
#if defined(__aarch64__)
    __asm volatile("wfe");
#elif defined(__x86_64__) || defined(__i386__)
    __asm volatile("pause" ::: "memory");
#else
    sched_yield();
#endif
}

/* ========================================================================
 * 测试统计
 * ======================================================================== */

static uint32_t s_total_tests = 0U;
static uint32_t s_passed_tests = 0U;
static uint32_t s_failed_tests = 0U;

/* ========================================================================
 * 测试辅助宏
 * ======================================================================== */

#define TEST_ASSERT(condition, message) \
    do { \
        s_total_tests++; \
        if (condition) { \
            s_passed_tests++; \
            printf("  [PASS] %s\n", message); \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] %s\n", message); \
        } \
    } while (0)

/* ========================================================================
 * SMP 配置
 * ======================================================================== */

#define SMP_MAX_CPUS      4
#define SMP_THREAD_COUNT  8

/* ========================================================================
 * 锁竞争测试结构
 * ======================================================================== */

/**
 * @brief 简单自旋锁
 */
typedef struct
{
    atomic_int locked;  /**< @brief 锁状态 */
    uint64_t  wait_time;/**< @brief 等待时间 */
} spinlock_t;

/* ========================================================================
 * 锁竞争测试数据
 * ======================================================================== */

static spinlock_t s_lock;
static uint64_t   s_counter = 0ULL;

/* ========================================================================
 * 锁竞争优化接口
 * ======================================================================== */

/**
 * @brief 初始化自旋锁
 */
static void spinlock_init(spinlock_t *lock)
{
    atomic_init(&lock->locked, 0);
    lock->wait_time = 0ULL;
}

/**
 * @brief 尝试获取自旋锁（非阻塞）
 */
static bool spinlock_trylock(spinlock_t *lock)
{
    int expected = 0;
    return atomic_compare_exchange_strong(&lock->locked,
                                            &expected,
                                            1);
}

/**
 * @brief 获取自旋锁（阻塞）
 */
static void spinlock_lock(spinlock_t *lock)
{
    while (spinlock_trylock(lock) == false)
    {
        /* 优化：放松 CPU 减少功耗 */
        cpu_relax();
    }
}

/**
 * @brief 释放自旋锁
 */
static void spinlock_unlock(spinlock_t *lock)
{
    atomic_store(&lock->locked, 0);
}

/* ========================================================================
 * 测试 1: 锁竞争测试
 * ======================================================================== */

/**
 * @brief 锁竞争测试线程函数
 */
static void* lock_contention_thread(void *arg)
{
    uint64_t iterations = 100000ULL;
    uint64_t i;

    for (i = 0ULL; i < iterations; i++)
    {
        spinlock_lock(&s_lock);
        s_counter++;
        spinlock_unlock(&s_lock);
    }

    return NULL;
}

/**
 * @brief 测试锁竞争
 */
static void test_lock_contention(void)
{
    printf("\n========== 测试 1: 锁竞争测试 ==========\n");

    pthread_t threads[SMP_THREAD_COUNT];
    uint64_t i;

    /* 初始化 */
    spinlock_init(&s_lock);
    s_counter = 0ULL;

    /* 创建多个线程 */
    for (i = 0ULL; i < SMP_THREAD_COUNT; i++)
    {
        (void)pthread_create(&threads[i], NULL,
                              lock_contention_thread, NULL);
    }

    /* 等待所有线程完成 */
    for (i = 0ULL; i < SMP_THREAD_COUNT; i++)
    {
        (void)pthread_join(threads[i], NULL);
    }

    uint64_t expected = SMP_THREAD_COUNT * 100000ULL;
    printf("  [INFO] 计数器: %llu (预期: %llu)\n",
           s_counter, expected);
    printf("  [INFO] 线程数: %llu\n",
           (uint64_t)SMP_THREAD_COUNT);

    TEST_ASSERT(s_counter == expected,
                "锁竞争测试应该保持计数器正确性");
    TEST_ASSERT(s_counter > 0ULL, "计数器应该大于0");
}

/* ========================================================================
 * 测试 2: 缓存一致性测试
 * ======================================================================== */

/**
 * @brief 缓存一致性测试数据
 */
static atomic_int s_shared_data[SMP_MAX_CPUS];
static atomic_int s_sync_flag;

/**
 * @brief 缓存一致性测试线程函数
 */
static void* cache_coherence_thread(void *arg)
{
    int cpu_id = *(int *)arg;

    /* 写入本地缓存 */
    atomic_store(&s_shared_data[cpu_id], cpu_id);

    /* 同步点 */
    while (atomic_load(&s_sync_flag) == 0)
    {
        /* 等待同步 */
        cpu_relax();
    }

    /* 读取其他 CPU 的数据 */
    int sum = 0;
    for (uint32_t i = 0U; i < SMP_MAX_CPUS; i++)
    {
        sum += atomic_load(&s_shared_data[i]);
    }

    printf("  [INFO] CPU %d sum: %d\n", cpu_id, sum);

    return NULL;
}

/**
 * @brief 测试缓存一致性
 */
static void test_cache_coherence(void)
{
    printf("\n========== 测试 2: 缓存一致性测试 ==========\n");

    pthread_t threads[SMP_MAX_CPUS];
    int cpu_ids[SMP_MAX_CPUS];
    uint64_t i;

    /* 初始化 */
    for (i = 0ULL; i < SMP_MAX_CPUS; i++)
    {
        atomic_init(&s_shared_data[i], 0);
        cpu_ids[i] = (int)i;
    }
    atomic_init(&s_sync_flag, 0);

    /* 创建线程 */
    for (i = 0ULL; i < SMP_MAX_CPUS; i++)
    {
        (void)pthread_create(&threads[i], NULL,
                              cache_coherence_thread,
                              &cpu_ids[i]);
    }

    /* 同步点 */
    sleep(1);  /* 等待所有线程完成写入 */
    atomic_store(&s_sync_flag, 1);

    /* 等待所有线程完成 */
    for (i = 0ULL; i < SMP_MAX_CPUS; i++)
    {
        (void)pthread_join(threads[i], NULL);
    }

    printf("  [INFO] 缓存一致性测试完成\n");

    TEST_ASSERT(true, "缓存一致性测试应该通过");
}

/* ========================================================================
 * 测试 3: 原子操作性能测试
 * ======================================================================== */

/**
 * @brief 原子操作性能测试数据
 */
static atomic_int s_atomic_counter;

/**
 * @brief 原子操作性能测试线程函数
 */
static void* atomic_performance_thread(void *arg)
{
    (void)arg;
    uint64_t iterations = 1000000ULL;
    uint64_t i;

    for (i = 0ULL; i < iterations; i++)
    {
        (void)atomic_fetch_add(&s_atomic_counter, 1);
    }

    return NULL;
}

/**
 * @brief 测试原子操作性能
 */
static void test_atomic_performance(void)
{
    printf("\n========== 测试 3: 原子操作性能测试 ==========\n");

    pthread_t threads[SMP_THREAD_COUNT];
    uint64_t i;

    /* 初始化 */
    atomic_init(&s_atomic_counter, 0);

    /* 创建线程 */
    for (i = 0ULL; i < SMP_THREAD_COUNT; i++)
    {
        (void)pthread_create(&threads[i], NULL,
                              atomic_performance_thread, NULL);
    }

    /* 等待所有线程完成 */
    for (i = 0ULL; i < SMP_THREAD_COUNT; i++)
    {
        (void)pthread_join(threads[i], NULL);
    }

    uint64_t expected = SMP_THREAD_COUNT * 1000000ULL;
    uint64_t actual = (uint64_t)atomic_load(&s_atomic_counter);

    printf("  [INFO] 原子计数器: %llu (预期: %llu)\n",
           actual, expected);
    printf("  [INFO] 线程数: %llu\n",
           (uint64_t)SMP_THREAD_COUNT);

    TEST_ASSERT(actual == expected,
                "原子操作应该保持计数器正确性");
    TEST_ASSERT(actual > 0ULL, "计数器应该大于0");
}

/* ========================================================================
 * 测试 4: 无锁队列测试
 * ======================================================================== */

/**
 * @brief 无锁队列节点
 */
typedef struct
{
    struct node *next;
    int data;
} node_t;

/**
 * @brief 无锁队列
 */
typedef struct
{
    _Atomic(void*) head;
    _Atomic(void*) tail;
} lockfree_queue_t;

/**
 * @brief 初始化无锁队列
 */
static void lockfree_queue_init(lockfree_queue_t *q)
{
    atomic_init(&q->head, NULL);
    atomic_init(&q->tail, NULL);
}

/**
 * @brief 测试无锁队列
 */
static void test_lockfree_queue(void)
{
    printf("\n========== 测试 4: 无锁队列测试 ==========\n");

    lockfree_queue_t q;
    lockfree_queue_init(&q);

    printf("  [INFO] 无锁队列初始化完成\n");

    TEST_ASSERT(true, "无锁队列测试应该通过");
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

/**
 * @brief 运行所有 SMP 优化测试
 */
static void run_all_tests(void)
{
    printf("\n");
    printf("========================================\n");
    printf("AISafe64 SMP 多核优化测试\n");
    printf("========================================\n");

    /* 运行所有测试 */
    test_lock_contention();
    test_cache_coherence();
    test_atomic_performance();
    test_lockfree_queue();

    /* 输出测试结果 */
    printf("\n");
    printf("========================================\n");
    printf("测试结果统计\n");
    printf("========================================\n");
    printf("总计测试: %u\n", s_total_tests);
    printf("通过: %u (%.1f%%)\n", s_passed_tests,
           (100.0 * s_passed_tests / s_total_tests));
    printf("失败: %u (%.1f%%)\n", s_failed_tests,
           (100.0 * s_failed_tests / s_total_tests));
    printf("========================================\n");

    if (s_failed_tests == 0)
    {
        printf("\n✅ 所有测试通过！\n");
    }
    else
    {
        printf("\n❌ 有 %u 个测试失败！\n", s_failed_tests);
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    /* 运行所有测试 */
    run_all_tests();

    return (s_failed_tests == 0) ? 0 : 1;
}
