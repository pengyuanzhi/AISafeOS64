/**
 * @file    test_page_cache.c
 * @brief   页缓存单元测试（自包含版本）
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details 测试页缓存功能：
 *          - 初始化
 *          - 插入和获取
 *          - 缓存命中/未命中
 *          - LRU 淘汰
 *          - 脏页标记和写回
 *          - 页失效
 *          - 统计信息
 *
 * @note MISRA-C:2012 合规（简化版）
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ========================================================================
 * 测试用精简页缓存（覆盖 FS_PAGE_CACHE_SIZE 为小值）
 * ======================================================================== */

#undef FS_PAGE_CACHE_SIZE
#define FS_PAGE_CACHE_SIZE    8U   /* 测试用：8 个页条目 */

/** @brief 页大小 */
#define TEST_PAGE_SIZE        64U  /* 测试用：64 字节页（减少内存） */

/** @brief 页状态 */
#define PAGE_INVALID          0U
#define PAGE_VALID            1U
#define PAGE_DIRTY            2U

/**
 * @brief 测试用页缓存条目
 */
typedef struct
{
    uint32_t    ino;
    uint64_t    offset;
    uint8_t     data[TEST_PAGE_SIZE];
    uint32_t    state;
    uint64_t    last_access;
    uint32_t    access_count;
} test_page_entry_t;

/**
 * @brief 测试用页缓存管理器
 */
typedef struct
{
    test_page_entry_t   entries[FS_PAGE_CACHE_SIZE];
    uint32_t            hit_count;
    uint32_t            miss_count;
    uint32_t            evict_count;
    uint32_t            writeback_count;
    bool                initialized;
} test_page_cache_t;

/* ========================================================================
 * 虚拟时间戳
 * ======================================================================== */

static uint64_t s_test_time = 0ULL;

static uint64_t test_get_timestamp(void)
{
    s_test_time += 1000ULL;
    return s_test_time;
}

/* ========================================================================
 * 页缓存实现（测试版）
 * ======================================================================== */

/**
 * @brief 将偏移对齐到页边界
 */
static uint64_t test_page_align(uint64_t offset)
{
    return offset & ~((uint64_t)TEST_PAGE_SIZE - 1ULL);
}

/**
 * @brief 初始化页缓存
 */
static int32_t test_page_cache_init(test_page_cache_t *cache)
{
    uint32_t i;

    if (cache == NULL)
    {
        return -1;
    }

    (void)memset(cache, 0, sizeof(test_page_cache_t));

    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        cache->entries[i].state = PAGE_INVALID;
    }

    cache->initialized = true;
    return 0;
}

/**
 * @brief 查找缓存条目
 */
static test_page_entry_t *test_find_entry(test_page_cache_t *cache,
                                           uint32_t ino, uint64_t offset)
{
    uint32_t i;

    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        if ((cache->entries[i].state == PAGE_VALID ||
             cache->entries[i].state == PAGE_DIRTY) &&
            cache->entries[i].ino == ino &&
            cache->entries[i].offset == offset)
        {
            return &cache->entries[i];
        }
    }

    return NULL;
}

/**
 * @brief 查找 LRU 或空闲条目
 */
static test_page_entry_t *test_find_lru(test_page_cache_t *cache)
{
    uint32_t i;
    uint32_t lru_idx = 0U;
    uint64_t min_access = UINT64_MAX;
    bool found = false;

    /* 优先查找空闲条目 */
    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state == PAGE_INVALID)
        {
            return &cache->entries[i];
        }
    }

    /* 查找 LRU */
    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].last_access < min_access)
        {
            min_access = cache->entries[i].last_access;
            lru_idx = i;
            found = true;
        }
    }

    if (!found)
    {
        return NULL;
    }

    return &cache->entries[lru_idx];
}

/**
 * @brief 获取缓存页
 */
