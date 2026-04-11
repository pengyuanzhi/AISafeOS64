/**
 * @file    puts.c
 * @brief   puts 实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 输出字符串到标准输出（附加换行）
 *          使用 write 系统调用实现
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <string.h>

/**
 * @brief 输出字符串到标准输出（附加换行）
 * @param s 要输出的字符串
 * @return 成功返回非负数，失败返回 EOF
 */
int puts(const char *s)
{
    size_t len = strlen(s);
    long ret;

    /* 使用系统调用 write(1, s, len) */
    __asm__ volatile(
        "mov x0, #1\n"
        "mov x1, %1\n"
        "mov x2, %2\n"
        "mov x8, #64\n"
        "svc #0\n"
        "mov %0, x0\n"
        : "=r"(ret)
        : "r"(s), "r"(len)
        : "x0", "x1", "x2", "x8"
    );

    if (ret < 0)
    {
        return EOF;
    }

    /* 输出换行符 */
    char nl = '\n';
    __asm__ volatile(
        "mov x0, #1\n"
        "mov x1, %1\n"
        "mov x2, #1\n"
        "mov x8, #64\n"
        "svc #0\n"
        "mov %0, x0\n"
        : "=r"(ret)
        : "r"(&nl)
        : "x0", "x1", "x2", "x8"
    );

    if (ret < 0)
    {
        return EOF;
    }

    return 0;
}
