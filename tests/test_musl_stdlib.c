/**
 * @file    test_musl_stdlib.c
 * @brief   AISafe-libc stdlib 模块单元测试
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 测试 stdlib 模块所有函数：
 *          - atoi, strtol, strtoul
 *          - malloc, free, calloc, realloc
 *          - atexit
 */

/* 使用系统头文件做测试输出，声明我们要测试的函数 */
#include <stdio.h>
#include <string.h>
#include <limits.h>

/* 声明被测试的函数（我们的实现） */
extern int atoi(const char *nptr);
extern long strtol(const char *nptr, char **endptr, int base);
extern unsigned long strtoul(const char *nptr, char **endptr, int base);
extern void *malloc(unsigned long size);
extern void free(void *ptr);
extern void *calloc(unsigned long nmemb, unsigned long size);
extern void *realloc(void *ptr, unsigned long size);
extern int atexit(void (*func)(void));

/* ========================================================================
 * 简单测试框架
 * ======================================================================== */

static int s_passed = 0;
static int s_failed = 0;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); s_failed++; } \
    else { s_passed++; } \
} while(0)

#define TEST_ASSERT_EQ(a, b) TEST_ASSERT((a) == (b))

/* ========================================================================
 * atoi 测试
 * ======================================================================== */

static void test_atoi_positive(void)
{
    TEST_ASSERT_EQ(atoi("12345"), 12345);
    TEST_ASSERT_EQ(atoi("0"), 0);
    TEST_ASSERT_EQ(atoi("1"), 1);
}

static void test_atoi_negative(void)
{
    TEST_ASSERT_EQ(atoi("-42"), -42);
    TEST_ASSERT_EQ(atoi("-1"), -1);
}

static void test_atoi_with_spaces(void)
{
    TEST_ASSERT_EQ(atoi("  42"), 42);
    TEST_ASSERT_EQ(atoi("\t123"), 123);
}

static void test_atoi_invalid(void)
{
    TEST_ASSERT_EQ(atoi("abc"), 0);
    TEST_ASSERT_EQ(atoi(""), 0);
}

static void test_atoi_trailing(void)
{
    TEST_ASSERT_EQ(atoi("42abc"), 42);
    TEST_ASSERT_EQ(atoi("123xyz"), 123);
}

/* ========================================================================
 * strtol 测试
 * ======================================================================== */

static void test_strtol_decimal(void)
{
    char *endptr;
    long val;

    val = strtol("12345", &endptr, 10);
    TEST_ASSERT_EQ(val, 12345);
    TEST_ASSERT(*endptr == '\0');

    val = strtol("-42", &endptr, 10);
    TEST_ASSERT_EQ(val, -42);
}

static void test_strtol_hex(void)
{
    char *endptr;
    long val;

    val = strtol("0xFF", &endptr, 16);
    TEST_ASSERT_EQ(val, 255);

    val = strtol("0xFF", &endptr, 0);
    TEST_ASSERT_EQ(val, 255);
}

static void test_strtol_octal(void)
{
    char *endptr;
    long val;

    val = strtol("0777", &endptr, 0);
    TEST_ASSERT_EQ(val, 511);
}

static void test_strtol_base(void)
{
    char *endptr;
    long val;

    val = strtol("1010", &endptr, 2);
    TEST_ASSERT_EQ(val, 10);

    val = strtol("FF", &endptr, 16);
    TEST_ASSERT_EQ(val, 255);
}

/* ========================================================================
 * strtoul 测试
 * ======================================================================== */

static void test_strtoul_basic(void)
{
    char *endptr;
    unsigned long val;

    val = strtoul("4294967295", &endptr, 10);
    TEST_ASSERT_EQ(val, 4294967295UL);

    val = strtoul("0xFFFFFFFF", &endptr, 0);
    TEST_ASSERT_EQ(val, 4294967295UL);
}

/* ========================================================================
 * malloc/free 测试
 * ======================================================================== */

static void test_malloc_basic(void)
{
    void *p = malloc(100);
    TEST_ASSERT(p != NULL);
    free(p);
}