static uint8_t *test_page_cache_get(test_page_cache_t *cache, uint32_t ino,
                                     uint64_t offset,
                                     const uint8_t *fallback, uint64_t size)
{
    test_page_entry_t *entry;
    uint64_t aligned_offset;

    if (cache == NULL)
    {
        return NULL;
    }

    aligned_offset = test_page_align(offset);

    entry = test_find_entry(cache, ino, aligned_offset);
    if (entry != NULL)
    {
        entry->last_access = test_get_timestamp();
        entry->access_count++;
        cache->hit_count++;
        return entry->data;
    }

    cache->miss_count++;

    if ((fallback != NULL) && (size > 0U))
    {
        uint64_t copy_size = size;
        if (copy_size > TEST_PAGE_SIZE)
        {
            copy_size = TEST_PAGE_SIZE;
        }

        entry = test_find_lru(cache);
        if (entry != NULL)
        {
            if (entry->state != PAGE_INVALID)
            {
                cache->evict_count++;
            }

            (void)memset(entry->data, 0, TEST_PAGE_SIZE);
            (void)memcpy(entry->data, fallback, (size_t)copy_size);
            entry->ino = ino;
            entry->offset = aligned_offset;
            entry->last_access = test_get_timestamp();
            entry->access_count = 1U;
            entry->state = PAGE_VALID;
            return entry->data;
        }
    }

    return NULL;
}

/**
 * @brief 放入缓存页
 */
static int32_t test_page_cache_put(test_page_cache_t *cache, uint32_t ino,
                                    uint64_t offset,
                                    const uint8_t *data, uint64_t size,
                                    bool dirty)
{
    test_page_entry_t *entry;
    uint64_t aligned_offset;
    uint64_t copy_size;

    if ((cache == NULL) || (data == NULL) || (size == 0U))
    {
        return -1;
    }

    aligned_offset = test_page_align(offset);
    copy_size = size;
    if (copy_size > TEST_PAGE_SIZE)
    {
        copy_size = TEST_PAGE_SIZE;
    }

    entry = test_find_entry(cache, ino, aligned_offset);
    if (entry != NULL)
    {
        (void)memset(entry->data, 0, TEST_PAGE_SIZE);
        (void)memcpy(entry->data, data, (size_t)copy_size);
        entry->last_access = test_get_timestamp();
        entry->access_count++;
        entry->state = dirty ? PAGE_DIRTY : PAGE_VALID;
        return 0;
    }

    entry = test_find_lru(cache);
    if (entry == NULL)
    {
        return -1;
    }

    if (entry->state != PAGE_INVALID)
    {
        cache->evict_count++;
    }

    (void)memset(entry->data, 0, TEST_PAGE_SIZE);
    (void)memcpy(entry->data, data, (size_t)copy_size);
    entry->ino = ino;
    entry->offset = aligned_offset;
    entry->last_access = test_get_timestamp();
    entry->access_count = 1U;
    entry->state = dirty ? PAGE_DIRTY : PAGE_VALID;

    return 0;
}

/**
 * @brief 使缓存页失效
 */
static int32_t test_page_cache_invalidate(test_page_cache_t *cache,
                                           uint32_t ino, uint64_t offset)
{
    test_page_entry_t *entry;
    uint64_t aligned_offset;

    if (cache == NULL)
    {
        return -1;
    }

    aligned_offset = test_page_align(offset);

    entry = test_find_entry(cache, ino, aligned_offset);
    if (entry == NULL)
    {
        return -1;
    }

    entry->state = PAGE_INVALID;
    entry->ino = 0U;
    entry->offset = 0ULL;
    entry->access_count = 0U;
    entry->last_access = 0ULL;

    return 0;
}

/**
 * @brief 刷新脏页
 */
static void test_page_cache_flush(test_page_cache_t *cache)
{
    uint32_t i;

    if (cache == NULL)
    {
        return;
    }

    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        if (cache->entries[i].state == PAGE_DIRTY)
        {
            cache->entries[i].state = PAGE_VALID;
            cache->writeback_count++;
        }
    }
}

/**
 * @brief 清空缓存
 */
