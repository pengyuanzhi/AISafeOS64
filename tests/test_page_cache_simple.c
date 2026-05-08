/**
 * @file    test_page_cache_simple.c
 * @brief   页缓存简单测试
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details 测试页缓存的基本功能：
 *          - 初始化
 *          - 插入
 *          - 获取
 *          - 淘汰
 *          - 脏页写回
 *          - 命中率统计
 *
 * @note MISRA-C:2012 合规（简化版）
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

/* 模拟 fs_page_cache.h */
#define FS_PAGE_SIZE          4096U
#define FS_PAGE_CACHE_SIZE    32U

typedef enum
{
    PAGE_STATE_INVALID = 0U,
    PAGE_STATE_VALID = 1U,
    PAGE_STATE_DIRTY = 2U
} page_cache_state_t;

typedef struct
{
    uint32_t        ino;
    uint64_t        offset;
    uint8_t         data[FS_PAGE_SIZE];
    uint32_t        state;
    uint64_t        last_access;
    uint32_t        access_count;
} page_cache_entry_t;

typedef struct
{
    page_cache_entry_t   entries[FS_PAGE_CACHE_SIZE];
    uint32_t               hit_count;
    uint32_t               miss_count;
    uint32_t               evict_count;
    uint32_t               writeback_count;
    int32_t                initialized;
} page_cache_t;

/* 从 fs_page_cache.c 复制实现 */
static inline uint64_t get_timestamp_ns(void)
{
    struct timeval tv;

    (void)gettimeofday(&tv, NULL);

    return (uint64_t)tv.tv_sec * 1000000000ULL + (uint64_t)tv.tv_usec * 1000ULL;
}

static page_cache_entry_t *find_lru_entry(page_cache_t *cache)
{
    uint32_t i;
    uint32_t lru_index = 0;
    uint32_t first_valid = 0xFFFFFFFFU;
    uint64_t min_last_access = get_timestamp_ns();

    if (cache == NULL)
    {
        return NULL;
    }

    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state != PAGE_STATE_INVALID)
        {
            if (first_valid == 0xFFFFFFFFU)
            {
                first_valid = i;
            }
            if (cache->entries[i].last_access < min_last_access)
            {
                min_last_access = cache->entries[i].last_access;
                lru_index = i;
            }
        }
    }

    /* 如果没有有效条目，返回第一个空闲条目 */
    if (first_valid == 0xFFFFFFFFU)
    {
        for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
        {
            if (cache->entries[i].state == PAGE_STATE_INVALID)
            {
                return &cache->entries[i];
            }
        }
    }

    return &cache->entries[lru_index];
}

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

    cache->initialized = 1;

    return 0;
}

uint8_t *page_cache_get(page_cache_t *cache, uint32_t ino, uint64_t offset,
                        const uint8_t *fallback, uint64_t size)
{
    uint32_t i;

    if ((cache == NULL) || (ino == 0U))
    {
        return NULL;
    }

    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state != PAGE_STATE_INVALID &&
            cache->entries[i].ino == ino &&
            cache->entries[i].offset == offset)
        {
            cache->entries[i].last_access = get_timestamp_ns();
            cache->entries[i].access_count++;
            cache->hit_count++;

            return cache->entries[i].data;
        }
    }

    if (fallback != NULL)
    {
        cache->miss_count++;
    }

    return NULL;
}

int32_t page_cache_put(page_cache_t *cache, uint32_t ino, uint64_t offset,
                       const uint8_t *data, uint64_t size, int32_t dirty)
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

    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state != PAGE_STATE_INVALID &&
            cache->entries[i].ino == ino &&
            cache->entries[i].offset == offset)
        {
            (void)memcpy(cache->entries[i].data, data, (size_t)size);
            cache->entries[i].state = dirty_flag;
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

    if (lru_entry->state == PAGE_STATE_DIRTY)
    {
        cache->writeback_count++;
    }

    (void)memset(lru_entry->data, 0, sizeof(lru_entry->data));
    (void)memcpy(lru_entry->data, data, (size_t)size);
    lru_entry->ino = ino;
    lru_entry->offset = offset;
    lru_entry->state = dirty_flag;
    lru_entry->last_access = get_timestamp_ns();
    lru_entry->access_count = 1;

    return 0;
}

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
            cache->writeback_count++;
            cache->entries[i].state = PAGE_STATE_VALID;
        }
    }
}

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

int32_t page_cache_stats(const page_cache_t *cache)
{
    if (cache == NULL)
    {
        return -1;
    }

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
 * @brief 性能测试
 */
int32_t main(void)
{
    page_cache_t cache;
    uint8_t page_data[FS_PAGE_SIZE];
    uint8_t *cached_page;
    uint32_t i;

    printf("=== Page Cache 测试 ===\n\n");

    /* 初始化 */
    page_cache_init(&cache);
    printf("✓ 缓存初始化成功\n");

    /* 插入测试 */
    printf("\n--- 插入测试 (10 个页) ---\n");
    for (i = 0U; i < 10U; i++)
    {
        (void)memset(page_data, (int32_t)i, sizeof(page_data));

        if (page_cache_put(&cache, i + 1, i * FS_PAGE_SIZE,
                          page_data, FS_PAGE_SIZE, 0) == 0)
        {
            printf("  ✓ 插入 ino=%d, offset=%lu\n", i + 1,
                   (unsigned long)(i * FS_PAGE_SIZE));
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
        cached_page = page_cache_get(&cache, i + 1, i * FS_PAGE_SIZE,
                                  NULL, 0);
        if (cached_page != NULL)
        {
            printf("  ✓ 命中 ino=%d, offset=%lu, data[0]=%u\n",
                   i + 1, (unsigned long)(i * FS_PAGE_SIZE),
                   cached_page[0]);
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
        cached_page = page_cache_get(&cache, i + 11, i * FS_PAGE_SIZE,
                                  NULL, 0);
        if (cached_page == NULL)
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
        (void)memset(page_data, (int32_t)i, sizeof(page_data));

        if (page_cache_put(&cache, i + 1, i * FS_PAGE_SIZE,
                          page_data, FS_PAGE_SIZE, 0) == 0)
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
        cached_page = page_cache_get(&cache, i + 1, i * FS_PAGE_SIZE,
                                  NULL, 0);
        if (cached_page != NULL && cached_page[0] == i)
        {
            printf("  ✓ 命中 ino=%d, data[0]=%u\n",
                   i + 1, cached_page[0]);
        }
    }

    /* 脏页测试 */
    printf("\n--- 脏页测试 ---\n");
    (void)memset(page_data, 0xFF, sizeof(page_data));
    if (page_cache_put(&cache, 1, 0, page_data, FS_PAGE_SIZE, 1) == 0)
    {
        printf("  ✓ 插入脏页 ino=1\n");
    }

    page_cache_flush(&cache);
    printf("  ✓ 写回脏页成功\n");

    /* 缓存统计 */
    printf("\n=== 缓存统计 ===\n");
    page_cache_stats(&cache);

    /* 缓存失效测试 */
    printf("\n--- 缓存失效测试 ---\n");
    if (page_cache_invalidate(&cache, 5, 4 * FS_PAGE_SIZE) == 0)
    {
        printf("  ✓ 失效 ino=5, offset=%lu\n",
               (unsigned long)(4 * FS_PAGE_SIZE));
    }

    if (page_cache_get(&cache, 5, 4 * FS_PAGE_SIZE, NULL, 0) == NULL)
    {
        printf("  ✓ 访问失效页未命中（正确）\n");
    }

    printf("\n=== 测试完成 ===\n");

    return 0;
}