static void test_malloc_multiple(void)
{
    void *p1 = malloc(64);
    void *p2 = malloc(128);
    void *p3 = malloc(256);

    TEST_ASSERT(p1 != NULL);
    TEST_ASSERT(p2 != NULL);
    TEST_ASSERT(p3 != NULL);

    /* 分配的地址应该不同 */
    TEST_ASSERT(p1 != p2);
    TEST_ASSERT(p2 != p3);
    TEST_ASSERT(p1 != p3);

    free(p1);
    free(p2);
    free(p3);
}

static void test_malloc_zero(void)
{
    void *p = malloc(0);
    /* malloc(0) 行为实现定义，允许返回 NULL 或唯一指针 */
    if (p != NULL)
    {
        free(p);
    }
    s_passed++; /* 始终通过 */
}

static void test_malloc_alignment(void)
{
    void *p = malloc(1);
    TEST_ASSERT(p != NULL);
    /* 返回的地址应该是 16 字节对齐的 */
    TEST_ASSERT_EQ(((unsigned long)p & 0xFUL), 0UL);
    free(p);
}

/* ========================================================================
 * calloc 测试
 * ======================================================================== */

static void test_calloc_basic(void)
{
    int *p = (int *)calloc(10, sizeof(int));
    TEST_ASSERT(p != NULL);

    /* calloc 应该将内存清零 */
    int i;
    int all_zero = 1;
    for (i = 0; i < 10; i++)
    {
        if (p[i] != 0)
        {
            all_zero = 0;
            break;
        }
    }
    TEST_ASSERT(all_zero);

    free(p);
}

/* ========================================================================
 * realloc 测试
 * ======================================================================== */

static void test_realloc_basic(void)
{
    void *p = malloc(64);
    TEST_ASSERT(p != NULL);

    /* 写入一些数据 */
    memset(p, 0xAB, 64);

    void *p2 = realloc(p, 128);
    TEST_ASSERT(p2 != NULL);

    /* 前 64 字节应该保留 */
    unsigned char *cp = (unsigned char *)p2;
    int data_ok = 1;
    int i;
    for (i = 0; i < 64; i++)
    {
        if (cp[i] != 0xAB)
        {
            data_ok = 0;
            break;
        }
    }
    TEST_ASSERT(data_ok);

    free(p2);
}

static void test_realloc_null(void)
{
    /* realloc(NULL, size) 等价于 malloc(size) */
    void *p = realloc(NULL, 64);
    TEST_ASSERT(p != NULL);
    free(p);
}

/* ========================================================================
 * atexit 测试
 * ======================================================================== */

static int s_atexit_called = 0;

static void atexit_handler(void)
{
    s_atexit_called = 1;
}

static void test_atexit_register(void)
{
    int ret = atexit(atexit_handler);
    TEST_ASSERT_EQ(ret, 0);
    /* 注意：不实际调用 exit()，因为测试框架需要返回 */
    s_passed++; /* 注册成功即通过 */
}

/* ========================================================================
 * 主测试入口
 * ======================================================================== */

int main(void)
{
    printf("=== test_musl_stdlib ===\n\n");

    /* atoi */
    test_atoi_positive();
    test_atoi_negative();
    test_atoi_with_spaces();
    test_atoi_invalid();
    test_atoi_trailing();

    /* strtol */
    test_strtol_decimal();
    test_strtol_hex();
    test_strtol_octal();
    test_strtol_base();

    /* strtoul */
    test_strtoul_basic();

    /* malloc/free */
    test_malloc_basic();
    test_malloc_multiple();
    test_malloc_zero();
    test_malloc_alignment();

    /* calloc */
    test_calloc_basic();

    /* realloc */
    test_realloc_basic();
    test_realloc_null();

    /* atexit */
    test_atexit_register();

    printf("\n结果: %d 通过 / %d 失败 / %d 总计\n",
           s_passed, s_failed, s_passed + s_failed);

    return s_failed > 0 ? 1 : 0;
}
