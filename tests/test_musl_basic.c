/**
 * @file    test_musl_basic.c
 * @brief   musl AISafeOS64 基础模块验证（简化版）
 * @version 1.0
 *
 * @note MISRA-C:2012 合规
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* 测试计数器 */
static int g_test_passed = 0;
static int g_test_failed = 0;

/* ========================================================================
 * 测试工具函数
 * ======================================================================== */

static void test_start(const char *name)
{
    printf("[MUSL] %s ... ", name);
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
 * string 模块测试
 * ======================================================================== */

static void test_memcpy(void)
{
    const char *src = "Hello, AISafeOS64!";
    char dst[32];

    test_start("memcpy");
    memcpy(dst, src, strlen(src) + 1);
    if (strcmp(dst, src) == 0) {
        test_pass();
    } else {
        test_fail("copy failed");
    }
}

static void test_memmove(void)
{
    char str[32] = "Hello, AISafe64!";

    test_start("memmove");
    memmove(str + 3, str, 10);
    if (strncmp(str, "HelHello, AI", 12) == 0) {
        test_pass();
    } else {
        test_fail("move failed");
    }
}

static void test_memcmp(void)
{
    const char *a = "test";
    const char *b = "test";

    test_start("memcmp");
    if (memcmp(a, b, 4) == 0) {
        test_pass();
    } else {
        test_fail("compare failed");
    }
}

static void test_strlen(void)
{
    const char *str = "Hello, world";

    test_start("strlen");
    if (strlen(str) == 12) {
        test_pass();
    } else {
        test_fail("length incorrect");
    }
}

static void test_strcmp(void)
{
    const char *a = "hello";
    const char *b = "hello";

    test_start("strcmp");
    if (strcmp(a, b) == 0) {
        test_pass();
    } else {
        test_fail("compare failed");
    }
}

static void test_strcpy(void)
{
    const char *src = "test string";
    char dst[32];

    test_start("strcpy");
    strcpy(dst, src);
    if (strcmp(dst, src) == 0) {
        test_pass();
    } else {
        test_fail("copy failed");
    }
}

static void test_strncpy(void)
{
    const char *src = "hello";
    char dst[10];

    test_start("strncpy");
    dst[0] = '\0';  /* 先清零 */
    strncpy(dst, src, 5);
    dst[5] = '\0';  /* 添加 null 终止符 */
    if (strcmp(dst, "hello") == 0) {
        test_pass();
    } else {
        test_fail("copy failed");
    }
}

static void test_strcat(void)
{
    char str[32] = "Hello";

    test_start("strcat");
    strcat(str, ", world");
    if (strcmp(str, "Hello, world") == 0) {
        test_pass();
    } else {
        test_fail("concat failed");
    }
}

static void test_strchr(void)
{
    const char *str = "Hello, world";
    const char *p;

    test_start("strchr");
    p = strchr(str, ',');
    if (p != NULL && *p == ',') {
        test_pass();
    } else {
        test_fail("find failed");
    }
}

static void test_strrchr(void)
{
    const char *str = "Hello, world";
    const char *p;

    test_start("strrchr");
    p = strrchr(str, ',');
    if (p != NULL && *p == ',') {
        test_pass();
    } else {
        test_fail("find failed");
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  musl AISafeOS64 基础模块验证\n");
    printf("========================================\n");
    printf("\n");

    /* string 模块测试 */
    test_memcpy();
    test_memmove();
    test_memcmp();
    test_strlen();
    test_strcmp();
    test_strcpy();
    test_strncpy();
    test_strcat();
    test_strchr();
    test_strrchr();

    /* 打印统计 */
    printf("\n");
    printf("========================================\n");
    printf("  测试结果\n");
    printf("========================================\n");
    printf("Total:    %d\n", g_test_passed + g_test_failed);
    printf("Passed:   %d ✓\n", g_test_passed);
    printf("Failed:   %d ✗\n", g_test_failed);
    printf("========================================\n");
    printf("\n");

    return (g_test_failed > 0) ?1 : 0;
}
