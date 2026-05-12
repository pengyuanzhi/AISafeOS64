/**
 * @file    test_ext4_cow.c
 * @brief   EXT4 写时复制（CoW）单元测试
 * @author  AISafe64 Team
 * @date    2026-05-11
 * @version 1.0
 *
 * @details EXT4 写时复制（CoW）测试：
 *          - CoW 块引用计数测试
 *          - 快照创建测试
 *          - 快照回滚测试
 *          - 快照清理测试
 *
 * @note MISRA-C:2012 合规
 * @note TDD: RED 阶段 - 测试先行
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "services/fs/fs_ext4/ext4_cow.h"
#include "services/fs/fs_ext4/ext4_journal.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* ========================================================================
 * 测试辅助宏
 * ======================================================================== */

/** @brief 测试断言 */
#define TEST_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            printf("❌ FAILED: %s:%d - %s\n", __FILE__, __LINE__, #condition); \
            return -1; \
        } \
    } while (0)

/** @brief 测试成功标记 */
#define TEST_PASS() \
    do { \
        printf("✅ PASSED: %s\n", __func__); \
        return 0; \
    } while (0)

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试 CoW 块引用计数初始化
 */
static int test_cow_refcount_init(void)
{
    int32_t ret;

    /* 初始化 Journal */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    /* 初始化 CoW 模块 */
    ret = ext4_cow_init();
    TEST_ASSERT(ret == 0);

    /* 验证引用计数为 0 */
    uint32_t refcount = ext4_cow_get_refcount(1U);
    TEST_ASSERT(refcount == 0U);

    /* 清理 */
    ext4_cow_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试 CoW 块引用计数递增
 */
static int test_cow_refcount_increment(void)
{
    int32_t ret;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_cow_init();
    TEST_ASSERT(ret == 0);

    /* 递增引用计数 */
    ret = ext4_cow_refcount_inc(1U);
    TEST_ASSERT(ret == 0);

    uint32_t refcount = ext4_cow_get_refcount(1U);
    TEST_ASSERT(refcount == 1U);

    /* 再次递增 */
    ret = ext4_cow_refcount_inc(1U);
    TEST_ASSERT(ret == 0);

    refcount = ext4_cow_get_refcount(1U);
    TEST_ASSERT(refcount == 2U);

    /* 清理 */
    ext4_cow_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试 CoW 块引用计数递减
 */
static int test_cow_refcount_decrement(void)
{
    int32_t ret;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_cow_init();
    TEST_ASSERT(ret == 0);

    /* 递增引用计数 */
    ret = ext4_cow_refcount_inc(1U);
    TEST_ASSERT(ret == 0);

    uint32_t refcount = ext4_cow_get_refcount(1U);
    TEST_ASSERT(refcount == 1U);

    /* 递减引用计数 */
    ret = ext4_cow_refcount_dec(1U);
    TEST_ASSERT(ret == 0);

    refcount = ext4_cow_get_refcount(1U);
    TEST_ASSERT(refcount == 0U);

    /* 清理 */
    ext4_cow_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试快照创建
 */
static int test_snapshot_create(void)
{
    int32_t ret;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_cow_init();
    TEST_ASSERT(ret == 0);

    /* 创建快照 */
    uint32_t snapshot_id = ext4_cow_snapshot_create("test_snapshot");
    TEST_ASSERT(snapshot_id > 0U);

    /* 验证快照 ID */
    TEST_ASSERT(snapshot_id == 1U);

    /* 验证引用计数增加 */
    uint32_t refcount = ext4_cow_get_refcount(snapshot_id);
    TEST_ASSERT(refcount == 1U);

    /* 清理 */
    ext4_cow_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试快照回滚
 */
static int test_snapshot_rollback(void)
{
    int32_t ret;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_cow_init();
    TEST_ASSERT(ret == 0);

    /* 创建快照 */
    uint32_t snapshot_id = ext4_cow_snapshot_create("test_snapshot");
    TEST_ASSERT(snapshot_id > 0U);

    /* 回滚到快照 */
    ret = ext4_cow_snapshot_rollback(snapshot_id);
    TEST_ASSERT(ret == 0);

    /* 验证引用计数 */
    uint32_t refcount = ext4_cow_get_refcount(snapshot_id);
    TEST_ASSERT(refcount == 0U);

    /* 清理 */
    ext4_cow_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试快照清理
 */
static int test_snapshot_cleanup(void)
{
    int32_t ret;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_cow_init();
    TEST_ASSERT(ret == 0);

    /* 创建多个快照 */
    uint32_t snapshot1 = ext4_cow_snapshot_create("snapshot1");
    uint32_t snapshot2 = ext4_cow_snapshot_create("snapshot2");
    uint32_t snapshot3 = ext4_cow_snapshot_create("snapshot3");

    TEST_ASSERT(snapshot1 > 0U);
    TEST_ASSERT(snapshot2 > 0U);
    TEST_ASSERT(snapshot3 > 0U);

    /* 创建快照时引用计数已增加 */
    TEST_ASSERT(ext4_cow_get_refcount(snapshot1) == 1U);
    TEST_ASSERT(ext4_cow_get_refcount(snapshot2) == 1U);
    TEST_ASSERT(ext4_cow_get_refcount(snapshot3) == 1U);

    /* 清理快照 */
    ret = ext4_cow_snapshot_cleanup(snapshot3);
    TEST_ASSERT(ret == 0);

    /* 验证快照 3 已清理 */
    uint32_t refcount = ext4_cow_get_refcount(snapshot3);
    TEST_ASSERT(refcount == 0U);

    /* 清理 */
    ext4_cow_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试快照管理
 */
static int test_snapshot_management(void)
{
    int32_t ret;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_cow_init();
    TEST_ASSERT(ret == 0);

    /* 创建快照 */
    uint32_t snapshot1 = ext4_cow_snapshot_create("snapshot1");
    uint32_t snapshot2 = ext4_cow_snapshot_create("snapshot2");
    uint32_t snapshot3 = ext4_cow_snapshot_create("snapshot3");

    TEST_ASSERT(snapshot1 > 0U);
    TEST_ASSERT(snapshot2 > 0U);
    TEST_ASSERT(snapshot3 > 0U);

    /* 验证快照 ID 连续 */
    TEST_ASSERT(snapshot2 == snapshot1 + 1U);
    TEST_ASSERT(snapshot3 == snapshot2 + 1U);

    /* 清理所有快照 */
    ret = ext4_cow_snapshot_cleanup(snapshot3);
    ret |= ext4_cow_snapshot_cleanup(snapshot2);
    ret |= ext4_cow_snapshot_cleanup(snapshot1);

    TEST_ASSERT(ret == 0);

    /* 清理 */
    ext4_cow_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试参数验证
 */
static int test_parameter_validation(void)
{
    int32_t ret;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_cow_init();
    TEST_ASSERT(ret == 0);

    /* 测试无效的块 ID */
    ret = ext4_cow_refcount_inc(0U);
    TEST_ASSERT(ret < 0);  /* 应该失败：块 ID 为 0 */

    ret = ext4_cow_refcount_dec(0U);
    TEST_ASSERT(ret < 0);  /* 应该失败：块 ID 为 0 */

    uint32_t refcount = ext4_cow_get_refcount(0U);
    TEST_ASSERT(refcount == 0U);  /* 应该返回 0 */

    /* 测试无效的快照 ID */
    ret = ext4_cow_snapshot_cleanup(9999U);
    TEST_ASSERT(ret < 0);  /* 应该失败：快照 ID 不存在 */

    ret = ext4_cow_snapshot_rollback(9999U);
    TEST_ASSERT(ret < 0);  /* 应该失败：快照 ID 不存在 */

    /* 清理 */
    ext4_cow_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/* ========================================================================
 * 测试运行器
 * ======================================================================== */

/**
 * @brief 测试运行器
 */
int main(void)
{
    int32_t passed = 0;
    int32_t failed = 0;

    printf("========================================\n");
    printf("EXT4 写时复制（CoW）测试套件\n");
    printf("========================================\n\n");

    /* CoW 块引用计数测试 */
    printf("【CoW 块引用计数测试】\n");
    if (test_cow_refcount_init() == 0) { passed++; } else { failed++; }
    if (test_cow_refcount_increment() == 0) { passed++; } else { failed++; }
    if (test_cow_refcount_decrement() == 0) { passed++; } else { failed++; }
    printf("\n");

    /* 快照创建测试 */
    printf("【快照创建测试】\n");
    if (test_snapshot_create() == 0) { passed++; } else { failed++; }
    printf("\n");

    /* 快照回滚测试 */
    printf("【快照回滚测试】\n");
    if (test_snapshot_rollback() == 0) { passed++; } else { failed++; }
    printf("\n");

    /* 快照清理测试 */
    printf("【快照清理测试】\n");
    if (test_snapshot_cleanup() == 0) { passed++; } else { failed++; }
    printf("\n");

    /* 快照管理测试 */
    printf("【快照管理测试】\n");
    if (test_snapshot_management() == 0) { passed++; } else { failed++; }
    printf("\n");

    /* 参数验证测试 */
    printf("【参数验证测试】\n");
    if (test_parameter_validation() == 0) { passed++; } else { failed++; }
    printf("\n");

    /* 测试结果统计 */
    printf("========================================\n");
    printf("测试结果: %d 通过, %d 失败\n", passed, failed);
    printf("========================================\n");

    return (failed == 0) ? 0 : -1;
}
