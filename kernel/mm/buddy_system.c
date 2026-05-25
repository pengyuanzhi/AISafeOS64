/**
 * @file    buddy_system.c
 * @brief   Buddy System 内存分配器实现
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 实现 Buddy System 内存分配器：
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

#include "buddy_system.h"
#include <kernel/printk.h>
#include <kernel/barrier.h>
#include <kernel/errno.h>
#include <kernel/spinlock.h>
#include <string.h>

/* ========================================================================
 * 全局 Buddy System 实例
 * ======================================================================== */

/**
 * @brief 全局 Buddy System 实例
 */
static buddy_system_t s_buddy_system CACHE_ALIGN(64);

/**
 * @brief Buddy System 锁
 */
static TicketLock_t s_buddy_system_lock;

/**
 * @brief Buddy System 初始化标志
 */
static bool s_buddy_system_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 计算块的阶数
 *
 * @param size 块大小
 *
 * @return 阶数，如果 size 不是 2 的幂次方则返回 0
 */
static inline uint32_t buddy_calculate_order(uint32_t size)
{
    uint32_t order = 0U;
    uint32_t temp = size;

    if ((temp & (temp - 1U)) != 0U)
    {
        return 0U;
    }

    while (temp > 1U)
    {
        temp = temp >> 1U;
        order++;
    }

    return order;
}

/**
 * @brief 查找块的 buddy
 *
 * @param base 块起始地址
 * @param order 块大小（2^order）
 *
 * @return buddy 的地址
 */
static inline paddr_t buddy_find_buddy(paddr_t base, uint32_t order)
{
    return base ^ (1U << order);
}

/**
 * @brief 判断两个块是否为 buddy
 *
 * @param base1 块 1 起始地址
 * @param base2 块 2 起始地址
 * @param order 块大小（2^order）
 *
 * @return true 表示是 buddy，false 表示不是
 */
static inline bool buddy_is_buddy(paddr_t base1, paddr_t base2, uint32_t order)
{
    return ((base1 ^ base2) == (1U << order));
}

/**
 * @brief 找到空闲块节点
 *
 * @param sys   Buddy System 实例
 * @param base  块起始地址
 * @param order 块大小（2^order）
 *
 * @return 块节点指针，失败返回 NULL
 */
static buddy_node_t *buddy_find_node(buddy_system_t *sys, paddr_t base, uint32_t order)
{
    uint32_t i;

    for (i = 0U; i < BUDDY_BLOCKS_PER_PAGE; i++)
    {
        if ((sys->nodes[i].base == base) &&
            (sys->nodes[i].order == order) &&
            (sys->nodes[i].status != BUDDY_ALLOCATED))
        {
            return &sys->nodes[i];
        }
    }

    return NULL;
}

/* ========================================================================
 * Buddy System 操作 API 实现
 * ======================================================================== */

/**
 * @brief 初始化 Buddy System
 *
 * @param sys         Buddy System 实例
 * @param phys_start  物理内存起始地址
 * @param num_pages   内存页数
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOMEM 内存不足
 */
