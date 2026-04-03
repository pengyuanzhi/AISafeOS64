/**
 * @file    test_phys_mem.c
 * @brief   AISafe64 RTOS - 物理内存 Buddy 分配器单元测试（宿主机）
 * @author  AISafe64 Team
 * @date    2026-04-03
 * @version 1.0
 *
 * @details 物理内存 Buddy 分配器宿主机自包含测试
 *          测试与内核 phys_mem.c 一致的逻辑：
 *          - 初始化（页帧描述符、空闲链表、统计）
 *          - 分配单页/多阶页帧（向上搜索 + 分裂）
 *          - 释放页帧（引用计数 + Buddy 合并）
 *          - 页帧保留（移出空闲链表）
 *          - 页帧描述符查询
 *          - 统计信息
 *          - NULL / 无效参数安全
 *          - 压力测试
 *
 * @note 对应需求: MM-001（静态分配）、MM-002（Buddy 分配）、TF-001
 */

#include "mock_kernel.h"

/* ========================================================================
 * 侵入式双向链表（与 kernel/list.h 一致）
 * ======================================================================== */

struct list_head
{
    struct list_head *next;
    struct list_head *prev;
};

static inline void init_list_head(struct list_head *head)
{
    if (head == NULL) { return; }
    head->next = head;
    head->prev = head;
}

static inline int list_empty(const struct list_head *head)
{
    if (head == NULL) { return 1; }
    return (head->next == head) ? 1 : 0;
}

static inline void list_add(struct list_head *new_node,
                             struct list_head *head)
{
    if (new_node == NULL) { return; }
    if (head == NULL) { return; }

    new_node->next = head->next;
    new_node->prev = head;
    head->next->prev = new_node;
    head->next = new_node;
}

static inline void list_add_tail(struct list_head *new_node,
                                  struct list_head *head)
{
    if (new_node == NULL) { return; }
    if (head == NULL) { return; }

    new_node->prev = head->prev;
    new_node->next = head;
    head->prev->next = new_node;
    head->prev = new_node;
}

static inline void list_del(struct list_head *entry)
{
    if (entry == NULL) { return; }

    entry->prev->next = entry->next;
    entry->next->prev = entry->prev;
    entry->next = entry;
    entry->prev = entry;
}

#undef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#undef list_entry
#define list_entry(ptr, type, member) container_of(ptr, type, member)

/* ========================================================================
 * 配置常量（与 kernel/config.h 一致）
 * ======================================================================== */

#define CONFIG_PAGE_SIZE  4096U

/* ========================================================================
 * 页帧状态与类型定义（与 kernel/phys_mem.h 一致）
 * ======================================================================== */

#define MAX_ORDER           11U
#define MAX_CONTIGUOUS_PAGES (1U << MAX_ORDER)

typedef enum
{
    PAGE_FREE = 0U,
    PAGE_ALLOCATED,
    PAGE_RESERVED,
    PAGE_KERNEL,
    PAGE_DEVICE
} page_state_t;

typedef struct
{
    paddr_t         phys_addr;
    page_state_t    state;
    uint32_t        ref_count;
    uint8_t         order;
    uint8_t         reserved[3];
    struct list_head buddy_list;
} page_frame_t;

typedef struct
{
    struct list_head free_lists[MAX_ORDER + 1U];
    uint32_t         free_counts[MAX_ORDER + 1U];
    TicketLock_t     lock;
    paddr_t          base_addr;
    uint32_t         total_pages;
    uint32_t         free_pages;
} buddy_allocator_t;

typedef struct
{
    uint32_t total_pages;
    uint32_t free_pages;
    uint32_t kernel_pages;
    uint32_t reserved_pages;
    uint32_t device_pages;
    uint32_t alloc_count;
    uint32_t free_count;
} phys_mem_stats_t;

/* ========================================================================
 * 测试常量
 * ======================================================================== */

/** @brief 测试最大页帧数（16 页 = 64KB） */
#define TEST_MAX_PAGES  16U

/** @brief 测试基地址（页对齐） */
#define TEST_MEM_BASE   ((paddr_t)0x10000000ULL)

