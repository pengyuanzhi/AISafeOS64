/**
 * @file    phys_mem.h
 * @brief   物理内存页帧管理接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了物理内存页帧管理接口：
 *          - 物理页帧分配/释放
 *          - Buddy 分配器（按 2 的幂次方分配）
 *          - 物理内存统计
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: MM-001（静态分配）、MM-002（内存池分区）、MM-004（统计监控）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_PHYS_MEM_H
#define KERNEL_PHYS_MEM_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/spinlock.h>
#include <kernel/list.h>
#include <stdint.h>

/* ========================================================================
 * 物理页帧常量
 * ======================================================================== */

/** @brief 页帧最大阶数（buddy 最大分配 2^MAX_ORDER 页） */
#define MAX_ORDER           11U

/** @brief 最大连续页数（2^11 = 2048 页 = 8MB） */
#define MAX_CONTIGUOUS_PAGES (1U << MAX_ORDER)

/* ========================================================================
 * 物理页帧描述符
 * ======================================================================== */

/**
 * @brief 页帧状态
 */
typedef enum
{
    PAGE_FREE = 0U,         /**< @brief 空闲 */
    PAGE_ALLOCATED,         /**< @brief 已分配 */
    PAGE_RESERVED,          /**< @brief 内核保留（不可分配） */
    PAGE_KERNEL,            /**< @brief 内核使用 */
    PAGE_DEVICE             /**< @brief 设备映射 */
} page_state_t;

/**
 * @brief 物理页帧描述符
 *
 * @details 每个物理页帧对应一个 page_frame_t 结构，
 *          用于跟踪页帧的状态和引用计数。
 */
typedef struct
{
    paddr_t         phys_addr;      /**< @brief 物理地址 */
    page_state_t    state;          /**< @brief 页帧状态 */
    uint32_t        ref_count;      /**< @brief 引用计数 */
    uint8_t         order;          /**< @brief buddy 阶数（空闲时有效） */
    uint8_t         reserved[3];    /**< @brief 保留对齐 */
    struct list_head buddy_list;    /**< @brief buddy 空闲链表节点 */
} page_frame_t;

/* ========================================================================
 * Buddy 分配器
 * ======================================================================== */

/**
 * @brief Buddy 分配器（Per-Zone）
 *
 * @details 管理一个连续物理内存区域的页帧分配。
 *          每个阶数维护一个空闲链表，分配/释放 O(log n)。
 */
typedef struct
{
    struct list_head free_lists[MAX_ORDER + 1U]; /**< @brief 每阶空闲链表 */
    uint32_t         free_counts[MAX_ORDER + 1U];/**< @brief 每阶空闲页数 */
    TicketLock_t     lock;                       /**< @brief 分配器锁 */
    paddr_t          base_addr;                  /**< @brief 区域起始物理地址 */
    uint32_t         total_pages;                /**< @brief 总页帧数 */
    uint32_t         free_pages;                 /**< @brief 当前空闲页帧数 */
} buddy_allocator_t;

/* ========================================================================
 * 物理内存统计
 * ======================================================================== */

/**
 * @brief 物理内存统计信息
 */
typedef struct
{
    uint32_t total_pages;     /**< @brief 总页帧数 */
    uint32_t free_pages;      /**< @brief 空闲页帧数 */
    uint32_t kernel_pages;    /**< @brief 内核使用页帧数 */
    uint32_t reserved_pages;  /**< @brief 保留页帧数 */
    uint32_t device_pages;    /**< @brief 设备映射页帧数 */
    uint32_t alloc_count;     /**< @brief 累计分配次数 */
    uint32_t free_count;      /**< @brief 累计释放次数 */
} phys_mem_stats_t;

/* ========================================================================
 * 物理内存管理 API
 * ======================================================================== */

/**
 * @brief 初始化物理内存管理器
 *
 * @details 扫描设备树或平台信息，初始化 buddy 分配器。
 *          将内核已使用的页面标记为保留。
 *
 * @param mem_base 物理内存起始地址
 * @param mem_size 物理内存总大小（字节）
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: MM-001, MM-002
 */
kernel_status_t phys_mem_init(paddr_t mem_base, uint64_t mem_size);

/**
 * @brief 分配连续的物理页帧
 *
 * @details 使用 buddy 算法分配 2^order 个连续页帧。
 *
 * @param order 阶数（0 = 1 页，1 = 2 页，...）
 *
 * @return 物理地址，失败返回 0
 *
 * @note 对应需求: MM-002
 */
paddr_t phys_mem_alloc_pages(uint32_t order);

/**
 * @brief 分配单个物理页帧
 *
 * @return 物理地址，失败返回 0
 */
paddr_t phys_mem_alloc_page(void);

/**
 * @brief 释放物理页帧
 *
 * @param paddr 物理地址
 * @param order 阶数
 */
void phys_mem_free_pages(paddr_t paddr, uint32_t order);

/**
 * @brief 释放单个物理页帧
 *
 * @param paddr 物理地址
 */
void phys_mem_free_page(paddr_t paddr);

/**
 * @brief 获取物理页帧描述符
 *
 * @param paddr 物理地址
 *
 * @return 页帧描述符指针
 */
page_frame_t *phys_mem_get_frame(paddr_t paddr);

/**
 * @brief 获取物理内存统计信息
 *
 * @param stats 输出统计信息
 */
void phys_mem_get_stats(phys_mem_stats_t *stats);

/**
 * @brief 保留物理页帧范围（不可分配）
 *
 * @param base 起始物理地址
 * @param size 大小（字节）
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t phys_mem_reserve(paddr_t base, uint64_t size);

#endif /* KERNEL_PHYS_MEM_H */
