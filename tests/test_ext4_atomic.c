/**
 * @file    test_ext4_atomic.c
 * @brief   EXT4 原子操作单元测试
 * @author  AISafe64 Team
 * @date    2026-05-11
 * @version 1.0
 *
 * @details EXT4 原子操作测试：
 *          - 文件级原子操作测试
 *          - 目录级原子操作测试
 *          - 并发原子性测试
 *
 * @note MISRA-C:2012 合规
 * @note TDD: RED 阶段 - 测试先行
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "services/fs/fs_ext4/ext4_atomic.h"
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
 * 全局变量
 * ======================================================================== */

/** @brief 测试挂载点 ID */
/* static uint32_t g_mount_id = 1U; */

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试原子创建文件
 */
static int test_atomic_create_file(void)
{
    int32_t ret;

    /* 初始化 Journal */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    /* 初始化原子操作 */
    ret = ext4_atomic_init();
    TEST_ASSERT(ret == 0);

    /* 测试参数验证 */
    ret = ext4_atomic_create_file(0U, NULL, 0644U, 0U, 0U);
    TEST_ASSERT(ret < 0);  /* 应该失败：路径为 NULL */

    ret = ext4_atomic_create_file(0U, "test.txt", 0644U, 0U, 0U);
    TEST_ASSERT(ret < 0);  /* 应该失败：父目录无效 */

    /* 测试正常创建文件 */
    ret = ext4_atomic_create_file(2U, "test.txt", 0644U, 1000U, 1000U);
    TEST_ASSERT(ret >= 0);  /* 应该成功：返回 inode */

    /* 验证 Journal 状态 */
    ext4_journal_state_t state = ext4_journal_get_state();
    TEST_ASSERT(state == EXT4_JOURNAL_CLEAN);

    /* 清理 */
    ext4_atomic_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试原子删除文件
 */
static int test_atomic_delete_file(void)
{
    int32_t ret;
    uint32_t inode;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_atomic_init();
    TEST_ASSERT(ret == 0);

    /* 先创建文件 */
    inode = (uint32_t)ext4_atomic_create_file(2U, "to_delete.txt", 0644U, 1000U, 1000U);
    TEST_ASSERT(inode > 0U);

    /* 测试参数验证 */
    ret = ext4_atomic_delete_file(0U, "to_delete.txt");
    TEST_ASSERT(ret < 0);  /* 应该失败：父目录无效 */

    ret = ext4_atomic_delete_file(2U, NULL);
    TEST_ASSERT(ret < 0);  /* 应该失败：文件名为 NULL */

    /* 测试正常删除文件 */
    ret = ext4_atomic_delete_file(2U, "to_delete.txt");
    TEST_ASSERT(ret == 0);  /* 应该成功 */

    /* 验证 Journal 状态 */
    ext4_journal_state_t state = ext4_journal_get_state();
    TEST_ASSERT(state == EXT4_JOURNAL_CLEAN);

    /* 清理 */
    ext4_atomic_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试原子重命名文件
 */
static int test_atomic_rename_file(void)
{
    int32_t ret;
    uint32_t inode;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_atomic_init();
    TEST_ASSERT(ret == 0);

    /* 先创建文件 */
    inode = (uint32_t)ext4_atomic_create_file(2U, "old_name.txt", 0644U, 1000U, 1000U);
    TEST_ASSERT(inode > 0U);

    /* 测试参数验证 */
    ret = ext4_atomic_rename_file(0U, "old_name.txt", 2U, "new_name.txt");
    TEST_ASSERT(ret < 0);  /* 应该失败：旧父目录无效 */

    ret = ext4_atomic_rename_file(2U, NULL, 2U, "new_name.txt");
    TEST_ASSERT(ret < 0);  /* 应该失败：旧文件名为 NULL */

    ret = ext4_atomic_rename_file(2U, "old_name.txt", 0U, "new_name.txt");
    TEST_ASSERT(ret < 0);  /* 应该失败：新父目录无效 */

    ret = ext4_atomic_rename_file(2U, "old_name.txt", 2U, NULL);
    TEST_ASSERT(ret < 0);  /* 应该失败：新文件名为 NULL */

    /* 测试正常重命名文件（同一目录） */
    ret = ext4_atomic_rename_file(2U, "old_name.txt", 2U, "new_name.txt");
    TEST_ASSERT(ret == 0);  /* 应该成功 */

    /* 验证 Journal 状态 */
    ext4_journal_state_t state = ext4_journal_get_state();
    TEST_ASSERT(state == EXT4_JOURNAL_CLEAN);

    /* 清理 */
    ext4_atomic_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试原子创建目录
 */
static int test_atomic_create_dir(void)
{
    int32_t ret;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_atomic_init();
    TEST_ASSERT(ret == 0);

    /* 测试参数验证 */
    ret = ext4_atomic_create_dir(0U, NULL, 0755U, 1000U, 1000U);
    TEST_ASSERT(ret < 0);  /* 应该失败：路径为 NULL */

    ret = ext4_atomic_create_dir(0U, "test_dir", 0755U, 1000U, 1000U);
    TEST_ASSERT(ret < 0);  /* 应该失败：父目录无效 */

    /* 测试正常创建目录 */
    ret = ext4_atomic_create_dir(2U, "test_dir", 0755U, 1000U, 1000U);
    TEST_ASSERT(ret >= 0);  /* 应该成功：返回 inode */

    /* 验证 Journal 状态 */
    ext4_journal_state_t state = ext4_journal_get_state();
    TEST_ASSERT(state == EXT4_JOURNAL_CLEAN);

    /* 清理 */
    ext4_atomic_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试原子删除目录
 */
static int test_atomic_delete_dir(void)
{
    int32_t ret;
    uint32_t inode;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_atomic_init();
    TEST_ASSERT(ret == 0);

    /* 先创建目录 */
    inode = (uint32_t)ext4_atomic_create_dir(2U, "to_delete_dir", 0755U, 1000U, 1000U);
    TEST_ASSERT(inode > 0U);

    /* 测试参数验证 */
    ret = ext4_atomic_delete_dir(0U, "to_delete_dir");
    TEST_ASSERT(ret < 0);  /* 应该失败：父目录无效 */

    ret = ext4_atomic_delete_dir(2U, NULL);
    TEST_ASSERT(ret < 0);  /* 应该失败：目录名为 NULL */

    /* 测试正常删除空目录 */
    ret = ext4_atomic_delete_dir(2U, "to_delete_dir");
    TEST_ASSERT(ret == 0);  /* 应该成功 */

    /* 验证 Journal 状态 */
    ext4_journal_state_t state = ext4_journal_get_state();
    TEST_ASSERT(state == EXT4_JOURNAL_CLEAN);

    /* 清理 */
    ext4_atomic_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试原子创建硬链接
 */
static int test_atomic_link(void)
{
    int32_t ret;
    uint32_t inode;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_atomic_init();
    TEST_ASSERT(ret == 0);

    /* 先创建文件 */
    inode = (uint32_t)ext4_atomic_create_file(2U, "source.txt", 0644U, 1000U, 1000U);
    TEST_ASSERT(inode > 0U);

    /* 测试参数验证 */
    ret = ext4_atomic_link(0U, "source.txt", 2U, "link.txt");
    TEST_ASSERT(ret < 0);  /* 应该失败：源父目录无效 */

    ret = ext4_atomic_link(2U, NULL, 2U, "link.txt");
    TEST_ASSERT(ret < 0);  /* 应该失败：源文件名为 NULL */

    ret = ext4_atomic_link(2U, "source.txt", 0U, "link.txt");
    TEST_ASSERT(ret < 0);  /* 应该失败：目标父目录无效 */

    ret = ext4_atomic_link(2U, "source.txt", 2U, NULL);
    TEST_ASSERT(ret < 0);  /* 应该失败：目标文件名为 NULL */

    /* 测试正常创建硬链接 */
    ret = ext4_atomic_link(2U, "source.txt", 2U, "link.txt");
    TEST_ASSERT(ret == 0);  /* 应该成功 */

    /* 验证 Journal 状态 */
    ext4_journal_state_t state = ext4_journal_get_state();
    TEST_ASSERT(state == EXT4_JOURNAL_CLEAN);

    /* 清理 */
    ext4_atomic_destroy();
    ext4_journal_destroy();

    TEST_PASS();
}

/**
 * @brief 测试原子性（失败回滚）
 */
static int test_atomic_rollback(void)
{
    int32_t ret;

    /* 初始化 */
    ret = ext4_journal_init();
    TEST_ASSERT(ret == 0);

    ret = ext4_atomic_init();
    TEST_ASSERT(ret == 0);

    /* 模拟一个会失败的操作（无效的父目录） */
    ret = ext4_atomic_create_file(0U, "test.txt", 0644U, 1000U, 1000U);
    TEST_ASSERT(ret < 0);  /* 应该失败 */

    /* 验证 Journal 状态应该恢复到 CLEAN */
    ext4_journal_state_t state = ext4_journal_get_state();
    TEST_ASSERT(state == EXT4_JOURNAL_CLEAN);

    /* 清理 */
    ext4_atomic_destroy();
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
    printf("EXT4 原子操作测试套件\n");
    printf("========================================\n\n");

    /* 文件级原子操作测试 */
    printf("【文件级原子操作测试】\n");
    if (test_atomic_create_file() == 0) { passed++; } else { failed++; }
    if (test_atomic_delete_file() == 0) { passed++; } else { failed++; }
    if (test_atomic_rename_file() == 0) { passed++; } else { failed++; }
    printf("\n");

    /* 目录级原子操作测试 */
    printf("【目录级原子操作测试】\n");
    if (test_atomic_create_dir() == 0) { passed++; } else { failed++; }
    if (test_atomic_delete_dir() == 0) { passed++; } else { failed++; }
    if (test_atomic_link() == 0) { passed++; } else { failed++; }
    printf("\n");

    /* 原子性测试 */
    printf("【原子性测试】\n");
    if (test_atomic_rollback() == 0) { passed++; } else { failed++; }
    printf("\n");

    /* 测试结果统计 */
    printf("========================================\n");
    printf("测试结果: %d 通过, %d 失败\n", passed, failed);
    printf("========================================\n");

    return (failed == 0) ? 0 : -1;
}
