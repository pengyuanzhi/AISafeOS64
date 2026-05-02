/**
 * @file    per_cpu_pool.h
 * @brief   Per-CPU 内存池接口
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 提供 Per-CPU 内存池接口：
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

#ifndef KERNEL_PER_CPU_POOL_H
#define KERNEL_PER_CPU_POOL_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/alignment.h>
#include <stdint.h>

/* ========================================================================
 * Per-CPU 内存池配置常量
 * ======================================================================== */

/** @brief Per-CPU 内存池大小（字节） */
#define PER_CPU_POOL_SIZE    (64U * 1024U)  /* 64KB */

/** @brief Per-CPU 内存池块大小（字节） */
#define PER_CPU_POOL_BLOCK_SIZE  128U  /* 128 bytes */

/** @brief Per-CPU 内存池最大块数 */
#define PER_CPU_POOL_MAX_BLOCKS  (PER_CPU_POOL_SIZE / PER_CPU_POOL_BLOCK_SIZE)

/** @brief Per-CPU 内存池平衡间隔（ticks） */
#define PER_CPU_POOL_BALANCE_INTERVAL  1000U

/** @brief Per-CPU 内存池平衡阈值 */
#define PER_CPU_POOL_BALANCE_THRESHOLD  10U

/* ========================================================================
 * Per-CPU 内存池块状态
 * ======================================================================== */

/**
 * @brief 块状态
 */
typedef enum
{
    PER_CPU_BLOCK_FREE = 0U,   /**< @brief 空闲 */
    PER_CPU_BLOCK_ALLOCATED    /**< @brief 已分配 */
} per_cpu_block_status_t;

/* ========================================================================
 * Per-CPU 内存池块
 * ======================================================================== */

/**
 * @brief Per-CPU 内存池块
 *
 * @details 每个 CPU 的内存池由多个块组成，
 *          每个块 128 字节。
 */
typedef struct CACHE_ALIGN(64)
{
    per_cpu_block_status_t status; /**< @brief 块状态 */
    uint8_t                    data[PER_CPU_POOL_BLOCK_SIZE]; /**< @brief 数据区域 */
    uint32_t                   alloc_count; /**< @brief 分配次数 */
    uint32_t                   free_count;  /**< @brief 释放次数 */
} per_cpu_block_t;

/* ========================================================================
 * Per-CPU 内存池
 * ======================================================================== */

/**
 * @brief Per-CPU 内存池
 *
 * @details 每个 CPU 的内存池。
 */
typedef struct CACHE_ALIGN(64)
{
    per_cpu_block_t  blocks[PER_CPU_POOL_MAX_BLOCKS]; /**< @brief 块数组 */
    uint32_t        free_count;                     /**< @brief 空闲块数 */
    uint32_t        allocated_count;                /**< @brief 已分配块数 */
    uint64_t        total_allocations;             /**< @brief 总分配次数 */
    uint64_t        total_frees;                    /**< @brief 总释放次数 */
    uint32_t        borrow_count;                   /**< @brief 借用次数 */
    uint32_t        lend_count;                     /**< @brief 借出次数 */
} per_cpu_pool_t;

/* ========================================================================
 * Per-CPU 内存池系统
 * ======================================================================== */

/**
 * @brief Per-CPU 内存池系统
 *
 * @details 管理所有 CPU 的内存池。
 */
typedef struct CACHE_ALIGN(64)
{
    per_cpu_pool_t   pools[CONFIG_MAX_CPUS]; /**< @brief Per-CPU 内存池 */
    uint32_t         total_size;               /**< @brief 总大小 */
    uint32_t         total_free;               /**< @brief 总空闲块数 */
    uint32_t         total_allocated;          /**< @brief 总已分配块数 */
    uint64_t         total_allocations;       /**< @brief 总分配次数 */
    uint64_t         total_frees;              /**< @brief 总释放次数 */
    uint32_t         balance_count;           /**< @brief 平衡次数 */
} per_cpu_pool_system_t;

/* ========================================================================
 * Per-CPU 内存池操作 API
 * ======================================================================== */

/**
 * @brief 初始化 Per-CPU 内存池系统
 *
 * @param sys   Per-CPU 内存池系统
 * @param size  每个内存池的大小（必须为 PER_CPU_POOL_SIZE 的倍数）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t per_cpu_pool_system_init(per_cpu_pool_system_t *sys,
                                        uint32_t size);

/**
 * @brief 分配内存
 *
 * @details 从本地 CPU 内存池分配内存。
 *          如果本地池不足，尝试从其他 CPU 池借用。
 *
 * @param sys   Per-CPU 内存池系统
 * @param size  分配大小（必须 <= PER_CPU_POOL_BLOCK_SIZE）
 *
 * @return 分配的指针，失败返回 NULL
 */
void *per_cpu_pool_alloc(per_cpu_pool_system_t *sys, uint32_t size);

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
kernel_status_t per_cpu_pool_free(per_cpu_pool_system_t *sys,
                                  void *ptr,
                                  uint32_t size);

/**
 * @brief 平衡 Per-CPU 内存池
 *
 * @details 平衡不同 CPU 的内存池，
 *          从内存充足的池转移到内存不足的池。
 *
 * @param sys  Per-CPU 内存池系统
 */
void per_cpu_pool_balance(per_cpu_pool_system_t *sys);

/**
 * @brief 获取本地 CPU 内存池
 *
 * @details 获取当前 CPU 的内存池。
 *
 * @param sys  Per-CPU 内存池系统
 *
 * @return 本地 CPU 内存池指针
 */
per_cpu_pool_t *per_cpu_pool_get_local(per_cpu_pool_system_t *sys);

/**
 * @brief 获取指定 CPU 内存池
 *
 * @param sys     Per-CPU 内存池系统
 * @param cpu_id  CPU ID
 *
 * @return 指定 CPU 内存池指针，失败返回 NULL
 */
per_cpu_pool_t *per_cpu_pool_get_cpu(per_cpu_pool_system_t *sys,
                                      uint32_t cpu_id);

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
kernel_status_t per_cpu_pool_get_stats(per_cpu_pool_system_t *sys,
                                       uint32_t cpu_id,
                                       uint32_t *free_count,
                                       uint32_t *allocated_count,
                                       uint64_t *total_allocations,
                                       uint64_t *total_frees,
                                       uint32_t *borrow_count,
                                       uint32_t *lend_count);

#endif /* KERNEL_PER_CPU_POOL_H */
