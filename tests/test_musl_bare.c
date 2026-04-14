/**
 * @file    test_musl_bare.c
 * @brief   musl libc 裸机端到端测试（无启动代码依赖）
 *
 * 直接在裸机环境下调用 musl 函数进行验证。
 * 不依赖 __libc_start_main / crt 启动代码。
 * 通过内核 SYS_DEBUG_PRINT 进行输出。
 *
 * 编译方式：aarch64-linux-gnu-gcc -static -nostartfiles -nostdlib
 *   链接 libmusl_aisafe.a + 手动提供 _start 入口
 */

/* musl 头文件 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* AISafeOS64 内核 SVC 调用 — SYS_DEBUG_PRINT */
#define AISAFE_SYS_DEBUG_PRINT  0x0500

static inline long aisafe_svc2(long nr, long a0, long a1)
{
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    __asm__ __volatile__("svc #0"
        : "=r"(x0)
        : "r"(x8), "0"(x0), "r"(x1)
        : "memory", "cc");
    return x0;
}

/* 通过内核调试输出打印字符串 */
static void uart_puts(const char *s)
{
    long len = 0;
    while (s[len] != '\0') len++;
    aisafe_svc2(AISAFE_SYS_DEBUG_PRINT, (long)(uintptr_t)s, len);
}

static void print_num(int n)
{
    char buf[16];
    int i = 0;
    int neg = 0;
    int j;
    char out[16];

    if (n < 0) { neg = 1; n = -n; }
    if (n == 0) { buf[i++] = '0'; }
    else {
        while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    }
    if (neg) buf[i++] = '-';

    for (j = 0; j < i; j++) out[j] = buf[i - 1 - j];
    out[j] = '\0';
    uart_puts(out);
}

/* 测试计数 */
static int s_pass = 0;
static int s_fail = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { s_pass++; } \
    else { s_fail++; uart_puts("[FAIL] " msg "\n"); } \
} while(0)

/* 线程退出 SVC */
#define AISAFE_SYS_THREAD_EXIT  0x0002

__attribute__((noreturn))
static void thread_exit(int code)
{
    register long x8 __asm__("x8") = AISAFE_SYS_THREAD_EXIT;
    register long x0 __asm__("x0") = (long)code;
    __asm__ __volatile__("svc #0" : : "r"(x8), "r"(x0) : "memory");
    __builtin_unreachable();
}

/**
 * @brief musl 测试入口（由内核创建 EL0 线程时调用）
 */
void musl_test_main(void *arg)
{
    (void)arg;

    uart_puts("\n===== musl libc ARM64 端到端测试 =====\n\n");

    /* ================================================================
     * 1. string 函数
     * ================================================================ */
    uart_puts("[STR] strlen...");
    TEST_ASSERT(strlen("") == 0, "strlen empty");
    TEST_ASSERT(strlen("hello") == 5, "strlen hello");
    uart_puts("OK\n");

    uart_puts("[STR] strcmp...");
    TEST_ASSERT(strcmp("abc", "abc") == 0, "strcmp eq");
    TEST_ASSERT(strcmp("abc", "abd") < 0, "strcmp lt");
    TEST_ASSERT(strcmp("abd", "abc") > 0, "strcmp gt");
    uart_puts("OK\n");

    uart_puts("[STR] strcpy...");
    {
        char buf[32];
        strcpy(buf, "hello musl");
        TEST_ASSERT(strcmp(buf, "hello musl") == 0, "strcpy");
    }
    uart_puts("OK\n");

    uart_puts("[STR] memcpy...");
    {
        const char src[] = "ABCDEFGHIJ";
        char dst[32];
        memcpy(dst, src, 10);
        dst[10] = '\0';
        TEST_ASSERT(strcmp(dst, "ABCDEFGHIJ") == 0, "memcpy");
    }
    uart_puts("OK\n");

    uart_puts("[STR] memset...");
    {
        char mbuf[16];
        memset(mbuf, 'X', 10);
        mbuf[10] = '\0';
        TEST_ASSERT(strcmp(mbuf, "XXXXXXXXXX") == 0, "memset");
    }
    uart_puts("OK\n");

    uart_puts("[STR] strchr/strrchr/strstr...");
    {
        const char *s = "hello world";
        TEST_ASSERT(strchr(s, 'w') == s + 6, "strchr");
        TEST_ASSERT(strrchr(s, 'l') == s + 9, "strrchr");
        TEST_ASSERT(strstr(s, "world") == s + 6, "strstr");
    }
    uart_puts("OK\n");

    /* ================================================================
     * 2. stdlib 函数
     * ================================================================ */
    uart_puts("[STDLIB] atoi...");
    TEST_ASSERT(atoi("12345") == 12345, "atoi+");
    TEST_ASSERT(atoi("-42") == -42, "atoi-");
    TEST_ASSERT(atoi("0") == 0, "atoi0");
    uart_puts("OK\n");

    uart_puts("[STDLIB] strtol...");
    TEST_ASSERT(strtol("255", NULL, 10) == 255, "strtol dec");
    TEST_ASSERT(strtol("FF", NULL, 16) == 255, "strtol hex");
    TEST_ASSERT(strtol("77", NULL, 8) == 63, "strtol oct");
    uart_puts("OK\n");

    /* ================================================================
     * 3. malloc/free
     * ================================================================ */
    uart_puts("[MALLOC] basic...");
    {
        char *p = (char *)malloc(64);
        TEST_ASSERT(p != NULL, "malloc 64");
        if (p != NULL)
        {
            strcpy(p, "malloc OK!");
            TEST_ASSERT(strcmp(p, "malloc OK!") == 0, "malloc use");
            free(p);
        }
    }
    uart_puts("OK\n");

    uart_puts("[MALLOC] multi...");
    {
        void *ptrs[8];
        int i;
        int ok = 1;
        for (i = 0; i < 8; i++)
        {
            ptrs[i] = malloc(128);
            if (ptrs[i] == NULL) { ok = 0; break; }
        }
        TEST_ASSERT(ok, "malloc 8x128");
        for (i = 0; i < 8; i++)
        {
            if (ptrs[i] != NULL) free(ptrs[i]);
        }
    }
    uart_puts("OK\n");

    /* ================================================================
     * 4. sprintf/snprintf
     * ================================================================ */
    uart_puts("[STDIO] sprintf...");
    {
        char buf[64];
        sprintf(buf, "%d", 12345);
        TEST_ASSERT(strcmp(buf, "12345") == 0, "sprintf %d");

        sprintf(buf, "0x%08X", 0xDEADBEEFU);
        TEST_ASSERT(strcmp(buf, "0xDEADBEEF") == 0, "sprintf %X");

        snprintf(buf, 5, "hello");
        TEST_ASSERT(strcmp(buf, "hell") == 0, "snprintf trunc");

        sprintf(buf, "%s %d", "count", 42);
        TEST_ASSERT(strcmp(buf, "count 42") == 0, "sprintf mix");
    }
    uart_puts("OK\n");

    /* ================================================================
     * 结果
     * ================================================================ */
    uart_puts("\n===== musl 测试结果 =====\n");
    uart_puts("通过: "); print_num(s_pass); uart_puts("\n");
    uart_puts("失败: "); print_num(s_fail); uart_puts("\n");

    if (s_fail == 0)
    {
        uart_puts("[musl] ALL PASSED ✅\n");
    }
    else
    {
        uart_puts("[musl] FAILED ❌\n");
    }
    uart_puts("=========================\n\n");

    thread_exit(0);
}
