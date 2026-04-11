/**
 * @file    putchar.c
 * @brief   putchar 实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 输出单个字符到标准输出
 *          使用 write 系统调用实现
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>

/**
 * @brief 输出单个字符到标准输出
 * @param c 要输出的字符
 * @return 成功返回字符，失败返回 EOF
 */
int putchar(int c)
{
    char ch = (char)c;
    long ret;

    /* 使用系统调用 write(1, &ch, 1) */
    __asm__ volatile(
        "mov x0, #1\n"
        "mov x1, %1\n"
        "mov x2, #1\n"
        "mov x8, #64\n"
        "svc #0\n"
        "mov %0, x0\n"
        : "=r"(ret)
        : "r"(&ch)
        : "x0", "x1", "x2", "x8"
    );

    if (ret < 0)
    {
        return EOF;
    }

    return c;
}
