/**
 * @file    hello_start.c
 * @brief   测试启动代码和 C 运行时（简化版）
 * @version 2.0
 */

#include <stdint.h>
#include <kernel/syscall.h>

/* 简单的字符串长度函数 */
static size_t my_strlen(const char *s)
{
    const char *p = s;
    while (*p)
    {
        p++;
    }
    return (size_t)(p - s);
}

/* 简单的字符串复制函数 */
static char *my_strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++) != '\0')
    {
    }
    return dest;
}

int main(void)
{
    /* 测试内核系统调用 */
    syscall2(SYS_DEBUG_PRINT, (uint64_t)"=== Hello from _start.c ===\n\n", 30);

    /* 测试简单字符串函数 */
    const char *msg = "musl_aisafe 启动代码测试\n";
    syscall2(SYS_DEBUG_PRINT, (uint64_t)msg, (uint64_t)my_strlen(msg));

    /* 测试字符串复制 */
    char buffer[100];
    my_strcpy(buffer, "✓ 字符串复制测试\n");
    syscall2(SYS_DEBUG_PRINT, (uint64_t)buffer, (uint64_t)my_strlen(buffer));

    /* 测试内核系统调用 */
    syscall2(SYS_DEBUG_PRINT, (uint64_t)"✓ kernel/syscall.h 正常工作\n", 40);

    /* 测试完成 */
    syscall2(SYS_DEBUG_PRINT, (uint64_t)"\n=== 测试完成 ===\n", 20);

    return 0;
}
