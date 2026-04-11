/**
 * @file    test_musl_stdio.c
 * @brief   musl stdio 格式化输出单元测试
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 测试 sprintf / snprintf 的格式化引擎：
 *          - 整数格式化（%d, %u, %ld, %lu）
 *          - 十六进制格式化（%x, %X）
 *          - 指针格式化（%p）
 *          - 字符串格式化（%s, NULL 处理）
 *          - 字符格式化（%c）
 *          - 百分号转义（%%）
 *          - 宽度、对齐、零填充
 *          - 精度控制
 *          - snprintf 截断行为
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 自包含测试框架
 * ======================================================================== */
#include <stdio.h>
static unsigned int s_total = 0U, s_passed = 0U, s_failed = 0U;

#define TEST_ASSERT(cond) \
    do { \
        s_total++; \
        if (cond) { s_passed++; } \
        else { s_failed++; printf("  FAIL: %s (line %d)\n", #cond, __LINE__); } \
    } while (0)

#define TEST_ASSERT_EQ(a, b) TEST_ASSERT((a) == (b))
#define TEST_ASSERT_NE(a, b) TEST_ASSERT((a) != (b))
#define TEST_ASSERT_NULL(p) TEST_ASSERT((p) == (void *)0)
#define TEST_ASSERT_NOT_NULL(p) TEST_ASSERT((p) != (void *)0)

/* ========================================================================
 * 包含 musl 头文件
 * ======================================================================== */
#include "stdio.h"
#include "string.h"
#include "sys/types.h"

/* ========================================================================
 * 测试用例：sprintf %d 有符号整数
 * ======================================================================== */

/**
 * @brief 测试 %d 正数格式化
 */
static void test_sprintf_d_positive(void)
{
    char buf[128];
    int ret;

    ret = sprintf(buf, "%d", 42);
    TEST_ASSERT_EQ(ret, 2);
    TEST_ASSERT_EQ(strcmp(buf, "42"), 0);

    ret = sprintf(buf, "%d", 12345);
    TEST_ASSERT_EQ(ret, 5);
    TEST_ASSERT_EQ(strcmp(buf, "12345"), 0);

    ret = sprintf(buf, "%d", 1);
    TEST_ASSERT_EQ(ret, 1);
    TEST_ASSERT_EQ(strcmp(buf, "1"), 0);
}

/**
 * @brief 测试 %d 负数格式化
 */
static void test_sprintf_d_negative(void)
{
    char buf[128];
    int ret;

    ret = sprintf(buf, "%d", -42);
    TEST_ASSERT_EQ(ret, 3);
    TEST_ASSERT_EQ(strcmp(buf, "-42"), 0);

    ret = sprintf(buf, "%d", -1);
    TEST_ASSERT_EQ(ret, 2);
    TEST_ASSERT_EQ(strcmp(buf, "-1"), 0);
}

/**
 * @brief 测试 %d 零值格式化
 */
static void test_sprintf_d_zero(void)
{
    char buf[128];
    int ret;

    ret = sprintf(buf, "%d", 0);
    TEST_ASSERT_EQ(ret, 1);
    TEST_ASSERT_EQ(strcmp(buf, "0"), 0);
}

/* ========================================================================
 * 测试用例：sprintf %u 无符号整数
 * ======================================================================== */

/**
 * @brief 测试 %u 基本无符号格式化
 */
static void test_sprintf_u_basic(void)
{
    char buf[128];
    int ret;

    ret = sprintf(buf, "%u", 0U);
    TEST_ASSERT_EQ(ret, 1);
    TEST_ASSERT_EQ(strcmp(buf, "0"), 0);

    ret = sprintf(buf, "%u", 4294967295U);
    TEST_ASSERT_EQ(ret, 10);
    TEST_ASSERT_EQ(strcmp(buf, "4294967295"), 0);

    ret = sprintf(buf, "%u", 12345U);
    TEST_ASSERT_EQ(ret, 5);
    TEST_ASSERT_EQ(strcmp(buf, "12345"), 0);
}

/* ========================================================================
 * 测试用例：sprintf %ld / %lu 长整型
 * ======================================================================== */

/**
 * @brief 测试 %ld 和 %lu 长整型格式化
 */
static void test_sprintf_long(void)
{
    char buf[128];
    int ret;

    ret = sprintf(buf, "%ld", (long)123456789L);
    TEST_ASSERT_EQ(ret, 9);
    TEST_ASSERT_EQ(strcmp(buf, "123456789"), 0);

    ret = sprintf(buf, "%ld", (long)-99999L);
    TEST_ASSERT_EQ(ret, 6);
    TEST_ASSERT_EQ(strcmp(buf, "-99999"), 0);

    ret = sprintf(buf, "%lu", (unsigned long)4294967295UL);
    TEST_ASSERT_EQ(ret, 10);
    TEST_ASSERT_EQ(strcmp(buf, "4294967295"), 0);
}

/* ========================================================================
 * 测试用例：sprintf %x / %X 十六进制
 * ======================================================================== */

/**
 * @brief 测试 %x / %X 十六进制格式化
 */
static void test_sprintf_hex(void)
{
    char buf[128];
    int ret;

    ret = sprintf(buf, "%x", 255);
    TEST_ASSERT_EQ(ret, 2);
    TEST_ASSERT_EQ(strcmp(buf, "ff"), 0);

    ret = sprintf(buf, "%X", 255);
    TEST_ASSERT_EQ(ret, 2);
    TEST_ASSERT_EQ(strcmp(buf, "FF"), 0);

    ret = sprintf(buf, "%x", 0);
    TEST_ASSERT_EQ(ret, 1);
    TEST_ASSERT_EQ(strcmp(buf, "0"), 0);

    ret = sprintf(buf, "%x", 0xDEAD);
    TEST_ASSERT_EQ(ret, 4);
    TEST_ASSERT_EQ(strcmp(buf, "dead"), 0);

    ret = sprintf(buf, "%X", 0xBEEF);
    TEST_ASSERT_EQ(ret, 4);
    TEST_ASSERT_EQ(strcmp(buf, "BEEF"), 0);
}

/* ========================================================================
 * 测试用例：sprintf %p 指针
 * ======================================================================== */

/**
 * @brief 测试 %p 指针格式化
 */
static void test_sprintf_pointer(void)
{
    char buf[128];
    int ret;
    size_t len;
    int has_prefix;

    ret = sprintf(buf, "%p", (void *)0);
    TEST_ASSERT(ret > 0);

    ret = sprintf(buf, "%p", (void *)0x1234);
    TEST_ASSERT(ret > 0);
    /* %p 应输出 "0x" 前缀 */
    has_prefix = (buf[0] == '0' && buf[1] == 'x') ? 1 : 0;
    TEST_ASSERT_EQ(has_prefix, 1);

    ret = sprintf(buf, "%p", (void *)0xDEADBEEF);
    len = (size_t)ret;
    TEST_ASSERT(len > 2U);
    has_prefix = (buf[0] == '0' && buf[1] == 'x') ? 1 : 0;
    TEST_ASSERT_EQ(has_prefix, 1);
}

/* ========================================================================
 * 测试用例：sprintf %s 字符串
 * ======================================================================== */

/**
 * @brief 测试 %s 字符串格式化
 */
static void test_sprintf_string(void)
{
    char buf[128];
    int ret;

    ret = sprintf(buf, "%s", "hello");
    TEST_ASSERT_EQ(ret, 5);
    TEST_ASSERT_EQ(strcmp(buf, "hello"), 0);

    ret = sprintf(buf, "%s", "");
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQ(strcmp(buf, ""), 0);

    /* NULL 字符串应输出 "(null)" */
    ret = sprintf(buf, "%s", (char *)0);
    TEST_ASSERT_EQ(ret, 6);
    TEST_ASSERT_EQ(strcmp(buf, "(null)"), 0);
}

/* ========================================================================
 * 测试用例：sprintf %c 字符
 * ======================================================================== */

/**
 * @brief 测试 %c 字符格式化
 */
static void test_sprintf_char(void)
{
    char buf[128];
    int ret;

    ret = sprintf(buf, "%c", 'A');
    TEST_ASSERT_EQ(ret, 1);
    TEST_ASSERT_EQ(buf[0], 'A');

    ret = sprintf(buf, "%c", 'z');
    TEST_ASSERT_EQ(ret, 1);
    TEST_ASSERT_EQ(buf[0], 'z');

    ret = sprintf(buf, "%c", 0);
    TEST_ASSERT_EQ(ret, 1);
    TEST_ASSERT_EQ(buf[0], '\0');
}

/* ========================================================================
 * 测试用例：sprintf %% 百分号转义
 * ======================================================================== */

/**
 * @brief 测试 %% 百分号转义
 */
static void test_sprintf_percent(void)
{
    char buf[128];
    int ret;

    ret = sprintf(buf, "%%");
    TEST_ASSERT_EQ(ret, 1);
    TEST_ASSERT_EQ(buf[0], '%');

    ret = sprintf(buf, "100%%");
    TEST_ASSERT_EQ(ret, 4);
    TEST_ASSERT_EQ(strcmp(buf, "100%"), 0);
}

/* ========================================================================
 * 测试用例：sprintf 宽度与对齐
 * ======================================================================== */

/**
 * @brief 测试宽度指定符 %10d（右对齐）
 */
static void test_sprintf_width_right(void)
{
    char buf[128];
    int ret;

    ret = sprintf(buf, "%10d", 42);
    TEST_ASSERT_EQ(ret, 10);
    TEST_ASSERT_EQ(strcmp(buf, "        42"), 0);
}

/**
 * @brief 测试左对齐 %-10d
 */
static void test_sprintf_width_left(void)
{
    char buf[128];
    int ret;

    ret = sprintf(buf, "%-10d", 42);
    TEST_ASSERT_EQ(ret, 10);
    TEST_ASSERT_EQ(strcmp(buf, "42        "), 0);
}

/* ========================================================================
 * 测试用例：sprintf 零填充
 * ======================================================================== */

/**
 * @brief 测试零填充 %05d
 */
static void test_sprintf_zero_pad(void)
{
    char buf[128];
    int ret;

    ret = sprintf(buf, "%05d", 42);
    TEST_ASSERT_EQ(ret, 5);
    TEST_ASSERT_EQ(strcmp(buf, "00042"), 0);

    ret = sprintf(buf, "%05d", -42);
    TEST_ASSERT_EQ(ret, 5);
    TEST_ASSERT_EQ(strcmp(buf, "-0042"), 0);
}

/* ========================================================================
 * 测试用例：sprintf 精度
 * ======================================================================== */

/**
 * @brief 测试字符串精度 %.5s（截断）
 */
static void test_sprintf_precision_string(void)
{
    char buf[128];
    int ret;

    ret = sprintf(buf, "%.5s", "hello world");
    TEST_ASSERT_EQ(ret, 5);
    TEST_ASSERT_EQ(strcmp(buf, "hello"), 0);

    ret = sprintf(buf, "%.3s", "hello");
    TEST_ASSERT_EQ(ret, 3);
    TEST_ASSERT_EQ(strcmp(buf, "hel"), 0);

    ret = sprintf(buf, "%.10s", "hi");
    TEST_ASSERT_EQ(ret, 2);
    TEST_ASSERT_EQ(strcmp(buf, "hi"), 0);
}

/**
 * @brief 测试整数精度 %.3d（最小位数）
 */
static void test_sprintf_precision_int(void)
{
    char buf[128];
    int ret;

    ret = sprintf(buf, "%.3d", 42);
    TEST_ASSERT_EQ(ret, 3);
    TEST_ASSERT_EQ(strcmp(buf, "042"), 0);

    ret = sprintf(buf, "%.5d", 42);
    TEST_ASSERT_EQ(ret, 5);
    TEST_ASSERT_EQ(strcmp(buf, "00042"), 0);

    ret = sprintf(buf, "%.3d", 12345);
    TEST_ASSERT_EQ(ret, 5);
    TEST_ASSERT_EQ(strcmp(buf, "12345"), 0);
}

/* ========================================================================
 * 测试用例：sprintf 组合格式化
 * ======================================================================== */

/**
 * @brief 测试多个参数组合
 */
static void test_sprintf_combined(void)
{
    char buf[256];
    int ret;

    ret = sprintf(buf, "%s has %d apples, price: %x", "Alice", 5, 255);
    TEST_ASSERT(ret > 0);
    TEST_ASSERT(strcmp(buf, "Alice has 5 apples, price: ff") == 0);

    ret = sprintf(buf, "[%10d] %-10s %05x", 42, "test", 0xAB);
    TEST_ASSERT(ret > 0);
    TEST_ASSERT(strcmp(buf, "[        42] test       000ab") == 0);
}

/* ========================================================================
 * 测试用例：snprintf 截断行为
 * ======================================================================== */

/**
 * @brief 测试 snprintf 正常截断
 */
static void test_snprintf_truncate(void)
{
    char buf[10];
    int ret;

    ret = snprintf(buf, 10, "hello world");
    /* 返回值为格式化后长度（不含终止符），即 11 */
    TEST_ASSERT_EQ(ret, 11);
    /* 缓冲区内容应被截断并正确终止 */
    TEST_ASSERT_EQ((int)strlen(buf), 9);
    TEST_ASSERT_EQ(strcmp(buf, "hello wor"), 0);
}

/**
 * @brief 测试 snprintf size=0
 */
static void test_snprintf_size_zero(void)
{
    char buf[10];
    int ret;

    buf[0] = 'X';
    ret = snprintf(buf, 0, "hello");
    /* 返回值为格式化后长度，缓冲区不变 */
    TEST_ASSERT_EQ(ret, 5);
    TEST_ASSERT_EQ(buf[0], 'X');
}

/**
 * @brief 测试 snprintf 精确大小
 */
static void test_snprintf_exact_fit(void)
{
    char buf[6];
    int ret;

    ret = snprintf(buf, 6, "hello");
    TEST_ASSERT_EQ(ret, 5);
    TEST_ASSERT_EQ(strcmp(buf, "hello"), 0);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

/**
 * @brief 测试主入口
 * @return 失败数 > 0 返回 1，否则返回 0
 */
int main(void)
{
    printf("=== test_musl_stdio ===\n");

    /* %d 测试 */
    test_sprintf_d_positive();
    test_sprintf_d_negative();
    test_sprintf_d_zero();

    /* %u 测试 */
    test_sprintf_u_basic();

    /* %ld / %lu 测试 */
    test_sprintf_long();

    /* %x / %X 测试 */
    test_sprintf_hex();

    /* %p 测试 */
    test_sprintf_pointer();

    /* %s 测试 */
    test_sprintf_string();

    /* %c 测试 */
    test_sprintf_char();

    /* %% 测试 */
    test_sprintf_percent();

    /* 宽度与对齐测试 */
    test_sprintf_width_right();
    test_sprintf_width_left();

    /* 零填充测试 */
    test_sprintf_zero_pad();

    /* 精度测试 */
    test_sprintf_precision_string();
    test_sprintf_precision_int();

    /* 组合格式化测试 */
    test_sprintf_combined();

    /* snprintf 测试 */
    test_snprintf_truncate();
    test_snprintf_size_zero();
    test_snprintf_exact_fit();

    printf("结果: %u 通过 / %u 失败 / %u 总计\n", s_passed, s_failed, s_total);
    return (s_failed > 0U) ? 1 : 0;
}
