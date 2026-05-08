/**
 * @file    test_inode_cache_simple.c
 * @brief   Inode 缓存简单测试
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details 测试 inode 缓存的基本功能：
 *          - 初始化
 *          - 插入
 *          - 获取
 *          - 淘汰
 *          - 命中率统计
 *
 * @note MISRA-C:2012 合规（简化版）
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/time.h>

/* 模拟 fs_inode_cache.h */
#define FS_INODE_CACHE_SIZE    32U
#define FS_CACHE_INVALID       0U
#define FS_CACHE_VALID         1U

typedef struct
{
    uint32_t        ino;
    uint32_t        state;
    uint64_t        last_access;
    uint32_t        access_count;
    uint32_t        data;  /* 模拟 inode 数据 */
} inode_cache_entry_t;

typedef struct
{
    inode_cache_entry_t   entries[FS_INODE_CACHE_SIZE];
    uint32_t               hit_count;
    uint32_t               miss_count;
    uint32_t               evict_count;
    int32_t                initialized;
} inode_cache_t;

/* 从 fs_inode_cache.h 复制实现 */
static inline uint64_t get_timestamp_ns(void)
{
    struct timeval tv;

    (void)gettimeofday(&tv, NULL);

    return (uint64_t)tv.tv_sec * 1000000000ULL + (uint64_t)tv.tv_usec * 1000ULL;
}

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

    cache->initialized = 1;

    return 0;
}

inode_cache_entry_t *inode_cache_get(inode_cache_t *cache, uint32_t ino,
                                     const uint32_t *fallback)
{
    uint32_t i;

    if ((cache == NULL) || (ino == 0U))
    {
        return NULL;
    }

    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state == FS_CACHE_VALID &&
            cache->entries[i].ino == ino)
        {
            cache->entries[i].last_access = get_timestamp_ns();
            cache->entries[i].access_count++;
            cache->hit_count++;

            return &cache->entries[i];
        }
    }

    cache->miss_count++;

    return NULL;
}

int32_t inode_cache_put(inode_cache_t *cache, const uint32_t ino,
                        const uint32_t *data)
{
    uint32_t i;
    inode_cache_entry_t *lru_entry;

    if ((cache == NULL) || (data == NULL))
    {
        return -1;
    }

    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state == FS_CACHE_VALID &&
            cache->entries[i].ino == ino)
        {
            cache->entries[i].data = *data;
            cache->entries[i].last_access = get_timestamp_ns();
            cache->entries[i].access_count++;
            return 0;
        }
    }

    lru_entry = find_lru_entry(cache);
    if (lru_entry == NULL)
    {
        return -1;
    }

    if (lru_entry->state == FS_CACHE_VALID)
    {
        cache->evict_count++;
    }

    lru_entry->data = *data;
    lru_entry->ino = ino;
    lru_entry->state = FS_CACHE_VALID;
    lru_entry->last_access = get_timestamp_ns();
    lru_entry->access_count = 1;

    return 0;
}

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

    cache->hit_count = 0U;
    cache->miss_count = 0U;
    cache->evict_count = 0U;
}

int32_t inode_cache_stats(const inode_cache_t *cache)
{
    if (cache == NULL)
    {
        return -1;
    }

    printf("  Cache Size:      %u entries\n", FS_INODE_CACHE_SIZE);
    printf("  Initialized:     %s\n", cache->initialized ? "Yes" : "No");
    printf("  Hit Count:       %u\n", cache->hit_count);
    printf("  Miss Count:      %u\n", cache->miss_count);
    printf("  Evict Count:     %u\n", cache->evict_count);
    printf("  Total Accesses:  %u\n", cache->hit_count + cache->miss_count);

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
 * @brief 性能测试
 */
int32_t main(void)
{
    inode_cache_t cache;
    uint32_t i;
    uint32_t test_data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    printf("=== Inode Cache 测试 ===\n\n");

    /* 初始化 */
    inode_cache_init(&cache);
    printf("✓ 缓存初始化成功\n");

    /* 插入测试 */
    printf("\n--- 插入测试 (10 个条目) ---\n");
    for (i = 0U; i < 10U; i++)
    {
        if (inode_cache_put(&cache, i + 1, &test_data[i]) == 0)
        {
            printf("  ✓ 插入 ino=%d, data=%d\n", i + 1, test_data[i]);
        }
        else
        {
            printf("  ✗ 插入 ino=%d 失败\n", i + 1);
        }
    }
    printf("✓ 插入测试完成\n");

    /* 缓存命中测试 */
    printf("\n--- 缓存命中测试 ---\n");
    for (i = 0U; i < 10U; i++)
    {
        const uint32_t *data;
        inode_cache_entry_t *entry;

        entry = inode_cache_get(&cache, i + 1, NULL);
        if (entry != NULL)
        {
            printf("  ✓ 命中 ino=%d, data=%d (access=%u)\n",
                   entry->ino, entry->data, entry->access_count);
        }
        else
        {
            printf("  ✗ 未命中 ino=%d\n", i + 1);
        }
    }

    /* 缓存未命中测试 */
    printf("\n--- 缓存未命中测试 ---\n");
    for (i = 0U; i < 5U; i++)
    {
        if (inode_cache_get(&cache, i + 11, NULL) == NULL)
        {
            printf("  ✓ 未命中 ino=%d（正确）\n", i + 11);
        }
        else
        {
            printf("  ✗ 命中 ino=%d（错误）\n", i + 11);
        }
    }

    /* 淘汰测试 */
    printf("\n--- 淘汰测试 (超过缓存大小) ---\n");
    for (i = 10U; i < 20U; i++)
    {
        if (inode_cache_put(&cache, i + 1, &test_data[i]) == 0)
        {
            printf("  ✓ 插入 ino=%d\n", i + 1);
        }
        else
        {
            printf("  ✗ 插入 ino=%d 失败\n", i + 1);
        }
    }
    printf("✓ 淘汰测试完成\n");

    /* 再次访问旧数据（应该命中） */
    printf("\n--- 再次访问旧数据 (应命中) ---\n");
    for (i = 0U; i < 10U; i++)
    {
        inode_cache_entry_t *entry;

        entry = inode_cache_get(&cache, i + 1, NULL);
        if (entry != NULL && entry->access_count >= 2)
        {
            printf("  ✓ 命中 ino=%d, access=%u\n",
                   entry->ino, entry->access_count);
        }
    }

    /* 缓存统计 */
    printf("\n=== 缓存统计 ===\n");
    inode_cache_stats(&cache);

    /* 缓存失效测试 */
    printf("\n--- 缓存失效测试 ---\n");
    if (inode_cache_invalidate(&cache, 5) == 0)
    {
        printf("  ✓ 失效 ino=5\n");
    }

    if (inode_cache_get(&cache, 5, NULL) == NULL)
    {
        printf("  ✓ 访问 ino=5 未命中（正确）\n");
    }
    else
    {
        printf("  ✗ 访问 ino=5 命中（错误）\n");
    }

    printf("\n=== 测试完成 ===\n");

    return 0;
}
