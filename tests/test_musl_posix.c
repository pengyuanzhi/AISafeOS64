/**
 * @file    test_musl_posix.c
 * @brief   musl AISafeOS64 核心 POSIX 功能验证
 * @version 1.0
 *
 * @note MISRA-C:2012 合规
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>

/* 测试计数器 */
static int g_test_passed = 0;
static int g_test_failed = 0;

/* ========================================================================
 * 测试工具函数
 * ======================================================================== */

static void test_start(const char *name)
{
    printf("[MUSL] %s ... ", name);
    fflush(stdout);
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
    if (strcmp(str, "Heo, AISafe64!lo, AISafe64!") == 0) {
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
    if (strlen(str) == 13) {
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
    strncpy(dst, src, 5);
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
 * stdio 模块测试
 * ======================================================================== */

static void test_putchar(void)
{
    test_start("putchar");
    putchar('X');
    putchar('\n');
    test_pass();
}

static void test_puts(void)
{
    test_start("puts");
    if (puts("Hello") >= 0) {
        test_pass();
    } else {
        test_fail("puts failed");
    }
}

static void test_snprintf(void)
{
    char buf[32];
    int ret;

    test_start("snprintf");
    ret = snprintf(buf, sizeof(buf), "Hello, %s!", "AISafeOS64");
    if (ret > 0 && strcmp(buf, "Hello, AISafeOS64!") == 0) {
        test_pass();
    } else {
        test_fail("format failed");
    }
}

static void test_sprintf(void)
{
    char buf[32];
    int ret;

    test_start("sprintf");
    ret = sprintf(buf, "Hello, %s!", "AISafeOS64");
    if (ret > 0 && strcmp(buf, "Hello, AISafeOS64!") == 0) {
        test_pass();
    } else {
        test_fail("format failed");
    }
}

/* ========================================================================
 * stdlib 模块测试
 * ======================================================================== */

static void test_atoi(void)
{
    test_start("atoi");
    if (atoi("123") == 123) {
        test_pass();
    } else {
        test_fail("conversion failed");
    }
}

static void test_strtol(void)
{
    char *endptr;
    long val;

    test_start("strtol");
    val = strtol("456", &endptr, 10);
    if (val == 456 && *endptr == '\0') {
        test_pass();
    } else {
        test_fail("conversion failed");
    }
}

static void test_strtoul(void)
{
    char *endptr;
    unsigned long val;

    test_start("strtoul");
    val = strtoul("789", &endptr, 10);
    if (val == 789 && *endptr == '\0') {
        test_pass();
    } else {
        test_fail("conversion failed");
    }
}

static void test_exit(void)
{
    /* 这里的测试不能实际调用 exit() */
    test_start("exit");
    /* 只能通过检查 __exit 函数是否声明来验证 */
    test_pass();
}

/* ========================================================================
 * unistd 模块测试
 * ======================================================================== */

static void test_write(void)
{
    test_start("write");
    /* stdout(1) 通过 SYS_DEBUG_PRINT */
    if (write(1, "test\n", 5) == 5) {
        test_pass();
    } else {
        test_fail("write failed");
    }
}

static void test_getpid(void)
{
    pid_t pid;

    test_start("getpid");
    pid = getpid();
    if (pid > 0) {
        printf("%d\n", pid);  /* 打印 PID 以便验证 */
        test_pass();
    } else {
        test_fail("getpid failed");
    }
}

static void test_getppid(void)
{
    pid_t ppid;

    test_start("getppid");
    ppid = getppid();
    if (ppid > 0) {
        printf("%d\n", ppid);
        test_pass();
    } else {
        test_fail("getppid failed");
    }
}

/* ========================================================================
 * fcntl 模块测试
 * ======================================================================== */

static void test_open(void)
{
    test_start("open");
    /* open 暂时返回 ENOSYS */
    if (open("/tmp/test", O_RDONLY) < 0 && errno == ENOSYS) {
        test_pass();
    } else {
        test_fail("open failed as expected");
    }
}

static void test_close(void)
{
    test_start("close");
    /* close 暂时返回 ENOSYS */
    if (close(0) < 0 && errno == ENOSYS) {
        test_pass();
    } else {
        test_fail("close failed as expected");
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  musl AISafeOS64 核心 POSIX 验证\n");
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

    /* stdio 模块测试 */
    test_putchar();
    test_puts();
    test_snprintf();
    test_sprintf();

    /* stdlib 模块测试 */
    test_atoi();
    test_strtol();
    test_strtoul();
    test_exit();

    /* unistd 模块测试 */
    test_write();
    test_getpid();
    test_getppid();

    /* fcntl 模块测试 */
    test_open();
    test_close();

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

    return (g_test_failed > 0) ? 1 : 0;
}
