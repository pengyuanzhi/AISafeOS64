/**
 * @file    kernel/mm/slab.c
 * @brief   Slab 分配器实现（内核版）
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @details Slab 分配器实现：
 *          - 对象分配/释放
 *          - 内存池管理
 *          - 缓存优化
 *          - SLAB 链表管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.3 - 内存管理优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/mm/slab.h>
#include <kernel/phys_mem.h>
#include <kernel/virt_phys.h>
#include <kernel/config.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/errno.h>
#include <kernel/string.h>

/* ========================================================================
 * Slab 分配器配置
 * ======================================================================== */

#define SLAB_MIN_ORDER        3       /* 最小分配顺序 */
#define SLAB_MAX_OBJ          8       /* 每个 Slab 最大对象数 */
#define SLAB_MAX_ORDER        10      /* 最大分配顺序 */
#define SLAB_CACHE_COUNT      64      /* 缓存数量 */

/* ========================================================================
 * Slab 节点结构
 * ======================================================================== */

/**
 * @brief Slab 节点
 *
 * @note node_paddr / node_order 记录该节点页的物理地址与 buddy 阶数，
 *       供 slab_destroy 通过 phys_mem_free_pages 归还给物理内存分配器。
 *       层级：本节点页由 phys_mem（buddy）分配，不再经由 kmalloc。
 */
typedef struct slab_node
{
    struct slab_node *next;  /**< @brief 下一个 Slab 节点 */
    paddr_t node_paddr;      /**< @brief 本节点页物理地址（释放用） */
    uint32_t node_order;     /**< @brief 本节点页 buddy 阶数（释放用） */
    uint8_t used[SLAB_MAX_OBJ];  /**< @brief 对象使用标记 */
    uint8_t objects[SLAB_MAX_OBJ][SLAB_OBJECT_SIZE];  /**< @brief 对象数据 */
} slab_node_t;

/* ========================================================================
 * Slab 缓存结构
 * ======================================================================== */

/**
 * @brief Slab 缓存
 */
/* ========================================================================
 * 全局 Slab 缓存
 * ======================================================================== */

static slab_cache_t s_slab_caches[SLAB_CACHE_COUNT];
static uint32_t s_cache_count = 0U;

/* ========================================================================
 * Slab 分配器接口实现
 * ======================================================================== */

/**
 * @brief 计算容纳 size 字节所需的最小 buddy 阶数
 *
 * @details 阶数 order 满足 CONFIG_PAGE_SIZE << order >= size。
 *          size 为 0 时返回 0；超出 MAX_ORDER 范围返回 MAX_ORDER+1
 *          （调用方据此判断分配不可行）。
 *
 * @param size 需要的字节数
 *
 * @return buddy 阶数
 */
static uint32_t slab_size_to_order(size_t size)
{
    uint32_t order = 0U;
    size_t capacity = (size_t)CONFIG_PAGE_SIZE;

    while (capacity < size)
    {
        capacity <<= 1U;
        order++;
    }

    return order;
}

/**
 * @brief 从物理内存分配器取页并返回线性映射虚拟地址
 *
 * @details 层级辅助：slab 直接向 phys_mem（buddy）申请页，不经过 kmalloc，
 *          避免层级倒置与循环依赖。
 *
 * @param size   需要的字节数
 * @param order  输出：实际分配的 buddy 阶数
 * @param paddr  输出：实际分配的物理地址（释放时回传）
 *
 * @return 可访问的内核虚拟地址指针，失败返回 NULL
 */
static void *slab_alloc_phys(size_t size, uint32_t *order, paddr_t *paddr)
{
    paddr_t pa;
    uint32_t ord;

    ord = slab_size_to_order(size);
    if (ord > MAX_ORDER)
    {
        return NULL;
    }

    pa = phys_mem_alloc_pages(ord);
    if (pa == (paddr_t)0U)
    {
        return NULL;
    }

    *order = ord;
    *paddr = pa;

    return phys_to_virt(pa);
}

