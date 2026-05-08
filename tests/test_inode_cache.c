/**
 * @file    test_inode_cache.c
 * @brief   inode 缓存单元测试
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details inode LRU 缓存单元测试：
 *          - 初始化测试
 *          - 缓存命中/未命中测试
 *          - LRU 淘汰策略测试
 *          - 缓存无效化测试
 *          - 统计信息一致性测试
 *          - 边界条件和错误处理
 *
 * @note MISRA-C:2012 合规
 * @note TDD: RED -> GREEN -> REFACTOR
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ========================================================================
 * 简易测试框架
 * ======================================================================== */

static uint32_t s_total   = 0U;
static uint32_t s_passed  = 0U;
static uint32_t s_failed  = 0U;

#define TEST_ASSERT_EQ(a, b)                                               \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((int64_t)(a) == (int64_t)(b)) { s_passed++; }                  \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 %lld 实际 %lld\n",                   \
                   __FILE__, __LINE__,                                      \
                   (long long)(int64_t)(b), (long long)(int64_t)(a));       \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_NE(a, b)                                               \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((int64_t)(a) != (int64_t)(b)) { s_passed++; }                  \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 %lld != %lld\n",                     \
                   __FILE__, __LINE__,                                      \
                   (long long)(int64_t)(a), (long long)(int64_t)(b));       \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_TRUE(cond)                                             \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if (cond) { s_passed++; }                                          \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 true: %s\n",                         \
                   __FILE__, __LINE__, #cond);                              \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_NULL(p)                                                \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((p) == NULL) { s_passed++; }                                   \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 NULL\n", __FILE__, __LINE__);         \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_NOT_NULL(p)                                            \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((p) != NULL) { s_passed++; }                                   \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望非 NULL\n", __FILE__, __LINE__);       \
        }                                                                  \
    } while (0)

/* 包含被测文件 */
#include "../services/fs/fs_ops/fs_inode_cache.h"

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 创建测试用 inode
 */
