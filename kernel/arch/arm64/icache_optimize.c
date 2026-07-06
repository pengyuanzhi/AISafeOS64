/**
 * @file    icache_optimize.c
 * @brief   指令缓存优化实现
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 实现指令缓存优化机制：
 *          - 指令缓存预热
 *          - 指令缓存无效化
 *          - 指令缓存同步
 *          - 指令缓存统计
 *
 * @note MISRA C:2012 合规
 * @note 对应阶段 1.6 - 缓存优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/icache_optimize.h>
#include <kernel/timer.h>
#include <kernel/barrier.h>
#include <kernel/compiler.h>
#include <kernel/printk.h>
#include <kernel/spinlock.h>
#include <kernel/errno.h>
#include <string.h>

/* ========================================================================
 * 指令缓存预热函数列表
 * ======================================================================== */

/**
 * @brief 指令缓存预热函数列表
 */
static icache_warmup_func_t s_warmup_funcs[ICACHE_WARMUP_FUNCS];

/**
 * @brief 指令缓存预热函数数量
 */
static uint32_t s_warmup_func_count = 0U;

/* ========================================================================
 * 指令缓存统计
 * ======================================================================== */

/**
 * @brief 指令缓存统计
 */
static icache_stats_t s_icache_stats CACHE_ALIGN(64);

/**
 * @brief 指令缓存统计锁
 */
static TicketLock_t s_icache_stats_lock;

/**
 * @brief 指令缓存优化初始化标志
 */
static bool s_icache_optimize_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 清空指令缓存
 *
 * @details 清空指令缓存，使用 ARM64 指令。
 */
static inline void icache_flush_all(void)
{
    __asm__ volatile("ic iallu" ::: "memory");
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

/**
 * @brief 同步数据缓存到内存
 *
 * @details 同步数据缓存到内存，使用 ARM64 指令。
 */
static inline void dcache_clean(void)
{
    __asm__ volatile("dc cvau, x0" ::: "memory");
}

/* ========================================================================
 * 指令缓存优化操作 API 实现
 * ======================================================================== */

/**
 * @brief 初始化指令缓存优化
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t icache_optimize_init(void)
{
    if (s_icache_optimize_initialized)
    {
        return KERNEL_OK;
    }

    /* 初始化预热函数列表 */
    (void)memset(s_warmup_funcs, 0, sizeof(s_warmup_funcs));
    s_warmup_func_count = 0U;

    /* 初始化统计 */
    (void)memset(&s_icache_stats, 0, sizeof(icache_stats_t));

    /* 初始化锁 */
    ticket_lock_init(&s_icache_stats_lock);

    s_icache_optimize_initialized = true;

    printk("Icache optimization initialized\n");

    return KERNEL_OK;
}

/**
 * @brief 指令缓存预热
 *
 * @details 预热指令缓存，加载热函数到指令缓存。
 *          此函数在启动时调用，提高后续执行速度。
 */
void icache_warmup(void)
{
    uint32_t i;
    uint32_t j;

    if (!s_icache_optimize_initialized)
    {
        return;
    }

    printk("Icache warmup started: %u functions\n", s_warmup_func_count);

    /* 预热所有函数 */
    for (j = 0U; j < ICACHE_WARMUP_ITERATIONS; j++)
    {
        for (i = 0U; i < s_warmup_func_count; i++)
        {
            if (s_warmup_funcs[i] != NULL)
            {
                /* 调用预热函数 */
                s_warmup_funcs[i]();
            }
        }
    }

    /* 更新统计 */
    ticket_lock_acquire(&s_icache_stats_lock);
    s_icache_stats.warmup_count++;
    ticket_lock_release(&s_icache_stats_lock);

    printk("Icache warmup completed\n");
}

/**
 * @brief 注册指令缓存预热函数
 *
 * @details 注册函数到指令缓存预热列表。
 *
 * @param func 预热函数
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t icache_warmup_register_func(icache_warmup_func_t func)
{
    if (func == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (s_warmup_func_count >= ICACHE_WARMUP_FUNCS)
    {
        return -(int32_t)ENOMEM;
    }

    s_warmup_funcs[s_warmup_func_count] = func;
    s_warmup_func_count++;

    return KERNEL_OK;
}

/**
 * @brief 指令缓存无效化
 *
 * @details 无效化指令缓存，清空指令缓存。
 *          此函数在加载新代码时调用。
 *
 * @param addr 起始地址
 * @param size 大小
 */
void icache_invalidate(void *addr, uint64_t size)
{
    uint64_t start_addr;
    uint64_t end_addr;
    uint64_t current_addr;

    if (!s_icache_optimize_initialized)
    {
        return;
    }

    /* 对齐地址 */
    start_addr = (uint64_t)addr;
    end_addr = start_addr + size;

    /* 对齐到 cache line */
    start_addr = start_addr & (~(CACHE_LINE_SIZE - 1ULL));
    end_addr = (end_addr + CACHE_LINE_SIZE - 1ULL) & (~(CACHE_LINE_SIZE - 1ULL));

    /* 无效化指令缓存 */
    for (current_addr = start_addr; current_addr < end_addr; current_addr += CACHE_LINE_SIZE)
    {
        __asm__ volatile("ic ivau, %0" :: "r"(current_addr) : "memory");
    }

    /* 数据同步屏障 */
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");

    /* 更新统计 */
    ticket_lock_acquire(&s_icache_stats_lock);
    s_icache_stats.invalidate_count++;
    ticket_lock_release(&s_icache_stats_lock);
}

/**
 * @brief 指令缓存同步
 *
 * @details 同步指令缓存，确保指令缓存与内存一致。
 *          此函数在修改代码后调用。
 *
 * @param addr 起始地址
 * @param size 大小
 */
void icache_sync(void *addr, uint64_t size)
{
    uint64_t start_addr;
    uint64_t end_addr;
    uint64_t current_addr;

    if (!s_icache_optimize_initialized)
    {
        return;
    }

    /* 对齐地址 */
    start_addr = (uint64_t)addr;
    end_addr = start_addr + size;

    /* 对齐到 cache line */
    start_addr = start_addr & (~(CACHE_LINE_SIZE - 1ULL));
    end_addr = (end_addr + CACHE_LINE_SIZE - 1ULL) & (~(CACHE_LINE_SIZE - 1ULL));

    /* 清空数据缓存 */
    for (current_addr = start_addr; current_addr < end_addr; current_addr += CACHE_LINE_SIZE)
    {
        __asm__ volatile("dc cvau, %0" :: "r"(current_addr) : "memory");
    }

    /* 无效化指令缓存 */
    for (current_addr = start_addr; current_addr < end_addr; current_addr += CACHE_LINE_SIZE)
    {
        __asm__ volatile("ic ivau, %0" :: "r"(current_addr) : "memory");
    }

    /* 数据同步屏障 */
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");

    /* 更新统计 */
    ticket_lock_acquire(&s_icache_stats_lock);
    s_icache_stats.sync_count++;
    ticket_lock_release(&s_icache_stats_lock);
}

/**
 * @brief 获取指令缓存统计
 *
 * @details 获取指令缓存的命中率统计。
 *
 * @param stats 输出参数，指令缓存统计
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t icache_optimize_get_stats(icache_stats_t *stats)
{
    if (!s_icache_optimize_initialized)
    {
        return -(int32_t)EINVAL;
    }

    if (stats == NULL)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&s_icache_stats_lock);

    (void)memcpy(stats, &s_icache_stats, sizeof(icache_stats_t));

    ticket_lock_release(&s_icache_stats_lock);

    return KERNEL_OK;
}
