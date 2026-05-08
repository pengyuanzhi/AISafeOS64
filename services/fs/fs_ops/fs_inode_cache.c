/**
 * @file    fs_inode_cache.c
 * @brief   inode LRU 缓存实现
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details inode LRU 缓存实现：
 *          - 基于 last_access 时间戳的 LRU 淘汰
 *          - 线性查找（适用于 32 条目的小缓存）
 *          - 缓存命中/未命中/淘汰统计
 *          - 单调递增时钟避免时间戳溢出
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fs_inode_cache.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 推进单调时钟
 *
 * @param cache 缓存管理器指针
 *
 * @return 当前时钟值
 */
static uint64_t cache_tick(inode_cache_t *cache)
{
    if (cache->clock < FS_CACHE_INVALID_TIME)
    {
        cache->clock++;
    }
    else
    {
        /* 时钟即将溢出，重新压缩 */
        uint64_t min_time = FS_CACHE_INVALID_TIME;
        uint32_t i;

        for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
        {
            if ((cache->entries[i].state == FS_CACHE_VALID) &&
                (cache->entries[i].last_access < min_time))
            {
                min_time = cache->entries[i].last_access;
            }
        }

        /* 将所有时间戳减去最小值，保持相对顺序 */
        if (min_time > 0U)
        {
            for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
            {
                if (cache->entries[i].state == FS_CACHE_VALID)
                {
                    cache->entries[i].last_access -= min_time;
                }
            }
        }

        cache->clock = (min_time == FS_CACHE_INVALID_TIME) ? 1U :
                        (cache->clock - min_time + 1U);
    }

    return cache->clock;
}

/**
 * @brief 根据 inode 编号查找缓存条目索引
 *
 * @param cache 缓存管理器指针
 * @param ino   inode 编号
 *
 * @return 条目索引，未找到返回 FS_INODE_CACHE_SIZE
 */
static uint32_t cache_find_index(const inode_cache_t *cache, uint32_t ino)
{
    uint32_t i;

    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        if ((cache->entries[i].state == FS_CACHE_VALID) &&
            (cache->entries[i].ino == ino))
        {
            return i;
        }
    }

    return FS_INODE_CACHE_SIZE;
}

/**
 * @brief 查找 LRU 条目（最久未使用）
 *
 * @details 遍历所有有效条目，找到 last_access 最小的。
 *          优先返回无效条目（空闲槽位）。
 *
 * @param cache 缓存管理器指针
 *
 * @return LRU 条目索引
 */
static uint32_t cache_find_lru(const inode_cache_t *cache)
{
    uint32_t i;
    uint32_t lru_index = 0U;
    uint64_t oldest_time = FS_CACHE_INVALID_TIME;
    bool found_invalid = false;

    /* 优先查找空闲槽位 */
    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state != FS_CACHE_VALID)
        {
            lru_index = i;
            found_invalid = true;
            break;
        }
    }

    /* 所有槽位都有效，找最久未使用的 */
    if (!found_invalid)
    {
        for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
        {
            if (cache->entries[i].last_access < oldest_time)
            {
                oldest_time = cache->entries[i].last_access;
                lru_index = i;
            }
        }
    }

    return lru_index;
}

/* ========================================================================
 * 公共接口实现
 * ======================================================================== */

/**
 * @brief 初始化 inode 缓存
 */
void inode_cache_init(inode_cache_t *cache)
{
    uint32_t i;

    if (cache == NULL)
    {
        return;
    }

    /* 清零所有条目 */
    (void)memset(cache, 0, sizeof(inode_cache_t));

    /* 显式初始化每个条目 */
    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        cache->entries[i].state = FS_CACHE_INVALID;
        cache->entries[i].ino = 0U;
        cache->entries[i].last_access = FS_CACHE_INVALID_TIME;
        cache->entries[i].access_count = 0U;
    }

    /* 初始化统计 */
    cache->stats.hit_count = 0U;
    cache->stats.miss_count = 0U;
    cache->stats.evict_count = 0U;
    cache->stats.entry_count = 0U;

    /* 时钟从 0 开始 */
    cache->clock = 0U;
    cache->initialized = true;
}

/**
 * @brief 从缓存获取 inode
 */