static void test_page_cache_clear(test_page_cache_t *cache)
{
    uint32_t i;

    if (cache == NULL)
    {
        return;
    }

    for (i = 0U; i < FS_PAGE_CACHE_SIZE; i++)
    {
        cache->entries[i].state = PAGE_INVALID;
        cache->entries[i].ino = 0U;
        cache->entries[i].offset = 0ULL;
        cache->entries[i].access_count = 0U;
    }
}

/* ========================================================================
 * 测试框架
 * ======================================================================== */

/** @brief 测试计数器 */
static uint32_t s_pass_count = 0U;
static uint32_t s_fail_count = 0U;

/**
 * @brief 断言宏
 */
#define TEST_ASSERT(cond, msg) \
    do { \
        if (cond) { \
            s_pass_count++; \
            printf("  [PASS] %s\n", msg); \
        } else { \
            s_fail_count++; \
            printf("  [FAIL] %s (line %d)\n", msg, __LINE__); \
        } \
    } while (0)

/**
 * @brief 断言相等
 */
#define TEST_ASSERT_EQ(expected, actual, msg) \
    do { \
        if ((expected) == (actual)) { \
            s_pass_count++; \
            printf("  [PASS] %s\n", msg); \
        } else { \
            s_fail_count++; \
            printf("  [FAIL] %s: expected=%d actual=%d (line %d)\n", \
                   msg, (int)(expected), (int)(actual), __LINE__); \
        } \
    } while (0)

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试1：初始化
 */
static void test_init(void)
{
    test_page_cache_t cache;

    printf("\n--- 测试1：初始化 ---\n");

    /* 正常初始化 */
    TEST_ASSERT_EQ(0, test_page_cache_init(&cache), "初始化成功");

    /* 验证初始化状态 */
    TEST_ASSERT(cache.initialized, "初始化标志为 true");
    TEST_ASSERT_EQ(0U, cache.hit_count, "命中计数初始为 0");
    TEST_ASSERT_EQ(0U, cache.miss_count, "未命中计数初始为 0");
    TEST_ASSERT_EQ(0U, cache.evict_count, "淘汰计数初始为 0");
    TEST_ASSERT_EQ(0U, cache.writeback_count, "写回计数初始为 0");

    /* 验证所有条目为无效 */
    TEST_ASSERT_EQ(PAGE_INVALID, cache.entries[0].state, "条目0状态为INVALID");
    TEST_ASSERT_EQ(PAGE_INVALID, cache.entries[7].state, "条目7状态为INVALID");

    /* NULL 参数测试 */
    TEST_ASSERT_EQ(-1, test_page_cache_init(NULL), "NULL参数返回失败");

    printf("  初始化测试完成\n");
}

/**
 * @brief 测试2：插入和获取
 */
static void test_put_get(void)
{
    test_page_cache_t cache;
    uint8_t write_data[TEST_PAGE_SIZE];
    uint8_t *read_ptr;
    uint32_t i;

    printf("\n--- 测试2：插入和获取 ---\n");

    test_page_cache_init(&cache);

    /* 准备测试数据 */
    for (i = 0U; i < TEST_PAGE_SIZE; i++)
    {
        write_data[i] = (uint8_t)(i & 0xFFU);
    }

    /* 插入页 */
    TEST_ASSERT_EQ(0,
        test_page_cache_put(&cache, 1U, 0ULL, write_data, TEST_PAGE_SIZE, false),
        "插入 ino=1 offset=0");

    /* 获取页（应命中） */
    read_ptr = test_page_cache_get(&cache, 1U, 0ULL, NULL, 0U);
    TEST_ASSERT(read_ptr != NULL, "获取 ino=1 offset=0 命中");
    TEST_ASSERT_EQ(0, memcmp(read_ptr, write_data, TEST_PAGE_SIZE),
        "数据一致");

    /* 获取不存在的页（应未命中） */
    read_ptr = test_page_cache_get(&cache, 99U, 0ULL, NULL, 0U);
    TEST_ASSERT(read_ptr == NULL, "获取 ino=99 未命中");
    TEST_ASSERT_EQ(1U, cache.miss_count, "未命中计数为 1");

    printf("  插入获取测试完成\n");
}

