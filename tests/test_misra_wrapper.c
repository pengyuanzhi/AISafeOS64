/**
 * @file    test_misra_wrapper.c
 * @brief   MISRA C:2012 合规包装测试
 * @version 1.0
 *
 * @details 测试 misra_wrapper.c 中所有包装函数的正确性
 *
 * @note MISRA-C:2012 合规
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>

#include "misra_wrapper.h"

/* 测试计数器 */
static int g_test_passed = 0;
static int g_test_failed = 0;

/* ========================================================================
 * 测试工具函数
 * ======================================================================== */

static void test_start(const char *name)
{
    printf("[MISRA_WRAPPER] %s ... ", name);
}

static void test_pass(void)
{
    printf("✓ PASSED\n");
    g_test_passed++;
}

static void test_fail(const char *reason)
{
    printf("✗ FAILED (%s)\n", reason);
    g_test_failed++;
}

/* ========================================================================
 * 内存操作测试
 * ======================================================================== */

static void test_memcpy_null_dest(void)
{
    void *result;
    char src[100] = "hello";

    test_start("memcpy_null_dest");
    result = misra_memcpy(NULL, src, 5);
    if (result == NULL) {
        test_pass();
    } else {
        test_fail("expected NULL for NULL dest");
    }
}

static void test_memcpy_null_src(void)
{
    void *result;
    char dest[100];

    test_start("memcpy_null_src");
    result = misra_memcpy(dest, NULL, 5);
    if (result == NULL) {
        test_pass();
    } else {
        test_fail("expected NULL for NULL src");
    }
}

static void test_memcpy_valid(void)
{
    void *result;
    char dest[100];
    char src[100] = "hello world";

    test_start("memcpy_valid");
    result = misra_memcpy(dest, src, 11);
    if ((result == dest) && (strcmp(dest, src) == 0)) {
        test_pass();
    } else {
        test_fail("memcpy failed");
    }
}

static void test_memset_null(void)
{
    void *result;

    test_start("memset_null");
    result = misra_memset(NULL, 0, 100);
    if (result == NULL) {
        test_pass();
    } else {
        test_fail("expected NULL for NULL pointer");
    }
}

static void test_memset_valid(void)
{
    void *result;
    char buffer[100];
    int i;

    test_start("memset_valid");
    result = misra_memset(buffer, 'X', 100);
    if (result == buffer) {
        bool all_x = true;
        for (i = 0; i < 100; i++) {
            if (buffer[i] != 'X') {
                all_x = false;
                break;
            }
        }
        if (all_x) {
            test_pass();
        } else {
            test_fail("memset did not fill correctly");
        }
    } else {
        test_fail("memset failed");
    }
}

static void test_memcmp_equal(void)
{
    int result;
    char buf1[100] = "hello";
    char buf2[100] = "hello";

    test_start("memcmp_equal");
    result = misra_memcmp(buf1, buf2, 5);
    if (result == 0) {
        test_pass();
    } else {
        test_fail("expected 0 for equal buffers");
    }
}

static void test_memcmp_less(void)
{
    int result;
    char buf1[100] = "hello";
    char buf2[100] = "world";

    test_start("memcmp_less");
    result = misra_memcmp(buf1, buf2, 5);
    if (result < 0) {
        test_pass();
    } else {
        test_fail("expected <0 for buf1 < buf2");
    }
}

/* ========================================================================
 * 字符串操作测试
 * ======================================================================== */

static void test_strlen_null(void)
{
    size_t result;

    test_start("strlen_null");
    result = misra_strlen(NULL, 100);
    if (result == 0) {
        test_pass();
    } else {
        test_fail("expected 0 for NULL string");
    }
}

static void test_strlen_valid(void)
{
    size_t result;
    char str[100] = "hello";

    test_start("strlen_valid");
    result = misra_strlen(str, 100);
    if (result == 5) {
        test_pass();
    } else {
        test_fail("expected 5 for 'hello'");
    }
}

