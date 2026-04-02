/**
 * @file    phys_mem.c
 * @brief   物理内存页帧管理器实现（Buddy 分配器）
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件实现了基于 Buddy 算法的物理内存页帧管理器：
 *          - 按 2 的幂次方分配/释放连续物理页帧
 *          - 分配/释放时间复杂度 O(log n)
 *          - 支持页帧保留和状态跟踪
 *          - 支持物理内存统计信息查询
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: MM-001（静态分配）、MM-002（Buddy 分配）、MM-004（统计监控）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */

#include <kernel/phys_mem.h>
#include <kernel/barrier.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <kernel/compiler.h>
#include <kernel/spinlock.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 编译时常量与断言
 * ======================================================================== */

/** @brief 系统支持的最大物理页帧数（128MB / 4KB = 32768 页） */
#define MAX_PHYS_PAGES  32768U

/** @brief 无效地址标识 */
#define INVALID_PADDR   ((paddr_t)0U)

/* 编译时检查：MAX_ORDER 不得超过合理范围 */
static_assert(MAX_ORDER <= 20U, "MAX_ORDER must not exceed 20");

/* ========================================================================
 * 静态全局状态
 * ======================================================================== */

/**
 * @brief 页帧描述符数组
 *
 * @details 静态预分配，最多管理 MAX_PHYS_PAGES 个页帧
 *          每个页帧对应一个 page_frame_t 描述符
 */
static page_frame_t s_page_frames[MAX_PHYS_PAGES];

/**
 * @brief Buddy 分配器实例
 */
static buddy_allocator_t s_buddy;

/**
 * @brief 物理内存统计信息
 */
static phys_mem_stats_t s_stats;

/**
 * @brief 总页帧数（运行时确定）
 */
static uint32_t s_frame_count;

/**
 * @brief 物理内存基地址
 */
static paddr_t s_mem_base;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 计算物理地址对应的页帧索引
 *
 * @param paddr 物理地址
 *
 * @return 页帧索引
 */
static inline uint32_t phys_to_index(paddr_t paddr)
{
    return (uint32_t)((paddr - s_mem_base) / (uint64_t)CONFIG_PAGE_SIZE);
}

/**
 * @brief 计算页帧索引对应的物理地址
 *
 * @param index 页帧索引
 *
 * @return 物理地址
 */
static inline paddr_t index_to_phys(uint32_t index)
{
    return s_mem_base + ((paddr_t)index * (paddr_t)CONFIG_PAGE_SIZE);
}

/**
 * @brief 判断指定阶数的 buddy 块是否与地址对齐
 *
 * @details 检查地址是否按照 2^order 页对齐
 *
 * @param paddr 物理地址
 * @param order 阶数
 *
 * @return 非0表示对齐，0表示未对齐
 */
static inline int is_buddy_aligned(paddr_t paddr, uint32_t order)
{
    paddr_t block_size = (paddr_t)CONFIG_PAGE_SIZE << order;
    return ((paddr - s_mem_base) % block_size) == (paddr_t)0U ? 1 : 0;
}

/**
 * @brief 获取指定地址和阶数的 buddy 地址
 *
 * @details 通过 XOR 运算找到 buddy 块的物理地址
 *
 * @param paddr 当前块的物理地址
 * @param order 阶数
 *
 * @return buddy 块的物理地址
 */
static inline paddr_t buddy_address(paddr_t paddr, uint32_t order)
{
    return paddr ^ ((paddr_t)CONFIG_PAGE_SIZE << order);
}

/* ========================================================================
 * 物理内存管理 API 实现
 * ======================================================================== */

/**
 * @brief 初始化物理内存管理器
 *
 * @details 扫描物理内存区域，初始化 buddy 分配器的空闲链表。
 *          将所有页帧初始放置到 MAX_ORDER 阶的空闲链表中。
 *          由调用方负责标记内核保留页和内核使用页。
 *
 * @param mem_base 物理内存起始地址（必须页对齐）
 * @param mem_size 物理内存总大小（字节，必须页大小的整数倍）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: MM-001, MM-002
 */