/**
 * @brief 测试3：缓存命中和未命中统计
 */
static void test_hit_miss_stats(void)
{
    test_page_cache_t cache;
    uint8_t data[TEST_PAGE_SIZE];
    uint32_t i;

    printf("\n--- 测试3：缓存命中和未命中统计 ---\n");

    test_page_cache_init(&cache);
    (void)memset(data, 0xAA, TEST_PAGE_SIZE);

    /* 插入 4 个页 */
    for (i = 1U; i <= 4U; i++)
    {
        (void)test_page_cache_put(&cache, i, 0ULL, data, TEST_PAGE_SIZE, false);
    }

    /* 重置统计 */
    cache.hit_count = 0U;
    cache.miss_count = 0U;

    /* 命中：访问已缓存的页 */
    for (i = 1U; i <= 4U; i++)
    {
        (void)test_page_cache_get(&cache, i, 0ULL, NULL, 0U);
    }
    TEST_ASSERT_EQ(4U, cache.hit_count, "4次命中");

    /* 未命中：访问不存在的页 */
    for (i = 10U; i <= 14U; i++)
    {
        (void)test_page_cache_get(&cache, i, 0ULL, NULL, 0U);
    }
    TEST_ASSERT_EQ(5U, cache.miss_count, "5次未命中");

    printf("  命中未命中统计测试完成\n");
}

/**
 * @brief 测试4：LRU 淘汰
 */
static void test_lru_eviction(void)
{
    test_page_cache_t cache;
    uint8_t data[TEST_PAGE_SIZE];
    uint8_t *ptr;
    uint32_t i;

    printf("\n--- 测试4：LRU 淘汰 ---\n");

    test_page_cache_init(&cache);
    (void)memset(data, 0xBB, TEST_PAGE_SIZE);

    /* 填满缓存（8个条目） */
    for (i = 1U; i <= 8U; i++)
    {
        data[0] = (uint8_t)i;
        (void)test_page_cache_put(&cache, i, 0ULL, data, TEST_PAGE_SIZE, false);
    }

    TEST_ASSERT_EQ(0U, cache.evict_count, "填满后无淘汰");

    /* 插入第9个页，应淘汰 ino=1（最早插入） */
    data[0] = 9U;
    (void)test_page_cache_put(&cache, 9U, 0ULL, data, TEST_PAGE_SIZE, false);
    TEST_ASSERT_EQ(1U, cache.evict_count, "淘汰1次");

    /* ino=1 应已被淘汰 */
    ptr = test_page_cache_get(&cache, 1U, 0ULL, NULL, 0U);
    TEST_ASSERT(ptr == NULL, "ino=1 已被淘汰");

    /* ino=9 应存在 */
    ptr = test_page_cache_get(&cache, 9U, 0ULL, NULL, 0U);
    TEST_ASSERT(ptr != NULL, "ino=9 存在");

    printf("  LRU淘汰测试完成\n");
}

/**
 * @brief 测试5：脏页和写回
 */