static fs_inode_t make_inode(uint32_t ino, uint32_t size, uint32_t mode)
{
    fs_inode_t inode;

    (void)memset(&inode, 0, sizeof(fs_inode_t));
    inode.ino    = ino;
    inode.size   = size;
    inode.mode   = mode;
    inode.type   = FS_TYPE_REGULAR;
    inode.nlinks = 1U;
    inode.dirty  = false;

    return inode;
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试1: 缓存初始化
 */
static void test_cache_init(void)
{
    inode_cache_t cache;

    printf("\n--- test_cache_init ---\n");

    inode_cache_init(&cache);

    /* 验证初始化状态 */
    TEST_ASSERT_TRUE(cache.initialized);
    TEST_ASSERT_EQ((int64_t)cache.clock, 0LL);

    /* 验证统计清零 */
    TEST_ASSERT_EQ((int64_t)cache.stats.hit_count, 0LL);
    TEST_ASSERT_EQ((int64_t)cache.stats.miss_count, 0LL);
    TEST_ASSERT_EQ((int64_t)cache.stats.evict_count, 0LL);
    TEST_ASSERT_EQ((int64_t)cache.stats.entry_count, 0LL);

    /* 验证所有条目为无效 */
    TEST_ASSERT_EQ((int64_t)cache.entries[0].state, (int64_t)FS_CACHE_INVALID);
    TEST_ASSERT_EQ((int64_t)cache.entries[31].state, (int64_t)FS_CACHE_INVALID);
}

/**
 * @brief 测试2: NULL 参数安全
 */
static void test_cache_null_safety(void)
{
    printf("\n--- test_cache_null_safety ---\n");

    /* 传入 NULL 不应崩溃 */
    inode_cache_init(NULL);
    TEST_ASSERT_NULL(inode_cache_get(NULL, 1U, NULL));
    TEST_ASSERT_EQ((int64_t)inode_cache_put(NULL, NULL), -1LL);
    inode_cache_invalidate(NULL, 1U);
    inode_cache_clear(NULL);
    TEST_ASSERT_EQ((int64_t)inode_cache_flush(NULL), -1LL);
    inode_cache_print_stats(NULL);
}

/**
 * @brief 测试3: 缓存命中 - put 后 get
 */
static void test_cache_hit(void)
{
    inode_cache_t cache;
    fs_inode_t inode;
    fs_inode_t *result;

    printf("\n--- test_cache_hit ---\n");

    inode_cache_init(&cache);

    /* 放入 inode */
    inode = make_inode(42U, 1024U, 0644U);
    TEST_ASSERT_EQ((int64_t)inode_cache_put(&cache, &inode), 0LL);

    /* 获取 inode - 应命中 */
    result = inode_cache_get(&cache, 42U, NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQ((int64_t)result->ino, 42LL);
    TEST_ASSERT_EQ((int64_t)result->size, 1024LL);
    TEST_ASSERT_EQ((int64_t)result->mode, 0644LL);

    /* 验证统计 */
    TEST_ASSERT_EQ((int64_t)cache.stats.hit_count, 1LL);
    TEST_ASSERT_EQ((int64_t)cache.stats.miss_count, 0LL);
}

/**
 * @brief 测试4: 缓存未命中 - 不存在的 inode
 */
static void test_cache_miss(void)
{
    inode_cache_t cache;
    fs_inode_t *result;

    printf("\n--- test_cache_miss ---\n");

    inode_cache_init(&cache);

    /* 查找不存在的 inode */
    result = inode_cache_get(&cache, 99U, NULL);
    TEST_ASSERT_NULL(result);

    /* 验证统计 */
    TEST_ASSERT_EQ((int64_t)cache.stats.hit_count, 0LL);
    TEST_ASSERT_EQ((int64_t)cache.stats.miss_count, 1LL);
}

/**
 * @brief 测试5: 带 fallback 的缓存未命中
 */
static void test_cache_miss_with_fallback(void)
{
    inode_cache_t cache;
    fs_inode_t fallback;
    fs_inode_t *result;

    printf("\n--- test_cache_miss_with_fallback ---\n");

    inode_cache_init(&cache);

    /* 未命中但提供 fallback */
    fallback = make_inode(7U, 2048U, 0755U);
    result = inode_cache_get(&cache, 7U, &fallback);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQ((int64_t)result->ino, 7LL);
    TEST_ASSERT_EQ((int64_t)result->size, 2048LL);

    /* 验证统计：1次未命中，然后自动 put */
    TEST_ASSERT_EQ((int64_t)cache.stats.miss_count, 1LL);
    TEST_ASSERT_EQ((int64_t)cache.stats.entry_count, 1LL);

    /* 再次获取应命中 */
    result = inode_cache_get(&cache, 7U, NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQ((int64_t)cache.stats.hit_count, 1LL);
}

/**
 * @brief 测试6: LRU 淘汰策略
 */
static void test_cache_lru_eviction(void)
{
    inode_cache_t cache;
    fs_inode_t inode;
    fs_inode_t *result;
    uint32_t i;
    inode_cache_stats_t stats;

    printf("\n--- test_cache_lru_eviction ---\n");

    inode_cache_init(&cache);

    /* 填满缓存（32 个条目） */
    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        inode = make_inode(i + 1U, i * 100U, 0644U);
        TEST_ASSERT_EQ((int64_t)inode_cache_put(&cache, &inode), 0LL);
    }

    stats = inode_cache_get_stats(&cache);
    TEST_ASSERT_EQ((int64_t)stats.entry_count, (int64_t)FS_INODE_CACHE_SIZE);
    TEST_ASSERT_EQ((int64_t)stats.evict_count, 0LL);

    /* 访问 ino=1（最久未使用），使其变成最近使用 */
    result = inode_cache_get(&cache, 1U, NULL);
    TEST_ASSERT_NOT_NULL(result);

    /* 添加第 33 个条目，应淘汰 ino=2（新的最久未使用） */
    inode = make_inode(100U, 9999U, 0U);
    TEST_ASSERT_EQ((int64_t)inode_cache_put(&cache, &inode), 0LL);

    stats = inode_cache_get_stats(&cache);
    TEST_ASSERT_EQ((int64_t)stats.evict_count, 1LL);

    /* ino=1 应该仍在缓存中（我们刚访问过） */
    result = inode_cache_get(&cache, 1U, NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQ((int64_t)result->ino, 1LL);

    /* ino=2 应被淘汰（新的未命中） */
    result = inode_cache_get(&cache, 2U, NULL);
    TEST_ASSERT_NULL(result);
}

/**
 * @brief 测试7: 缓存无效化
 */
static void test_cache_invalidate(void)
{
    inode_cache_t cache;
    fs_inode_t inode;
    fs_inode_t *result;
    inode_cache_stats_t stats;

    printf("\n--- test_cache_invalidate ---\n");

    inode_cache_init(&cache);

    /* 放入 inode */
    inode = make_inode(10U, 500U, 0644U);
    (void)inode_cache_put(&cache, &inode);

    /* 验证存在 */
    result = inode_cache_get(&cache, 10U, NULL);
    TEST_ASSERT_NOT_NULL(result);

    /* 无效化 */
    inode_cache_invalidate(&cache, 10U);

    /* 验证已无效 */
    result = inode_cache_get(&cache, 10U, NULL);
    TEST_ASSERT_NULL(result);

    /* 验证条目计数减少 */
    stats = inode_cache_get_stats(&cache);
    TEST_ASSERT_EQ((int64_t)stats.entry_count, 0LL);
}

/**
 * @brief 测试8: 无效化不存在的 inode（无副作用）
 */
static void test_cache_invalidate_nonexistent(void)
{
    inode_cache_t cache;
    fs_inode_t inode;
    inode_cache_stats_t stats_before;
    inode_cache_stats_t stats_after;

    printf("\n--- test_cache_invalidate_nonexistent ---\n");

    inode_cache_init(&cache);

    /* 放入 inode */
    inode = make_inode(1U, 100U, 0644U);
    (void)inode_cache_put(&cache, &inode);

    stats_before = inode_cache_get_stats(&cache);

    /* 无效化不存在的 inode */
    inode_cache_invalidate(&cache, 999U);

    stats_after = inode_cache_get_stats(&cache);

    /* 统计不应变化 */
    TEST_ASSERT_EQ((int64_t)stats_before.entry_count,
                   (int64_t)stats_after.entry_count);
}

/**
 * @brief 测试9: 缓存清空
 */
static void test_cache_clear(void)
{
    inode_cache_t cache;
    fs_inode_t inode;
    fs_inode_t *result;
    uint32_t i;

    printf("\n--- test_cache_clear ---\n");

    inode_cache_init(&cache);

    /* 填入多个 inode */
    for (i = 0U; i < 10U; i++)
    {
        inode = make_inode(i + 1U, i * 50U, 0644U);
        (void)inode_cache_put(&cache, &inode);
    }

    /* 验证有条目 */
    result = inode_cache_get(&cache, 5U, NULL);
    TEST_ASSERT_NOT_NULL(result);

    /* 清空 */
    inode_cache_clear(&cache);

    /* 验证所有数据已清空 */
    result = inode_cache_get(&cache, 5U, NULL);
    TEST_ASSERT_NULL(result);

    result = inode_cache_get(&cache, 1U, NULL);
    TEST_ASSERT_NULL(result);

    /* 统计应重置 */
    TEST_ASSERT_EQ((int64_t)cache.stats.hit_count, 0LL);
    TEST_ASSERT_EQ((int64_t)cache.stats.miss_count, 2LL);
    TEST_ASSERT_EQ((int64_t)cache.stats.entry_count, 0LL);
}

/**
 * @brief 测试10: 缓存更新（相同 ino 的 put）
 */
static void test_cache_update(void)
{
    inode_cache_t cache;
    fs_inode_t inode;
    fs_inode_t *result;
    inode_cache_stats_t stats;

    printf("\n--- test_cache_update ---\n");

    inode_cache_init(&cache);

    /* 放入 inode */
    inode = make_inode(42U, 100U, 0644U);
    (void)inode_cache_put(&cache, &inode);

    /* 更新同一 inode */
    inode = make_inode(42U, 200U, 0755U);
    (void)inode_cache_put(&cache, &inode);

    /* 验证更新后的值 */
    result = inode_cache_get(&cache, 42U, NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQ((int64_t)result->size, 200LL);
    TEST_ASSERT_EQ((int64_t)result->mode, 0755LL);

    /* 不应增加条目数 */
    stats = inode_cache_get_stats(&cache);
    TEST_ASSERT_EQ((int64_t)stats.entry_count, 1LL);
}

/**
 * @brief 测试11: 访问计数
 */
static void test_cache_access_count(void)
{
    inode_cache_t cache;
    fs_inode_t inode;
    uint32_t idx;
    uint32_t i;

    printf("\n--- test_cache_access_count ---\n");

    inode_cache_init(&cache);

    /* 放入 inode */
    inode = make_inode(1U, 100U, 0644U);
    (void)inode_cache_put(&cache, &inode);

    /* 访问 5 次 */
    for (i = 0U; i < 5U; i++)
    {
        (void)inode_cache_get(&cache, 1U, NULL);
    }

    /* 验证访问计数：put 算1次 + get 算5次 = 6次 */
    idx = 0U; /* 第一个条目 */
    TEST_ASSERT_EQ((int64_t)cache.entries[idx].access_count, 6LL);
}

/**
 * @brief 测试12: LRU 顺序正确性
 */
static void test_cache_lru_order(void)
{
    inode_cache_t cache;
    fs_inode_t inode;
    fs_inode_t *result;
    uint32_t i;

    printf("\n--- test_cache_lru_order ---\n");

    inode_cache_init(&cache);

    /* 填满缓存：ino = 1, 2, 3, ..., 32 */
    for (i = 0U; i < FS_INODE_CACHE_SIZE; i++)
    {
        inode = make_inode(i + 1U, i * 10U, 0644U);
        (void)inode_cache_put(&cache, &inode);
    }

    /* 访问 ino=1 使其成为最近使用 */
    (void)inode_cache_get(&cache, 1U, NULL);

    /* ino=2 现在是最久未使用 */
    /* 添加新条目应淘汰 ino=2 */
    inode = make_inode(100U, 0U, 0U);
    (void)inode_cache_put(&cache, &inode);

    /* ino=1 应存在（刚访问） */
    result = inode_cache_get(&cache, 1U, NULL);
    TEST_ASSERT_NOT_NULL(result);

    /* ino=2 应被淘汰 */
    result = inode_cache_get(&cache, 2U, NULL);
    TEST_ASSERT_NULL(result);

    /* ino=3 应存在 */
    result = inode_cache_get(&cache, 3U, NULL);
    TEST_ASSERT_NOT_NULL(result);

    /* ino=100 应存在（新加入的） */
    result = inode_cache_get(&cache, 100U, NULL);
    TEST_ASSERT_NOT_NULL(result);
}

/**
 * @brief 测试13: 命中率计算
 */
static void test_cache_hit_rate(void)
{
    inode_cache_t cache;
    fs_inode_t inode;
    inode_cache_stats_t stats;

    printf("\n--- test_cache_hit_rate ---\n");

    inode_cache_init(&cache);

    /* 放入 1 个 inode */
    inode = make_inode(1U, 100U, 0644U);
    (void)inode_cache_put(&cache, &inode);

    /* 命中 8 次 */
    for (uint32_t i = 0U; i < 8U; i++)
    {
        (void)inode_cache_get(&cache, 1U, NULL);
    }

    /* 未命中 2 次 */
    (void)inode_cache_get(&cache, 99U, NULL);
    (void)inode_cache_get(&cache, 98U, NULL);

    stats = inode_cache_get_stats(&cache);
    TEST_ASSERT_EQ((int64_t)stats.hit_count, 8LL);
    TEST_ASSERT_EQ((int64_t)stats.miss_count, 2LL);

    /* 命中率 = 8/10 = 80% */
    {
        uint32_t total = stats.hit_count + stats.miss_count;
        uint32_t rate = (stats.hit_count * 100U) / total;
        TEST_ASSERT_EQ((int64_t)rate, 80LL);
    }
}

/**
 * @brief 测试14: flush 操作
 */
static void test_cache_flush(void)
{
    inode_cache_t cache;

    printf("\n--- test_cache_flush ---\n");

    inode_cache_init(&cache);

    /* flush 应成功 */
    TEST_ASSERT_EQ((int64_t)inode_cache_flush(&cache), 0LL);
}

/**
 * @brief 测试15: 满缓存压力测试
 */
static void test_cache_full_pressure(void)
{
    inode_cache_t cache;
    fs_inode_t inode;
    fs_inode_t *result;
    uint32_t i;

    printf("\n--- test_cache_full_pressure ---\n");

    inode_cache_init(&cache);

    /* 连续写入 64 个 inode（缓存只有 32 个槽位） */
    for (i = 0U; i < 64U; i++)
    {
        inode = make_inode(i + 1U, i, 0644U);
        (void)inode_cache_put(&cache, &inode);
    }

    /* 应只有最后 32 个在缓存中（ino=33..64） */
    /* ino=1 应被淘汰 */
    result = inode_cache_get(&cache, 1U, NULL);
    TEST_ASSERT_NULL(result);

    /* ino=33 应存在 */
    result = inode_cache_get(&cache, 33U, NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQ((int64_t)result->ino, 33LL);

    /* ino=64 应存在 */
    result = inode_cache_get(&cache, 64U, NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQ((int64_t)result->ino, 64LL);

    /* 淘汰次数 = 64 - 32 = 32 */
    TEST_ASSERT_EQ((int64_t)cache.stats.evict_count, 32LL);
}

/**
 * @brief 测试16: 打印统计（不崩溃即可）
 */
static void test_cache_print_stats(void)
{
    inode_cache_t cache;
    fs_inode_t inode;

    printf("\n--- test_cache_print_stats ---\n");

    inode_cache_init(&cache);

    inode = make_inode(1U, 100U, 0644U);
    (void)inode_cache_put(&cache, &inode);
    (void)inode_cache_get(&cache, 1U, NULL);

    /* 应正常打印，不崩溃 */
    inode_cache_print_stats(&cache);

    TEST_ASSERT_TRUE(true);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int32_t main(void)
{
    printf("========================================\n");
    printf("  inode 缓存单元测试\n");
    printf("========================================\n");

    test_cache_init();
    test_cache_null_safety();
    test_cache_hit();
    test_cache_miss();
    test_cache_miss_with_fallback();
    test_cache_lru_eviction();
    test_cache_invalidate();
    test_cache_invalidate_nonexistent();
    test_cache_clear();
    test_cache_update();
    test_cache_access_count();
    test_cache_lru_order();
    test_cache_hit_rate();
    test_cache_flush();
    test_cache_full_pressure();
    test_cache_print_stats();

    printf("\n========================================\n");
    printf("  测试结果: %u/%u 通过", s_passed, s_total);
    if (s_failed > 0U)
    {
        printf(", %u 失败", s_failed);
    }
    printf("\n========================================\n");

    return (s_failed == 0U) ? 0 : 1;
}
