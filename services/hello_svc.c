/**
 * @file    hello_svc.c
 * @brief   最小用户态冒烟测试（无 musl/crt0 依赖）
 * @details 直接从 _start 入口执行 SVC 系统调用，
 *          不依赖 TLS/errno/crt0 初始化。
 */

#include <stdint.h>

/* 系统调用号 */
#define SYS_DEBUG_PRINT  0x0800U

/* 内联 SVC 调用 */
static inline void svc_debug_print(const char *str, uint32_t len)
{
    register uint64_t x8 asm("x8") = SYS_DEBUG_PRINT;
    register uint64_t x0 asm("x0") = (uint64_t)(uintptr_t)str;
    register uint64_t x1 asm("x1") = (uint64_t)len;
    asm volatile("svc #0" : : "r"(x8), "r"(x0), "r"(x1) : "memory");
}

static uint32_t my_strlen(const char *s)
{
    uint32_t len = 0U;
    while (s[len] != '\0')
    {
        len++;
    }
    return len;
}

/**
 * @brief 用户态入口（链接脚本 ENTRY）
 */
void _start(void)
{
    const char *msg = "Hello from user space!\n";

    svc_debug_print(msg, my_strlen(msg));

    /* 循环等待 */
    for (;;)
    {
        asm volatile("wfe");
    }
}
