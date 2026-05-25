/**
 * @file    pcpu_pool.c
 * @brief   Per-CPU 内存池实现
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 实现 Per-CPU 内存池：
 *          - Per-CPU 内存池分配
 *          - Per-CPU 内存池释放
 *          - Per-CPU 内存池平衡
 *          - 与 Slab 配合使用
 *
 * @note MISRA C:2012 合规
 * @note 对应优化点：Per-CPU 内存池
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/pcpu_pool.h>
#include <kernel/timer.h>
#include <kernel/barrier.h>
#include <kernel/compiler.h>
#include <kernel/printk.h>
#include <kernel/errno.h>
#include "hal.h"
#include <string.h>

/* ========================================================================
 * 全局 Per-CPU 内存池系统
 * ======================================================================== */

/**
 * @brief 全局 Per-CPU 内存池系统
 */
static pcpu_pool_system_t s_pcpu_pool_system CACHE_ALIGN(64);

/**
 * @brief Per-CPU 内存池系统初始化标志
 */
static bool s_pcpu_pool_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 查找空闲块
 *
 * @details 在指定内存池中查找空闲块。
 *
 * @param pool  内存池
 *
 * @return 空闲块索引，失败返回 PCPU_POOL_MAX_BLOCKS
 */
static uint32_t pcpu_pool_find_free_block(pcpu_pool_t *pool)
{
    uint32_t i;

    for (i = 0U; i < PCPU_POOL_MAX_BLOCKS; i++)
    {
        if (pool->blocks[i].status == PCPU_BLOCK_FREE)
        {
            return i;
        }
    }

    return PCPU_POOL_MAX_BLOCKS;
}

/**
 * @brief 查找块索引
 *
 * @details 根据指针查找块索引。
 *
 * @param pool  内存池
 * @param ptr   指针
 *
 * @return 块索引，失败返回 PCPU_POOL_MAX_BLOCKS
 */
static uint32_t pcpu_pool_find_block(pcpu_pool_t *pool, void *ptr)
{
    uint32_t i;
    uintptr_t ptr_val = (uintptr_t)ptr;

    for (i = 0U; i < PCPU_POOL_MAX_BLOCKS; i++)
    {
        uintptr_t block_ptr = (uintptr_t)&pool->blocks[i].data;

        if (ptr_val == block_ptr)
        {
            return i;
        }
    }

    return PCPU_POOL_MAX_BLOCKS;
}

/**
 * @brief 借用块
 *
 * @details 从其他 CPU 池借用一个块。
 *
 * @param sys     Per-CPU 内存池系统
 * @param local_id 本地 CPU ID
 *
 * @return 借用的块指针，失败返回 NULL
 */
static void *pcpu_pool_borrow(pcpu_pool_system_t *sys, uint32_t local_id)
{
    uint32_t i;
    uint32_t best_cpu_id = CONFIG_MAX_CPUS;
    uint32_t max_free = 0U;

    /* 找到空闲块最多的 CPU */
    for (i = 0U; i < CONFIG_MAX_CPUS; i++)
    {
        if (i == local_id)
        {
            continue;
        }

        if (sys->pools[i].free_count > max_free)
        {
            max_free = sys->pools[i].free_count;
            best_cpu_id = i;
        }
    }

    if (best_cpu_id >= CONFIG_MAX_CPUS)
    {
        return NULL;
    }

    /* 从该 CPU 借用一个块 */
    pcpu_pool_t *borrow_pool = &sys->pools[best_cpu_id];
    uint32_t block_idx = pcpu_pool_find_free_block(borrow_pool);

    if (block_idx >= PCPU_POOL_MAX_BLOCKS)
    {
        return NULL;
    }

    /* 借用块 */
    borrow_pool->blocks[block_idx].status = PCPU_BLOCK_ALLOCATED;
    borrow_pool->blocks[block_idx].alloc_count++;
    borrow_pool->free_count--;
    borrow_pool->allocated_count++;
    borrow_pool->lend_count++;

    return borrow_pool->blocks[block_idx].data;
}

/* ========================================================================
 * Per-CPU 内存池操作 API 实现
 * ======================================================================== */

