/**
 * @file    test_ext4_txn.c
 * @brief   EXT4 事务管理引擎单元测试
 * @author  AISafe64 Team
 * @date    2026-05-11
 * @version 1.0
 *
 * @details EXT4 事务管理引擎测试：
 *          - 事务状态机测试
 *          - 事务超时测试
 *          - 事务冲突检测测试
 *          - 事务清理测试
 *
 * @note MISRA-C:2012 合规
 * @note TDD: RED 阶段 - 测试先行
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "services/fs/fs_ext4/ext4_txn.h"
#include "services/fs/fs_ext4/ext4_journal.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

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
 * @brief 测试事务初始化
 */
static int test_txn_init(void)
{
    int32_t ret;

    /* 初始化 Journal */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    /* 初始化事务管理引擎 */
    ret = ext4_txn_init();
    TEST_ASSERT(ret == 0);

    /* 验证事务引擎已初始化 */
    TEST_ASSERT(ext4_txn_is_initialized());

    /* 清理 */
    ext4_txn_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试事务状态机
 */
static int test_txn_state_machine(void)
{
    int32_t ret;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_txn_init();
    TEST_ASSERT(ret == 0);

    /* 创建事务 */
    ext4_txn_t *txn = ext4_txn_begin();
    TEST_ASSERT(txn != NULL);

    /* 验证初始状态 */
    TEST_ASSERT(txn->state == EXT4_txn_ACTIVE);

    /* 提交事务 */
    ret = ext4_txn_commit(txn);
    TEST_ASSERT(ret == 0);

    /* 验证提交后状态 */
    TEST_ASSERT(txn->state == EXT4_txn_COMMITTED);

    /* 回滚事务 */
    ret = ext4_txn_rollback(txn);
    TEST_ASSERT(ret == 0);

    /* 验证回滚后状态 */
    TEST_ASSERT(txn->state == EXT4_txn_ROLLED_BACK);

    /* 清理 */
    ext4_txn_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试事务超时检测
 */
static int test_txn_timeout(void)
{
    int32_t ret;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_txn_init();
    TEST_ASSERT(ret == 0);

    /* 创建事务 */
    ext4_txn_t *txn = ext4_txn_begin();
    TEST_ASSERT(txn != NULL);

    /* 验证初始状态 */
    TEST_ASSERT(txn->state == EXT4_txn_ACTIVE);

    /* 设置超时时间 */
    ext4_txn_set_timeout(txn, 100);  /* 100ms */

    /* 模拟超时（等待超过超时时间） */
    usleep(150000);  /* 150ms */

    /* 验证事务已超时 */
    TEST_ASSERT(txn->state == EXT4_txn_TIMEOUT);

    /* 清理 */
    ext4_txn_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试事务冲突检测
 */
static int test_txn_conflict_detection(void)
{
    int32_t ret;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_txn_init();
    TEST_ASSERT(ret == 0);

    /* 创建第一个事务 */
    ext4_txn_t *txn1 = ext4_txn_begin();
    TEST_ASSERT(txn1 != NULL);

    /* 验证第一个事务可以开始 */
    TEST_ASSERT(txn1->state == EXT4_txn_ACTIVE);

    /* 创建第二个事务（应该与第一个冲突）*/
    ext4_txn_t *txn2 = ext4_txn_begin();
    TEST_ASSERT(txn2 != NULL);

    /* 验证第二个事务被检测为冲突 */
    TEST_ASSERT(txn2->state == EXT4_txn_CONFLICT);

    /* 清理 */
    ext4_txn_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试事务资源管理
 */
static int test_txn_resource_management(void)
{
    int32_t ret;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_txn_init();
    TEST_ASSERT(ret == 0);

    /* 创建多个事务 */
    ext4_txn_t *txn1 = ext4_txn_begin();
    TEST_ASSERT(txn1 != NULL);

    ext4_txn_t *txn2 = ext4_txn_begin();
    TEST_ASSERT(txn2 != NULL);

    /* 验证资源分配 */
    TEST_ASSERT(txn1->id == 1U);
    TEST_ASSERT(txn2->id == 2U);

    /* 提交事务1 */
    ret = ext4_txn_commit(txn1);
    TEST_ASSERT(ret == 0);

    /* 回滚事务2 */
    ret = ext4_txn_rollback(txn2);
    TEST_ASSERT(ret == 0);

    /* 清理 */
    ext4_txn_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试事务统计
 */
static int test_txn_statistics(void)
{
    int32_t ret;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_txn_init();
    TEST_ASSERT(ret == 0);

    /* 创建和提交事务 */
    ext4_txn_t *txn1 = ext4_txn_begin();
    TEST_ASSERT(txn1 != NULL);
    ret = ext4_txn_commit(txn1);
    TEST_ASSERT(ret == 0);

    ext4_txn_t *txn2 = ext4_txn_begin();
    TEST_ASSERT(txn2 != NULL);
    ret = ext4_txn_commit(txn2);
    TEST_ASSERT(ret == 0);

    /* 回滚事务 */
    ext4_txn_t *txn3 = ext4_txn_begin();
    TEST_ASSERT(txn3 != NULL);
    ret = ext4_txn_rollback(txn3);
    TEST_ASSERT(ret == 0);

    /* 获取统计信息 */
    ext4_txn_stats_t stats;
    ret = ext4_txn_get_stats(&stats);
    TEST_ASSERT(ret == 0);

    /* 验证统计信息 */
    TEST_ASSERT(stats.active_count == 0U);
    TEST_ASSERT(stats.committed_count >= 2U);
    TEST_ASSERT(stats.rolled_back_count >= 1U);

    /* 清理 */
    ext4_txn_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试事务清理
 */
static int test_txn_cleanup(void)
{
    int32_t ret;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_txn_init();
    TEST_ASSERT(ret == 0);

    /* 创建并清理事务 */
    ext4_txn_t *txn = ext4_txn_begin();
    TEST_ASSERT(txn != NULL);

    ret = ext4_txn_commit(txn);
    TEST_ASSERT(ret == 0);

    /* 清理已提交的事务 */
    ret = ext4_txn_cleanup();
    TEST_ASSERT(ret == 0);

    /* 验证已清理 */
    TEST_ASSERT(txn->state == EXT4_txn_CLEARED);

    /* 清理 */
    ext4_txn_destroy();
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

    ret = ext4_txn_init();
    TEST_ASSERT(ret == 0);

    /* 测试 NULL 参数 */
    ret = ext4_txn_commit(NULL);
    TEST_ASSERT(ret < 0);  /* 应该失败 */

    ret = ext4_txn_rollback(NULL);
    TEST_ASSERT(ret < 0);  /* 应该失败 */

    ret = ext4_txn_get_stats(NULL);
    TEST_ASSERT(ret < 0);  /* 应该失败 */

    /* 清理 */
    ext4_txn_destroy();
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
    printf("EXT4 事务管理引擎测试套件\n");
    printf("========================================\n\n");

    /* 事务初始化测试 */
    printf("【事务初始化测试】\n");
    if (test_txn_init() == 0) { passed++; } else { failed++; }
    printf("\n");

    /* 事务状态机测试 */
    printf("【事务状态机测试】\n");
    if (test_txn_state_machine() == 0) { passed++; } else { failed++; }
    printf("\n");

    /* 事务超时测试 */
    printf("【事务超时测试】\n");
    if (test_txn_timeout() == 0) { passed++; } else { failed++; }
    printf("\n");

    /* 事务冲突检测测试 */
    printf("【事务冲突检测测试】\n");
    if (test_txn_conflict_detection() == 0) { passed++; } else { failed++; }
    printf("\n");

    /* 事务资源管理测试 */
    printf("【事务资源管理测试】\n");
    if (test_txn_resource_management() == 0) { passed++; } else { failed++; }
    printf("\n");

    /* 事务统计测试 */
    printf("【事务统计测试】\n");
    if (test_txn_statistics() == 0) { passed++; } else { failed++; }
    printf("\n");

    /* 事务清理测试 */
    printf("【事务清理测试】\n");
    if (test_txn_cleanup() == 0) { passed++; } else { failed++; }
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