static void test_strlen_unterminated(void)
{
    size_t result;
    char str[100];

    test_start("strlen_unterminated");

    /* 创建没有 NULL 终止符的字符串 */
    memset(str, 'X', sizeof(str));

    result = misra_strlen(str, 100);
    if (result == 0) {
        test_pass();
    } else {
        test_fail("expected 0 for unterminated string");
    }
}

static void test_strcmp_null1(void)
{
    int result;

    test_start("strcmp_null1");
    result = misra_strcmp(NULL, "hello", 100);
    if (result == 0) {
        test_pass();
    } else {
        test_fail("expected 0 for NULL string");
    }
}

static void test_strcmp_equal(void)
{
    int result;

    test_start("strcmp_equal");
    result = misra_strcmp("hello", "hello", 100);
    if (result == 0) {
        test_pass();
    } else {
        test_fail("expected 0 for equal strings");
    }
}

static void test_strcmp_less(void)
{
    int result;

    test_start("strcmp_less");
    result = misra_strcmp("hello", "world", 100);
    if (result < 0) {
        test_pass();
    } else {
        test_fail("expected <0 for s1 < s2");
    }
}

static void test_strcmp_greater(void)
{
    int result;

    test_start("strcmp_greater");
    result = misra_strcmp("world", "hello", 100);
    if (result > 0) {
        test_pass();
    } else {
        test_fail("expected >0 for s1 > s2");
    }
}

static void test_strncmp_equal(void)
{
    int result;

    test_start("strncmp_equal");
    result = misra_strncmp("hello", "hello", 10);
    if (result == 0) {
        test_pass();
    } else {
        test_fail("expected 0 for equal prefix");
    }
}

static void test_strncmp_less(void)
{
    int result;

    test_start("strncmp_less");
    result = misra_strncmp("hell", "help", 4);
    if (result < 0) {
        test_pass();
    } else {
        test_fail("expected <0 for prefix less");
    }
}

static void test_strcpy_null(void)
{
    char *result;
    char dest[100];

    test_start("strcpy_null");
    result = misra_strcpy(dest, NULL, 100);
    if (result == NULL) {
        test_pass();
    } else {
        test_fail("expected NULL for NULL src");
    }
}

static void test_strcpy_valid(void)
{
    char *result;
    char dest[100];

    test_start("strcpy_valid");
    result = misra_strcpy(dest, "hello", 100);
    if ((result == dest) && (strcmp(dest, "hello") == 0)) {
        test_pass();
    } else {
        test_fail("strcpy failed");
    }
}

static void test_strcpy_buffer_too_small(void)
{
    char *result;
    char dest[5];

    test_start("strcpy_buffer_too_small");
    result = misra_strcpy(dest, "hello world", 5);
    if (result == NULL) {
        test_pass();
    } else {
        test_fail("expected NULL for buffer too small");
    }
}

static void test_strncpy_null(void)
{
    char *result;
    char dest[100];

    test_start("strncpy_null");
    result = misra_strncpy(dest, NULL, 100, 10);
    if (result == NULL) {
        test_pass();
    } else {
        test_fail("expected NULL for NULL src");
    }
}

static void test_strncpy_valid(void)
{
    char *result;
    char dest[100];

    test_start("strncpy_valid");
    result = misra_strncpy(dest, "hello", 100, 10);
    if ((result == dest) && (strcmp(dest, "hello") == 0)) {
        test_pass();
    } else {
        test_fail("strncpy failed");
    }
}

static void test_strncpy_terminated(void)
{
    char *result;
    char dest[10];

    test_start("strncpy_terminated");
    result = misra_strncpy(dest, "hello world", 10, 10);
    if ((result == dest) && (dest[9] == '\0')) {
        test_pass();
    } else {
        test_fail("strncpy did not null-terminate");
    }
}

