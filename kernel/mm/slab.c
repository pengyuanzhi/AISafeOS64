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
#include <kernel/mm/kmalloc.h>
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
 */
typedef struct slab_node
{
    struct slab_node *next;  /**< @brief 下一个 Slab 节点 */
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
 * @brief 创建 Slab 分配器
 */
int32_t slab_create(slab_cache_t *cache, size_t pool_size)
{
    int32_t ret;

    if (cache == NULL || pool_size == 0)
    {
        return -EINVAL;
    }

    /* 初始化 TicketLock */
    ticket_lock_init(&cache->lock);

    /* 分配内存池 */
    cache->pool = kmalloc(pool_size);
    if (cache->pool == NULL)
    {
        return -ENOMEM;
    }

    (void)memset(cache->pool, 0, pool_size);
    cache->pool_size = pool_size;
    cache->slabs = NULL;
    cache->num_slabs = 0;
    cache->alloc_count = 0;

    return KERNEL_OK;
}

/**
 * @brief 销毁 Slab 分配器
 */
int32_t slab_destroy(slab_cache_t *cache)
{
    if (cache == NULL)
    {
        return -EINVAL;
    }

    /* 销毁所有 Slab 节点 */
    slab_node_t *current = cache->slabs;
    while (current != NULL)
    {
        slab_node_t *next = current->next;
        kfree(current);
        current = next;
    }

    /* 释放内存池 */
    if (cache->pool != NULL)
    {
        kfree(cache->pool);
        cache->pool = NULL;
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

    current = cache->slabs;
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
     * 关键：禁止在持 cache->lock 时调用 kmalloc（kmalloc 内部另持全局锁，
     * 持锁分配会违反"持锁禁止 kmalloc"且可能死锁）。
     * 因此先释放 cache->lock，再 kmalloc。 */
    ticket_lock_release_irqrestore(&cache->lock, irq_state);

    node = (slab_node_t *)kmalloc(sizeof(slab_node_t));
    if (node == NULL)
    {
        return NULL;
    }

    (void)memset(node, 0, sizeof(slab_node_t));

    /* 重新获取锁后挂入链表。
     * 因释放过锁，期间其它 CPU 可能已分配新节点并腾出空闲槽，
     * 故挂入后再次扫描一遍：优先复用已有空闲槽，避免无谓扩张。 */
    irq_state = ticket_lock_acquire_irqsave(&cache->lock);

    current = cache->slabs;
    while (current != NULL)
    {
        for (uint32_t i = 0U; i < SLAB_MAX_OBJ; i++)
        {
            if (current->used[i] == 0)
            {
                current->used[i] = 1;
                cache->alloc_count++;
                ticket_lock_release_irqrestore(&cache->lock, irq_state);

                /* 不再需要预分配的节点，释放 */
                kfree(node);
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
        last = cache->slabs;
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
        return -EINVAL;
    }

    /* irqsave 版本：与 slab_alloc 保持一致，允许在中断路径调用 */
    irq_state = ticket_lock_acquire_irqsave(&cache->lock);

    /* 查找对象并释放 */
    current = cache->slabs;
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
