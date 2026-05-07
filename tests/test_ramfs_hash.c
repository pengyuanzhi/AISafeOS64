/**
 * @file    test_ramfs_hash.c
 * @brief   ramfs_hash 哈希索引模块单元测试
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details ramfs_hash 哈希索引模块 TDD 测试
 *          RED 阶段 - 先写测试，验证哈希索引功能
 *
 *          测试覆盖：
 *          - 哈希表初始化
 *          - 哈希函数正确性
 *          - 插入和查找
 *          - 冲突处理（开放寻址）
 *          - 删除操作
 *          - 边界条件（空表、满表、NULL参数）
 *          - 性能对比（线性 vs 哈希）
 *
 * @note MISRA-C:2012 合规
 * @note TDD: RED 阶段
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

/* ========================================================================
 * 简易测试框架（内联版）
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
            printf("  FAIL %s:%u 期望 NULL, 实际 %p\n",                    \
                   __FILE__, __LINE__, (void *)(p));                        \
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
            printf("  FAIL %s:%u 期望非 NULL\n",                           \
                   __FILE__, __LINE__);                                     \
        }                                                                  \
    } while (0)

#define TEST_RUN(name)                                                     \
    do                                                                     \
    {                                                                      \
        printf("  [RUN] %s\n", #name);                                     \
        test_##name();                                                     \
    } while (0)

/* ========================================================================
 * ramfs_hash 模块接口（测试目标）
 * ======================================================================== */

#include "ramfs_hash.h"

/* ========================================================================
 * 测试用例 - RED 阶段
 * ======================================================================== */

/**
 * @brief 测试哈希表初始化
 *
 * 验证 ramfs_hash_init 将哈希表重置为空状态
 */
void test_hash_init(void)
{
    ramfs_hash_table_t table;
    int32_t ret;

    ret = ramfs_hash_init(&table);
    TEST_ASSERT_EQ(ret, 0);

    /* 所有槽位应为空 */
    TEST_ASSERT_EQ(ramfs_hash_count(&table), 0U);
}

/**
 * @brief 测试哈希函数一致性
 *
 * 验证相同输入产生相同输出
 */
void test_hash_function_consistency(void)
{
    uint32_t hash1;
    uint32_t hash2;
    uint32_t hash3;

    hash1 = ramfs_hash_djb2("test.txt");
    hash2 = ramfs_hash_djb2("test.txt");
    hash3 = ramfs_hash_djb2("other.txt");

    TEST_ASSERT_EQ(hash1, hash2);
    TEST_ASSERT_TRUE(hash1 != hash3);
}

/**
 * @brief 测试哈希函数返回值在有效范围
 *
 * 验证哈希值 % table_size 在 [0, table_size) 范围内
 */
void test_hash_function_range(void)
{
    uint32_t hash_val;
    uint32_t idx;

    hash_val = ramfs_hash_djb2("");
    idx = hash_val % RAMFS_HASH_SIZE;
    TEST_ASSERT_TRUE(idx < RAMFS_HASH_SIZE);

    hash_val = ramfs_hash_djb2("a");
    idx = hash_val % RAMFS_HASH_SIZE;
    TEST_ASSERT_TRUE(idx < RAMFS_HASH_SIZE);

    hash_val = ramfs_hash_djb2("/very/long/path/to/some/file.txt");
    idx = hash_val % RAMFS_HASH_SIZE;
    TEST_ASSERT_TRUE(idx < RAMFS_HASH_SIZE);
}

/**
 * @brief 测试空字符串哈希
 */
void test_hash_empty_string(void)
{
    uint32_t hash_val;

    hash_val = ramfs_hash_djb2("");
    /* 空字符串 djb2 应返回 5381（初始种子值） */
    TEST_ASSERT_EQ(hash_val, 5381U);
}

/**
 * @brief 测试插入和查找
 *
 * 验证插入文件后可以按名称找到
 */