/** @brief 测试内存大小 */
#define TEST_MEM_SIZE   ((uint64_t)TEST_MAX_PAGES * (uint64_t)CONFIG_PAGE_SIZE)

/** @brief 无效地址 */
#define INVALID_PADDR   ((paddr_t)0U)

/* ========================================================================
 * Buddy 分配器模拟实现（与 phys_mem.c 逻辑一致）
 * ======================================================================== */

static page_frame_t s_page_frames[TEST_MAX_PAGES];
static buddy_allocator_t s_buddy;
static phys_mem_stats_t s_stats;
static uint32_t s_frame_count;
static paddr_t s_mem_base;

/**
 * @brief 计算物理地址对应的页帧索引
 */
static inline uint32_t phys_to_index(paddr_t paddr)
{
    return (uint32_t)((paddr - s_mem_base) / (uint64_t)CONFIG_PAGE_SIZE);
}

/**
 * @brief 计算页帧索引对应的物理地址
 */
static inline paddr_t index_to_phys(uint32_t index)
{
    return s_mem_base + ((paddr_t)index * (paddr_t)CONFIG_PAGE_SIZE);
}

/**
 * @brief 获取 buddy 地址（XOR 运算）
 */
static inline paddr_t buddy_address(paddr_t paddr, uint32_t order)
{
    return paddr ^ ((paddr_t)CONFIG_PAGE_SIZE << order);
}

/**
 * @brief 判断是否 buddy 对齐
 */
static inline int is_buddy_aligned(paddr_t paddr, uint32_t order)
{
    paddr_t block_size = (paddr_t)CONFIG_PAGE_SIZE << order;
    return ((paddr - s_mem_base) % block_size) == (paddr_t)0U ? 1 : 0;
}

/**
 * @brief 初始化物理内存管理器
 * @details 与 phys_mem.c 的 phys_mem_init 逻辑一致
 */
static kernel_status_t phys_mem_test_init(paddr_t mem_base, uint64_t mem_size)
{
    uint32_t total_pages;
    uint32_t current_order;
    uint32_t pages_remaining;
    uint32_t page_index;
    uint32_t i;

    if (mem_base == (paddr_t)0U)
    {
        return -(int32_t)EINVAL;
    }

    if ((mem_base % (paddr_t)CONFIG_PAGE_SIZE) != (paddr_t)0U)
    {
        return -(int32_t)EINVAL;
    }

    if ((mem_size == (uint64_t)0U) ||
        ((mem_size % (uint64_t)CONFIG_PAGE_SIZE) != (uint64_t)0U))
    {
        return -(int32_t)EINVAL;
    }

    total_pages = (uint32_t)(mem_size / (uint64_t)CONFIG_PAGE_SIZE);

    if (total_pages > TEST_MAX_PAGES)
    {
        return -(int32_t)EINVAL;
    }

    s_mem_base = mem_base;
    s_frame_count = total_pages;

    (void)memset(s_page_frames, 0, sizeof(s_page_frames));
    (void)memset(&s_stats, 0, sizeof(s_stats));
    s_stats.total_pages = total_pages;

    s_buddy.base_addr = mem_base;
    s_buddy.total_pages = total_pages;
    s_buddy.free_pages = 0U;

    for (i = 0U; i <= MAX_ORDER; i++)
    {
        init_list_head(&s_buddy.free_lists[i]);
        s_buddy.free_counts[i] = 0U;
    }

    ticket_lock_init(&s_buddy.lock);

    for (i = 0U; i < total_pages; i++)
    {
        s_page_frames[i].phys_addr = index_to_phys(i);
        s_page_frames[i].state = PAGE_FREE;
        s_page_frames[i].ref_count = 0U;
        s_page_frames[i].order = 0U;
        init_list_head(&s_page_frames[i].buddy_list);
    }

    /* 贪心放置：从最大阶向下分配块 */
    pages_remaining = total_pages;
    page_index = 0U;

    for (current_order = MAX_ORDER; ; current_order--)
    {
        uint32_t block_pages = (uint32_t)1U << current_order;

        while (pages_remaining >= block_pages)
        {
            page_frame_t *frame = &s_page_frames[page_index];

            frame->order = (uint8_t)current_order;
            frame->state = PAGE_FREE;

            list_add_tail(&frame->buddy_list, &s_buddy.free_lists[current_order]);
            s_buddy.free_counts[current_order]++;
            s_buddy.free_pages += block_pages;

            page_index += block_pages;
            pages_remaining -= block_pages;
        }

        if (pages_remaining == 0U)
        {
            break;
        }

        if (current_order == 0U)
        {
            break;
        }
    }

    s_stats.free_pages = s_buddy.free_pages;

    return KERNEL_OK;
}

