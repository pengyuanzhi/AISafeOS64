/**
 * @file    test_musl_safety.c
 * @brief   musl AISafeOS64 功能安全包装测试
 * @version 1.0
 *
 * @note MISRA-C:2012 合规
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>

#include "musl_safety.h"

/* 测试计数器 */
static int g_test_passed = 0;
static int g_test_failed = 0;

/* ========================================================================
 * 测试工具函数
 * ======================================================================== */

static void test_start(const char *name)
{
    printf("[MUSL_SAFETY] %s ... ", name);
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
 * 参数验证测试
 * ======================================================================== */

static void test_validate_pointer_null(void)
{
    int result;

    test_start("validate_pointer_null");
    result = musl_validate_pointer(NULL, 100);
    if (result == -EINVAL) {
        test_pass();
    } else {
        test_fail("expected -EINVAL for NULL pointer");
    }
}

static void test_validate_pointer_valid(void)
{
    int result;
    char buffer[100];

    test_start("validate_pointer_valid");
    result = musl_validate_pointer(buffer, 100);
    if (result == 0) {
        test_pass();
    } else {
        test_fail("expected 0 for valid pointer");
    }
}

static void test_validate_size_too_large(void)
{
    int result;
    char buffer[100];

    test_start("validate_size_too_large");
    result = musl_validate_pointer(buffer, 0x100000000);
    if (result == -EINVAL) {
        test_pass();
    } else {
        test_fail("expected -EINVAL for too large size");
    }
}

static void test_validate_fd_invalid(void)
{
    int result;

    test_start("validate_fd_invalid");
    result = musl_validate_fd(-1);
    if (result == -EBADF) {
        test_pass();
    } else {
        test_fail("expected -EBADF for invalid fd");
    }
}

static void test_validate_fd_valid(void)
{
    int result;

    test_start("validate_fd_valid");
    result = musl_validate_fd(0);
    if (result == 0) {
        test_pass();
    } else {
        test_fail("expected 0 for valid fd");
    }
}

static void test_validate_string_null(void)
{
    int result;

    test_start("validate_string_null");
    result = musl_validate_string(NULL, 100);
    if (result == -EINVAL) {
        test_pass();
    } else {
        test_fail("expected -EINVAL for NULL string");
    }
}

static void test_validate_string_unterminated(void)
{
    int result;
    char buffer[100];

    test_start("validate_string_unterminated");

    /* 创建一个没有 NULL 终止符的字符串 */
    memset(buffer, 'X', sizeof(buffer));
    strncpy(buffer, "no_null", 7);  /* 不复制 NULL 终止符 */

    result = musl_validate_string(buffer, 100);
    if (result == -EINVAL) {
        test_pass();
    } else {
        test_fail("expected -EINVAL for unterminated string");
    }
}

static void test_validate_string_valid(void)
{
    int result;
    char buffer[100] = "hello";

    test_start("validate_string_valid");
    result = musl_validate_string(buffer, 100);
    if (result == 0) {
        test_pass();
    } else {
        test_fail("expected 0 for valid string");
    }
}

/* ========================================================================
 * 审计日志测试
 * ======================================================================== */

static void test_audit_log_init(void)
{
    int result;

    test_start("audit_log_init");
    result = musl_audit_log_init();
    if (result == 0) {
        test_pass();
    } else {
        test_fail("expected 0 for successful init");
    }
}

static void test_audit_log_syscall(void)
{
    int result;

    test_start("audit_log_syscall");
    result = musl_audit_log_syscall(SYS_OPEN, 0, (uintptr_t)"test.txt", O_RDONLY, 0);
    if (result == 0) {
        test_pass();
    } else {
        test_fail("expected 0 for successful audit log");
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  musl AISafeOS64 功能安全包装测试\n");
    printf("========================================\n");
    printf("\n");

    /* 参数验证测试 */
    test_validate_pointer_null();
    test_validate_pointer_valid();
    test_validate_size_too_large();
    test_validate_fd_invalid();
    test_validate_fd_valid();
    test_validate_string_null();
    test_validate_string_unterminated();
    test_validate_string_valid();

    /* 审计日志测试 */
    test_audit_log_init();
    test_audit_log_syscall();

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