void test_hash_insert_and_lookup(void)
{
    ramfs_hash_table_t table;
    int32_t ret;
    ramfs_file_ref_t ref;
    ramfs_file_ref_t *found;

    (void)ramfs_hash_init(&table);

    ref.ino = 1U;
    ref.file_index = 0U;
    (void)strncpy(ref.name, "hello.txt", RAMFS_HASH_NAME_MAX - 1U);
    ref.name[RAMFS_HASH_NAME_MAX - 1U] = '\0';

    ret = ramfs_hash_insert(&table, &ref);
    TEST_ASSERT_EQ(ret, 0);

    found = ramfs_hash_lookup(&table, "hello.txt");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQ(found->ino, 1U);
    TEST_ASSERT_EQ(found->file_index, 0U);
}

/**
 * @brief 测试查找不存在的文件
 */
void test_hash_lookup_not_found(void)
{
    ramfs_hash_table_t table;
    ramfs_file_ref_t *found;

    (void)ramfs_hash_init(&table);

    found = ramfs_hash_lookup(&table, "nonexistent.txt");
    TEST_ASSERT_NULL(found);
}

/**
 * @brief 测试删除
 *
 * 验证删除后查找返回 NULL
 */
void test_hash_remove(void)
{
    ramfs_hash_table_t table;
    ramfs_file_ref_t ref;
    ramfs_file_ref_t *found;
    int32_t ret;

    (void)ramfs_hash_init(&table);

    ref.ino = 1U;
    ref.file_index = 0U;
    (void)strncpy(ref.name, "remove_me.txt", RAMFS_HASH_NAME_MAX - 1U);
    ref.name[RAMFS_HASH_NAME_MAX - 1U] = '\0';

    ret = ramfs_hash_insert(&table, &ref);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQ(ramfs_hash_count(&table), 1U);

    ret = ramfs_hash_remove(&table, "remove_me.txt");
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQ(ramfs_hash_count(&table), 0U);

    found = ramfs_hash_lookup(&table, "remove_me.txt");
    TEST_ASSERT_NULL(found);
}

/**
 * @brief 测试删除不存在的文件
 */
void test_hash_remove_not_found(void)
{
    ramfs_hash_table_t table;
    int32_t ret;

    (void)ramfs_hash_init(&table);

    ret = ramfs_hash_remove(&table, "ghost.txt");
    TEST_ASSERT_TRUE(ret < 0);
}

/**
 * @brief 测试重复插入
 *
 * 验证相同文件名的重复插入返回错误
 */
void test_hash_insert_duplicate(void)
{
    ramfs_hash_table_t table;
    ramfs_file_ref_t ref;
    int32_t ret;

    (void)ramfs_hash_init(&table);

    ref.ino = 1U;
    ref.file_index = 0U;
    (void)strncpy(ref.name, "dup.txt", RAMFS_HASH_NAME_MAX - 1U);
    ref.name[RAMFS_HASH_NAME_MAX - 1U] = '\0';

    ret = ramfs_hash_insert(&table, &ref);
    TEST_ASSERT_EQ(ret, 0);

    /* 再次插入同名文件应失败 */
    ref.ino = 2U;
    ref.file_index = 1U;
    ret = ramfs_hash_insert(&table, &ref);
    TEST_ASSERT_TRUE(ret < 0);
}

/**
 * @brief 测试冲突处理（开放寻址）
 *
 * 插入多个可能哈希到相同桶的文件，验证都能正确查找
 */
void test_hash_collision_resolution(void)
{
    ramfs_hash_table_t table;
    ramfs_file_ref_t ref;
    ramfs_file_ref_t *found;
    uint32_t i;
    int32_t ret;
    char name[RAMFS_HASH_NAME_MAX];

    (void)ramfs_hash_init(&table);

    /* 插入多个文件，依赖开放寻址处理冲突 */
    for (i = 0U; i < 20U; i++)
    {
        (void)snprintf(name, sizeof(name), "file_%u.bin", i);
        ref.ino = i + 1U;
        ref.file_index = i;
        (void)strncpy(ref.name, name, RAMFS_HASH_NAME_MAX - 1U);
        ref.name[RAMFS_HASH_NAME_MAX - 1U] = '\0';

        ret = ramfs_hash_insert(&table, &ref);
        TEST_ASSERT_EQ(ret, 0);
    }

    /* 验证每个文件都能找到 */
    for (i = 0U; i < 20U; i++)
    {
        (void)snprintf(name, sizeof(name), "file_%u.bin", i);
        found = ramfs_hash_lookup(&table, name);
        TEST_ASSERT_NOT_NULL(found);
        TEST_ASSERT_EQ(found->ino, i + 1U);
        TEST_ASSERT_EQ(found->file_index, i);
    }

    TEST_ASSERT_EQ(ramfs_hash_count(&table), 20U);
}