/**
 * @brief 初始化 Per-CPU 内存池系统
 *
 * @param sys   Per-CPU 内存池系统
 * @param size  每个内存池的大小（必须为 PCPU_POOL_SIZE 的倍数）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t pcpu_pool_system_init(pcpu_pool_system_t *sys,
                                        uint32_t size)
{
    uint32_t i;
    uint32_t j;

    if (sys == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (size < PCPU_POOL_SIZE)
    {
        return -(int32_t)22;
    }

    if ((size % PCPU_POOL_SIZE) != 0U)
    {
        return -(int32_t)22;
    }

    (void)memset(sys, 0U, sizeof(pcpu_pool_system_t));

    for (i = 0U; i < CONFIG_MAX_CPUS; i++)
    {
        (void)memset(&sys->pools[i], 0U, sizeof(pcpu_pool_t));

        for (j = 0U; j < PCPU_POOL_MAX_BLOCKS; j++)
        {
            sys->pools[i].blocks[j].status = PCPU_BLOCK_FREE;
            sys->pools[i].blocks[j].alloc_count = 0U;
            sys->pools[i].free_count = 0U;
        }

        sys->pools[i].free_count = PCPU_POOL_MAX_BLOCKS;
        sys->pools[i].allocated_count = 0U;
        sys->pools[i].total_allocations = 0ULL;
        sys->pools[i].total_frees = 0ULL;
        sys->pools[i].borrow_count = 0U;
        sys->pools[i].lend_count = 0U;
    }

    sys->total_size = size * CONFIG_MAX_CPUS;
    sys->total_free = PCPU_POOL_MAX_BLOCKS * CONFIG_MAX_CPUS;
    sys->total_allocated = 0U;
    sys->total_allocations = 0ULL;
    sys->total_frees = 0ULL;
    sys->balance_count = 0U;

    s_pcpu_pool_initialized = true;

    printk("Per-CPU Pool System initialized: %u CPUs, %u bytes per pool\n",
           CONFIG_MAX_CPUS, PCPU_POOL_SIZE);

    return KERNEL_OK;
}

/**
 * @brief 分配内存
 *
 * @details 从本地 CPU 内存池分配内存。
 *          如果本地池不足，尝试从其他 CPU 池借用。
 *
 * @param sys   Per-CPU 内存池系统
 * @param size  分配大小（必须 <= PCPU_POOL_BLOCK_SIZE）
 *
 * @return 分配的指针，失败返回 NULL
 */
void *pcpu_pool_alloc(pcpu_pool_system_t *sys, uint32_t size)
{
    uint32_t cpu_id;
    pcpu_pool_t *pool;
    uint32_t block_idx;
    void *ptr = NULL;

    if (sys == NULL)
    {
        return NULL;
    }

    if (size == 0U)
    {
        return NULL;
    }

    if (size > PCPU_POOL_BLOCK_SIZE)
    {
        return NULL;
    }

    if (!s_pcpu_pool_initialized)
    {
        return NULL;
    }

    cpu_id = hal_get_cpu_id();
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return NULL;
    }

    pool = &sys->pools[cpu_id];

    /* 从本地池分配 */
    block_idx = pcpu_pool_find_free_block(pool);

    if (block_idx >= PCPU_POOL_MAX_BLOCKS)
    {
        /* 本地池不足，尝试借用 */
        ptr = pcpu_pool_borrow(sys, cpu_id);

        if (ptr == NULL)
        {
            return NULL;
        }

        pool->borrow_count++;
    }
    else
    {
        pool->blocks[block_idx].status = PCPU_BLOCK_ALLOCATED;
        pool->blocks[block_idx].alloc_count++;
        pool->free_count--;
        pool->allocated_count++;
        pool->total_allocations++;

        ptr = pool->blocks[block_idx].data;
    }

    sys->total_allocations++;
    sys->total_allocated++;
    sys->total_free--;

    return ptr;
}

/**
 * @brief 释放内存
 *
 * @details 释放之前分配的内存到本地 CPU 内存池。
 *
 * @param sys   Per-CPU 内存池系统
 * @param ptr   要释放的指针
 * @param size  块大小
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t pcpu_pool_free(pcpu_pool_system_t *sys,
                                  void *ptr,
                                  uint32_t size)
{
    uint32_t cpu_id;
    pcpu_pool_t *pool;
    uint32_t block_idx;

    if (sys == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (ptr == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (size == 0U)
    {
        return -(int32_t)EINVAL;
    }

    if (size > PCPU_POOL_BLOCK_SIZE)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_pcpu_pool_initialized)
    {
        return -(int32_t)EINVAL;
    }

    cpu_id = hal_get_cpu_id();
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return -(int32_t)EINVAL;
    }

    pool = &sys->pools[cpu_id];

    /* 找到块索引 */
    block_idx = pcpu_pool_find_block(pool, ptr);

    if (block_idx >= PCPU_POOL_MAX_BLOCKS)
    {
        return -(int32_t)EINVAL;
    }

    pool->blocks[block_idx].status = PCPU_BLOCK_FREE;
    pool->blocks[block_idx].free_count++;
    pool->free_count++;
    pool->allocated_count--;
    pool->total_frees++;

    sys->total_frees++;
    sys->total_allocated--;
    sys->total_free++;

    return KERNEL_OK;
}

/**
 * @brief 平衡 Per-CPU 内存池
 *
 * @details 平衡不同 CPU 的内存池，
 *          从内存充足的池转移到内存不足的池。
 *
 * @param sys  Per-CPU 内存池系统
 */