/**
 * @brief 分配连续物理页帧
 * @details 与 phys_mem.c 的 phys_mem_alloc_pages 逻辑一致
 */
static paddr_t phys_mem_alloc_pages(uint32_t order)
{
    paddr_t result_addr = INVALID_PADDR;
    uint32_t current_order;
    uint32_t nr_pages;
    page_frame_t *frame;
    struct list_head *node;

    if (order > MAX_ORDER)
    {
        return INVALID_PADDR;
    }

    ticket_lock_acquire(&s_buddy.lock);

    current_order = order;
    while (current_order <= MAX_ORDER)
    {
        if (list_empty(&s_buddy.free_lists[current_order]) == 0)
        {
            break;
        }
        current_order++;
    }

    if (current_order > MAX_ORDER)
    {
        ticket_lock_release(&s_buddy.lock);
        return INVALID_PADDR;
    }

    node = s_buddy.free_lists[current_order].next;
    list_del(node);
    init_list_head(node);
    s_buddy.free_counts[current_order]--;

    frame = list_entry(node, page_frame_t, buddy_list);

    /* 逐级分裂 */
    while (current_order > order)
    {
        paddr_t right_addr;
        uint32_t right_index;
        page_frame_t *right_frame;

        current_order--;

        right_addr = frame->phys_addr +
                     ((paddr_t)CONFIG_PAGE_SIZE << current_order);
        right_index = phys_to_index(right_addr);
        right_frame = &s_page_frames[right_index];

        right_frame->state = PAGE_FREE;
        right_frame->order = (uint8_t)current_order;
        right_frame->ref_count = 0U;
        init_list_head(&right_frame->buddy_list);

        list_add(&right_frame->buddy_list, &s_buddy.free_lists[current_order]);
        s_buddy.free_counts[current_order]++;
    }

    nr_pages = (uint32_t)1U << order;
    frame->state = PAGE_ALLOCATED;
    frame->order = (uint8_t)order;
    frame->ref_count = 1U;

    s_buddy.free_pages -= nr_pages;
    s_stats.free_pages -= nr_pages;
    s_stats.alloc_count++;

    result_addr = frame->phys_addr;

    ticket_lock_release(&s_buddy.lock);

    return result_addr;
}

/**
 * @brief 分配单个物理页帧
 */
static paddr_t phys_mem_alloc_page(void)
{
    return phys_mem_alloc_pages(0U);
}

/**
 * @brief 释放物理页帧（含 Buddy 合并）
 * @details 与 phys_mem.c 的 phys_mem_free_pages 逻辑一致
 */