/**
 * @brief 创建 Slab 分配器
 *
 * @details 内存池直接从物理内存分配器（buddy）按页获取，不再经由 kmalloc，
 *          恢复 phys_mem → slab → kmalloc 的标准三层结构。
 */
int32_t slab_create(slab_cache_t *cache, size_t pool_size)
{
    void *pool;
    uint32_t order;
    paddr_t paddr;

    if (cache == NULL || pool_size == 0)
    {
        return -(int32_t)EINVAL;
    }

    /* 初始化 TicketLock */
    ticket_lock_init(&cache->lock);

    /* 从物理内存分配器直接分配页（不经过 kmalloc，避免层级倒置） */
    pool = slab_alloc_phys(pool_size, &order, &paddr);
    if (pool == NULL)
    {
        return -(int32_t)ENOMEM;
    }

    (void)memset(pool, 0, pool_size);
    cache->pool = pool;
    cache->pool_size = pool_size;
    cache->pool_paddr = paddr;
    cache->pool_order = order;
    cache->slabs = NULL;
    cache->num_slabs = 0;
    cache->alloc_count = 0;

    return KERNEL_OK;
}

/**
 * @brief 销毁 Slab 分配器
 *
 * @details 将 slab_create 取得的内存池页和 slab_alloc 取得的节点页
 *          归还给物理内存分配器（buddy），与分配路径对称。
 */
