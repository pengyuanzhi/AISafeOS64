/**
 * @file    stdio.h
 * @brief   标准输入输出函数声明
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 提供 POSIX 标准格式化输出函数：
 *          - sprintf / snprintf（纯字符串格式化引擎）
 *          - puts / putchar（基于系统调用的输出）
 *
 *          格式化引擎支持：
 *          - %d, %u, %ld, %lu, %x, %X, %p, %s, %c, %%
 *          - 宽度, 精度, 左对齐(-), 前导零(0)
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef AISAFE_STDIO_H
#define AISAFE_STDIO_H

#include <sys/types.h>
#include <stdarg.h>

/* ========================================================================
 * 格式化输出函数
 * ======================================================================== */

/**
 * @brief 格式化字符串到缓冲区（可变参数列表版本，带长度限制）
 * @param buf 目标缓冲区
 * @param size 缓冲区大小
 * @param fmt 格式字符串
 * @param ap 可变参数列表
 * @return 格式化后的字符串长度（不含终止符）
 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

/**
 * @brief 格式化字符串到缓冲区
 * @param buf 目标缓冲区
 * @param fmt 格式字符串
 * @param ... 可变参数
 * @return 写入的字符数（不含终止符）
 */
int sprintf(char *buf, const char *fmt, ...);

/**
 * @brief 格式化字符串到缓冲区（带长度限制）
 * @param buf 目标缓冲区
 * @param size 缓冲区大小
 * @param fmt 格式字符串
 * @param ... 可变参数
 * @return 格式化后的字符串长度（不含终止符）
 */
int snprintf(char *buf, size_t size, const char *fmt, ...);

/**
 * @brief 输出字符串到标准输出（附加换行）
 * @param s 要输出的字符串
 * @return 成功返回非负数，失败返回 EOF
 */
int puts(const char *s);

/**
 * @brief 输出单个字符到标准输出
 * @param c 要输出的字符
 * @return 成功返回字符，失败返回 EOF
 */
int putchar(int c);

/** @brief 文件结束标志 */
#define EOF (-1)

#endif /* AISAFE_STDIO_H */