kernel_status_t phys_mem_init(paddr_t mem_base, uint64_t mem_size)
{
    kernel_status_t ret = KERNEL_OK;
    uint32_t i;
    uint32_t total_pages;
    uint32_t current_order;
    uint32_t pages_remaining;
    uint32_t page_index;

    /* 参数检查：基地址必须非零 */
    if (mem_base == (paddr_t)0U)
    {
        return -(int32_t)EINVAL;
    }

    /* 参数检查：基地址必须页对齐 */
    if ((mem_base % (paddr_t)CONFIG_PAGE_SIZE) != (paddr_t)0U)
    {
        return -(int32_t)EINVAL;
    }

    /* 参数检查：大小必须非零且为页大小的整数倍 */
    if ((mem_size == (uint64_t)0U) ||
        ((mem_size % (uint64_t)CONFIG_PAGE_SIZE) != (uint64_t)0U))
    {
        return -(int32_t)EINVAL;
    }

    /* 计算总页帧数 */
    total_pages = (uint32_t)(mem_size / (uint64_t)CONFIG_PAGE_SIZE);

    /* 参数检查：总页帧数不得超过最大值 */
    if (total_pages > MAX_PHYS_PAGES)
    {
        return -(int32_t)EINVAL;
    }

    /* 保存全局状态 */
    s_mem_base = mem_base;
    s_frame_count = total_pages;

    /* 清零页帧描述符数组 */
    (void)memset(s_page_frames, 0, sizeof(s_page_frames));

    /* 清零统计信息 */
    (void)memset(&s_stats, 0, sizeof(s_stats));
    s_stats.total_pages = total_pages;

    /* 初始化 buddy 分配器 */
    s_buddy.base_addr = mem_base;
    s_buddy.total_pages = total_pages;
    s_buddy.free_pages = (uint32_t)0U;

    /* 初始化每阶空闲链表 */
    for (i = (uint32_t)0U; i <= MAX_ORDER; i++)
    {
        INIT_LIST_HEAD(&s_buddy.free_lists[i]);
        s_buddy.free_counts[i] = (uint32_t)0U;
    }

    /* 初始化自旋锁 */
    ticket_lock_init(&s_buddy.lock);

    /* 初始化所有页帧描述符 */
    for (i = (uint32_t)0U; i < total_pages; i++)
    {
        s_page_frames[i].phys_addr = index_to_phys(i);
        s_page_frames[i].state = PAGE_FREE;
        s_page_frames[i].ref_count = (uint32_t)0U;
        s_page_frames[i].order = (uint8_t)0U;
        INIT_LIST_HEAD(&s_page_frames[i].buddy_list);
    }

    /*
     * 将所有页帧按最大阶数块放入空闲链表
     * 从基地址开始，尽可能多地分配 MAX_ORDER 大小的块
     * 剩余不足 MAX_ORDER 的页帧按较低阶放入
     */
    pages_remaining = total_pages;
    page_index = (uint32_t)0U;

    for (current_order = MAX_ORDER; ; current_order--)
    {
        uint32_t block_pages = (uint32_t)1U << current_order;

        while (pages_remaining >= block_pages)
        {
            page_frame_t *frame = &s_page_frames[page_index];

            frame->order = (uint8_t)current_order;
            frame->state = PAGE_FREE;

            /* 将块首帧加入对应阶的空闲链表 */
            list_add_tail(&frame->buddy_list, &s_buddy.free_lists[current_order]);
            s_buddy.free_counts[current_order]++;
            s_buddy.free_pages += block_pages;

            page_index += block_pages;
            pages_remaining -= block_pages;
        }

        /* 所有页帧已分配完毕 */
        if (pages_remaining == (uint32_t)0U)
        {
            break;
        }

        /* 防止下溢：current_order 为 0 时必须终止 */
        if (current_order == (uint32_t)0U)
        {
            break;
        }
    }

    /* 更新统计：初始空闲页数 */
    s_stats.free_pages = s_buddy.free_pages;

    /* 确保共享状态可见 */
    barrier();

    return ret;
}

