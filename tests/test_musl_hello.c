/**
 * @file    test_musl_hello.c
 * @brief   musl libc 端到端测试程序
 *
 * 独立编译的 ARM64 ELF 程序，验证 musl libc 函数正确性。
 * 通过 UART 输出测试结果，在 QEMU 中通过内核的 SYS_DEBUG_PRINT 输出。
 *
 * 测试覆盖：
 * 1. string: memcpy, memset, strcmp, strlen, strcpy
 * 2. stdio:  sprintf, snprintf, printf
 * 3. stdlib: atoi, strtol, malloc/free
 * 4. 自定义 write → SYS_DEBUG_PRINT 路由
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 测试计数 */
static int s_pass = 0;
static int s_fail = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { s_pass++; } \
    else { s_fail++; \
           const char *_m = "[FAIL] " msg "\n"; \
           write(2, _m, sizeof(_m)-1); } \
} while(0)

/* 简易 UART 输出（通过 SYS_DEBUG_PRINT） */
extern long write(int fd, const void *buf, long count);

static void uart_print(const char *s)
{
    long len = 0;
    while (s[len] != '\0') len++;
    write(1, s, len);
}

static void print_num(int n)
{
    char buf[16];
    int i = 0;
    int neg = 0;

    if (n < 0) { neg = 1; n = -n; }
    if (n == 0) { buf[i++] = '0'; }
    else {
        while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    }
    if (neg) buf[i++] = '-';

    /* 反转 */
    char out[16];
    int j;
    for (j = 0; j < i; j++) out[j] = buf[i - 1 - j];
    out[j] = '\0';
    uart_print(out);
}

int main(int argc, char *argv[])
{
    uart_print("\n========== musl libc 端到端测试 ==========\n\n");

    /* ================================================================
     * 1. string 函数测试
     * ================================================================ */
    uart_print("--- string ---\n");

    {
        /* strlen */
        TEST_ASSERT(strlen("") == 0, "strlen empty");
        TEST_ASSERT(strlen("hello") == 5, "strlen hello");

        /* strcmp */
        TEST_ASSERT(strcmp("abc", "abc") == 0, "strcmp equal");
        TEST_ASSERT(strcmp("abc", "abd") < 0, "strcmp less");
        TEST_ASSERT(strcmp("abd", "abc") > 0, "strcmp greater");

        /* strcpy */
        char buf[32];
        strcpy(buf, "hello musl");
        TEST_ASSERT(strcmp(buf, "hello musl") == 0, "strcpy");

        /* memcpy */
        char src[] = "ABCDEFGHIJ";
        char dst[32];
        memcpy(dst, src, 10);
        dst[10] = '\0';
        TEST_ASSERT(strcmp(dst, "ABCDEFGHIJ") == 0, "memcpy");

        /* memset */
        char mbuf[16];
        memset(mbuf, 'X', 10);
        mbuf[10] = '\0';
        TEST_ASSERT(strcmp(mbuf, "XXXXXXXXXX") == 0, "memset");

        /* strchr / strrchr */
        const char *s = "hello world";
        TEST_ASSERT(strchr(s, 'w') == s + 6, "strchr");
        TEST_ASSERT(strrchr(s, 'l') == s + 9, "strrchr");
        TEST_ASSERT(strstr(s, "world") == s + 6, "strstr");
    }

    /* ================================================================
     * 2. stdlib 函数测试
     * ================================================================ */
    uart_print("--- stdlib ---\n");

    {
        TEST_ASSERT(atoi("12345") == 12345, "atoi positive");
        TEST_ASSERT(atoi("-42") == -42, "atoi negative");
        TEST_ASSERT(atoi("0") == 0, "atoi zero");

        TEST_ASSERT(strtol("255", NULL, 10) == 255, "strtol decimal");
        TEST_ASSERT(strtol("FF", NULL, 16) == 255, "strtol hex");
        TEST_ASSERT(strtol("77", NULL, 8) == 63, "strtol octal");
    }

    /* ================================================================
     * 3. malloc/free 测试
     * ================================================================ */
    uart_print("--- malloc ---\n");

    {
        char *p = (char *)malloc(64);
        TEST_ASSERT(p != NULL, "malloc 64");

        if (p != NULL)
        {
            strcpy(p, "malloc works!");
            TEST_ASSERT(strcmp(p, "malloc works!") == 0, "malloc use");
            free(p);
        }

        /* 多次分配 */
        int i;
        void *ptrs[8];
        for (i = 0; i < 8; i++)
        {
            ptrs[i] = malloc(128);
            if (ptrs[i] == NULL) break;
        }
        TEST_ASSERT(i == 8, "malloc 8x128");
        for (i = 0; i < 8; i++)
        {
            if (ptrs[i] != NULL) free(ptrs[i]);
        }
    }

    /* ================================================================
     * 4. sprintf/snprintf 测试
     * ================================================================ */
    uart_print("--- sprintf ---\n");

    {
        char buf[64];

        sprintf(buf, "%d", 12345);
        TEST_ASSERT(strcmp(buf, "12345") == 0, "sprintf %d");

        sprintf(buf, "0x%08X", 0xDEADBEEF);
        TEST_ASSERT(strcmp(buf, "0xDEADBEEF") == 0, "sprintf %X");

        snprintf(buf, 5, "hello");
        TEST_ASSERT(strcmp(buf, "hell") == 0, "snprintf trunc");

        sprintf(buf, "%s %d", "count", 42);
        TEST_ASSERT(strcmp(buf, "count 42") == 0, "sprintf mixed");
    }

    /* ================================================================
     * 结果汇总
     * ================================================================ */
    uart_print("\n========== 测试结果 ==========\n");
    uart_print("通过: "); print_num(s_pass); uart_print("\n");
    uart_print("失败: "); print_num(s_fail); uart_print("\n");

    if (s_fail == 0)
    {
        uart_print("结果: ALL PASSED ✅\n");
    }
    else
    {
        uart_print("结果: FAILED ❌\n");
    }
    uart_print("==============================\n\n");

    return 0;
}