kernel_status_t buddy_system_init(buddy_system_t *sys,
                                   paddr_t phys_start,
                                   uint32_t num_pages)
{
    uint32_t i;

    if (sys == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (phys_start == 0U)
    {
        return -(int32_t)EINVAL;
    }

    if (num_pages == 0U)
    {
        return -(int32_t)EINVAL;
    }

    (void)memset(sys, 0U, sizeof(buddy_system_t));

    for (i = 0U; i < BUDDY_BLOCKS_PER_PAGE; i++)
    {
        sys->nodes[i].status = BUDDY_ALLOCATED;
        sys->nodes[i].base = 0U;
        sys->nodes[i].order = 0U;
        sys->nodes[i].parent = NULL;
        sys->nodes[i].left = NULL;
        sys->nodes[i].right = NULL;
    }

    sys->nodes[0].status = BUDDY_FREE;
    sys->nodes[0].base = phys_start;
    sys->nodes[0].order = buddy_calculate_order(num_pages * BUDDY_PAGE_SIZE);
    sys->nodes[0].parent = NULL;
    sys->nodes[0].left = NULL;
    sys->nodes[0].right = NULL;

    sys->total_pages = num_pages;
    sys->free_pages = num_pages;
    sys->allocated_pages = 0U;
    sys->total_allocations = 0U;
    sys->total_frees = 0U;
    sys->fragmentation_count = 0U;

    ticket_lock_init(&s_buddy_system_lock);

    s_buddy_system_initialized = true;

    printk("Buddy System initialized: %u pages, base 0x%llx\n",
           num_pages, (unsigned long long)phys_start);

    return KERNEL_OK;
}

/**
 * @brief 分配内存块
 *
 * @param sys   Buddy System 实例
 * @param size  分配大小（必须为 2 的幂次方）
 *
 * @return 分配的物理地址，失败返回 -1
 */
paddr_t buddy_system_alloc(buddy_system_t *sys, uint32_t size)
{
    buddy_node_t *node;
    uint32_t order;
    paddr_t addr = 0xFFFFFFFFFFFFFFFFULL;

    if (sys == NULL)
    {
        return addr;
    }

    if (size == 0U)
    {
        return addr;
    }

    order = buddy_calculate_order(size);
    if (order == 0U)
    {
        return addr;
    }

    if (order > BUDDY_MAX_ORDER)
    {
        return addr;
    }

    ticket_lock_acquire(&s_buddy_system_lock);

    node = buddy_system_find_free(sys, order);

    if (node == NULL)
    {
        uint32_t i;

        for (i = order + 1U; i <= BUDDY_MAX_ORDER; i++)
        {
            node = buddy_system_find_free(sys, i);

            if (node != NULL)
            {
                node = buddy_system_split(node, order);

                if (node != NULL)
                {
                    break;
                }
            }
        }
    }

    if (node == NULL)
    {
        ticket_lock_release(&s_buddy_system_lock);
        return addr;
    }

    node->status = BUDDY_ALLOCATED;

    uint32_t allocated_pages = (1U << order) / BUDDY_PAGE_SIZE;
    sys->allocated_pages += allocated_pages;
    sys->free_pages -= allocated_pages;
    sys->total_allocations++;

    ticket_lock_release(&s_buddy_system_lock);

    return node->base;
}

/**
 * @brief 释放内存块
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
                                   uint32_t size)
{
    buddy_node_t *node;
    uint32_t order;

    if (sys == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (addr == 0U)
    {
        return -(int32_t)EINVAL;
    }

    if (size == 0U)
    {
        return -(int32_t)EINVAL;
    }

    order = buddy_calculate_order(size);
    if (order == 0U)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&s_buddy_system_lock);

    node = buddy_find_node(sys, addr, order);

    if (node == NULL)
    {
        ticket_lock_release(&s_buddy_system_lock);
        return -(int32_t)EINVAL;
    }

    node->status = BUDDY_FREE;

    uint32_t freed_pages = (1U << order) / BUDDY_PAGE_SIZE;
    sys->allocated_pages -= freed_pages;
    sys->free_pages += freed_pages;
    sys->total_frees++;

    buddy_node_t *merged_node = node;
    while (merged_node != NULL)
    {
        merged_node = buddy_system_merge(merged_node);
    }

    ticket_lock_release(&s_buddy_system_lock);

    return KERNEL_OK;
}

/**
 * @brief 查找最小合适的空闲块
 *
 * @param sys          Buddy System 实例
 * @param start_order  起始阶数
 *
 * @return 块节点指针，失败返回 NULL
 */
buddy_node_t *buddy_system_find_free(buddy_system_t *sys, uint32_t start_order)
{
    uint32_t i;

    if (sys == NULL)
    {
        return NULL;
    }

    if (start_order > BUDDY_MAX_ORDER)
    {
        return NULL;
    }

    for (i = start_order; i <= BUDDY_MAX_ORDER; i++)
    {
        uint32_t j;

        for (j = 0U; j < BUDDY_BLOCKS_PER_PAGE; j++)
        {
            if ((sys->nodes[j].order == i) &&
                (sys->nodes[j].status == BUDDY_FREE))
            {
                return &sys->nodes[j];
            }
        }
    }

    return NULL;
}

/**
 * @brief 合并 buddy 块
 *
 * @param node 块节点
 *
 * @return 合并后的父节点，失败返回 NULL
 */
buddy_node_t *buddy_system_merge(buddy_node_t *node)
{
    paddr_t buddy_addr;
    buddy_node_t *buddy_node;
    uint32_t order;

    if (node == NULL)
    {
        return NULL;
    }

    if (node->status != BUDDY_FREE)
    {
        return NULL;
    }

    order = node->order;
    buddy_addr = buddy_find_buddy(node->base, order);

    buddy_node = buddy_find_node(&s_buddy_system, buddy_addr, order);

    if (buddy_node == NULL)
    {
        return NULL;
    }

    if (buddy_node->status != BUDDY_FREE)
    {
        return NULL;
    }

    paddr_t merged_base;
    if (node->base < buddy_addr)
    {
        merged_base = node->base;
    }
    else
    {
        merged_base = buddy_addr;
    }

    buddy_node->status = BUDDY_ALLOCATED;

    node->base = merged_base;
    node->order = order + 1U;

    return node;
}

/**
 * @brief 分割 buddy 块
 *
 * @param node      块节点
 * @param new_order 新的阶数（必须小于原阶数）
 *
 * @return 分割后的左子节点，失败返回 NULL
 */
buddy_node_t *buddy_system_split(buddy_node_t *node, uint32_t new_order)
{
    uint32_t i;
    buddy_node_t *left_child = NULL;
    buddy_node_t *right_child = NULL;

    if (node == NULL)
    {
        return NULL;
    }

    if (node->status != BUDDY_FREE)
    {
        return NULL;
    }

    if (new_order >= node->order)
    {
        return NULL;
    }

    for (i = 0U; i < BUDDY_BLOCKS_PER_PAGE; i++)
    {
        if (s_buddy_system.nodes[i].status == BUDDY_ALLOCATED)
        {
            if (left_child == NULL)
            {
                left_child = &s_buddy_system.nodes[i];
            }
            else if (right_child == NULL)
            {
                right_child = &s_buddy_system.nodes[i];
                break;
            }
        }
    }

    if ((left_child == NULL) || (right_child == NULL))
    {
        return NULL;
    }

    uint32_t child_order = new_order;
    uint32_t child_size = 1U << child_order;

    left_child->status = BUDDY_FREE;
    left_child->base = node->base;
    left_child->order = child_order;
    left_child->parent = node;
    left_child->left = NULL;
    left_child->right = NULL;

    right_child->status = BUDDY_FREE;
    right_child->base = node->base + child_size;
    right_child->order = child_order;
    right_child->parent = node;
    right_child->left = NULL;
    right_child->right = NULL;

    node->left = left_child;
    node->right = right_child;
    node->status = BUDDY_ALLOCATED;

    return left_child;
}

/**
 * @brief 碎片整理
 *
 * @param sys  Buddy System 实例
 */
void buddy_system_defragment(buddy_system_t *sys)
{
    uint32_t i;

    if (sys == NULL)
    {
        return;
    }

    ticket_lock_acquire(&s_buddy_system_lock);

    for (i = 0U; i < BUDDY_BLOCKS_PER_PAGE; i++)
    {
        if (sys->nodes[i].status == BUDDY_FREE)
        {
            buddy_node_t *merged_node = &sys->nodes[i];
            uint32_t merge_count = 0U;

            while (merged_node != NULL)
            {
                buddy_node_t *prev = merged_node;
                merged_node = buddy_system_merge(merged_node);

                if (merged_node == prev)
                {
                    break;
                }

                merge_count++;
            }

            if (merge_count > 0U)
            {
                sys->fragmentation_count -= merge_count;
            }
        }
    }

    ticket_lock_release(&s_buddy_system_lock);
}

/**
 * @brief 获取 Buddy System 统计信息
 *
 * @param sys               Buddy System 实例
 * @param total_pages       输出：总页数
 * @param free_pages        输出：空闲页数
 * @param allocated_pages    输出：已分配页数
 * @param total_allocations  输出：总分配次数
 * @param total_frees       输出：总释放次数
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
                                       uint32_t *fragmentation_count)
{
    if (sys == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if ((total_pages == NULL) || (free_pages == NULL) ||
        (allocated_pages == NULL) || (total_allocations == NULL) ||
        (total_frees == NULL) || (fragmentation_count == NULL))
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&s_buddy_system_lock);

    *total_pages = sys->total_pages;
    *free_pages = sys->free_pages;
    *allocated_pages = sys->allocated_pages;
    *total_allocations = sys->total_allocations;
    *total_frees = sys->total_frees;
    *fragmentation_count = sys->fragmentation_count;

    ticket_lock_release(&s_buddy_system_lock);

    return KERNEL_OK;
}