static void phys_mem_free_pages(paddr_t paddr, uint32_t order)
{
    uint32_t index;
    uint32_t nr_pages;
    page_frame_t *frame;

    if (paddr == INVALID_PADDR)
    {
        return;
    }

    if (order > MAX_ORDER)
    {
        return;
    }

    ticket_lock_acquire(&s_buddy.lock);

    index = phys_to_index(paddr);
    if (index >= s_frame_count)
    {
        ticket_lock_release(&s_buddy.lock);
        return;
    }

    frame = &s_page_frames[index];

    if (frame->state != PAGE_ALLOCATED)
    {
        ticket_lock_release(&s_buddy.lock);
        return;
    }

    if (frame->ref_count > 0U)
    {
        frame->ref_count--;
    }

    if (frame->ref_count > 0U)
    {
        ticket_lock_release(&s_buddy.lock);
        return;
    }

    /* Buddy 合并 */
    while (order < MAX_ORDER)
    {
        paddr_t buddy_addr;
        uint32_t buddy_index;
        page_frame_t *buddy_frame;

        buddy_addr = buddy_address(frame->phys_addr, order);

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

        if (buddy_frame->state != PAGE_FREE)
        {
            break;
        }

        if ((uint32_t)buddy_frame->order != order)
        {
            break;
        }

        /* 移除 buddy 块并减去其页数（合并修正） */
        list_del(&buddy_frame->buddy_list);
        init_list_head(&buddy_frame->buddy_list);
        s_buddy.free_counts[order]--;

        /* 减去 buddy 已计入的空闲页数，最后统一加回合并后的块 */
        {
            uint32_t buddy_nr = (uint32_t)1U << order;
            s_buddy.free_pages -= buddy_nr;
            s_stats.free_pages -= buddy_nr;
        }

        /* 确定合并后的块首 */
        if (is_buddy_aligned(frame->phys_addr, order + 1U) == 0)
        {
            if (buddy_addr < frame->phys_addr)
            {
                frame = buddy_frame;
            }
        }

        order++;
        frame->order = (uint8_t)order;
    }

    frame->state = PAGE_FREE;
    frame->ref_count = 0U;
    init_list_head(&frame->buddy_list);

    list_add(&frame->buddy_list, &s_buddy.free_lists[order]);
    s_buddy.free_counts[order]++;

    nr_pages = (uint32_t)1U << order;
    s_buddy.free_pages += nr_pages;
    s_stats.free_pages += nr_pages;
    s_stats.free_count++;

    ticket_lock_release(&s_buddy.lock);
}

/**
 * @brief 释放单个物理页帧
 */
static void phys_mem_free_page(paddr_t paddr)
{
    phys_mem_free_pages(paddr, 0U);
}

/**
 * @brief 获取页帧描述符
 * @details 与 phys_mem.c 的 phys_mem_get_frame 一致
 */