static void test_dirty_writeback(void)
{
    test_page_cache_t cache;
    uint8_t data[TEST_PAGE_SIZE];
    uint32_t i;

    printf("\n--- 测试5：脏页和写回 ---\n");

    test_page_cache_init(&cache);
    (void)memset(data, 0xCC, TEST_PAGE_SIZE);

    /* 插入脏页 */
    TEST_ASSERT_EQ(0,
        test_page_cache_put(&cache, 1U, 0ULL, data, TEST_PAGE_SIZE, true),
        "插入脏页 ino=1");
    TEST_ASSERT_EQ(PAGE_DIRTY, cache.entries[0].state, "状态为DIRTY");

    /* 插入干净页 */
    TEST_ASSERT_EQ(0,
        test_page_cache_put(&cache, 2U, 0ULL, data, TEST_PAGE_SIZE, false),
        "插入干净页 ino=2");

    /* 插入更多脏页 */
    for (i = 3U; i <= 5U; i++)
    {
        (void)test_page_cache_put(&cache, i, 0ULL, data, TEST_PAGE_SIZE, true);
    }

    /* 刷新脏页 */
    test_page_cache_flush(&cache);
    TEST_ASSERT_EQ(4U, cache.writeback_count, "写回4个脏页");

    /* 验证所有页状态为 VALID */
    TEST_ASSERT_EQ(PAGE_VALID, cache.entries[0].state, "刷新后条目0为VALID");

    /* 再次刷新（无脏页） */
    cache.writeback_count = 0U;
    test_page_cache_flush(&cache);
    TEST_ASSERT_EQ(0U, cache.writeback_count, "无脏页时不写回");

    printf("  脏页写回测试完成\n");
}

/**
 * @brief 测试6：页失效
 */
static void test_invalidate(void)
{
    test_page_cache_t cache;
    uint8_t data[TEST_PAGE_SIZE];
    uint8_t *ptr;

    printf("\n--- 测试6：页失效 ---\n");

    test_page_cache_init(&cache);
    (void)memset(data, 0xDD, TEST_PAGE_SIZE);

    /* 插入页 */
    (void)test_page_cache_put(&cache, 1U, 0ULL, data, TEST_PAGE_SIZE, false);
    (void)test_page_cache_put(&cache, 2U, 0ULL, data, TEST_PAGE_SIZE, false);

    /* 使 ino=1 失效 */
    TEST_ASSERT_EQ(0, test_page_cache_invalidate(&cache, 1U, 0ULL),
        "失效 ino=1 offset=0");

    /* 验证 ino=1 不存在 */
    ptr = test_page_cache_get(&cache, 1U, 0ULL, NULL, 0U);
    TEST_ASSERT(ptr == NULL, "失效后 ino=1 不可访问");

    /* 验证 ino=2 仍存在 */
    ptr = test_page_cache_get(&cache, 2U, 0ULL, NULL, 0U);
    TEST_ASSERT(ptr != NULL, "ino=2 仍可访问");

    /* 失效不存在的页 */
    TEST_ASSERT_EQ(-1, test_page_cache_invalidate(&cache, 99U, 0ULL),
        "失效不存在的页返回失败");

    printf("  页失效测试完成\n");
}

/**
 * @brief 测试7：fallback 机制
 */
static void test_fallback(void)
{
    test_page_cache_t cache;
    uint8_t fallback_data[TEST_PAGE_SIZE];
    uint8_t *ptr;
    uint32_t i;

    printf("\n--- 测试7：fallback 机制 ---\n");

    test_page_cache_init(&cache);

    /* 准备 fallback 数据 */
    for (i = 0U; i < TEST_PAGE_SIZE; i++)
    {
        fallback_data[i] = (uint8_t)(0xEEU ^ (uint8_t)i);
    }

    /* 获取不存在的页，提供 fallback */
    ptr = test_page_cache_get(&cache, 1U, 0ULL, fallback_data, TEST_PAGE_SIZE);
    TEST_ASSERT(ptr != NULL, "fallback 缓存成功");

    /* 验证数据一致 */
    TEST_ASSERT_EQ(0, memcmp(ptr, fallback_data, TEST_PAGE_SIZE),
        "fallback 数据一致");

    /* 再次获取（应命中，不使用 fallback） */
    uint8_t dummy = 0xFFU;
    (void)memset(fallback_data, dummy, TEST_PAGE_SIZE);
    ptr = test_page_cache_get(&cache, 1U, 0ULL, fallback_data, TEST_PAGE_SIZE);
    TEST_ASSERT(ptr[0] != dummy, "命中时不使用 fallback");

    printf("  fallback 测试完成\n");
}

/**
 * @brief 测试8：页对齐
 */
