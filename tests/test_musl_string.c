/**
 * @file    test_musl_string.c
 * @brief   AISafe-libc string 模块测试
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 覆盖所有 string 函数的基本功能和边界条件测试
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>

/* 简单测试框架 */
static int s_pass = 0;
static int s_fail = 0;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { printf("  FAIL %s:%d: %s\n", __func__, __LINE__, #cond); s_fail++; } \
    else { s_pass++; } \
} while (0)

#define TEST_ASSERT_EQUAL_INT(a, b) TEST_ASSERT((a) == (b))
#define TEST_ASSERT_EQUAL_PTR(a, b) TEST_ASSERT((a) == (b))
#define TEST_ASSERT_EQUAL_STRING(a, b) TEST_ASSERT(strcmp((a), (b)) == 0)
#define TEST_ASSERT_NULL(p) TEST_ASSERT((p) == NULL)
#define TEST_ASSERT_NOT_NULL(p) TEST_ASSERT((p) != NULL)

/* ========================================================================
 * memcpy 测试
 * ======================================================================== */

void test_memcpy_normal(void)
{
    char src[] = "Hello, World!";
    char dst[32] = {0};

    void *ret = memcpy(dst, src, 14U);
    TEST_ASSERT_EQUAL_PTR(ret, dst);
    TEST_ASSERT_EQUAL_STRING(dst, "Hello, World!");
}

void test_memcpy_zero(void)
{
    char dst[4] = { 'A', 'B', 'C', 'D' };
    char src[] = "XXXX";

    void *ret = memcpy(dst, src, 0U);
    TEST_ASSERT_EQUAL_PTR(ret, dst);
    /* 零长度拷贝不应改变 dst */
    TEST_ASSERT(dst[0] == 'A');
}

/* ========================================================================
 * memset 测试
 * ======================================================================== */

void test_memset_normal(void)
{
    char buf[16];

    void *ret = memset(buf, 'X', 10U);
    TEST_ASSERT_EQUAL_PTR(ret, buf);

    size_t i;
    for (i = 0U; i < 10U; i++)
    {
        TEST_ASSERT(buf[i] == 'X');
    }
}

void test_memset_zero(void)
{
    char buf[4] = { 'A', 'B', 'C', 'D' };

    void *ret = memset(buf, 'X', 0U);
    TEST_ASSERT_EQUAL_PTR(ret, buf);
    TEST_ASSERT(buf[0] == 'A');
}

/* ========================================================================
 * memmove 测试
 * ======================================================================== */

void test_memmove_normal(void)
{
    char src[] = "ABCDE";
    char dst[16] = {0};

    void *ret = memmove(dst, src, 6U);
    TEST_ASSERT_EQUAL_PTR(ret, dst);
    TEST_ASSERT_EQUAL_STRING(dst, "ABCDE");
}

void test_memmove_overlap(void)
{
    /* 测试 src < dst 重叠 */
    {
        char buf[] = "ABCDEFGH";
        /* 将 "ABCD" 向右移动 2 字节 -> "ABABCDEF" */
        memmove(buf + 2, buf, 4U);
        TEST_ASSERT(buf[2] == 'A');
        TEST_ASSERT(buf[3] == 'B');
        TEST_ASSERT(buf[4] == 'C');
        TEST_ASSERT(buf[5] == 'D');
    }

    /* 测试 src > dst 重叠 */
    {
        char buf[] = "ABCDEFGH";
        /* 将 "CDEF" 向左移动 2 字节 -> "CDEFGHGH" */
        memmove(buf, buf + 2, 4U);
        TEST_ASSERT(buf[0] == 'C');
        TEST_ASSERT(buf[1] == 'D');
        TEST_ASSERT(buf[2] == 'E');
        TEST_ASSERT(buf[3] == 'F');
    }
}

/* ========================================================================
 * memcmp 测试
 * ======================================================================== */

void test_memcmp_equal(void)
{
    char a[] = "Hello";
    char b[] = "Hello";

    TEST_ASSERT_EQUAL_INT(0, memcmp(a, b, 6U));
}

void test_memcmp_less(void)
{
    char a[] = "Apple";
    char b[] = "Banana";

    TEST_ASSERT(memcmp(a, b, 5U) < 0);
}

void test_memcmp_greater(void)
{
    char a[] = "Banana";
    char b[] = "Apple";

    TEST_ASSERT(memcmp(a, b, 5U) > 0);
}

void test_memcmp_zero(void)
{
    char a[] = "XXX";
    char b[] = "YYY";

    /* 零长度比较应返回 0 */
    TEST_ASSERT_EQUAL_INT(0, memcmp(a, b, 0U));
}

/* ========================================================================
 * memchr 测试
 * ======================================================================== */

void test_memchr_found(void)
{
    char buf[] = "Hello";

    void *p = memchr(buf, 'l', 5U);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_PTR(p, &buf[2]);
}

void test_memchr_not_found(void)
{
    char buf[] = "Hello";

    void *p = memchr(buf, 'z', 5U);
    TEST_ASSERT_NULL(p);
}

/* ========================================================================
 * strlen 测试
 * ======================================================================== */

void test_strlen_normal(void)
{
    TEST_ASSERT_EQUAL_INT((int)strlen("Hello"), 5);
}

void test_strlen_empty(void)
{
    TEST_ASSERT_EQUAL_INT((int)strlen(""), 0);
}

/* ========================================================================
 * strcmp 测试
 * ======================================================================== */

void test_strcmp_equal(void)
{
    TEST_ASSERT_EQUAL_INT(0, strcmp("Hello", "Hello"));
}

void test_strcmp_less(void)
{
    TEST_ASSERT(strcmp("Apple", "Banana") < 0);
}

void test_strcmp_greater(void)
{
    TEST_ASSERT(strcmp("Banana", "Apple") > 0);
}

/* ========================================================================
 * strncmp 测试
 * ======================================================================== */

void test_strncmp_equal(void)
{
    TEST_ASSERT_EQUAL_INT(0, strncmp("Hello", "Hello", 5U));
}

void test_strncmp_limited(void)
{
    /* 仅比较前 3 个字符，应相等 */
    TEST_ASSERT_EQUAL_INT(0, strncmp("Hello", "Helium", 3U));

    /* 比较前 4 个字符，应不等 */
    TEST_ASSERT(strncmp("Hello", "Helium", 4U) > 0);  /* 'l'(108) > 'i'(105) */
}

/* ========================================================================
 * strcpy 测试
 * ======================================================================== */

void test_strcpy_normal(void)
{
    char dst[32];

    char *ret = strcpy(dst, "Hello");
    TEST_ASSERT_EQUAL_PTR(ret, dst);
    TEST_ASSERT_EQUAL_STRING(dst, "Hello");
}

/* ========================================================================
 * strncpy 测试
 * ======================================================================== */

void test_strncpy_normal(void)
{
    char dst[32];

    char *ret = strncpy(dst, "Hello", 32U);
    TEST_ASSERT_EQUAL_PTR(ret, dst);
    TEST_ASSERT_EQUAL_STRING(dst, "Hello");
}

void test_strncpy_truncate(void)
{
    char dst[8];
    const char *long_src = "Hello, World!";

    char *ret = strncpy(dst, long_src, 5U);
    TEST_ASSERT_EQUAL_PTR(ret, dst);
    TEST_ASSERT_EQUAL_INT(dst[0], 'H');
    TEST_ASSERT_EQUAL_INT(dst[1], 'e');
    TEST_ASSERT_EQUAL_INT(dst[2], 'l');
    TEST_ASSERT_EQUAL_INT(dst[3], 'l');
    TEST_ASSERT_EQUAL_INT(dst[4], 'o');
}

/* ========================================================================
 * strcat 测试
 * ======================================================================== */

void test_strcat_normal(void)
{
    char dst[32] = "Hello";

    char *ret = strcat(dst, ", World!");
    TEST_ASSERT_EQUAL_PTR(ret, dst);
    TEST_ASSERT_EQUAL_STRING(dst, "Hello, World!");
}

/* ========================================================================
 * strncat 测试
 * ======================================================================== */

void test_strncat_normal(void)
{
    char dst[32] = "Hello";

    char *ret = strncat(dst, ", World!", 4U);
    TEST_ASSERT_EQUAL_PTR(ret, dst);
    TEST_ASSERT_EQUAL_STRING(dst, "Hello, Wo");
}

/* ========================================================================
 * strchr 测试
 * ======================================================================== */

void test_strchr_found(void)
{
    const char *s = "Hello";

    char *p = strchr(s, 'l');
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(p - s, 2);
}

void test_strchr_not_found(void)
{
    char *p = strchr("Hello", 'z');
    TEST_ASSERT_NULL(p);
}

void test_strchr_null_char(void)
{
    const char *s = "Hello";

    /* 查找终止符 '\0' 应该返回指向字符串末尾的指针 */
    char *p = strchr(s, '\0');
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(p - s, 5);
}

/* ========================================================================
 * strrchr 测试
 * ======================================================================== */

void test_strrchr_found(void)
{
    const char *s = "Hello";

    char *p = strrchr(s, 'l');
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(p - s, 3);
}

void test_strrchr_not_found(void)
{
    char *p = strrchr("Hello", 'z');
    TEST_ASSERT_NULL(p);
}

/* ========================================================================
 * strstr 测试
 * ======================================================================== */

void test_strstr_found(void)
{
    const char *haystack = "Hello, World!";

    char *p = strstr(haystack, "World");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(p - haystack, 7);
}

void test_strstr_not_found(void)
{
    char *p = strstr("Hello, World!", "xyz");
    TEST_ASSERT_NULL(p);
}

void test_strstr_empty_needle(void)
{
    const char *haystack = "Hello";

    /* 空 needle 应返回 haystack */
    char *p = strstr(haystack, "");
    TEST_ASSERT_EQUAL_PTR(p, haystack);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("Running test_musl_string...\n\n");

    /* memcpy */
    test_memcpy_normal();
    test_memcpy_zero();

    /* memset */
    test_memset_normal();
    test_memset_zero();

    /* memmove */
    test_memmove_normal();
    test_memmove_overlap();

    /* memcmp */
    test_memcmp_equal();
    test_memcmp_less();
    test_memcmp_greater();
    test_memcmp_zero();

    /* memchr */
    test_memchr_found();
    test_memchr_not_found();

    /* strlen */
    test_strlen_normal();
    test_strlen_empty();

    /* strcmp */
    test_strcmp_equal();
    test_strcmp_less();
    test_strcmp_greater();

    /* strncmp */
    test_strncmp_equal();
    test_strncmp_limited();

    /* strcpy */
    test_strcpy_normal();

    /* strncpy */
    test_strncpy_normal();
    test_strncpy_truncate();

    /* strcat */
    test_strcat_normal();

    /* strncat */
    test_strncat_normal();

    /* strchr */
    test_strchr_found();
    test_strchr_not_found();
    test_strchr_null_char();

    /* strrchr */
    test_strrchr_found();
    test_strrchr_not_found();

    /* strstr */
    test_strstr_found();
    test_strstr_not_found();
    test_strstr_empty_needle();

    printf("\n========== test_musl_string ==========\n");
    printf("Passed: %d  Failed: %d  Total: %d\n",
           s_pass, s_fail, s_pass + s_fail);

    return s_fail > 0 ? 1 : 0;
}