static page_frame_t *phys_mem_get_frame(paddr_t paddr)
{
    uint32_t index;

    if (paddr < s_mem_base)
    {
        return NULL;
    }

    if (paddr >= (s_mem_base + ((paddr_t)s_frame_count * (paddr_t)CONFIG_PAGE_SIZE)))
    {
        return NULL;
    }

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
 * @brief 获取统计信息
 */
static void phys_mem_get_stats(phys_mem_stats_t *stats)
{
    if (stats == NULL)
    {
        return;
    }

    ticket_lock_acquire(&s_buddy.lock);
    (void)memcpy(stats, &s_stats, sizeof(phys_mem_stats_t));
    ticket_lock_release(&s_buddy.lock);
}

/**
 * @brief 保留物理页帧范围
 * @details 与 phys_mem.c 的 phys_mem_reserve 一致
 */
static kernel_status_t phys_mem_reserve(paddr_t base, uint64_t size)
{
    paddr_t end_addr;
    uint32_t index;
    uint32_t start_index;
    uint32_t end_index;

    if (size == (uint64_t)0U)
    {
        return -(int32_t)EINVAL;
    }

    if ((base % (paddr_t)CONFIG_PAGE_SIZE) != (paddr_t)0U)
    {
        return -(int32_t)EINVAL;
    }

    end_addr = base + size;
    end_addr = (end_addr >> 12U) << 12U;

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

    ticket_lock_acquire(&s_buddy.lock);

    for (index = start_index; index < end_index; index++)
    {
        page_frame_t *frame = &s_page_frames[index];

        if (frame->state == PAGE_FREE)
        {
            /* 仅当页帧实际在空闲链表中时才移除（非自引用） */
            if (frame->buddy_list.next != &frame->buddy_list)
            {
                uint32_t frame_order = (uint32_t)frame->order;

                list_del(&frame->buddy_list);
                init_list_head(&frame->buddy_list);

                if (frame_order <= MAX_ORDER)
                {
                    s_buddy.free_counts[frame_order]--;
                }
            }

            frame->state = PAGE_RESERVED;
            frame->ref_count = 0U;
            frame->order = 0U;

            s_buddy.free_pages--;
            s_stats.free_pages--;
            s_stats.reserved_pages++;
        }
    }

    ticket_lock_release(&s_buddy.lock);

    return KERNEL_OK;
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试 1: 初始化成功，统计正确
 */
static void test_init_basic(void)
{
    kernel_status_t ret;

    ret = phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(s_frame_count, TEST_MAX_PAGES);
    TEST_ASSERT_EQ(s_stats.total_pages, TEST_MAX_PAGES);
    TEST_ASSERT_EQ(s_stats.free_pages, TEST_MAX_PAGES);
    TEST_ASSERT_EQ(s_buddy.free_pages, TEST_MAX_PAGES);
    TEST_ASSERT_EQ(s_buddy.total_pages, TEST_MAX_PAGES);

    /* 16 页 = 1 个 order-4 块 */
    TEST_ASSERT_EQ(s_buddy.free_counts[4U], 1U);
}

/**
 * @brief 测试 2: 无效参数初始化失败
 */
static void test_init_invalid(void)
{
    kernel_status_t ret;

    /* 基地址为 0 */
    ret = phys_mem_test_init(0U, TEST_MEM_SIZE);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 基地址未页对齐 */
    ret = phys_mem_test_init((paddr_t)0x10000100ULL, TEST_MEM_SIZE);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 大小为 0 */
    ret = phys_mem_test_init(TEST_MEM_BASE, 0U);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 大小不是页大小的整数倍 */
    ret = phys_mem_test_init(TEST_MEM_BASE, 100U);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

/**
 * @brief 测试 3: 分配单页成功
 */
static void test_alloc_one_page(void)
{
    paddr_t addr;
    page_frame_t *frame;

    phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    addr = phys_mem_alloc_page();
    TEST_ASSERT_NE(addr, INVALID_PADDR);

    /* 地址应该页对齐 */
    TEST_ASSERT_EQ((addr % (paddr_t)CONFIG_PAGE_SIZE), (paddr_t)0U);

    /* 检查页帧状态 */
    frame = phys_mem_get_frame(addr);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQ(frame->state, PAGE_ALLOCATED);
    TEST_ASSERT_EQ(frame->ref_count, 1U);
    TEST_ASSERT_EQ(frame->order, 0U);

    /* 空闲页减少 1 */
    TEST_ASSERT_EQ(s_stats.free_pages, TEST_MAX_PAGES - 1U);
    TEST_ASSERT_EQ(s_stats.alloc_count, 1U);
}

/**
 * @brief 测试 4: 分配 order-2 块（4 页）
 */
static void test_alloc_order(void)
{
    paddr_t addr;
    page_frame_t *frame;

    phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    /* 分配 order-2 = 4 页 */
    addr = phys_mem_alloc_pages(2U);
    TEST_ASSERT_NE(addr, INVALID_PADDR);

    frame = phys_mem_get_frame(addr);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQ(frame->state, PAGE_ALLOCATED);
    TEST_ASSERT_EQ(frame->order, 2U);
    TEST_ASSERT_EQ(frame->ref_count, 1U);

    /* 空闲页减少 4 */
    TEST_ASSERT_EQ(s_stats.free_pages, TEST_MAX_PAGES - 4U);

    /* 剩余应有一个 order-2 块在空闲链表中 */
    TEST_ASSERT_TRUE(s_buddy.free_counts[2U] >= 1U);
}

/**
 * @brief 测试 5: 分配所有页后耗尽
 */
static void test_alloc_exhaust(void)
{
    paddr_t addrs[TEST_MAX_PAGES];
    paddr_t extra;
    uint32_t i;

    phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    /* 分配全部 16 个单页 */
    for (i = 0U; i < TEST_MAX_PAGES; i++)
    {
        addrs[i] = phys_mem_alloc_page();
        TEST_ASSERT_NE(addrs[i], INVALID_PADDR);
    }

    TEST_ASSERT_EQ(s_stats.free_pages, 0U);

    /* 再次分配应失败 */
    extra = phys_mem_alloc_page();
    TEST_ASSERT_EQ(extra, INVALID_PADDR);

    /* 清理 */
    for (i = 0U; i < TEST_MAX_PAGES; i++)
    {
        phys_mem_free_page(addrs[i]);
    }
}

/**
 * @brief 测试 6: 释放单页后状态恢复
 */
static void test_free_basic(void)
{
    paddr_t addr;
    page_frame_t *frame;

    phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    addr = phys_mem_alloc_page();
    TEST_ASSERT_EQ(s_stats.free_pages, TEST_MAX_PAGES - 1U);

    phys_mem_free_page(addr);

    /* 空闲页恢复 */
    TEST_ASSERT_EQ(s_stats.free_pages, TEST_MAX_PAGES);
    TEST_ASSERT_EQ(s_stats.free_count, 1U);

    /* 页帧状态恢复为空闲 */
    frame = phys_mem_get_frame(addr);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQ(frame->state, PAGE_FREE);
    TEST_ASSERT_EQ(frame->ref_count, 0U);
}

/**
 * @brief 测试 7: Buddy 合并 — 分配两个相邻页，释放后合并
 */
static void test_buddy_merge(void)
{
    paddr_t addr0;
    paddr_t addr1;

    phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    /* 分配两个单页（从 order-4 分裂而来） */
    addr0 = phys_mem_alloc_page();
    addr1 = phys_mem_alloc_page();
    TEST_ASSERT_NE(addr0, INVALID_PADDR);
    TEST_ASSERT_NE(addr1, INVALID_PADDR);

    /* 验证是不同页 */
    TEST_ASSERT_NE(addr0, addr1);

    /* 释放两个页 */
    phys_mem_free_page(addr0);
    phys_mem_free_page(addr1);

    /* 全部释放后，空闲页恢复到 16 */
    TEST_ASSERT_EQ(s_stats.free_pages, TEST_MAX_PAGES);

    /* 应该合并回 order-4 块 */
    TEST_ASSERT_EQ(s_buddy.free_counts[4U], 1U);
}

/**
 * @brief 测试 8: Buddy 合并 — 高阶分配释放后完全合并
 */
static void test_buddy_merge_high_order(void)
{
    paddr_t addr;

    phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    /* 分配 order-3（8 页） */
    addr = phys_mem_alloc_pages(3U);
    TEST_ASSERT_NE(addr, INVALID_PADDR);
    TEST_ASSERT_EQ(s_stats.free_pages, TEST_MAX_PAGES - 8U);

    /* 释放 */
    phys_mem_free_pages(addr, 3U);

    /* 应合并回 order-4 */
    TEST_ASSERT_EQ(s_stats.free_pages, TEST_MAX_PAGES);
    TEST_ASSERT_EQ(s_buddy.free_counts[4U], 1U);
}

/**
 * @brief 测试 9: 无效参数不崩溃
 */
static void test_free_invalid(void)
{
    phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    /* 释放地址 0 — 不崩溃 */
    phys_mem_free_page(INVALID_PADDR);

    /* 释放超出范围的地址 — 不崩溃 */
    phys_mem_free_page((paddr_t)0xFFFFFFFFFULL);

    /* 超大 order — 不崩溃 */
    phys_mem_free_pages(TEST_MEM_BASE, 20U);

    /* 分配超大 order — 返回 0 */
    TEST_ASSERT_EQ(phys_mem_alloc_pages(20U), INVALID_PADDR);

    TEST_ASSERT_TRUE(true);
}

/**
 * @brief 测试 10: 释放后可再次分配
 */
static void test_alloc_after_free(void)
{
    paddr_t addr1;
    paddr_t addr2;

    phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    /* 分配、释放、再分配 */
    addr1 = phys_mem_alloc_page();
    TEST_ASSERT_NE(addr1, INVALID_PADDR);

    phys_mem_free_page(addr1);

    addr2 = phys_mem_alloc_page();
    TEST_ASSERT_NE(addr2, INVALID_PADDR);

    /* 可能分配到相同地址 */
    phys_mem_free_page(addr2);
}

/**
 * @brief 测试 11: 保留页帧范围
 */
static void test_reserve_basic(void)
{
    kernel_status_t ret;
    paddr_t reserve_base;

    phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    /* 保留前 2 页 */
    reserve_base = TEST_MEM_BASE;
    ret = phys_mem_reserve(reserve_base, (uint64_t)CONFIG_PAGE_SIZE * 2U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    TEST_ASSERT_EQ(s_stats.reserved_pages, 2U);
    TEST_ASSERT_EQ(s_stats.free_pages, TEST_MAX_PAGES - 2U);
}

/**
 * @brief 测试 12: 保留的页不能被分配
 *
 * @details 使用 5 页内存（非 2 的幂），初始化后产生：
 *          - order-2 块（页 0-3），在 free_lists[2]
 *          - order-0 块（页 4），在 free_lists[0]
 *          保留页 0-3（整个 order-2 块）后：
 *          - order-2 块被移出空闲链表
 *          - 页 4（order-0）仍可分配
 *          再次分配应只能获得页 4
 */
static void test_reserve_not_allocatable(void)
{
    paddr_t addr;
    kernel_status_t ret;
    page_frame_t *frame;
    uint32_t i;

    /* 使用 5 页 = order-2（4页）+ order-0（1页） */
    uint32_t count = 5U;
    uint64_t size  = (uint64_t)count * (uint64_t)CONFIG_PAGE_SIZE;

    phys_mem_test_init(TEST_MEM_BASE, size);

    /* 验证初始状态：order-2 + order-0 */
    TEST_ASSERT_EQ(s_buddy.free_counts[2U], 1U);
    TEST_ASSERT_EQ(s_buddy.free_counts[0U], 1U);

    /* 保留前 4 页（整个 order-2 块） */
    ret = phys_mem_reserve(TEST_MEM_BASE, (uint64_t)CONFIG_PAGE_SIZE * 4U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(s_stats.reserved_pages, 4U);
    TEST_ASSERT_EQ(s_stats.free_pages, 1U);

    /* 验证保留页状态 */
    for (i = 0U; i < 4U; i++)
    {
        frame = phys_mem_get_frame(TEST_MEM_BASE +
                                   (paddr_t)(i * CONFIG_PAGE_SIZE));
        TEST_ASSERT_NOT_NULL(frame);
        TEST_ASSERT_EQ(frame->state, PAGE_RESERVED);
    }

    /* 页 4 仍可分配 */
    addr = phys_mem_alloc_page();
    TEST_ASSERT_EQ(addr, TEST_MEM_BASE + (paddr_t)(4U * CONFIG_PAGE_SIZE));

    /* 无更多空闲页 */
    addr = phys_mem_alloc_page();
    TEST_ASSERT_EQ(addr, INVALID_PADDR);
    TEST_ASSERT_EQ(s_stats.free_pages, 0U);
}

/**
 * @brief 测试 13: get_frame 有效地址返回正确描述符
 */
static void test_get_frame_valid(void)
{
    page_frame_t *frame;

    phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    /* 第一页 */
    frame = phys_mem_get_frame(TEST_MEM_BASE);
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQ(frame->phys_addr, TEST_MEM_BASE);
    TEST_ASSERT_EQ(frame->state, PAGE_FREE);

    /* 最后一页 */
    frame = phys_mem_get_frame(TEST_MEM_BASE + (paddr_t)(15U * CONFIG_PAGE_SIZE));
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQ(frame->phys_addr, TEST_MEM_BASE + (paddr_t)(15U * CONFIG_PAGE_SIZE));
}

/**
 * @brief 测试 14: get_frame 无效地址返回 NULL
 */
static void test_get_frame_invalid(void)
{
    page_frame_t *frame;

    phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    /* 低于基地址 */
    frame = phys_mem_get_frame(TEST_MEM_BASE - (paddr_t)CONFIG_PAGE_SIZE);
    TEST_ASSERT_NULL(frame);

    /* 超出范围 */
    frame = phys_mem_get_frame(TEST_MEM_BASE + (paddr_t)(TEST_MAX_PAGES * CONFIG_PAGE_SIZE));
    TEST_ASSERT_NULL(frame);

    /* 未页对齐 */
    frame = phys_mem_get_frame(TEST_MEM_BASE + 1U);
    TEST_ASSERT_NULL(frame);

    /* NULL stats */
    phys_mem_get_stats(NULL);
    /* 不崩溃 */
}

/**
 * @brief 测试 15: 统计信息正确
 */
static void test_get_stats(void)
{
    phys_mem_stats_t stats;
    paddr_t addr;

    phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    phys_mem_get_stats(&stats);

    TEST_ASSERT_EQ(stats.total_pages, TEST_MAX_PAGES);
    TEST_ASSERT_EQ(stats.free_pages, TEST_MAX_PAGES);
    TEST_ASSERT_EQ(stats.alloc_count, 0U);
    TEST_ASSERT_EQ(stats.free_count, 0U);

    /* 分配一页 */
    addr = phys_mem_alloc_page();

    phys_mem_get_stats(&stats);
    TEST_ASSERT_EQ(stats.free_pages, TEST_MAX_PAGES - 1U);
    TEST_ASSERT_EQ(stats.alloc_count, 1U);
    TEST_ASSERT_EQ(stats.free_count, 0U);

    /* 释放 */
    phys_mem_free_page(addr);

    phys_mem_get_stats(&stats);
    TEST_ASSERT_EQ(stats.free_pages, TEST_MAX_PAGES);
    TEST_ASSERT_EQ(stats.free_count, 1U);
}

/**
 * @brief 测试 16: reserve 无效参数
 */
static void test_reserve_invalid(void)
{
    kernel_status_t ret;

    phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    /* 大小为 0 */
    ret = phys_mem_reserve(TEST_MEM_BASE, 0U);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 地址未页对齐 */
    ret = phys_mem_reserve((paddr_t)0x10000100ULL, (uint64_t)CONFIG_PAGE_SIZE);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 地址低于基地址 */
    ret = phys_mem_reserve(TEST_MEM_BASE - (paddr_t)CONFIG_PAGE_SIZE,
                           (uint64_t)CONFIG_PAGE_SIZE);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

/**
 * @brief 测试 17: 压力测试 — 循环 500 次分配释放
 */
static void test_stress_alloc_free(void)
{
    paddr_t addrs[TEST_MAX_PAGES];
    uint32_t iter;
    uint32_t i;

    phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    for (iter = 0U; iter < 500U; iter++)
    {
        /* 分配全部 */
        for (i = 0U; i < TEST_MAX_PAGES; i++)
        {
            addrs[i] = phys_mem_alloc_page();
            TEST_ASSERT_NE(addrs[i], INVALID_PADDR);
        }
        TEST_ASSERT_EQ(s_stats.free_pages, 0U);

        /* 释放全部 */
        for (i = 0U; i < TEST_MAX_PAGES; i++)
        {
            phys_mem_free_page(addrs[i]);
        }
        TEST_ASSERT_EQ(s_stats.free_pages, TEST_MAX_PAGES);
    }
}

/**
 * @brief 测试 18: 多阶交替分配释放
 */
static void test_mixed_orders(void)
{
    paddr_t a0;
    paddr_t a1;
    paddr_t a2;

    phys_mem_test_init(TEST_MEM_BASE, TEST_MEM_SIZE);

    /* 分配 order-0 (1页) + order-1 (2页) + order-2 (4页) = 7 页 */
    a0 = phys_mem_alloc_pages(0U);
    a1 = phys_mem_alloc_pages(1U);
    a2 = phys_mem_alloc_pages(2U);

    TEST_ASSERT_NE(a0, INVALID_PADDR);
    TEST_ASSERT_NE(a1, INVALID_PADDR);
    TEST_ASSERT_NE(a2, INVALID_PADDR);
    TEST_ASSERT_EQ(s_stats.free_pages, TEST_MAX_PAGES - 7U);

    /* 释放 */
    phys_mem_free_pages(a0, 0U);
    phys_mem_free_pages(a1, 1U);
    phys_mem_free_pages(a2, 2U);

    TEST_ASSERT_EQ(s_stats.free_pages, TEST_MAX_PAGES);
    TEST_ASSERT_EQ(s_buddy.free_counts[4U], 1U);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("=== 物理内存 Buddy 分配器测试 ===\n\n");

    test_init_basic();
    test_init_invalid();
    test_alloc_one_page();
    test_alloc_order();
    test_alloc_exhaust();
    test_free_basic();
    test_buddy_merge();
    test_buddy_merge_high_order();
    test_free_invalid();
    test_alloc_after_free();
    test_reserve_basic();
    test_reserve_not_allocatable();
    test_get_frame_valid();
    test_get_frame_invalid();
    test_get_stats();
    test_reserve_invalid();
    test_stress_alloc_free();
    test_mixed_orders();

    TEST_SUMMARY("test_phys_mem");

    return TEST_RESULT();
}