static void test_page_alignment(void)
{
    test_page_cache_t cache;
    uint8_t data[TEST_PAGE_SIZE];
    uint8_t *ptr;

    printf("\n--- 测试8：页对齐 ---\n");

    test_page_cache_init(&cache);
    (void)memset(data, 0x11, TEST_PAGE_SIZE);

    /* 插入 offset=0 */
    (void)test_page_cache_put(&cache, 1U, 0ULL, data, TEST_PAGE_SIZE, false);

    /* 获取 offset=10（应对齐到 offset=0） */
    ptr = test_page_cache_get(&cache, 1U, 10ULL, NULL, 0U);
    TEST_ASSERT(ptr != NULL, "非对齐偏移自动对齐后命中");

    /* 获取 offset=63（应对齐到 offset=0） */
    ptr = test_page_cache_get(&cache, 1U, 63ULL, NULL, 0U);
    TEST_ASSERT(ptr != NULL, "offset=63 对齐到 offset=0 命中");

    printf("  页对齐测试完成\n");
}

/**
 * @brief 测试9：清空缓存
 */
static void test_clear(void)
{
    test_page_cache_t cache;
    uint8_t data[TEST_PAGE_SIZE];
    uint8_t *ptr;

    printf("\n--- 测试9：清空缓存 ---\n");

    test_page_cache_init(&cache);
    (void)memset(data, 0x22, TEST_PAGE_SIZE);

    /* 插入数据 */
    (void)test_page_cache_put(&cache, 1U, 0ULL, data, TEST_PAGE_SIZE, false);
    (void)test_page_cache_put(&cache, 2U, 0ULL, data, TEST_PAGE_SIZE, false);

    /* 产生一些统计 */
    (void)test_page_cache_get(&cache, 1U, 0ULL, NULL, 0U);
    (void)test_page_cache_get(&cache, 99U, 0ULL, NULL, 0U);

    /* 清空 */
    test_page_cache_clear(&cache);

    /* 验证所有条目无效 */
    ptr = test_page_cache_get(&cache, 1U, 0ULL, NULL, 0U);
    TEST_ASSERT(ptr == NULL, "清空后 ino=1 不可访问");

    /* 验证统计信息保留 */
    TEST_ASSERT_EQ(1U, cache.hit_count, "清空后命中计数保留");
    TEST_ASSERT_EQ(2U, cache.miss_count, "清空后未命中计数保留");

    printf("  清空缓存测试完成\n");
}

/**
 * @brief 测试10：更新已存在的页
 */
static void test_update(void)
{
    test_page_cache_t cache;
    uint8_t data1[TEST_PAGE_SIZE];
    uint8_t data2[TEST_PAGE_SIZE];
    uint8_t *ptr;
    uint32_t i;

    printf("\n--- 测试10：更新已存在的页 ---\n");

    test_page_cache_init(&cache);

    for (i = 0U; i < TEST_PAGE_SIZE; i++)
    {
        data1[i] = 0xAAU;
        data2[i] = 0xBBU;
    }

    /* 插入数据 */
    (void)test_page_cache_put(&cache, 1U, 0ULL, data1, TEST_PAGE_SIZE, false);

    /* 更新同一页 */
    (void)test_page_cache_put(&cache, 1U, 0ULL, data2, TEST_PAGE_SIZE, true);

    /* 验证数据已更新 */
    ptr = test_page_cache_get(&cache, 1U, 0ULL, NULL, 0U);
    TEST_ASSERT(ptr != NULL, "更新后页存在");
    TEST_ASSERT_EQ(0, memcmp(ptr, data2, TEST_PAGE_SIZE), "数据已更新");
    TEST_ASSERT_EQ(PAGE_DIRTY, cache.entries[0].state, "更新后状态为DIRTY");

    /* 不应产生淘汰 */
    TEST_ASSERT_EQ(0U, cache.evict_count, "更新不产生淘汰");

    printf("  更新测试完成\n");
}

/**
 * @brief 测试11：NULL 参数防御
 */
