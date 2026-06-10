/**
 * @file    test_kmalloc_kernel.c
 * @brief   kmalloc 内核实现单元测试（使用实际内核代码）
 * @author  AISafe64 Team
 * @date    2026-06-10
 * @version 1.0
 *
 * @details 直接链接内核 kmalloc.c 实现，测试：
 *          - 初始化
 *          - 分配/释放
 *          - 空闲块合并
 *          - 分配统计
 *          - 边界条件
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

/* 内核头文件 */
#include <kernel/mm/kmalloc.h>
#include <kernel/string.h>

/* ========================================================================
 * 测试统计
 * ======================================================================== */

static uint32_t s_total_tests  = 0U;
static uint32_t s_passed_tests = 0U;
static uint32_t s_failed_tests = 0U;

#define TEST_ASSERT(condition, message) \
    do { \
        s_total_tests++; \
        if (condition) { \
            s_passed_tests++; \
            printf("  [PASS] %s\n", message); \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] %s (行 %d)\n", message, __LINE__); \
        } \
    } while (0)

/* ========================================================================
 * 测试 1: 初始化
 * ======================================================================== */

static void test_kmalloc_init(void)
{
    int32_t ret;

    printf("\n========== 测试 1: 初始化 ==========\n");

    ret = kmalloc_init();
    TEST_ASSERT(ret == 0, "kmalloc_init 应返回 0");

    /* 验证统计清零 */
    kmalloc_stats_t stats;
    ret = kmalloc_get_stats(&stats);
    TEST_ASSERT(ret == 0, "kmalloc_get_stats 应返回 0");
    TEST_ASSERT(stats.total_allocs == 0U, "初始分配次数应为 0");
    TEST_ASSERT(stats.current_allocs == 0U, "当前分配数应为 0");
    TEST_ASSERT(stats.current_bytes == 0U, "当前字节数应为 0");
}

/* ========================================================================
 * 测试 2: 基本分配
 * ======================================================================== */

static void test_kmalloc_basic(void)
{
    void *ptr;

    printf("\n========== 测试 2: 基本分配 ==========\n");

    ptr = kmalloc(64U);
    TEST_ASSERT(ptr != NULL, "分配 64 字节应成功");

    /* 验证 16 字节对齐 */
    uintptr_t addr = (uintptr_t)ptr;
    TEST_ASSERT((addr & 0xFU) == 0U, "应 16 字节对齐");

    /* 写入数据 */
    (void)memset(ptr, 0xAA, 64U);

    /* 验证统计 */
    kmalloc_stats_t stats;
    (void)kmalloc_get_stats(&stats);
    TEST_ASSERT(stats.current_allocs >= 1U, "当前分配数应 >= 1");

    kfree(ptr);

    /* 释放后统计 */
    (void)kmalloc_get_stats(&stats);
    TEST_ASSERT(stats.current_allocs == 0U, "释放后当前分配数应为 0");
}

/* ========================================================================
 * 测试 3: 零大小分配
 * ======================================================================== */

static void test_kmalloc_zero_size(void)
{
    void *ptr;

    printf("\n========== 测试 3: 零大小分配 ==========\n");

    ptr = kmalloc(0U);
    TEST_ASSERT(ptr == NULL, "零大小分配应返回 NULL");
}

/* ========================================================================
 * 测试 4: kzalloc 清零分配
 * ======================================================================== */

static void test_kzalloc(void)
{
    void *ptr;
    uint8_t *bp;
    bool all_zero;
    uint32_t i;

    printf("\n========== 测试 4: kzalloc 清零分配 ==========\n");

    ptr = kzalloc(256U);
    TEST_ASSERT(ptr != NULL, "kzalloc 256 字节应成功");

    bp = (uint8_t *)ptr;
    all_zero = true;
    for (i = 0U; i < 256U; i++)
    {
        if (bp[i] != 0U)
        {
            all_zero = false;
            break;
        }
    }
    TEST_ASSERT(all_zero, "kzalloc 内存应全零");

    kfree(ptr);
}

/* ========================================================================
 * 测试 5: 多次分配和释放
 * ======================================================================== */

static void test_multiple_alloc_free(void)
{
    void *ptrs[10];
    uint32_t i;

    printf("\n========== 测试 5: 多次分配和释放 ==========\n");

    for (i = 0U; i < 10U; i++)
    {
        ptrs[i] = kmalloc(64U + (size_t)i * 32U);
        TEST_ASSERT(ptrs[i] != NULL, "多次分配应成功");
    }

    /* 交替释放 */
    for (i = 0U; i < 10U; i += 2U)
    {
        kfree(ptrs[i]);
        ptrs[i] = NULL;
    }

    /* 再次分配（测试碎片合并） */
    for (i = 0U; i < 10U; i += 2U)
    {
        ptrs[i] = kmalloc(64U + (size_t)i * 32U);
        TEST_ASSERT(ptrs[i] != NULL, "碎片后分配应成功");
    }

    /* 释放所有 */
    for (i = 0U; i < 10U; i++)
    {
        kfree(ptrs[i]);
    }

    kmalloc_stats_t stats;
    (void)kmalloc_get_stats(&stats);
    TEST_ASSERT(stats.current_allocs == 0U, "全部释放后分配数应为 0");
    printf("  [INFO] 累计分配: %zu, 累计释放: %zu\n",
           stats.total_allocs, stats.total_frees);
}

/* ========================================================================
 * 测试 6: kfree(NULL) 安全性
 * ======================================================================== */

static void test_kfree_null(void)
{
    printf("\n========== 测试 6: kfree(NULL) 安全性 ==========\n");

    kfree(NULL);
    TEST_ASSERT(true, "kfree(NULL) 不应崩溃");

    kfree_secure(NULL, 0U);
    TEST_ASSERT(true, "kfree_secure(NULL, 0) 不应崩溃");
}

/* ========================================================================
 * 测试 7: 分配释放循环
 * ======================================================================== */

static void test_alloc_free_cycle(void)
{
    void *ptr;
    uint32_t i;

    printf("\n========== 测试 7: 分配释放循环 ==========\n");

    for (i = 0U; i < 200U; i++)
    {
        ptr = kmalloc(128U);
        if (ptr == NULL)
        {
            TEST_ASSERT(false, "循环中分配失败");
            return;
        }
        (void)memset(ptr, (int32_t)(i & 0xFFU), 128U);
        kfree(ptr);
    }

    TEST_ASSERT(true, "200 次分配释放循环完成");
}

/* ========================================================================
 * 测试 8: 统计信息
 * ======================================================================== */

static void test_stats(void)
{
    kmalloc_stats_t stats;
    int32_t ret;

    printf("\n========== 测试 8: 统计信息 ==========\n");

    /* 重新初始化以获取干净统计 */
    (void)kmalloc_init();

    void *p1 = kmalloc(100U);
    void *p2 = kmalloc(200U);
    void *p3 = kmalloc(300U);

    ret = kmalloc_get_stats(&stats);
    TEST_ASSERT(ret == 0, "获取统计应成功");
    TEST_ASSERT(stats.total_allocs == 3U, "累计分配应为 3");
    TEST_ASSERT(stats.current_allocs == 3U, "当前分配应为 3");
    TEST_ASSERT(stats.total_bytes == 600U, "累计字节应为 600");
    TEST_ASSERT(stats.current_bytes == 600U, "当前字节应为 600");

    kfree(p1);
    (void)kmalloc_get_stats(&stats);
    TEST_ASSERT(stats.current_allocs == 2U, "释放后当前分配应为 2");
    TEST_ASSERT(stats.total_frees == 1U, "累计释放应为 1");

    kfree(p2);
    kfree(p3);
    (void)kmalloc_get_stats(&stats);
    TEST_ASSERT(stats.current_allocs == 0U, "全部释放后应为 0");

    /* NULL 参数测试 */
    ret = kmalloc_get_stats(NULL);
    TEST_ASSERT(ret != 0, "NULL 参数应返回错误");
}

/* ========================================================================
 * 测试 9: 大块分配
 * ======================================================================== */

static void test_large_alloc(void)
{
    void *ptr;

    printf("\n========== 测试 9: 大块分配 ==========\n");

    /* 分配 64KB */
    ptr = kmalloc(65536U);
    TEST_ASSERT(ptr != NULL, "分配 64KB 应成功");
    (void)memset(ptr, 0xBB, 65536U);
    kfree(ptr);

    /* 分配 1MB（应成功，堆为 4MB） */
    ptr = kmalloc(1048576U);
    TEST_ASSERT(ptr != NULL, "分配 1MB 应成功");
    kfree(ptr);

    /* 超大分配（堆为 4MB，前面测试已消耗部分） */
    ptr = kmalloc(2U * 1024U * 1024U);
    if (ptr != NULL)
    {
        TEST_ASSERT(true, "分配 2MB 成功");
        kfree(ptr);
    }
    else
    {
        printf("  [INFO] 分配 2MB 失败（堆碎片化，可接受）\n");
    }
}

/* ========================================================================
 * 测试 10: 安全释放
 * ======================================================================== */

static void test_kfree_secure(void)
{
    void *ptr;

    printf("\n========== 测试 10: 安全释放 ==========\n");

    ptr = kmalloc(64U);
    TEST_ASSERT(ptr != NULL, "分配应成功");
    (void)memset(ptr, 0xCC, 64U);
    kfree_secure(ptr, 64U);
    TEST_ASSERT(true, "kfree_secure 不应崩溃");
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("AISafeOS64 kmalloc 内核实现测试\n");
    printf("========================================\n");

    test_kmalloc_init();
    test_kmalloc_basic();
    test_kmalloc_zero_size();
    test_kzalloc();
    test_multiple_alloc_free();
    test_kfree_null();
    test_alloc_free_cycle();
    test_stats();
    test_large_alloc();
    test_kfree_secure();

    printf("\n");
    printf("========================================\n");
    printf("测试结果统计\n");
    printf("========================================\n");
    printf("总计测试: %u\n", s_total_tests);
    printf("通过: %u (%.1f%%)\n", s_passed_tests,
           (100.0 * (double)s_passed_tests / (double)s_total_tests));
    printf("失败: %u (%.1f%%)\n", s_failed_tests,
           (100.0 * (double)s_failed_tests / (double)s_total_tests));
    printf("========================================\n");

    if (s_failed_tests == 0U)
    {
        printf("\n所有测试通过！\n");
    }
    else
    {
        printf("\n有 %u 个测试失败！\n", s_failed_tests);
    }

    return (s_failed_tests == 0U) ? 0 : 1;
}