void pcpu_pool_balance(pcpu_pool_system_t *sys)
{
    uint32_t i;
    uint32_t j;

    if (sys == NULL)
    {
        return;
    }

    if (!s_pcpu_pool_initialized)
    {
        return;
    }

    /* 遍历所有 CPU */
    for (i = 0U; i < CONFIG_MAX_CPUS; i++)
    {
        pcpu_pool_t *from_pool = &sys->pools[i];
        uint32_t borrow_count = 0U;

        /* 找到需要借用的 CPU（空闲块 < 阈值）*/
        if (from_pool->free_count < PCPU_POOL_BALANCE_THRESHOLD)
        {
            /* 从其他 CPU 借用 */
            for (j = 0U; j < CONFIG_MAX_CPUS; j++)
            {
                if (j == i)
                {
                    continue;
                }

                pcpu_pool_t *to_pool = &sys->pools[j];

                /* 找到可以借出的 CPU（空闲块 > 阈值）*/
                if (to_pool->free_count > PCPU_POOL_BALANCE_THRESHOLD)
                {
                    uint32_t block_idx = pcpu_pool_find_free_block(to_pool);

                    if (block_idx < PCPU_POOL_MAX_BLOCKS)
                    {
                        /* 转移块 */
                        to_pool->blocks[block_idx].status = PCPU_BLOCK_ALLOCATED;
                        from_pool->blocks[borrow_count].status = PCPU_BLOCK_ALLOCATED;
                        from_pool->blocks[borrow_count].alloc_count++;

                        to_pool->free_count--;
                        to_pool->allocated_count++;
                        to_pool->lend_count++;

                        from_pool->free_count++;
                        from_pool->allocated_count--;
                        from_pool->borrow_count++;

                        borrow_count++;

                        if (borrow_count >= PCPU_POOL_BALANCE_THRESHOLD)
                        {
                            break;
                        }
                    }
                }
            }
        }
    }

    sys->balance_count++;
}

/**
 * @brief 获取本地 CPU 内存池
 *
 * @details 获取当前 CPU 的内存池。
 *
 * @param sys  Per-CPU 内存池系统
 *
 * @return 本地 CPU 内存池指针
 */
pcpu_pool_t *pcpu_pool_get_local(pcpu_pool_system_t *sys)
{
    uint32_t cpu_id;

    if (sys == NULL)
    {
        return NULL;
    }

    if (!s_pcpu_pool_initialized)
    {
        return NULL;
    }

    cpu_id = hal_get_cpu_id();
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return NULL;
    }

    return &sys->pools[cpu_id];
}

/**
 * @brief 获取指定 CPU 内存池
 *
 * @param sys     Per-CPU 内存池系统
 * @param cpu_id  CPU ID
 *
 * @return 指定 CPU 内存池指针，失败返回 NULL
 */
pcpu_pool_t *pcpu_pool_get_cpu(pcpu_pool_system_t *sys,
                                      uint32_t cpu_id)
{
    if (sys == NULL)
    {
        return NULL;
    }

    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return NULL;
    }

    if (!s_pcpu_pool_initialized)
    {
        return NULL;
    }

    return &sys->pools[cpu_id];
}

/**
 * @brief 获取 Per-CPU 内存池统计信息
 *
 * @param sys                 Per-CPU 内存池系统
 * @param cpu_id              CPU ID
 * @param free_count          输出：空闲块数
 * @param allocated_count     输出：已分配块数
 * @param total_allocations   输出：总分配次数
 * @param total_frees         输出：总释放次数
 * @param borrow_count        输出：借用次数
 * @param lend_count          输出：借出次数
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t pcpu_pool_get_stats(pcpu_pool_system_t *sys,
                                       uint32_t cpu_id,
                                       uint32_t *free_count,
                                       uint32_t *allocated_count,
                                       uint64_t *total_allocations,
                                       uint64_t *total_frees,
                                       uint32_t *borrow_count,
                                       uint32_t *lend_count)
{
    if (sys == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return -(int32_t)EINVAL;
    }

    if ((free_count == NULL) || (allocated_count == NULL) ||
        (total_allocations == NULL) || (total_frees == NULL) ||
        (borrow_count == NULL) || (lend_count == NULL))
    {
        return -(int32_t)EINVAL;
    }

    if (!s_pcpu_pool_initialized)
    {
        return -(int32_t)EINVAL;
    }

    *free_count = sys->pools[cpu_id].free_count;
    *allocated_count = sys->pools[cpu_id].allocated_count;
    *total_allocations = sys->pools[cpu_id].total_allocations;
    *total_frees = sys->pools[cpu_id].total_frees;
    *borrow_count = sys->pools[cpu_id].borrow_count;
    *lend_count = sys->pools[cpu_id].lend_count;

    return KERNEL_OK;
}