/**
 * @brief 测试表满时插入失败
 */
void test_hash_table_full(void)
{
    ramfs_hash_table_t table;
    ramfs_file_ref_t ref;
    uint32_t i;
    int32_t ret;
    char name[RAMFS_HASH_NAME_MAX];

    (void)ramfs_hash_init(&table);

    /* 填满哈希表 */
    for (i = 0U; i < RAMFS_HASH_SIZE; i++)
    {
        (void)snprintf(name, sizeof(name), "full_%u.dat", i);
        ref.ino = i + 1U;
        ref.file_index = i;
        (void)strncpy(ref.name, name, RAMFS_HASH_NAME_MAX - 1U);
        ref.name[RAMFS_HASH_NAME_MAX - 1U] = '\0';

        ret = ramfs_hash_insert(&table, &ref);
        TEST_ASSERT_EQ(ret, 0);
    }

    TEST_ASSERT_EQ(ramfs_hash_count(&table), RAMFS_HASH_SIZE);

    /* 再插入应失败 */
    ref.ino = 999U;
    ref.file_index = 999U;
    (void)strncpy(ref.name, "overflow.dat", RAMFS_HASH_NAME_MAX - 1U);
    ref.name[RAMFS_HASH_NAME_MAX - 1U] = '\0';

    ret = ramfs_hash_insert(&table, &ref);
    TEST_ASSERT_TRUE(ret < 0);
}

/**
 * @brief 测试 NULL 参数安全
 */
void test_hash_null_params(void)
{
    ramfs_hash_table_t table;
    ramfs_file_ref_t ref;
    int32_t ret;

    /* NULL 表指针 */
    ret = ramfs_hash_init(NULL);
    TEST_ASSERT_TRUE(ret < 0);

    ret = ramfs_hash_insert(NULL, &ref);
    TEST_ASSERT_TRUE(ret < 0);

    ret = ramfs_hash_remove(NULL, "test");
    TEST_ASSERT_TRUE(ret < 0);

    /* NULL 文件名 */
    (void)ramfs_hash_init(&table);
    TEST_ASSERT_NULL(ramfs_hash_lookup(&table, NULL));

    ret = ramfs_hash_remove(&table, NULL);
    TEST_ASSERT_TRUE(ret < 0);

    /* NULL ref */
    ret = ramfs_hash_insert(&table, NULL);
    TEST_ASSERT_TRUE(ret < 0);

    /* NULL name in ref */
    (void)memset(&ref, 0, sizeof(ref));
    ret = ramfs_hash_insert(&table, &ref);
    TEST_ASSERT_TRUE(ret < 0);
}

/**
 * @brief 测试删除后重新插入（验证槽位复用）
 */
void test_hash_reuse_after_remove(void)
{
    ramfs_hash_table_t table;
    ramfs_file_ref_t ref;
    ramfs_file_ref_t *found;
    int32_t ret;

    (void)ramfs_hash_init(&table);

    /* 插入 */
    ref.ino = 1U;
    ref.file_index = 0U;
    (void)strncpy(ref.name, "reuse.txt", RAMFS_HASH_NAME_MAX - 1U);
    ref.name[RAMFS_HASH_NAME_MAX - 1U] = '\0';

    ret = ramfs_hash_insert(&table, &ref);
    TEST_ASSERT_EQ(ret, 0);

    /* 删除 */
    ret = ramfs_hash_remove(&table, "reuse.txt");
    TEST_ASSERT_EQ(ret, 0);

    /* 重新插入 */
    ref.ino = 2U;
    ref.file_index = 5U;
    ret = ramfs_hash_insert(&table, &ref);
    TEST_ASSERT_EQ(ret, 0);

    found = ramfs_hash_lookup(&table, "reuse.txt");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQ(found->ino, 2U);
    TEST_ASSERT_EQ(found->file_index, 5U);
}

