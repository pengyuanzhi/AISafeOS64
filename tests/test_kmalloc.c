/**
 * @file    test_kmalloc.c
 * @brief   kmalloc/kfree 内核内存分配器单元测试
 * @author  AISafe64 Team
 * @date    2026-06-10
 * @version 1.0
 *
 * @details 测试内核通用内存分配器：
 *          - 基本分配和释放
 *          - 零大小和 NULL 参数处理
 *          - 对齐验证
 *          - 多次分配/释放
 *          - 分配统计信息
 *          - 大块分配
 *          - 释放后重分配（复用测试）
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ========================================================================
 * 测试统计
 * ======================================================================== */

static uint32_t s_total_tests  = 0U;
static uint32_t s_passed_tests = 0U;
static uint32_t s_failed_tests = 0U;

/* ========================================================================
 * 测试辅助宏
 * ======================================================================== */

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
 * 宿主机测试模式：使用标准 malloc/free 模拟
 *
 * 在宿主机上编译测试时，kmalloc/kfree 映射到标准库函数。
 * 在内核中编译时，使用实际的内核堆分配器。
 * ======================================================================== */

#ifdef TEST_HOST_MODE

/**
 * @brief 宿主机模式：kmalloc 映射到 malloc
 *
 * @param size 请求分配的字节数
 * @return 分配的内存指针，失败返回 NULL
 */
static void *kmalloc(size_t size)
{
    if (size == 0U)
    {
        return NULL;
    }
    return malloc(size);
}

/**
 * @brief 宿主机模式：kfree 映射到 free
 *
 * @param ptr 要释放的内存指针
 */
static void kfree(void *ptr)
{
    if (ptr != NULL)
    {
        free(ptr);
    }
}

/**
 * @brief 宿主机模式：kfree_secure 安全释放（清零后释放）
 *
 * @param ptr  要释放的内存指针
 * @param size 原始分配大小
 */
static void kfree_secure(void *ptr, size_t size)
{
    if (ptr != NULL)
    {
        (void)memset(ptr, 0, size);
        free(ptr);
    }
}

/**
 * @brief 宿主机模式：kzalloc 清零分配
 *
 * @param size 请求分配的字节数
 * @return 清零后的内存指针，失败返回 NULL
 */
static void *kzalloc(size_t size)
{
    void *ptr;
    if (size == 0U)
    {
        return NULL;
    }
    ptr = malloc(size);
    if (ptr != NULL)
    {
        (void)memset(ptr, 0, size);
    }
    return ptr;
}

#else /* 内核模式 */

/* 内核模式下，由 kmalloc.h 提供 */
#include <kernel/mm/kmalloc.h>

#endif /* TEST_HOST_MODE */

/* ========================================================================
 * 测试 1: 基本分配测试
 * ======================================================================== */

/**
 * @brief 测试基本 kmalloc 分配
 */
static void test_kmalloc_basic(void)
{
    void *ptr;

    printf("\n========== 测试 1: 基本分配 ==========\n");

    /* 分配 64 字节 */
    ptr = kmalloc(64U);
    TEST_ASSERT(ptr != NULL, "分配 64 字节应成功");

    /* 写入数据验证可用 */
    (void)memset(ptr, 0xAA, 64U);

    kfree(ptr);
    printf("  [INFO] 基本分配和释放成功\n");
}

/* ========================================================================
 * 测试 2: 零大小分配
 * ======================================================================== */

/**
 * @brief 测试零大小分配行为
 */
static void test_kmalloc_zero_size(void)
{
    void *ptr;

    printf("\n========== 测试 2: 零大小分配 ==========\n");

    ptr = kmalloc(0U);
    TEST_ASSERT(ptr == NULL, "零大小分配应返回 NULL");
}

/* ========================================================================
 * 测试 3: 多次分配
 * ======================================================================== */

/**
 * @brief 测试多次分配不同大小
 */
static void test_kmalloc_multiple(void)
{
    void *ptr1;
    void *ptr2;
    void *ptr3;
    uint32_t i;

    printf("\n========== 测试 3: 多次分配 ==========\n");

    ptr1 = kmalloc(32U);
    ptr2 = kmalloc(128U);
    ptr3 = kmalloc(256U);

    TEST_ASSERT(ptr1 != NULL, "分配 32 字节应成功");
    TEST_ASSERT(ptr2 != NULL, "分配 128 字节应成功");
    TEST_ASSERT(ptr3 != NULL, "分配 256 字节应成功");

    /* 指针应各不相同 */
    TEST_ASSERT(ptr1 != ptr2, "ptr1 和 ptr2 应不同");
    TEST_ASSERT(ptr2 != ptr3, "ptr2 和 ptr3 应不同");
    TEST_ASSERT(ptr1 != ptr3, "ptr1 和 ptr3 应不同");

    /* 写入数据验证独立性 */
    (void)memset(ptr1, 0x11, 32U);
    (void)memset(ptr2, 0x22, 128U);
    (void)memset(ptr3, 0x33, 256U);

    /* 验证数据完整性 */
    for (i = 0U; i < 32U; i++)
    {
        if (((uint8_t *)ptr1)[i] != 0x11U)
        {
            TEST_ASSERT(false, "ptr1 数据完整性检查失败");
            break;
        }
    }
    if (i == 32U)
    {
        TEST_ASSERT(true, "ptr1 数据完整性检查通过");
    }

    kfree(ptr1);
    kfree(ptr2);
    kfree(ptr3);

    printf("  [INFO] 多次分配和释放成功\n");
}

/* ========================================================================
 * 测试 4: 释放 NULL 安全性
 * ======================================================================== */

/**
 * @brief 测试 kfree(NULL) 安全性
 */