fs_inode_t *inode_cache_get(inode_cache_t *cache, uint32_t ino,
                             const fs_inode_t *fallback)
{
    uint32_t idx;

    if (cache == NULL)
    {
        return NULL;
    }

    /* 查找缓存 */
    idx = cache_find_index(cache, ino);

    if (idx < FS_INODE_CACHE_SIZE)
    {
        /* 缓存命中 */
        cache->stats.hit_count++;
        cache->entries[idx].last_access = cache_tick(cache);
        cache->entries[idx].access_count++;
        return &cache->entries[idx].inode;
    }

    /* 缓存未命中 */
    cache->stats.miss_count++;

    /* 若有 fallback 数据，写入缓存 */
    if (fallback != NULL)
    {
        (void)inode_cache_put(cache, fallback);

        /* 重新查找刚写入的条目 */
        idx = cache_find_index(cache, ino);
        if (idx < FS_INODE_CACHE_SIZE)
        {
            return &cache->entries[idx].inode;
        }
    }

    return NULL;
}

/**
 * @brief 放入 inode 到缓存
 */
int32_t inode_cache_put(inode_cache_t *cache, const fs_inode_t *inode)
{
    uint32_t idx;

    if ((cache == NULL) || (inode == NULL))
    {
        return -1;
    }

    /* 检查是否已存在 */
    idx = cache_find_index(cache, inode->ino);

    if (idx < FS_INODE_CACHE_SIZE)
    {
        /* 更新已有条目 */
        (void)memcpy(&cache->entries[idx].inode, inode, sizeof(fs_inode_t));
        cache->entries[idx].last_access = cache_tick(cache);
        cache->entries[idx].access_count++;
        return 0;
    }

    /* 查找淘汰目标（空闲槽位或 LRU） */
    idx = cache_find_lru(cache);

    /* 如果淘汰的是有效条目，增加淘汰计数 */
    if (cache->entries[idx].state == FS_CACHE_VALID)
    {
        cache->stats.evict_count++;
    }
    else
    {
        cache->stats.entry_count++;
    }

    /* 写入新条目 */
    cache->entries[idx].ino = inode->ino;
    cache->entries[idx].state = FS_CACHE_VALID;
    cache->entries[idx].last_access = cache_tick(cache);
    cache->entries[idx].access_count = 1U;
    (void)memcpy(&cache->entries[idx].inode, inode, sizeof(fs_inode_t));

    return 0;
}

/**
 * @brief 使指定 inode 缓存无效
 */
void inode_cache_invalidate(inode_cache_t *cache, uint32_t ino)
{
    uint32_t idx;

    if (cache == NULL)
    {
        return;
    }

    idx = cache_find_index(cache, ino);

    if (idx < FS_INODE_CACHE_SIZE)
    {
        cache->entries[idx].state = FS_CACHE_INVALID;
        cache->entries[idx].ino = 0U;
        cache->entries[idx].last_access = FS_CACHE_INVALID_TIME;
        cache->entries[idx].access_count = 0U;

        if (cache->stats.entry_count > 0U)
        {
            cache->stats.entry_count--;
        }
    }
}

/**
 * @brief 刷新所有脏缓存条目
 */
int32_t inode_cache_flush(inode_cache_t *cache)
{
    /* 简化实现：RAMFS 不需要持久化，直接返回成功 */
    if (cache == NULL)
    {
        return -1;
    }

    return 0;
}

/**
 * @brief 清空缓存
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
        cache->entries[i].ino = 0U;
        cache->entries[i].last_access = FS_CACHE_INVALID_TIME;
        cache->entries[i].access_count = 0U;
    }

    /* 重置统计 */
    cache->stats.hit_count = 0U;
    cache->stats.miss_count = 0U;
    cache->stats.evict_count = 0U;
    cache->stats.entry_count = 0U;
    cache->clock = 0U;
}

/**
 * @brief 获取缓存统计信息
 */
inode_cache_stats_t inode_cache_get_stats(const inode_cache_t *cache)
{
    inode_cache_stats_t empty;

    if (cache == NULL)
    {
        (void)memset(&empty, 0, sizeof(inode_cache_stats_t));
        return empty;
    }

    return cache->stats;
}

/**
 * @brief 打印缓存统计信息
 */
void inode_cache_print_stats(const inode_cache_t *cache)
{
    inode_cache_stats_t stats;
    uint32_t total;
    uint32_t hit_rate_pct;

    if (cache == NULL)
    {
        return;
    }

    stats = cache->stats;
    total = stats.hit_count + stats.miss_count;

    if (total > 0U)
    {
        hit_rate_pct = (stats.hit_count * 100U) / total;
    }
    else
    {
        hit_rate_pct = 0U;
    }

    (void)printf("=== inode 缓存统计 ===\n");
    (void)printf("  命中次数: %u\n", stats.hit_count);
    (void)printf("  未命中次数: %u\n", stats.miss_count);
    (void)printf("  淘汰次数: %u\n", stats.evict_count);
    (void)printf("  有效条目: %u / %u\n", stats.entry_count, FS_INODE_CACHE_SIZE);
    (void)printf("  命中率: %u%%\n", hit_rate_pct);
}
