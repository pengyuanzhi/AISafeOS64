/**
 * @file    unity.h
 * @brief   Unity 测试框架简易实现（Mock）
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Unity 测试框架的简易实现，用于 VMM 集成测试
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef UNITY_H
#define UNITY_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 测试统计
 * ======================================================================== */

static uint32_t s_total_tests = 0U;
static uint32_t s_passed_tests = 0U;
static uint32_t s_failed_tests = 0U;

/* ========================================================================
 * 测试辅助宏
 * ======================================================================== */

/** @brief 测试失败信息 */
#define UNITY_FAIL_AND_RETURN(condition, message) \
    do { \
        if (!(condition)) { \
            s_total_tests++; \
            s_failed_tests++; \
            printf("  [FAIL] %s (at %s:%u)\n", message, __FILE__, __LINE__); \
            return; \
        } \
    } while (0)

/** @brief 测试断言：等于 */
#define TEST_ASSERT_EQUAL_INT(expected, actual) \
    do { \
        s_total_tests++; \
        if ((expected) == (actual)) { \
            s_passed_tests++; \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] Expected %d, but got %d (at %s:%u)\n", \
                   (int)(expected), (int)(actual), __FILE__, __LINE__); \
        } \
    } while (0)

/** @brief 测试断言：不等于 */
#define TEST_ASSERT_NOT_EQUAL(expected, actual) \
    do { \
        s_total_tests++; \
        if ((expected) != (actual)) { \
            s_passed_tests++; \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] Expected %d not equal to %d (at %s:%u)\n", \
                   (int)(expected), (int)(actual), __FILE__, __LINE__); \
        } \
    } while (0)

/** @brief 测试断言：指针不等于 NULL */
#define TEST_ASSERT_NOT_NULL(pointer) \
    do { \
        s_total_tests++; \
        if ((pointer) != NULL) { \
            s_passed_tests++; \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] Expected not NULL (at %s:%u)\n", __FILE__, __LINE__); \
        } \
    } while (0)

/** @brief 测试断言：指针等于 NULL */
#define TEST_ASSERT_NULL(pointer) \
    do { \
        s_total_tests++; \
        if ((pointer) == NULL) { \
            s_passed_tests++; \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] Expected NULL (at %s:%u)\n", __FILE__, __LINE__); \
        } \
    } while (0)

/** @brief 测试断言：指针相等 */
#define TEST_ASSERT_EQUAL_PTR(expected, actual) \
    do { \
        s_total_tests++; \
        if ((expected) == (actual)) { \
            s_passed_tests++; \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] Expected pointer %p, but got %p (at %s:%u)\n", \
                   (void*)(expected), (void*)(actual), __FILE__, __LINE__); \
        } \
    } while (0)

/** @brief 测试断言：布尔真 */
#define TEST_ASSERT_TRUE(condition) \
    do { \
        s_total_tests++; \
        if (condition) { \
            s_passed_tests++; \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] Expected true, but got false (at %s:%u)\n", \
                   __FILE__, __LINE__); \
        } \
    } while (0)

/** @brief 测试断言：布尔假 */
#define TEST_ASSERT_FALSE(condition) \
    do { \
        s_total_tests++; \
        if (!(condition)) { \
            s_passed_tests++; \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] Expected false, but got true (at %s:%u)\n", \
                   __FILE__, __LINE__); \
        } \
    } while (0)

/** @brief 测试断言：无符号整数相等 */
#define TEST_ASSERT_EQUAL_UINT(expected, actual) \
    do { \
        s_total_tests++; \
        if ((expected) == (actual)) { \
            s_passed_tests++; \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] Expected %u, but got %u (at %s:%u)\n", \
                   (unsigned int)(expected), (unsigned int)(actual), __FILE__, __LINE__); \
        } \
    } while (0)

/** @brief 测试断言：字符串相等 */
#define TEST_ASSERT_EQUAL_STRING(expected, actual) \
    do { \
        s_total_tests++; \
        if (strcmp((expected), (actual)) == 0) { \
            s_passed_tests++; \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] Expected string \"%s\", but got \"%s\" (at %s:%u)\n", \
                   (expected), (actual), __FILE__, __LINE__); \
        } \
    } while (0)

/** @brief 测试断言：内存相等 */
#define TEST_ASSERT_EQUAL_MEMORY(expected, actual, len) \
    do { \
        s_total_tests++; \
        if (memcmp((expected), (actual), (len)) == 0) { \
            s_passed_tests++; \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] Memory mismatch (at %s:%u)\n", __FILE__, __LINE__); \
        } \
    } while (0)

/** @brief 测试断言：浮点数相等 */
#define TEST_ASSERT_EQUAL_FLOAT(expected, actual) \
    do { \
        s_total_tests++; \
        float exp = (float)(expected); \
        float act = (float)(actual); \
        float diff = (exp > act) ? (exp - act) : (act - exp); \
        if (diff < 0.001f) { \
            s_passed_tests++; \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] Expected %f, but got %f (at %s:%u)\n", \
                   exp, act, __FILE__, __LINE__); \
        } \
    } while (0)

/* ========================================================================
 * 测试夹具（空实现，由具体测试文件实现）
 * ======================================================================== */

void setUp(void);
void tearDown(void);

/* ========================================================================
 * 测试主函数（空实现）
 * ======================================================================== */

static inline void unity_main(void)
{
    /* 空实现，由具体测试文件实现 */
}

#endif /* UNITY_H */
