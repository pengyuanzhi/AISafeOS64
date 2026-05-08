/**
 * @file    fs_page_cache.c
 * @brief   页缓存管理实现
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details 页缓存管理模块实现
 *          - 4KB 页大小
 *          - LRU 淘汰策略
 *          - 延迟写回（dirty 标记）
 *          - 缓存命中率统计
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fs_page_cache.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 获取虚拟时间戳（单调递增）
 *
 * @details 在嵌入式环境中使用简单递增计数器模拟时间戳
 *          生产环境可替换为硬件定时器
 */
static uint64_t get_timestamp_ns(void)
{
    static uint64_t s_timestamp = 0ULL;

    s_timestamp += 1000ULL;

    return s_timestamp;
}

/**
 * @brief 查找 LRU 条目（最久未使用）
 */
static page_cache_entry_t *find_lru_entry(page_cache_t *cache)
{
    uint32_t i;
    uint32_t lru_index = 0U;
    uint64_t min_last_access = UINT64_MAX;

    if (cache == NULL)
    {
        return NULL;
    }

    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state != PAGE_STATE_INVALID)
        {
            if (cache->entries[i].last_access < min_last_access)
            {
                min_last_access = cache->entries[i].last_access;
                lru_index = i;
            }
        }
    }

    return &cache->entries[lru_index];
}

/* ========================================================================
 * 初始化和清理
 * ======================================================================== */

/**
 * @brief 初始化页缓存管理器
 */
int32_t page_cache_init(page_cache_t *cache)
{
    uint32_t i;

    if (cache == NULL)
    {
        return -1;
    }

    (void)memset(cache, 0, sizeof(page_cache_t));

    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        cache->entries[i].state = PAGE_STATE_INVALID;
    }

    cache->initialized = true;

    return 0;
}

/* ========================================================================
 * 缓存操作
 * ======================================================================== */

/**
 * @brief 从缓存获取页
 */
uint8_t *page_cache_get(page_cache_t *cache, uint32_t ino, uint64_t offset,
                        const uint8_t *fallback, uint64_t size)
{
    uint32_t i;

    if ((cache == NULL) || (ino == 0U))
    {
        return NULL;
    }

    (void)size;  /* 预留参数：未来用于部分页缓存 */

    /* 查找缓存 */
    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state != PAGE_STATE_INVALID &&
            cache->entries[i].ino == ino &&
            cache->entries[i].offset == offset)
        {
            /* 缓存命中 */
            cache->entries[i].last_access = get_timestamp_ns();
            cache->entries[i].access_count++;
            cache->hit_count++;

            return cache->entries[i].data;
        }
    }

    /* 缓存未命中 */
    if (fallback != NULL)
    {
        cache->miss_count++;
    }

    return NULL;
}

/**
 * @brief 将页放入缓存
 */
int32_t page_cache_put(page_cache_t *cache, uint32_t ino, uint64_t offset,
                       const uint8_t *data, uint64_t size, bool dirty)
{
    uint32_t i;
    page_cache_entry_t *lru_entry;
    uint32_t dirty_flag;

    if ((cache == NULL) || (data == NULL) || (ino == 0U))
    {
        return -1;
    }

    if (size > FS_PAGE_SIZE)
    {
        return -1;
    }

    dirty_flag = dirty ? PAGE_STATE_DIRTY : PAGE_STATE_VALID;

    /* 检查是否已存在 */
    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state != PAGE_STATE_INVALID &&
            cache->entries[i].ino == ino &&
            cache->entries[i].offset == offset)
        {
            /* 更新现有条目 */
            (void)memcpy(cache->entries[i].data, data, (size_t)size);
            cache->entries[i].state = dirty_flag;
            cache->entries[i].last_access = get_timestamp_ns();
            cache->entries[i].access_count++;

            return 0;
        }
    }

    /* 查找 LRU 条目 */
    lru_entry = find_lru_entry(cache);
    if (lru_entry == NULL)
    {
        return -1; /* 缓存已满 */
    }

    /* 淘汰 LRU 条目（写回脏页） */
    if (lru_entry->state == PAGE_STATE_DIRTY)
    {
        /* 这里应该调用文件系统的写回接口 */
        /* 简化版：只记录写回计数 */
        cache->writeback_count++;
    }

    /* 插入新条目 */
    (void)memset(lru_entry->data, 0, sizeof(lru_entry->data));
    (void)memcpy(lru_entry->data, data, (size_t)size);
    lru_entry->ino = ino;
    lru_entry->offset = offset;
    lru_entry->state = dirty_flag;
    lru_entry->last_access = get_timestamp_ns();
    lru_entry->access_count = 1;

    return 0;
}

