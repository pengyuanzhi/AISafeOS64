/**
 * @file    mock_kernel.h
 * @brief   宿主机测试共享 Mock 基础设施
 * @author  AISafe64 Team
 * @date    2026-04-02
 * @version 1.0
 *
 * @details 为宿主机（x86_64）单元测试提供内核依赖的 Mock 替代：
 *          - ARM64 原子操作（LDXR/STXR → 简单 C 操作）
 *          - 内存屏障宏（DMB/DSB/ISB → 空操作）
 *          - HAL 函数（hal_get_cpu_id 等 → 可配置 Mock）
 *          - 内核类型定义（kernel_status_t, EINVAL 等）
 *          - 通用断言宏
 *
 * @note 所有测试文件在 #include 真实内核头文件之前包含此文件，
 *       通过宏保护避免 ARM64 内联汇编被编译
 * @note 对应需求: TF-001（单元测试框架）
 */

#ifndef MOCK_KERNEL_H
#define MOCK_KERNEL_H

/* ========================================================================
 * 标准 C11 头文件
 * ======================================================================== */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ========================================================================
 * 防止包含真实内核头文件中的 ARM64 内联汇编
 * ======================================================================== */

/* 已包含标记 — 被测试的内核头文件检查此宏以跳过 ARM64 汇编 */
#define KERNEL_BARRIER_H
#define KERNEL_COMPILER_H
#define KERNEL_TYPES_H
#define KERNEL_ERRNO_H
#define KERNEL_SPINLOCK_H
#define KERNEL_LIST_H
#define KERNEL_CONFIG_H
#define KERNEL_OBJECT_POOL_H

/* ========================================================================
 * 内核类型定义（Mock）
 * ======================================================================== */

typedef int32_t kernel_status_t;

#define KERNEL_OK       ((kernel_status_t)0)
#define KERNEL_ERROR    ((kernel_status_t)(-1))

/* POSIX 错误码 */
#define EINVAL         22U
#define ENOMEM         12U
#define EBUSY          16U
#define EPERM           1U
#define EAGAIN         11U
#define ETIMEDOUT     116U

/* 地址类型 */
typedef uint64_t paddr_t;
typedef uint64_t vaddr_t;
typedef uintptr_t regval_t;
typedef uint32_t cpu_id_t;
typedef uint8_t  priority_t;
typedef uint64_t tick_t;
typedef uint32_t thread_id_t;
typedef uint32_t kobj_id_t;

#define PRIORITY_MIN    ((priority_t)0U)
#define PRIORITY_MAX    ((priority_t)255U)

/* NULL 定义 */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* 工具宏 */
#define ARRAY_SIZE(arr)     ((size_t)(sizeof(arr) / sizeof((arr)[0U])))
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* 编译器辅助宏 */
#define likely(x)   (x)
#define unlikely(x) (x)
#define ALIGNED(n)
#define PACKED
#define SECTION(s)
#define NORETURN
#define WEAK
#define UNUSED      __attribute__((unused))
#define USED        __attribute__((used))
#define ALWAYS_INLINE
#define NOINLINE    __attribute__((noinline))
#define RESTRICT    restrict
#define BUILD_BUG_ON(expr)

/* ========================================================================
 * 内存屏障 Mock（空操作）
 * ======================================================================== */

#define barrier()
#define barrier_store()
#define barrier_load()
#define barrier_inst()
#define full_barrier()
#define cpu_relax()
#define WFE()
#define SEV()
#define SEVL()
#define COMPILER_BARRIER()

/* ========================================================================
 * ARM64 原子操作 Mock（单线程安全）
 * ======================================================================== */

/**
 * @brief 32位原子 CAS
 * @details 宿主机单线程环境，无需真正原子操作
 */
static inline bool atomic_cas_u32(volatile uint32_t *addr,
                                  uint32_t expected,
                                  uint32_t desired)
{
    if (*addr == expected)
    {
        *addr = desired;
        return true;
    }
    return false;
}

/**
 * @brief 32位原子递增（返回旧值）
 */
static inline uint32_t atomic_inc_u32(volatile uint32_t *addr)
{
    uint32_t old = *addr;
    *addr = old + 1U;
    return old;
}

/**
 * @brief 32位原子递减（返回旧值）
 */
static inline uint32_t atomic_dec_u32(volatile uint32_t *addr)
{
    uint32_t old = *addr;
    *addr = old - 1U;
    return old;
}

/**
 * @brief 32位原子加法（返回旧值）
 */
static inline uint32_t atomic_add_u32(volatile uint32_t *addr, uint32_t value)
{
    uint32_t old = *addr;
    *addr = old + value;
    return old;
}