/**
 * @brief 测试大量插入后的查找正确性
 */
void test_hash_large_scale(void)
{
    ramfs_hash_table_t table;
    ramfs_file_ref_t ref;
    ramfs_file_ref_t *found;
    uint32_t i;
    int32_t ret;
    char name[RAMFS_HASH_NAME_MAX];

    (void)ramfs_hash_init(&table);

    /* 插入 100 个文件 */
    for (i = 0U; i < 100U; i++)
    {
        (void)snprintf(name, sizeof(name), "/data/log_%04u.txt", i);
        ref.ino = i + 1U;
        ref.file_index = i;
        (void)strncpy(ref.name, name, RAMFS_HASH_NAME_MAX - 1U);
        ref.name[RAMFS_HASH_NAME_MAX - 1U] = '\0';

        ret = ramfs_hash_insert(&table, &ref);
        TEST_ASSERT_EQ(ret, 0);
    }

    TEST_ASSERT_EQ(ramfs_hash_count(&table), 100U);

    /* 查找偶数编号文件 */
    for (i = 0U; i < 100U; i += 2U)
    {
        (void)snprintf(name, sizeof(name), "/data/log_%04u.txt", i);
        found = ramfs_hash_lookup(&table, name);
        TEST_ASSERT_NOT_NULL(found);
        TEST_ASSERT_EQ(found->ino, i + 1U);
    }

    /* 删除一半文件 */
    for (i = 0U; i < 100U; i += 2U)
    {
        (void)snprintf(name, sizeof(name), "/data/log_%04u.txt", i);
        ret = ramfs_hash_remove(&table, name);
        TEST_ASSERT_EQ(ret, 0);
    }

    TEST_ASSERT_EQ(ramfs_hash_count(&table), 50U);

    /* 验证奇数编号文件仍然存在 */
    for (i = 1U; i < 100U; i += 2U)
    {
        (void)snprintf(name, sizeof(name), "/data/log_%04u.txt", i);
        found = ramfs_hash_lookup(&table, name);
        TEST_ASSERT_NOT_NULL(found);
        TEST_ASSERT_EQ(found->ino, i + 1U);
    }

    /* 验证偶数编号文件已被删除 */
    for (i = 0U; i < 100U; i += 2U)
    {
        (void)snprintf(name, sizeof(name), "/data/log_%04u.txt", i);
        found = ramfs_hash_lookup(&table, name);
        TEST_ASSERT_NULL(found);
    }
}

/**
 * @brief 测试更新操作
 *
 * 验证可以更新已存在条目的 ino 和 file_index
 */
void test_hash_update(void)
{
    ramfs_hash_table_t table;
    ramfs_file_ref_t ref;
    ramfs_file_ref_t *found;
    int32_t ret;

    (void)ramfs_hash_init(&table);

    /* 插入 */
    ref.ino = 1U;
    ref.file_index = 0U;
    (void)strncpy(ref.name, "update.txt", RAMFS_HASH_NAME_MAX - 1U);
    ref.name[RAMFS_HASH_NAME_MAX - 1U] = '\0';

    ret = ramfs_hash_insert(&table, &ref);
    TEST_ASSERT_EQ(ret, 0);

    /* 更新 */
    ref.ino = 99U;
    ref.file_index = 42U;
    ret = ramfs_hash_update(&table, &ref);
    TEST_ASSERT_EQ(ret, 0);

    found = ramfs_hash_lookup(&table, "update.txt");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQ(found->ino, 99U);
    TEST_ASSERT_EQ(found->file_index, 42U);
}

/* ========================================================================
 * 性能对比测试
 * ======================================================================== */

/**
 * @brief 简单计时辅助（使用 clock）
 */
static uint64_t get_time_ns(void)
{
    return (uint64_t)clock();
}

/**
 * @brief 线性搜索模拟（O(n)）
 */