/**
 * @brief 分配连续的物理页帧
 *
 * @details 使用 buddy 算法分配 2^order 个连续页帧。
 *          从请求阶数开始向上查找空闲块，找到后逐级分裂。
 *
 * @param order 阶数（0 = 1 页，1 = 2 页，...，不超过 MAX_ORDER）
 *
 * @return 物理地址，失败返回 0
 *
 * @note 对应需求: MM-002
 */
paddr_t phys_mem_alloc_pages(uint32_t order)
{
    paddr_t result_addr = INVALID_PADDR;
    uint32_t current_order;
    uint32_t nr_pages;
    page_frame_t *frame;
    struct list_head *node;

    /* 参数检查：阶数不得超过 MAX_ORDER */
    if (order > MAX_ORDER)
    {
        return INVALID_PADDR;
    }

    /* 获取 buddy 锁 */
    ticket_lock_acquire(&s_buddy.lock);

    /* 从请求阶数开始，向上查找第一个有空闲块的阶 */
    current_order = order;
    while (current_order <= MAX_ORDER)
    {
        if (list_empty(&s_buddy.free_lists[current_order]) == 0)
        {
            /* 找到空闲块 */
            break;
        }
        current_order++;
    }

    /* 未找到足够大的空闲块 */
    if (current_order > MAX_ORDER)
    {
        ticket_lock_release(&s_buddy.lock);
        return INVALID_PADDR;
    }

    /* 从空闲链表中取出一个块 */
    node = s_buddy.free_lists[current_order].next;
    list_del(node);
    INIT_LIST_HEAD(node);
    s_buddy.free_counts[current_order]--;

    frame = list_entry(node, page_frame_t, buddy_list);

    /*
     * 如果当前阶大于请求阶，需要逐级分裂
     * 将当前阶的块分裂为两个 order-1 的块
     */
    while (current_order > order)
    {
        paddr_t right_addr;
        uint32_t right_index;
        page_frame_t *right_frame;

        current_order--;

        /* 计算右半块的地址和索引 */
        right_addr = frame->phys_addr +
                     ((paddr_t)CONFIG_PAGE_SIZE << current_order);
        right_index = phys_to_index(right_addr);
        right_frame = &s_page_frames[right_index];

        /* 初始化右半块并加入低阶空闲链表 */
        right_frame->state = PAGE_FREE;
        right_frame->order = (uint8_t)current_order;
        right_frame->ref_count = (uint32_t)0U;
        INIT_LIST_HEAD(&right_frame->buddy_list);

        list_add(&right_frame->buddy_list, &s_buddy.free_lists[current_order]);
        s_buddy.free_counts[current_order]++;

        /* 确保链表操作对其他核可见 */
        barrier();
    }

    /* 标记页帧为已分配 */
    nr_pages = (uint32_t)1U << order;
    frame->state = PAGE_ALLOCATED;
    frame->order = (uint8_t)order;
    frame->ref_count = (uint32_t)1U;

    /* 更新 buddy 统计 */
    s_buddy.free_pages -= nr_pages;

    /* 更新全局统计 */
    s_stats.free_pages -= nr_pages;
    s_stats.alloc_count++;

    /* 确保状态修改对其他核可见 */
    barrier();

    result_addr = frame->phys_addr;

    /* 释放 buddy 锁 */
    ticket_lock_release(&s_buddy.lock);

    return result_addr;
}

/**
 * @brief 分配单个物理页帧
 *
 * @return 物理地址，失败返回 0
 */
paddr_t phys_mem_alloc_page(void)
{
    return phys_mem_alloc_pages((uint32_t)0U);
}

/**
 * @brief 释放物理页帧
 *
 * @details 释放指定地址和阶数的物理页帧。
 *          当引用计数归零后，尝试与 buddy 块合并。
 *
 * @param paddr 物理地址
 * @param order 阶数
 *
 * @note 对应需求: MM-002
 */