/**
 * @brief 32位原子减法（返回旧值）
 */
static inline uint32_t atomic_sub_u32(volatile uint32_t *addr, uint32_t value)
{
    uint32_t old = *addr;
    *addr = old - value;
    return old;
}

/**
 * @brief 32位原子交换（返回旧值）
 */
static inline uint32_t atomic_xchg_u32(volatile uint32_t *addr, uint32_t value)
{
    uint32_t old = *addr;
    *addr = value;
    return old;
}

/**
 * @brief 32位原子 load-acquire
 */
static inline uint32_t atomic_load_acquire_u32(const volatile uint32_t *addr)
{
    return *addr;
}

/**
 * @brief 32位原子 store-release
 */
static inline void atomic_store_release_u32(volatile uint32_t *addr, uint32_t value)
{
    *addr = value;
}

/**
 * @brief 64位原子 CAS
 */
static inline bool atomic_cas_u64(volatile uint64_t *addr,
                                  uint64_t expected,
                                  uint64_t desired)
{
    if (*addr == expected)
    {
        *addr = desired;
        return true;
    }
    return false;
}

/**
 * @brief 64位原子递增（返回旧值）
 */
static inline uint64_t atomic_inc_u64(volatile uint64_t *addr)
{
    uint64_t old = *addr;
    *addr = old + 1ULL;
    return old;
}

/**
 * @brief 64位原子递减（返回旧值）
 */
static inline uint64_t atomic_dec_u64(volatile uint64_t *addr)
{
    uint64_t old = *addr;
    *addr = old - 1ULL;
    return old;
}

/**
 * @brief 64位原子加法（返回旧值）
 */
static inline uint64_t atomic_add_u64(volatile uint64_t *addr, uint64_t value)
{
    uint64_t old = *addr;
    *addr = old + value;
    return old;
}

/**
 * @brief 64位原子减法（返回旧值）
 */
static inline uint64_t atomic_sub_u64(volatile uint64_t *addr, uint64_t value)
{
    uint64_t old = *addr;
    *addr = old - value;
    return old;
}

/**
 * @brief 64位原子交换（返回旧值）
 */
static inline uint64_t atomic_xchg_u64(volatile uint64_t *addr, uint64_t value)
{
    uint64_t old = *addr;
    *addr = value;
    return old;
}

/**
 * @brief 64位原子 load-acquire
 */
static inline uint64_t atomic_load_acquire_u64(const volatile uint64_t *addr)
{
    return *addr;
}

/**
 * @brief 64位原子 store-release
 */
static inline void atomic_store_release_u64(volatile uint64_t *addr, uint64_t value)
{
    *addr = value;
}

/* ========================================================================
 * HAL 函数 Mock（可配置）
 * ======================================================================== */

/** @brief Mock: 当前 CPU ID，默认 0，测试中可修改 */
static uint32_t mock_cpu_id = 0U;

static inline uint32_t hal_get_cpu_id(void)
{
    return mock_cpu_id;
}

/** @brief Mock: IRQ 保存状态 */
static uint32_t mock_irq_state = 0U;

static inline uint32_t hal_irq_saved_state(void)
{
    return mock_irq_state;
}

static inline void hal_irq_disable(void)
{
    /* 空操作 */
}

static inline void hal_irq_restore(uint32_t state)
{
    (void)state;
}

/* ========================================================================
 * TicketLock_t 定义（Mock 版本）
 * ======================================================================== */

/**
 * @brief Ticket Lock 结构（与内核 spinlock.h 一致）
 */
typedef struct
{
    volatile uint32_t next_ticket;    /**< @brief 下一个发放的票号 */
    volatile uint32_t serving_ticket; /**< @brief 当前服务的票号 */
    uint32_t cpu_id;                  /**< @brief 持有者的 CPU ID */
    uint32_t nest_count;              /**< @brief 嵌套计数 */
} TicketLock_t;

#define TICKET_LOCK_INIT { 0U, 0U, 0xFFFFFFFFU, 0U }

/* ========================================================================
 * TicketLock 操作（Mock 版本 — 宿主机兼容）
 *
 * @details 由于 mock_atomic_inc_u32 返回旧值，
 *          释放时使用 atomic_store_release_u32 写入 serving_ticket + 1，
 *          这与真实内核 spinlock.c 逻辑一致。
 *          宿主机上 WFE/SEV 为空操作，不会死锁。
 * ======================================================================== */