static void test_strchr_null(void)
{
    char *result;

    test_start("strchr_null");
    result = misra_strchr(NULL, 'l', 100);
    if (result == NULL) {
        test_pass();
    } else {
        test_fail("expected NULL for NULL string");
    }
}

static void test_strchr_found(void)
{
    char *result;
    char str[100] = "hello";

    test_start("strchr_found");
    result = misra_strchr(str, 'l', 100);
    if ((result != NULL) && (*result == 'l')) {
        test_pass();
    } else {
        test_fail("strchr failed");
    }
}

static void test_strchr_not_found(void)
{
    char *result;
    char str[100] = "hello";

    test_start("strchr_not_found");
    result = misra_strchr(str, 'x', 100);
    if (result == NULL) {
        test_pass();
    } else {
        test_fail("expected NULL for not found");
    }
}

static void test_strstr_null(void)
{
    char *result;

    test_start("strstr_null");
    result = misra_strstr(NULL, "world", 100);
    if (result == NULL) {
        test_pass();
    } else {
        test_fail("expected NULL for NULL haystack");
    }
}

static void test_strstr_found(void)
{
    char *result;
    char haystack[100] = "hello world";

    test_start("strstr_found");
    result = misra_strstr(haystack, "world", 100);
    if ((result != NULL) && (strcmp(result, "world") == 0)) {
        test_pass();
    } else {
        test_fail("strstr failed");
    }
}

static void test_strstr_not_found(void)
{
    char *result;
    char haystack[100] = "hello world";

    test_start("strstr_not_found");
    result = misra_strstr(haystack, "python", 100);
    if (result == NULL) {
        test_pass();
    } else {
        test_fail("expected NULL for not found");
    }
}

static void test_snprintf_null(void)
{
    int result;

    test_start("snprintf_null");
    result = misra_snprintf(NULL, 100, "hello");
    if (result == -1) {
        test_pass();
    } else {
        test_fail("expected -1 for NULL str");
    }
}

static void test_snprintf_valid(void)
{
    int result;
    char buffer[100];

    test_start("snprintf_valid");
    result = misra_snprintf(buffer, 100, "hello %d", 42);
    if ((result > 0) && (strcmp(buffer, "hello 42") == 0)) {
        test_pass();
    } else {
        test_fail("snprintf failed");
    }
}

/* ========================================================================
 * 标准库测试
 * ======================================================================== */

static void test_atoi_null(void)
{
    int result;

    test_start("atoi_null");
    result = misra_atoi(NULL, 100);
    if (result == 0) {
        test_pass();
    } else {
        test_fail("expected 0 for NULL string");
    }
}

static void test_atoi_valid(void)
{
    int result;

    test_start("atoi_valid");
    result = misra_atoi("42", 100);
    if (result == 42) {
        test_pass();
    } else {
        test_fail("expected 42 for '42'");
    }
}

static void test_atoi_negative(void)
{
    int result;

    test_start("atoi_negative");
    result = misra_atoi("-42", 100);
    if (result == -42) {
        test_pass();
    } else {
        test_fail("expected -42 for '-42'");
    }
}

static void test_strtol_valid(void)
{
    long result;

    test_start("strtol_valid");
    result = misra_strtol("42", NULL, 10, 100);
    if (result == 42) {
        test_pass();
    } else {
        test_fail("expected 42 for '42'");
    }
}

static void test_strtol_hex(void)
{
    long result;

    test_start("strtol_hex");
    result = misra_strtol("0xFF", NULL, 0, 100);
    if (result == 255) {
        test_pass();
    } else {
        test_fail("expected 255 for '0xFF'");
    }
}

static void test_abs_positive(void)
{
    int result;

    test_start("abs_positive");
    result = misra_abs(42);
    if (result == 42) {
        test_pass();
    } else {
        test_fail("expected 42 for abs(42)");
    }
}

