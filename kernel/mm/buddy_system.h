/**
 * @file    buddy_system.h
 * @brief   Buddy System 内存分配器接口
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 提供 Buddy System 内存分配器接口：
 *          - 大块内存分配（2^n）
 *          - Buddy 合并和分割
 *          - 碎片整理
 *          - 与 Slab 缓存配合使用
 *
 * @note MISRA C:2012 合规
 * @note 对应优化点：Buddy System
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_BUDDY_SYSTEM_H
#define KERNEL_BUDDY_SYSTEM_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/alignment.h>
#include <stdint.h>

/* ========================================================================
 * Buddy System 配置常量
 * ======================================================================== */

/** @brief Buddy System 最大阶数（支持的最大块大小） */
#define BUDDY_MAX_ORDER      10U

/** @brief Buddy System 最小阶数（对应的最小块大小） */
#define BUDDY_MIN_ORDER      0U

/** @brief Buddy System 块大小 */
#define BUDDY_BLOCK_SIZE(order)  (1U << (order))

/** @brief Buddy System 最大块大小（2^10 = 1KB） */
#define BUDDY_MAX_BLOCK_SIZE   (1U << BUDDY_MAX_ORDER)

/** @brief Buddy System 最小块大小（2^0 = 1 字节） */
#define BUDDY_MIN_BLOCK_SIZE   (1U << BUDDY_MIN_ORDER)

/** @brief Buddy System 页大小 */
#define BUDDY_PAGE_SIZE        4096U

/** @brief Buddy System 每页的块数量 */
#define BUDDY_BLOCKS_PER_PAGE  (BUDDY_PAGE_SIZE / BUDDY_MIN_BLOCK_SIZE)

/* ========================================================================
 * 块状态
 * ======================================================================== */

/**
 * @brief 块状态
 */
typedef enum
{
    BUDDY_FREE = 0U,     /**< @brief 空闲 */
    BUDDY_ALLOCATED      /**< @brief 已分配 */
} buddy_block_status_t;

/* ========================================================================
 * Buddy System 状态
 * ======================================================================== */

/**
 * @brief Buddy 块
 *
 * @details 每个 Buddy 块包含以下信息：
 *          - 状态：空闲或已分配
 *          - 基址：块起始物理地址
 *          - 大小：块大小（2^order）
 *          - 父节点：用于合并操作
 */
typedef struct CACHE_ALIGN(64)
{
    buddy_block_status_t status;    /**< @brief 块状态 */
    paddr_t               base;     /**< @brief 块起始物理地址 */
    uint32_t              order;    /**< @block 大小（2^order） */
    struct buddy_node    *parent;  /**< @brief 父节点指针 */
    struct buddy_node    *left;    /**< @brief 左子节点指针 */
    struct buddy_node    *right;   /**< @brief 右子节点指针 */
} buddy_node_t;

/**
 * @brief Buddy System 根节点
 *
 * @details Buddy System 的根节点，管理所有块。
 *          根节点不占用实际内存，只是管理容器。
 */
typedef struct CACHE_ALIGN(64)
{
    buddy_node_t       nodes[BUDDY_BLOCKS_PER_PAGE];  /**< @brief 块节点数组 */
    uint32_t           total_pages;                     /**< @brief 总页数 */
    uint32_t           free_pages;                      /**< @brief 空闲页数 */
    uint32_t           allocated_pages;                  /**< @brief 已分配页数 */
    uint32_t           total_allocations;                /**< @brief 总分配次数 */
    uint32_t           total_frees;                     /**< @brief 总释放次数 */
    uint32_t           fragmentation_count;             /**< @brief 碎片计数 */
} buddy_system_t;

/* ========================================================================
 * Buddy System 操作 API
 * ======================================================================== */

/**
 * @brief 初始化 Buddy System
 *
 * @param sys  Buddy System 实例
 * @param phys_start 物理内存起始地址
 * @param num_pages 内存页数
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOMEM 内存不足
 */
kernel_status_t buddy_system_init(buddy_system_t *sys,
                                   paddr_t phys_start,
                                   uint32_t num_pages);

/**
 * @brief 分配内存块
 *
 * @details 分配指定大小的内存块。
 *          如果没有合适大小的块，尝试分割更大的块。
 *
 * @param sys   Buddy System 实例
 * @param size  分配大小（必须为 2 的幂次方）
 *
 * @return 分配的物理地址，失败返回 -1
 */
paddr_t buddy_system_alloc(buddy_system_t *sys, uint32_t size);

/**
 * @brief 释放内存块
 *
 * @details 释放之前分配的内存块。
 *          如果 buddy 空闲，尝试合并成更大的块。
 *
 * @param sys  Buddy System 实例
 * @param addr 要释放的物理地址
 * @param size 块大小（必须为 2 的幂次方）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t buddy_system_free(buddy_system_t *sys,
                                   paddr_t addr,
                                   uint32_t size);

/**
 * @brief 查找最小合适的空闲块
 *
 * @details 从指定阶数开始，找到最小的空闲块。
 *
 * @param sys   Buddy System 实例
 * @param start_order 起始阶数
 *
 * @return 块节点指针，失败返回 NULL
 */
buddy_node_t *buddy_system_find_free(buddy_system_t *sys, uint32_t start_order);

/**
 * @brief 合并 buddy 块
 *
 * @details 如果 buddy 空闲且地址连续，合并成更大的块。
 *
 * @param node 块节点
 *
 * @return 合并后的父节点，失败返回 NULL
 */
buddy_node_t *buddy_system_merge(buddy_node_t *node);

/**
 * @brief 分割 buddy 块
 *
 * @details 将一个块分割成两个相同的子块。
 *
 * @param node 块节点
 * @param new_order 新的阶数（必须小于原阶数）
 *
 * @return 分割后的左子节点，失败返回 NULL
 */
buddy_node_t *buddy_system_split(buddy_node_t *node, uint32_t new_order);

/**
 * @brief 碎片整理
 *
 * @details 合并所有连续的空闲 buddy 块。
 *
 * @param sys  Buddy System 实例
 */
void buddy_system_defragment(buddy_system_t *sys);

/**
 * @brief 获取 Buddy System 统计信息
 *
 * @param sys     Buddy System 实例
 * @param total_pages   输出：总页数
 * @param free_pages    输出：空闲页数
 * @param allocated_pages 输出：已分配页数
 * @param total_allocations  输出：总分配次数
 * @param total_frees     输出：总释放次数
 * @param fragmentation_count 输出：碎片计数
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t buddy_system_get_stats(buddy_system_t *sys,
                                       uint32_t *total_pages,
                                       uint32_t *free_pages,
                                       uint32_t *allocated_pages,
                                       uint32_t *total_allocations,
                                       uint32_t *total_frees,
                                       uint32_t *fragmentation_count);

#endif /* KERNEL_BUDDY_SYSTEM_H */