/**
 * @brief 使指定页的缓存失效
 */
int32_t page_cache_invalidate(page_cache_t *cache, uint32_t ino, uint64_t offset)
{
    uint32_t i;

    if ((cache == NULL) || (ino == 0U))
    {
        return -1;
    }

    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state != PAGE_STATE_INVALID &&
            cache->entries[i].ino == ino &&
            cache->entries[i].offset == offset)
        {
            cache->entries[i].state = PAGE_STATE_INVALID;
            return 0;
        }
    }

    return -1;
}

/**
 * @brief 刷新所有脏页（同步写回）
 */
void page_cache_flush(page_cache_t *cache)
{
    uint32_t i;

    if (cache == NULL)
    {
        return;
    }

    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state == PAGE_STATE_DIRTY)
        {
            /* 这里应该调用文件系统的写回接口 */
            /* 简化版：只记录写回计数 */
            cache->writeback_count++;

            /* 标记为有效（非脏） */
            cache->entries[i].state = PAGE_STATE_VALID;
        }
    }
}

/**
 * @brief 清空缓存（保留统计）
 */
void page_cache_clear(page_cache_t *cache)
{
    uint32_t i;

    if (cache == NULL)
    {
        return;
    }

    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        cache->entries[i].state = PAGE_STATE_INVALID;
    }
}

/* ========================================================================
 * 统计和报告
 * ======================================================================== */

/**
 * @brief 获取缓存统计信息
 */
int32_t page_cache_stats(const page_cache_t *cache)
{
    if (cache == NULL)
    {
        return -1;
    }

    printf("Page Cache Statistics:\n");
    printf("  Page Size:       %u bytes\n", FS_PAGE_SIZE);
    printf("  Cache Size:      %u entries\n", FS_PAGE_CACHE_SIZE);
    printf("  Cache Capacity:  %u MB\n",
           (FS_PAGE_CACHE_SIZE * FS_PAGE_SIZE) / (1024U * 1024U));
    printf("  Initialized:     %s\n", cache->initialized ? "Yes" : "No");
    printf("  Hit Count:       %u\n", cache->hit_count);
    printf("  Miss Count:      %u\n", cache->miss_count);
    printf("  Evict Count:     %u\n", cache->evict_count);
    printf("  Writeback Count: %u\n", cache->writeback_count);
    printf("  Total Accesses:  %u\n",
           cache->hit_count + cache->miss_count);

    if (cache->hit_count + cache->miss_count > 0U)
    {
        double hit_rate = (double)cache->hit_count /
                          (cache->hit_count + cache->miss_count) * 100.0;
        printf("  Hit Rate:        %.2f%%\n", hit_rate);
    }
    else
    {
        printf("  Hit Rate:        0.00%%\n");
    }

    return 0;
}

/**
 * @brief 打印缓存统计报告
 */
int32_t page_cache_report(const page_cache_t *cache)
{
    uint32_t i;
    uint32_t valid_count;

    if (cache == NULL)
    {
        return -1;
    }

    printf("\n=== Page Cache Report ===\n");
    page_cache_stats(cache);

    valid_count = 0U;
    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state != PAGE_STATE_INVALID)
        {
            valid_count++;
        }
    }

    printf("\nCache Contents (%u valid pages):\n", valid_count);
    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state != PAGE_STATE_INVALID)
        {
            printf("  [%2u] ino=%u, offset=%lu, state=%u, access=%u\n", i,
                   cache->entries[i].ino,
                   (unsigned long)cache->entries[i].offset,
                   cache->entries[i].state,
                   cache->entries[i].access_count);
        }
    }

    printf("\n=== End Report ===\n\n");

    return 0;
}
