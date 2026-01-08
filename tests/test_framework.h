/**
 * @file test_framework.h
 * @brief AISafe64 RTOS - 简单单元测试框架
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 轻量级单元测试框架
 *          - 不依赖外部库
 *          - 适合嵌入式环境
 *          - 支持测试统计
 *
 * @note MISRA-C:2012合规
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 测试统计结构
     */
    typedef struct
    {
        uint32_t total_tests;   /**< 总测试数 */
        uint32_t passed_tests;  /**< 通过测试数 */
        uint32_t failed_tests;  /**< 失败测试数 */
        uint32_t skipped_tests; /**< 跳过测试数 */
    } test_stats_t;

    /**
     * @brief 全局测试统计
     */
    extern test_stats_t g_test_stats;

/**
 * @brief 测试断言宏
 */

/* 相等断言 */
#define TEST_ASSERT_EQ(actual, expected) \
    test_assert_eq((uint64_t)(actual), (uint64_t)(expected), #actual, #expected, __FILE__, __LINE__)

#define TEST_ASSERT_EQ_PTR(actual, expected)                                                 \
    test_assert_eq_ptr((const void *)(actual), (const void *)(expected), #actual, #expected, \
                       __FILE__, __LINE__)

/* 不等断言 */
#define TEST_ASSERT_NE(actual, unexpected)                                                     \
    test_assert_ne((uint64_t)(actual), (uint64_t)(unexpected), #actual, #unexpected, __FILE__, \
                   __LINE__)

/* 大于断言 */
#define TEST_ASSERT_GT(actual, min) \
    test_assert_gt((uint64_t)(actual), (uint64_t)(min), #actual, #min, __FILE__, __LINE__)

/* 小于断言 */
#define TEST_ASSERT_LT(actual, max) \
    test_assert_lt((uint64_t)(actual), (uint64_t)(max), #actual, #max, __FILE__, __LINE__)

/* 真值断言 */
#define TEST_ASSERT_TRUE(condition) test_assert_true((condition), #condition, __FILE__, __LINE__)

/* 假值断言 */
#define TEST_ASSERT_FALSE(condition) test_assert_false((condition), #condition, __FILE__, __LINE__)

/* NULL指针断言 */
#define TEST_ASSERT_NULL(ptr) test_assert_null((const void *)(ptr), #ptr, __FILE__, __LINE__)

/* 非NULL指针断言 */
#define TEST_ASSERT_NOT_NULL(ptr) \
    test_assert_not_null((const void *)(ptr), #ptr, __FILE__, __LINE__)

/**
 * @brief 测试套件宏
 */

/* 测试用例定义 */
#define TEST_CASE(name) void test_##name(void)

/* 测试运行器 */
#define TEST_RUN(name)                \
    do                                \
    {                                 \
        test_run(#name, test_##name); \
    } while (0)

/* 测试套件开始 */
#define TEST_SUITE_START(suite_name)   \
    void test_suite_##suite_name(void) \
    {                                  \
        test_suite_start(suite_name);

/* 测试套件结束 */
#define TEST_SUITE_END() \
    test_suite_end();    \
    }

    /**
     * @brief 测试框架函数
     */

    /**
     * @brief 初始化测试框架
     */
    void test_init(void);

    /**
     * @brief 运行单个测试用例
     * @param name 测试用例名称
     * @param test_func 测试函数指针
     */
    void test_run(const char *name, void (*test_func)(void));

    /**
     * @brief 测试套件开始
     * @param suite_name 测试套件名称
     */
    void test_suite_start(const char *suite_name);

    /**
     * @brief 测试套件结束
     */
    void test_suite_end(void);

    /**
     * @brief 打印测试结果
     */
    void test_report(void);

    /**
     * @brief 相等断言实现
     */
    void test_assert_eq(uint64_t actual, uint64_t expected, const char *actual_str,
                        const char *expected_str, const char *file, uint32_t line);

    /**
     * @brief 指针相等断言实现
     */
    void test_assert_eq_ptr(const void *actual, const void *expected, const char *actual_str,
                            const char *expected_str, const char *file, uint32_t line);

    /**
     * @brief 不等断言实现
     */
    void test_assert_ne(uint64_t actual, uint64_t unexpected, const char *actual_str,
                        const char *unexpected_str, const char *file, uint32_t line);

    /**
     * @brief 大于断言实现
     */
    void test_assert_gt(uint64_t actual, uint64_t min, const char *actual_str, const char *min_str,
                        const char *file, uint32_t line);

    /**
     * @brief 小于断言实现
     */
    void test_assert_lt(uint64_t actual, uint64_t max, const char *actual_str, const char *max_str,
                        const char *file, uint32_t line);

    /**
     * @brief 真值断言实现
     */
    void test_assert_true(bool condition, const char *condition_str, const char *file,
                          uint32_t line);

    /**
     * @brief 假值断言实现
     */
    void test_assert_false(bool condition, const char *condition_str, const char *file,
                           uint32_t line);

    /**
     * @brief NULL指针断言实现
     */
    void test_assert_null(const void *ptr, const char *ptr_str, const char *file, uint32_t line);

    /**
     * @brief 非NULL指针断言实现
     */
    void test_assert_not_null(const void *ptr, const char *ptr_str, const char *file,
                              uint32_t line);

#ifdef __cplusplus
}
#endif

#endif /* TEST_FRAMEWORK_H */