void phys_mem_free_pages(paddr_t paddr, uint32_t order)
{
    uint32_t index;
    uint32_t nr_pages;
    page_frame_t *frame;

    /* 参数检查 */
    if (paddr == INVALID_PADDR)
    {
        return;
    }

    if (order > MAX_ORDER)
    {
        return;
    }

    /* 获取 buddy 锁 */
    ticket_lock_acquire(&s_buddy.lock);

    /* 获取页帧描述符 */
    index = phys_to_index(paddr);
    if (index >= s_frame_count)
    {
        ticket_lock_release(&s_buddy.lock);
        return;
    }

    frame = &s_page_frames[index];

    /* 状态检查：必须为已分配状态 */
    if (frame->state != PAGE_ALLOCATED)
    {
        ticket_lock_release(&s_buddy.lock);
        return;
    }

    /* 递减引用计数 */
    if (frame->ref_count > (uint32_t)0U)
    {
        frame->ref_count--;
    }

    /* 引用计数未归零，仅减少引用，不释放 */
    if (frame->ref_count > (uint32_t)0U)
    {
        barrier();
        ticket_lock_release(&s_buddy.lock);
        return;
    }

    /*
     * 引用计数归零，执行 buddy 合并
     * 从当前阶开始，尝试与 buddy 块合并到更高阶
     */
    while (order < MAX_ORDER)
    {
        paddr_t buddy_addr;
        uint32_t buddy_index;
        page_frame_t *buddy_frame;

        /* 计算 buddy 地址 */
        buddy_addr = buddy_address(frame->phys_addr, order);

        /* 检查 buddy 是否在管理范围内 */
        if (buddy_addr < s_mem_base)
        {
            break;
        }

        buddy_index = phys_to_index(buddy_addr);
        if (buddy_index >= s_frame_count)
        {
            break;
        }

        buddy_frame = &s_page_frames[buddy_index];

        /* 检查 buddy 是否空闲且处于同一阶 */
        if (buddy_frame->state != PAGE_FREE)
        {
            break;
        }

        if ((uint32_t)buddy_frame->order != order)
        {
            break;
        }

        /* 检查地址对齐：buddy 块首地址必须在更高阶上对齐 */
        if (is_buddy_aligned(frame->phys_addr, order + (uint32_t)1U) == 0)
        {
            /* frame 是右半块，buddy 是左半块，需要将 frame 指向左半块 */
            if (buddy_addr < frame->phys_addr)
            {
                /* 将 buddy 从空闲链表移除 */
                list_del(&buddy_frame->buddy_list);
                INIT_LIST_HEAD(&buddy_frame->buddy_list);
                s_buddy.free_counts[order]--;

                /* frame 指向左半块（较低地址） */
                frame = buddy_frame;
            }
            else
            {
                /* frame 已是左半块，将 buddy 从空闲链表移除 */
                list_del(&buddy_frame->buddy_list);
                INIT_LIST_HEAD(&buddy_frame->buddy_list);
                s_buddy.free_counts[order]--;
            }
        }
        else
        {
            /* frame 是左半块，将 buddy 从空闲链表移除 */
            list_del(&buddy_frame->buddy_list);
            INIT_LIST_HEAD(&buddy_frame->buddy_list);
            s_buddy.free_counts[order]--;
        }

        /* 合并：提升到更高阶 */
        order++;
        frame->order = (uint8_t)order;

        /* 确保 buddy 移除操作对其他核可见 */
        barrier();
    }

    /* 标记为空闲并加入对应阶的空闲链表 */
    frame->state = PAGE_FREE;
    frame->ref_count = (uint32_t)0U;
    INIT_LIST_HEAD(&frame->buddy_list);

    list_add(&frame->buddy_list, &s_buddy.free_lists[order]);
    s_buddy.free_counts[order]++;

    /* 更新统计 */
    nr_pages = (uint32_t)1U << order;
    s_buddy.free_pages += nr_pages;
    s_stats.free_pages += nr_pages;
    s_stats.free_count++;

    /* 确保状态修改对其他核可见 */
    barrier();

    /* 释放 buddy 锁 */
    ticket_lock_release(&s_buddy.lock);
}

/**
 * @brief 释放单个物理页帧
 *
 * @param paddr 物理地址
 */
void phys_mem_free_page(paddr_t paddr)
{
    phys_mem_free_pages(paddr, (uint32_t)0U);
}

/**
 * @brief 获取物理页帧描述符
 *
 * @details 根据物理地址计算页帧索引，返回对应的描述符指针。
 *
 * @param paddr 物理地址
 *
 * @return 页帧描述符指针，地址无效时返回 NULL
 */