int32_t slab_destroy(slab_cache_t *cache)
{
    slab_node_t *current;

    if (cache == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 销毁所有 Slab 节点（每节点页归还 phys_mem） */
    current = (slab_node_t *)cache->slabs;
    while (current != NULL)
    {
        slab_node_t *next = current->next;
        phys_mem_free_pages(current->node_paddr, current->node_order);
        current = next;
    }

    /* 释放内存池（归还 phys_mem） */
    if ((cache->pool != NULL) && (cache->pool_paddr != (paddr_t)0U))
    {
        phys_mem_free_pages(cache->pool_paddr, cache->pool_order);
    }

    (void)memset(cache, 0, sizeof(slab_cache_t));

    return KERNEL_OK;
}

/**
 * @brief 分配对象
 */
void* slab_alloc(slab_cache_t *cache)
{
    slab_node_t *current;
    slab_node_t *node;
    slab_node_t *last;
    uint32_t irq_state;

    if (cache == NULL)
    {
        return NULL;
    }

    /* 持锁扫描已有 slab 节点，查找空闲对象。
     * 使用 irqsave 版本：slab 分配可能在中断/系统调用路径被调用。 */
    irq_state = ticket_lock_acquire_irqsave(&cache->lock);

    current = (slab_node_t *)cache->slabs;
    while (current != NULL)
    {
        for (uint32_t i = 0U; i < SLAB_MAX_OBJ; i++)
        {
            if (current->used[i] == 0)
            {
                current->used[i] = 1;
                cache->alloc_count++;
                ticket_lock_release_irqrestore(&cache->lock, irq_state);
                return current->objects[i];
            }
        }
        current = current->next;
    }

    /* 所有节点已满，需要分配新节点。
     * 关键：禁止在持 cache->lock 时分配（phys_mem 内部另持 buddy 锁，
     * 持锁分配可能死锁）。因此先释放 cache->lock，再向 phys_mem 取页。
     * 层级：slab 节点页直接来自 phys_mem（buddy），不经由 kmalloc。 */
    ticket_lock_release_irqrestore(&cache->lock, irq_state);

    {
        paddr_t node_pa;
        uint32_t node_order;
        void *node_va = slab_alloc_phys(sizeof(slab_node_t), &node_order, &node_pa);
        if (node_va == NULL)
        {
            return NULL;
        }
        node = (slab_node_t *)node_va;
        node->node_paddr = node_pa;
        node->node_order = node_order;
    }

    (void)memset(node->used, 0, sizeof(node->used));
    node->next = NULL;

    /* 重新获取锁后挂入链表。
     * 因释放过锁，期间其它 CPU 可能已分配新节点并腾出空闲槽，
     * 故挂入后再次扫描一遍：优先复用已有空闲槽，避免无谓扩张。 */
    irq_state = ticket_lock_acquire_irqsave(&cache->lock);

    current = (slab_node_t *)cache->slabs;
    while (current != NULL)
    {
        for (uint32_t i = 0U; i < SLAB_MAX_OBJ; i++)
        {
            if (current->used[i] == 0)
            {
                current->used[i] = 1;
                cache->alloc_count++;
                ticket_lock_release_irqrestore(&cache->lock, irq_state);

                /* 不再需要预分配的节点，归还 phys_mem */
                phys_mem_free_pages(node->node_paddr, node->node_order);
                return current->objects[i];
            }
        }
        current = current->next;
    }

    /* 仍无空闲槽，把新节点挂到链表尾部 */
    if (cache->slabs == NULL)
    {
        cache->slabs = node;
    }
    else
    {
        last = (slab_node_t *)cache->slabs;
        while (last->next != NULL)
        {
            last = last->next;
        }
        last->next = node;
    }

    cache->num_slabs++;
    node->used[0] = 1;
    cache->alloc_count++;

    ticket_lock_release_irqrestore(&cache->lock, irq_state);

    return node->objects[0];
}

/**
 * @brief 释放对象
 */
int32_t slab_free(slab_cache_t *cache, void *ptr)
{
    slab_node_t *current;
    uint32_t irq_state;

    if (cache == NULL || ptr == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* irqsave 版本：与 slab_alloc 保持一致，允许在中断路径调用 */
    irq_state = ticket_lock_acquire_irqsave(&cache->lock);

    /* 查找对象并释放 */
    current = (slab_node_t *)cache->slabs;
    while (current != NULL)
    {
        for (uint32_t i = 0U; i < SLAB_MAX_OBJ; i++)
        {
            if (current->used[i] && current->objects[i] == ptr)
            {
                current->used[i] = 0;
                cache->alloc_count--;
                ticket_lock_release_irqrestore(&cache->lock, irq_state);
                return KERNEL_OK;
            }
        }
        current = current->next;
    }

    ticket_lock_release_irqrestore(&cache->lock, irq_state);

    return -ENOSYS; /* 对象未找到 */
}

/**
 * @brief 获取已分配对象数
 */
size_t slab_get_alloc_count(slab_cache_t *cache)
{
    size_t count;
    uint32_t irq_state;

    if (cache == NULL)
    {
        return 0;
    }

    irq_state = ticket_lock_acquire_irqsave(&cache->lock);
    count = cache->alloc_count;
    ticket_lock_release_irqrestore(&cache->lock, irq_state);

    return count;
}

/* ========================================================================
 * Slab 分配器管理接口
 * ======================================================================== */

/**
 * @brief 初始化 Slab 分配器系统
 */
int32_t slab_system_init(void)
{
    int32_t ret;
    uint32_t i;

    (void)memset(s_slab_caches, 0, sizeof(s_slab_caches));
    s_cache_count = 0U;

    /* 创建多个缓存 */
    for (i = 0U; i < SLAB_CACHE_COUNT; i++)
    {
        /* 每个缓存 64KB 内存池 */
        ret = slab_create(&s_slab_caches[i], 65536);
        if (ret != KERNEL_OK)
        {
            return ret;
        }
        s_cache_count++;
    }

    return KERNEL_OK;
}

/**
 * @brief 关闭 Slab 分配器系统
 */
int32_t slab_system_shutdown(void)
{
    int32_t ret;
    uint32_t i;

    for (i = 0U; i < s_cache_count; i++)
    {
        ret = slab_destroy(&s_slab_caches[i]);
        if (ret != KERNEL_OK)
        {
            return ret;
        }
    }

    s_cache_count = 0U;

    return KERNEL_OK;
}