static void test_null_args(void)
{
    test_page_cache_t cache;
    uint8_t data[TEST_PAGE_SIZE];

    printf("\n--- 测试11：NULL 参数防御 ---\n");

    test_page_cache_init(&cache);
    (void)memset(data, 0x33, TEST_PAGE_SIZE);

    /* NULL cache */
    TEST_ASSERT(test_page_cache_get(NULL, 1U, 0ULL, data, TEST_PAGE_SIZE) == NULL,
        "get(NULL cache) 返回 NULL");
    TEST_ASSERT_EQ(-1, test_page_cache_put(NULL, 1U, 0ULL, data, TEST_PAGE_SIZE, false),
        "put(NULL cache) 返回失败");
    TEST_ASSERT_EQ(-1, test_page_cache_invalidate(NULL, 1U, 0ULL),
        "invalidate(NULL cache) 返回失败");

    /* NULL data */
    TEST_ASSERT_EQ(-1, test_page_cache_put(&cache, 1U, 0ULL, NULL, TEST_PAGE_SIZE, false),
        "put(NULL data) 返回失败");

    /* size=0 */
    TEST_ASSERT_EQ(-1, test_page_cache_put(&cache, 1U, 0ULL, data, 0U, false),
        "put(size=0) 返回失败");

    printf("  NULL参数防御测试完成\n");
}

/**
 * @brief 测试12：LRU 顺序验证
 */
static void test_lru_order(void)
{
    test_page_cache_t cache;
    uint8_t data[TEST_PAGE_SIZE];
    uint8_t *ptr;
    uint32_t i;

    printf("\n--- 测试12：LRU 顺序验证 ---\n");

    test_page_cache_init(&cache);
    (void)memset(data, 0x44, TEST_PAGE_SIZE);

    /* 填满缓存（ino=1~8） */
    for (i = 1U; i <= 8U; i++)
    {
        data[0] = (uint8_t)i;
        (void)test_page_cache_put(&cache, i, 0ULL, data, TEST_PAGE_SIZE, false);
    }

    /* 访问 ino=1，使其变为最近使用 */
    (void)test_page_cache_get(&cache, 1U, 0ULL, NULL, 0U);

    /* 插入新页，应淘汰 ino=2（当前最久未使用） */
    data[0] = 9U;
    (void)test_page_cache_put(&cache, 9U, 0ULL, data, TEST_PAGE_SIZE, false);

    /* ino=1 应仍存在（被访问过） */
    ptr = test_page_cache_get(&cache, 1U, 0ULL, NULL, 0U);
    TEST_ASSERT(ptr != NULL, "LRU: ino=1 仍存在（被访问过）");

    /* ino=2 应被淘汰（最久未使用） */
    ptr = test_page_cache_get(&cache, 2U, 0ULL, NULL, 0U);
    TEST_ASSERT(ptr == NULL, "LRU: ino=2 被淘汰（最久未使用）");

    printf("  LRU顺序验证测试完成\n");
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

/**
 * @brief 测试入口
 */
int32_t main(void)
{
    printf("========================================\n");
    printf("  页缓存单元测试 (test_page_cache)\n");
    printf("========================================\n");

    /* 执行所有测试 */
    test_init();
    test_put_get();
    test_hit_miss_stats();
    test_lru_eviction();
    test_dirty_writeback();
    test_invalidate();
    test_fallback();
    test_page_alignment();
    test_clear();
    test_update();
    test_null_args();
    test_lru_order();

    /* 输出汇总 */
    printf("\n========================================\n");
    printf("  测试结果汇总\n");
    printf("========================================\n");
    printf("  通过: %u\n", s_pass_count);
    printf("  失败: %u\n", s_fail_count);
    printf("  总计: %u\n", s_pass_count + s_fail_count);

    if (s_fail_count == 0U)
    {
        printf("  结果: ALL PASSED\n");
    }
    else
    {
        printf("  结果: HAS FAILURES\n");
    }

    printf("========================================\n");

    return (s_fail_count > 0U) ? 1 : 0;
}