page_frame_t *phys_mem_get_frame(paddr_t paddr)
{
    uint32_t index;

    /* 地址范围检查 */
    if (paddr < s_mem_base)
    {
        return NULL;
    }

    if (paddr >= (s_mem_base + ((paddr_t)s_frame_count * (paddr_t)CONFIG_PAGE_SIZE)))
    {
        return NULL;
    }

    /* 地址必须页对齐 */
    if ((paddr % (paddr_t)CONFIG_PAGE_SIZE) != (paddr_t)0U)
    {
        return NULL;
    }

    index = phys_to_index(paddr);

    if (index >= s_frame_count)
    {
        return NULL;
    }

    return &s_page_frames[index];
}

/**
 * @brief 获取物理内存统计信息
 *
 * @param stats 输出统计信息（调用者提供缓冲区）
 */
void phys_mem_get_stats(phys_mem_stats_t *stats)
{
    if (stats == NULL)
    {
        return;
    }

    /* 在锁保护下复制统计信息 */
    ticket_lock_acquire(&s_buddy.lock);
    (void)memcpy(stats, &s_stats, sizeof(phys_mem_stats_t));
    ticket_lock_release(&s_buddy.lock);
}

/**
 * @brief 保留物理页帧范围
 *
 * @details 将指定地址范围内的页帧标记为 PAGE_RESERVED，
 *          被保留的页帧不会被 buddy 分配器分配。
 *          对于已处于空闲链表中的页帧，会先从空闲链表中移除。
 *
 * @param base 起始物理地址（必须页对齐）
 * @param size 大小（字节）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t phys_mem_reserve(paddr_t base, uint64_t size)
{
    paddr_t end_addr;
    uint32_t index;
    uint32_t start_index;
    uint32_t end_index;

    /* 参数检查 */
    if (size == (uint64_t)0U)
    {
        return -(int32_t)EINVAL;
    }

    /* 基地址必须页对齐 */
    if ((base % (paddr_t)CONFIG_PAGE_SIZE) != (paddr_t)0U)
    {
        return -(int32_t)EINVAL;
    }

    /* 计算结束地址（向下对齐到页边界） */
    end_addr = base + size;
    end_addr = (end_addr >> 12U) << 12U;

    /* 检查范围有效性 */
    if (base < s_mem_base)
    {
        return -(int32_t)EINVAL;
    }

    if (end_addr > (s_mem_base + ((paddr_t)s_frame_count * (paddr_t)CONFIG_PAGE_SIZE)))
    {
        return -(int32_t)EINVAL;
    }

    start_index = phys_to_index(base);
    end_index = phys_to_index(end_addr);

    /* 获取 buddy 锁 */
    ticket_lock_acquire(&s_buddy.lock);

    for (index = start_index; index < end_index; index++)
    {
        page_frame_t *frame = &s_page_frames[index];

        if (frame->state == PAGE_FREE)
        {
            /* 从空闲链表中移除（如果在链表中） */
            if (frame->buddy_list.next != NULL)
            {
                uint32_t frame_order = (uint32_t)frame->order;

                list_del(&frame->buddy_list);
                INIT_LIST_HEAD(&frame->buddy_list);

                if (frame_order <= MAX_ORDER)
                {
                    s_buddy.free_counts[frame_order]--;
                }
            }

            /* 标记为保留 */
            frame->state = PAGE_RESERVED;
            frame->ref_count = (uint32_t)0U;
            frame->order = (uint8_t)0U;

            /* 更新统计 */
            s_buddy.free_pages--;
            s_stats.free_pages--;
            s_stats.reserved_pages++;

            /* 确保状态修改可见 */
            barrier();
        }
        else if (frame->state == PAGE_KERNEL)
        {
            /* 内核页已经是保留的，更新统计 */
            s_stats.reserved_pages++;
            barrier();
        }
        else
        {
            /* 其他状态（已分配、设备映射等），跳过 */
        }
    }

    /* 释放 buddy 锁 */
    ticket_lock_release(&s_buddy.lock);

    return KERNEL_OK;
}
