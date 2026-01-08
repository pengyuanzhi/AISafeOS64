/**
 * @file test_framework.c
 * @brief AISafe64 RTOS - 单元测试框架实现
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 轻量级单元测试框架实现
 */

#include "test_framework.h"
#include "../src/include/printk.h"
#include <string.h>

/**
 * @brief 全局测试统计
 */
test_stats_t g_test_stats = {0};

/**
 * @brief 当前测试套件名称
 */
static const char *g_current_suite = NULL;

/**
 * @brief 当前测试用例名称
 */
static const char *g_current_test = NULL;

/**
 * @brief 当前测试是否失败
 */
static bool g_test_failed = false;

/**
 * @brief 初始化测试框架
 */
void test_init(void)
{
    (void)memset(&g_test_stats, 0, sizeof(g_test_stats));
    g_current_suite = NULL;
    g_current_test = NULL;
    g_test_failed = false;

    printk("\n");
    printk("========================================\n");
    printk("     AISafe64 RTOS Unit Tests\n");
    printk("========================================\n");
}

/**
 * @brief 运行单个测试用例
 */
void test_run(const char *name, void (*test_func)(void))
{
    if (name == NULL || test_func == NULL) {
        return;
    }

    g_current_test = name;
    g_test_failed = false;
    g_test_stats.total_tests++;

    printk("  [RUN] %s\n", name);

    /* 运行测试函数 */
    test_func();

    if (!g_test_failed) {
        g_test_stats.passed_tests++;
        printk("  [PASS] %s\n", name);
    } else {
        g_test_stats.failed_tests++;
        printk("  [FAIL] %s\n", name);
    }

    g_current_test = NULL;
}

/**
 * @brief 测试套件开始
 */
void test_suite_start(const char *suite_name)
{
    g_current_suite = suite_name;
    printk("\n");
    printk("----------------------------------------\n");
    printk("Test Suite: %s\n", suite_name);
    printk("----------------------------------------\n");
}

/**
 * @brief 测试套件结束
 */
void test_suite_end(void)
{
    g_current_suite = NULL;
}

/**
 * @brief 打印测试结果
 */
void test_report(void)
{
    printk("\n");
    printk("========================================\n");
    printk("           Test Summary\n");
    printk("========================================\n");
    printk("Total Tests:  %u\n", g_test_stats.total_tests);
    printk("Passed:       %u\n", g_test_stats.passed_tests);
    printk("Failed:       %u\n", g_test_stats.failed_tests);
    printk("Skipped:      %u\n", g_test_stats.skipped_tests);

    if (g_test_stats.failed_tests == 0U) {
        printk("\n*** ALL TESTS PASSED ***\n");
    } else {
        printk("\n*** SOME TESTS FAILED ***\n");
    }

    printk("========================================\n");
}

/**
 * @brief 打印失败信息
 */
static void test_fail(const char *msg, const char *file, uint32_t line)
{
    g_test_failed = true;
    printk("        FAILED at %s:%u\n", file, line);
    printk("        %s\n", msg);
}

/**
 * @brief 相等断言实现
 */
void test_assert_eq(uint64_t actual, uint64_t expected, const char *actual_str,
                    const char *expected_str, const char *file, uint32_t line)
{
    if (actual != expected) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg), "Expected: %s = 0x%lx, but got: %s = 0x%lx", expected_str,
                       expected, actual_str, actual);
        test_fail(msg, file, line);
    }
}

/**
 * @brief 指针相等断言实现
 */
void test_assert_eq_ptr(const void *actual, const void *expected, const char *actual_str,
                        const char *expected_str, const char *file, uint32_t line)
{
    if (actual != expected) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg), "Expected: %s = %p, but got: %s = %p", expected_str,
                       expected, actual_str, actual);
        test_fail(msg, file, line);
    }
}

/**
 * @brief 不等断言实现
 */
void test_assert_ne(uint64_t actual, uint64_t unexpected, const char *actual_str,
                    const char *unexpected_str, const char *file, uint32_t line)
{
    if (actual == unexpected) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg), "Expected: %s != 0x%lx, but got: %s = 0x%lx", actual_str,
                       unexpected, actual_str, actual);
        test_fail(msg, file, line);
    }
}

/**
 * @brief 大于断言实现
 */
void test_assert_gt(uint64_t actual, uint64_t min, const char *actual_str, const char *min_str,
                    const char *file, uint32_t line)
{
    if (actual <= min) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg), "Expected: %s > 0x%lx, but got: %s = 0x%lx", actual_str,
                       min, actual_str, actual);
        test_fail(msg, file, line);
    }
}

/**
 * @brief 小于断言实现
 */
void test_assert_lt(uint64_t actual, uint64_t max, const char *actual_str, const char *max_str,
                    const char *file, uint32_t line)
{
    if (actual >= max) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg), "Expected: %s < 0x%lx, but got: %s = 0x%lx", actual_str,
                       max, actual_str, actual);
        test_fail(msg, file, line);
    }
}

/**
 * @brief 真值断言实现
 */
void test_assert_true(bool condition, const char *condition_str, const char *file, uint32_t line)
{
    if (!condition) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg), "Expected true: %s", condition_str);
        test_fail(msg, file, line);
    }
}

/**
 * @brief 假值断言实现
 */
void test_assert_false(bool condition, const char *condition_str, const char *file, uint32_t line)
{
    if (condition) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg), "Expected false: %s", condition_str);
        test_fail(msg, file, line);
    }
}

/**
 * @brief NULL指针断言实现
 */
void test_assert_null(const void *ptr, const char *ptr_str, const char *file, uint32_t line)
{
    if (ptr != NULL) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg), "Expected NULL: %s = %p", ptr_str, ptr);
        test_fail(msg, file, line);
    }
}

/**
 * @brief 非NULL指针断言实现
 */
void test_assert_not_null(const void *ptr, const char *ptr_str, const char *file, uint32_t line)
{
    if (ptr == NULL) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg), "Expected non-NULL: %s", ptr_str);
        test_fail(msg, file, line);
    }
}