static inline void ticket_lock_init(TicketLock_t *lock)
{
    if (lock == NULL) { return; }
    lock->next_ticket = 0U;
    lock->serving_ticket = 0U;
    lock->cpu_id = 0xFFFFFFFFU;
    lock->nest_count = 0U;
}

static inline void ticket_lock_acquire(TicketLock_t *lock)
{
    uint32_t my_ticket;
    if (lock == NULL) { return; }
    my_ticket = atomic_inc_u32(&lock->next_ticket);
    for (;;)
    {
        if (atomic_load_acquire_u32(&lock->serving_ticket) == my_ticket)
        {
            break;
        }
        /* WFE() — 宿主机空操作 */
    }
    lock->cpu_id = hal_get_cpu_id();
    lock->nest_count++;
}

static inline void ticket_lock_release(TicketLock_t *lock)
{
    if (lock == NULL) { return; }
    lock->nest_count--;
    lock->cpu_id = 0xFFFFFFFFU;
    atomic_store_release_u32(&lock->serving_ticket,
                             lock->serving_ticket + 1U);
}

static inline bool ticket_lock_try_acquire(TicketLock_t *lock)
{
    uint32_t expected;
    bool success;
    if (lock == NULL) { return false; }
    expected = atomic_load_acquire_u32(&lock->next_ticket);
    if (atomic_load_acquire_u32(&lock->serving_ticket) != expected)
    {
        return false;
    }
    success = atomic_cas_u32(&lock->next_ticket, expected, expected + 1U);
    if (success)
    {
        lock->cpu_id = hal_get_cpu_id();
        lock->nest_count++;
        return true;
    }
    return false;
}

static inline bool ticket_lock_is_held(const TicketLock_t *lock)
{
    if (lock == NULL) { return false; }
    return (lock->next_ticket != lock->serving_ticket) ? true : false;
}

static inline uint32_t ticket_lock_acquire_irqsave(TicketLock_t *lock)
{
    uint32_t state = hal_irq_saved_state();
    ticket_lock_acquire(lock);
    return state;
}

static inline void ticket_lock_release_irqrestore(TicketLock_t *lock, uint32_t irq_state)
{
    ticket_lock_release(lock);
    hal_irq_restore(irq_state);
}

/* ========================================================================
 * object_pool_t 定义（Mock 版本 — 与内核 object_pool.h 一致）
 * ======================================================================== */

typedef struct
{
    uint8_t         *buffer;
    uint32_t         obj_size;
    uint32_t         capacity;
    uint32_t        *free_stack;
    uint32_t         free_count;
    uint32_t         alloc_count;
    TicketLock_t     lock;
} object_pool_t;

/* ========================================================================
 * 通用断言宏
 * ======================================================================== */

static uint32_t s_total  = 0U;
static uint32_t s_passed = 0U;
static uint32_t s_failed = 0U;

#define TEST_ASSERT(cond) do { \
    s_total++; \
    if (cond) { s_passed++; } \
    else { s_failed++; printf("  失败: %s (行 %d)\n", #cond, __LINE__); } \
} while (0)

#define TEST_ASSERT_EQ(a, b) TEST_ASSERT((a) == (b))
#define TEST_ASSERT_NE(a, b) TEST_ASSERT((a) != (b))
#define TEST_ASSERT_TRUE(x)  TEST_ASSERT((x) == true)
#define TEST_ASSERT_FALSE(x) TEST_ASSERT((x) == false)
#define TEST_ASSERT_GT(a, b) TEST_ASSERT((a) > (b))
#define TEST_ASSERT_GE(a, b) TEST_ASSERT((a) >= (b))
#define TEST_ASSERT_LT(a, b) TEST_ASSERT((a) < (b))
#define TEST_ASSERT_LE(a, b) TEST_ASSERT((a) <= (b))
#define TEST_ASSERT_NULL(p)  TEST_ASSERT((p) == NULL)
#define TEST_ASSERT_NOT_NULL(p) TEST_ASSERT((p) != NULL)

/* ========================================================================
 * 测试辅助宏
 * ======================================================================== */

/** @brief 重置断言计数器 */
#define TEST_RESET() do { \
    s_total = 0U; s_passed = 0U; s_failed = 0U; \
} while (0)

/** @brief 返回测试结果（0=全部通过） */
#define TEST_RESULT() ((s_failed > 0U) ? 1 : 0)

/** @brief 打印测试总结 */
#define TEST_SUMMARY(name) do { \
    printf("\n结果: %u 通过 / %u 失败 / %u 总计\n", \
           s_passed, s_failed, s_total); \
} while (0)

#endif /* MOCK_KERNEL_H */