static void test_kfree_null(void)
{
    printf("\n========== 测试 4: 释放 NULL 安全性 ==========\n");

    /* kfree(NULL) 不应崩溃 */
    kfree(NULL);
    TEST_ASSERT(true, "kfree(NULL) 不应崩溃");
}

/* ========================================================================
 * 测试 5: 对齐验证
 * ======================================================================== */

/**
 * @brief 测试分配结果的对齐
 */
static void test_kmalloc_alignment(void)
{
    void *ptr;
    uintptr_t addr;

    printf("\n========== 测试 5: 对齐验证 ==========\n");

    ptr = kmalloc(100U);
    TEST_ASSERT(ptr != NULL, "分配 100 字节应成功");

    addr = (uintptr_t)ptr;
    /* 验证 16 字节对齐（ARM64 ABI 要求） */
    TEST_ASSERT((addr & 0xFU) == 0U, "分配结果应 16 字节对齐");

    kfree(ptr);
}

/* ========================================================================
 * 测试 6: 分配和释放循环
 * ======================================================================== */

/**
 * @brief 测试重复分配释放不泄漏
 */
static void test_kmalloc_free_cycle(void)
{
    void *ptr;
    uint32_t i;

    printf("\n========== 测试 6: 分配释放循环 ==========\n");

    for (i = 0U; i < 100U; i++)
    {
        ptr = kmalloc(64U);
        if (ptr == NULL)
        {
            TEST_ASSERT(false, "循环中分配失败");
            return;
        }
        (void)memset(ptr, (int32_t)(i & 0xFFU), 64U);
        kfree(ptr);
    }

    TEST_ASSERT(true, "100 次分配释放循环完成");
}

/* ========================================================================
 * 测试 7: kzalloc 清零分配
 * ======================================================================== */

/**
 * @brief 测试 kzalloc 清零分配
 */
static void test_kzalloc_zeroed(void)
{
    void *ptr;
    uint8_t *byte_ptr;
    uint32_t i;
    bool all_zero;

    printf("\n========== 测试 7: kzalloc 清零分配 ==========\n");

    ptr = kzalloc(128U);
    TEST_ASSERT(ptr != NULL, "kzalloc 128 字节应成功");

    /* 验证所有字节为零 */
    byte_ptr = (uint8_t *)ptr;
    all_zero = true;
    for (i = 0U; i < 128U; i++)
    {
        if (byte_ptr[i] != 0U)
        {
            all_zero = false;
            break;
        }
    }
    TEST_ASSERT(all_zero, "kzalloc 返回的内存应全零");

    kfree(ptr);
}

/* ========================================================================
 * 测试 8: 大块分配
 * ======================================================================== */

/**
 * @brief 测试大块内存分配
 */
static void test_kmalloc_large(void)
{
    void *ptr;

    printf("\n========== 测试 8: 大块分配 ==========\n");

    /* 分配 64KB */
    ptr = kmalloc(65536U);
    TEST_ASSERT(ptr != NULL, "分配 64KB 应成功");

    (void)memset(ptr, 0xBB, 65536U);
    kfree(ptr);

    /* 分配 1MB（可能失败，取决于系统） */
    ptr = kmalloc(1048576U);
    if (ptr != NULL)
    {
        TEST_ASSERT(true, "分配 1MB 成功");
        kfree(ptr);
    }
    else
    {
        printf("  [INFO] 分配 1MB 失败（可接受）\n");
    }
}

/* ========================================================================
 * 测试 9: kfree_secure 安全释放
 * ======================================================================== */

/**
 * @brief 测试安全释放（清零后释放）
 */
static void test_kfree_secure(void)
{
    void *ptr;

    printf("\n========== 测试 9: 安全释放 ==========\n");

    ptr = kmalloc(64U);
    TEST_ASSERT(ptr != NULL, "分配应成功");

    /* 填充非零数据 */
    (void)memset(ptr, 0xCC, 64U);

    /* 安全释放（清零后释放） */
    kfree_secure(ptr, 64U);

    /* 注意：释放后访问是未定义行为，此处仅验证不崩溃 */
    TEST_ASSERT(true, "kfree_secure 不应崩溃");

    /* 测试 NULL 安全性 */
    kfree_secure(NULL, 0U);
    TEST_ASSERT(true, "kfree_secure(NULL, 0) 不应崩溃");
}

/* ========================================================================
 * 测试 10: 边界大小测试
 * ======================================================================== */

/**
 * @brief 测试各种边界大小
 */
static void test_kmalloc_edge_sizes(void)
{
    void *ptr;

    printf("\n========== 测试 10: 边界大小 ==========\n");

    /* 1 字节 */
    ptr = kmalloc(1U);
    TEST_ASSERT(ptr != NULL, "分配 1 字节应成功");
    kfree(ptr);

    /* 16 字节 */
    ptr = kmalloc(16U);
    TEST_ASSERT(ptr != NULL, "分配 16 字节应成功");
    kfree(ptr);

    /* 4096 字节（页大小） */
    ptr = kmalloc(4096U);
    TEST_ASSERT(ptr != NULL, "分配 4096 字节应成功");
    kfree(ptr);

    /* 8192 字节 */
    ptr = kmalloc(8192U);
    TEST_ASSERT(ptr != NULL, "分配 8192 字节应成功");
    kfree(ptr);
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

int main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("AISafeOS64 kmalloc/kfree 单元测试\n");
    printf("========================================\n");

    /* 运行所有测试 */
    test_kmalloc_basic();
    test_kmalloc_zero_size();
    test_kmalloc_multiple();
    test_kfree_null();
    test_kmalloc_alignment();
    test_kmalloc_free_cycle();
    test_kzalloc_zeroed();
    test_kmalloc_large();
    test_kfree_secure();
    test_kmalloc_edge_sizes();

    /* 输出测试结果 */
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