static int32_t linear_search(char names[][RAMFS_HASH_NAME_MAX],
                              uint32_t count, const char *target)
{
    uint32_t i;

    for (i = 0U; i < count; i++)
    {
        if (strcmp(names[i], target) == 0)
        {
            return (int32_t)i;
        }
    }

    return -1;
}

/**
 * @brief 性能对比：线性搜索 vs 哈希查找
 */
void test_perf_hash_vs_linear(void)
{
    ramfs_hash_table_t table;
    ramfs_file_ref_t ref;
    char names[100][RAMFS_HASH_NAME_MAX];
    char target[RAMFS_HASH_NAME_MAX];
    uint64_t t_start;
    uint64_t t_linear;
    uint64_t t_hash;
    uint32_t i;
    uint32_t iterations;
    int32_t linear_result;
    ramfs_file_ref_t *hash_result;

    (void)ramfs_hash_init(&table);

    /* 准备数据：插入 100 个文件 */
    for (i = 0U; i < 100U; i++)
    {
        (void)snprintf(names[i], RAMFS_HASH_NAME_MAX, "/app/config/module_%02u.cfg", i);
        ref.ino = i + 1U;
        ref.file_index = i;
        (void)strncpy(ref.name, names[i], RAMFS_HASH_NAME_MAX - 1U);
        ref.name[RAMFS_HASH_NAME_MAX - 1U] = '\0';

        (void)ramfs_hash_insert(&table, &ref);
    }

    /* 目标：查找第 90 个文件（靠后位置，线性搜索更慢） */
    (void)strncpy(target, names[90], RAMFS_HASH_NAME_MAX - 1U);
    target[RAMFS_HASH_NAME_MAX - 1U] = '\0';

    /* 线性搜索计时 */
    iterations = 10000U;
    t_start = get_time_ns();
    for (i = 0U; i < iterations; i++)
    {
        linear_result = linear_search(names, 100U, target);
        (void)linear_result;
    }
    t_linear = get_time_ns() - t_start;

    /* 哈希查找计时 */
    t_start = get_time_ns();
    for (i = 0U; i < iterations; i++)
    {
        hash_result = ramfs_hash_lookup(&table, target);
        (void)hash_result;
    }
    t_hash = get_time_ns() - t_start;

    printf("    线性搜索: %llu ticks\n", (unsigned long long)t_linear);
    printf("    哈希查找: %llu ticks\n", (unsigned long long)t_hash);

    /* 哈希查找应该更快 */
    TEST_ASSERT_TRUE(t_hash < t_linear);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n");
    printf("============================================\n");
    printf("  ramfs_hash 哈希索引 TDD 测试 (RED)\n");
    printf("============================================\n");
    printf("\n");

    printf("=== 哈希表初始化 ===\n");
    TEST_RUN(hash_init);

    printf("\n=== 哈希函数 ===\n");
    TEST_RUN(hash_function_consistency);
    TEST_RUN(hash_function_range);
    TEST_RUN(hash_empty_string);

    printf("\n=== 插入和查找 ===\n");
    TEST_RUN(hash_insert_and_lookup);
    TEST_RUN(hash_lookup_not_found);

    printf("\n=== 删除操作 ===\n");
    TEST_RUN(hash_remove);
    TEST_RUN(hash_remove_not_found);

    printf("\n=== 冲突和边界 ===\n");
    TEST_RUN(hash_insert_duplicate);
    TEST_RUN(hash_collision_resolution);
    TEST_RUN(hash_table_full);

    printf("\n=== NULL 安全 ===\n");
    TEST_RUN(hash_null_params);

    printf("\n=== 高级操作 ===\n");
    TEST_RUN(hash_reuse_after_remove);
    TEST_RUN(hash_large_scale);
    TEST_RUN(hash_update);

    printf("\n=== 性能对比 ===\n");
    TEST_RUN(perf_hash_vs_linear);

    printf("\n");
    printf("============================================\n");
    printf("  测试总结\n");
    printf("============================================\n");
    printf("  总测试数: %u\n", s_total);
    printf("  通过: %u\n", s_passed);
    printf("  失败: %u\n", s_failed);
    printf("============================================\n");
    printf("\n");

    return (s_failed > 0U) ? 1 : 0;
}
