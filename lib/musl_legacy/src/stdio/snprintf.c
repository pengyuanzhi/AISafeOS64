/**
 * @file    snprintf.c
 * @brief   snprintf 实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 调用 vsnprintf 的封装（带缓冲区大小限制）
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdarg.h>

/**
 * @brief 格式化字符串到缓冲区（带长度限制）
 * @param buf 目标缓冲区
 * @param size 缓冲区大小
 * @param fmt 格式字符串
 * @param ... 可变参数
 * @return 格式化后的字符串长度（不含终止符）
 */
int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);

    return ret;
}