static void test_abs_negative(void)
{
    int result;

    test_start("abs_negative");
    result = misra_abs(-42);
    if (result == 42) {
        test_pass();
    } else {
        test_fail("expected 42 for abs(-42)");
    }
}

static void test_abs_zero(void)
{
    int result;

    test_start("abs_zero");
    result = misra_abs(0);
    if (result == 0) {
        test_pass();
    } else {
        test_fail("expected 0 for abs(0)");
    }
}

static void test_labs_positive(void)
{
    long result;

    test_start("labs_positive");
    result = misra_labs(42L);
    if (result == 42L) {
        test_pass();
    } else {
        test_fail("expected 42 for labs(42)");
    }
}

static void test_labs_negative(void)
{
    long result;

    test_start("labs_negative");
    result = misra_labs(-42L);
    if (result == 42L) {
        test_pass();
    } else {
        test_fail("expected 42 for labs(-42)");
    }
}

/* ========================================================================
 * 辅助宏测试
 * ======================================================================== */

static void test_macro_strcpy(void)
{
    char dest[100];

    test_start("macro_strcpy");
    if (MISRA_STRCPY(dest, "hello") != NULL) {
        if (strcmp(dest, "hello") == 0) {
            test_pass();
        } else {
            test_fail("MISRA_STRCPY failed");
        }
    } else {
        test_fail("MISRA_STRCPY returned NULL");
    }
}

static void test_macro_strlen(void)
{
    size_t result;

    test_start("macro_strlen");
    result = MISRA_STRLEN("hello");
    if (result == 5) {
        test_pass();
    } else {
        test_fail("MISRA_STRLEN failed");
    }
}

static void test_macro_strcmp(void)
{
    int result;

    test_start("macro_strcmp");
    result = MISRA_STRCMP("hello", "hello");
    if (result == 0) {
        test_pass();
    } else {
        test_fail("MISRA_STRCMP failed");
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  MISRA C:2012 合规包装测试\n");
    printf("========================================\n");
    printf("\n");

    /* 内存操作测试 */
    test_memcpy_null_dest();
    test_memcpy_null_src();
    test_memcpy_valid();
    test_memset_null();
    test_memset_valid();
    test_memcmp_equal();
    test_memcmp_less();

    /* 字符串操作测试 */
    test_strlen_null();
    test_strlen_valid();
    test_strlen_unterminated();
    test_strcmp_null1();
    test_strcmp_equal();
    test_strcmp_less();
    test_strcmp_greater();
    test_strncmp_equal();
    test_strncmp_less();
    test_strcpy_null();
    test_strcpy_valid();
    test_strcpy_buffer_too_small();
    test_strncpy_null();
    test_strncpy_valid();
    test_strncpy_terminated();
    test_strchr_null();
    test_strchr_found();
    test_strchr_not_found();
    test_strstr_null();
    test_strstr_found();
    test_strstr_not_found();
    test_snprintf_null();
    test_snprintf_valid();

    /* 标准库测试 */
    test_atoi_null();
    test_atoi_valid();
    test_atoi_negative();
    test_strtol_valid();
    test_strtol_hex();
    test_abs_positive();
    test_abs_negative();
    test_abs_zero();
    test_labs_positive();
    test_labs_negative();

    /* 辅助宏测试 */
    test_macro_strcpy();
    test_macro_strlen();
    test_macro_strcmp();

    /* 测试总结 */
    printf("\n");
    printf("========================================\n");
    printf("  测试结果\n");
    printf("========================================\n");
    printf("Total:    %d\n", g_test_passed + g_test_failed);
    printf("Passed:   %d ✓\n", g_test_passed);
    printf("Failed:   %d ✗\n", g_test_failed);
    printf("========================================\n");
    printf("\n");

    if (g_test_failed == 0) {
        printf("✓ 所有测试通过\n");
        return 0;
    } else {
        printf("✗ 部分测试失败\n");
        return 1;
    }
}
