/**
 * @file    fs_inode_cache.c
 * @brief   Inode 缓存管理实现
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details Inode 缓存管理模块实现
 *          - LRU 淘汰策略
 *          - 缓存命中率统计
 *          - 访问计数
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fs_inode_cache.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 获取当前时间戳（纳秒）
 */
static uint64_t get_timestamp_ns(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * @brief 查找 LRU 条目（最久未使用）
 */
static inode_cache_entry_t *find_lru_entry(inode_cache_t *cache)
{
    uint32_t i;
    uint32_t lru_index = 0;
    uint64_t min_last_access = get_timestamp_ns();

    if (cache == NULL)
    {
        return NULL;
    }

    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state == FS_CACHE_VALID)
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
 * @brief 初始化 inode 缓存管理器
 */
int32_t inode_cache_init(inode_cache_t *cache)
{
    uint32_t i;

    if (cache == NULL)
    {
        return -1;
    }

    (void)memset(cache, 0, sizeof(inode_cache_t));

    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        cache->entries[i].state = FS_CACHE_INVALID;
    }

    cache->initialized = true;

    return 0;
}

/* ========================================================================
 * 缓存操作
 * ======================================================================== */

/**
 * @brief 从缓存获取 inode
 */
inode_cache_entry_t *inode_cache_get(inode_cache_t *cache, uint32_t ino,
                                     const fs_inode_t *fallback)
{
    uint32_t i;

    if ((cache == NULL) || (ino == 0U))
    {
        return NULL;
    }

    /* 查找缓存 */
    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state == FS_CACHE_VALID &&
            cache->entries[i].ino == ino)
        {
            /* 缓存命中 */
            cache->entries[i].last_access = get_timestamp_ns();
            cache->entries[i].access_count++;
            cache->hit_count++;

            return &cache->entries[i];
        }
    }

    /* 缓存未命中 */
    if (fallback != NULL)
    {
        cache->miss_count++;
        return NULL; /* 调用者应使用 fallback */
    }

    return NULL;
}

/**
 * @brief 将 inode 放入缓存
 */
int32_t inode_cache_put(inode_cache_t *cache, const fs_inode_t *inode)
{
    uint32_t i;
    inode_cache_entry_t *lru_entry;

    if ((cache == NULL) || (inode == NULL))
    {
        return -1;
    }

    /* 检查是否已存在 */
    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state == FS_CACHE_VALID &&
            cache->entries[i].ino == inode->ino)
        {
            /* 更新现有条目 */
            cache->entries[i].inode = *inode;
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

    /* 淘汰 LRU 条目 */
    if (lru_entry->state == FS_CACHE_VALID)
    {
        cache->evict_count++;
    }

    /* 插入新条目 */
    (void)memcpy(&lru_entry->inode, inode, sizeof(fs_inode_t));
    lru_entry->ino = inode->ino;
    lru_entry->state = FS_CACHE_VALID;
    lru_entry->last_access = get_timestamp_ns();
    lru_entry->access_count = 1;

    return 0;
}

/**
 * @brief 使指定 inode 的缓存失效
 */
int32_t inode_cache_invalidate(inode_cache_t *cache, uint32_t ino)
{
    uint32_t i;

    if ((cache == NULL) || (ino == 0U))
    {
        return -1;
    }

    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state == FS_CACHE_VALID &&
            cache->entries[i].ino == ino)
        {
            cache->entries[i].state = FS_CACHE_INVALID;
            return 0;
        }
    }

    return -1;
}

/**
 * @brief 刷新所有缓存
 */
void inode_cache_flush(inode_cache_t *cache)
{
    uint32_t i;

    if (cache == NULL)
    {
        return;
    }

    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        cache->entries[i].state = FS_CACHE_INVALID;
    }

    /* 重置统计 */
    cache->hit_count = 0U;
    cache->miss_count = 0U;
    cache->evict_count = 0U;
}

/**
 * @brief 清空缓存（保留统计）
 */
void inode_cache_clear(inode_cache_t *cache)
{
    uint32_t i;

    if (cache == NULL)
    {
        return;
    }

    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        cache->entries[i].state = FS_CACHE_INVALID;
    }
}

/* ========================================================================
 * 统计和报告
 * ======================================================================== */

/**
 * @brief 获取缓存统计信息
 */
int32_t inode_cache_stats(const inode_cache_t *cache)
{
    if (cache == NULL)
    {
        return -1;
    }

    printf("Inode Cache Statistics:\n");
    printf("  Cache Size:      %u entries\n", FS_INODE_CACHE_SIZE);
    printf("  Initialized:     %s\n", cache->initialized ? "Yes" : "No");
    printf("  Hit Count:       %u\n", cache->hit_count);
    printf("  Miss Count:      %u\n", cache->miss_count);
    printf("  Evict Count:     %u\n", cache->evict_count);
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
int32_t inode_cache_report(const inode_cache_t *cache)
{
    uint32_t i;
    uint32_t valid_count;

    if (cache == NULL)
    {
        return -1;
    }

    printf("\n=== Inode Cache Report ===\n");
    inode_cache_stats(cache);

    valid_count = 0U;
    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state == FS_CACHE_VALID)
        {
            valid_count++;
        }
    }

    printf("\nCache Contents (%u valid entries):\n", valid_count);
    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state == FS_CACHE_VALID)
        {
            printf("  [%2u] ino=%u, access=%u\n", i,
                   cache->entries[i].ino,
                   cache->entries[i].access_count);
        }
    }

    printf("\n=== End Report ===\n\n");

    return 0;
}
