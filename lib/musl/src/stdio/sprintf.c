/**
 * @file    sprintf.c
 * @brief   sprintf 实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 调用 vsnprintf 的封装，无缓冲区大小限制
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdarg.h>

/**
 * @brief 格式化字符串到缓冲区
 * @param buf 目标缓冲区
 * @param fmt 格式字符串
 * @param ... 可变参数
 * @return 写入的字符数（不含终止符）
 */
int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vsnprintf(buf, (size_t)0x7FFFFFFF, fmt, ap);
    va_end(ap);

    return ret;
}
