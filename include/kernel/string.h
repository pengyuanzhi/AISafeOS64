/**
 * @file    string.h
 * @brief   内核字符串和内存操作函数声明
 * @author  AISafe64 Team
 * @date    2026-04-04
#include <stdbool.h>
 * @version 1.0
 *
 * @details 本文件声明了内核所需的字符串和内存操作函数，
 *          替代标准库 <string.h>，用于 freestanding 环境：
 *          - 内存复制/移动/填充
 *          - 字符串长度/比较/复制/连接
 *          - 字符串搜索
 *          - 字符串转整数
 *
 * @note MISRA-C:2012 合规
 * @note 仅使用 C11 标准特性
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_STRING_H
#define KERNEL_STRING_H

#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

/* ========================================================================
 * 内存操作函数
 * ======================================================================== */

/**
 * @brief 内存复制（不处理重叠区域）
 *
 * @param dest 目标地址
 * @param src  源地址
 * @param n    复制字节数
 *
 * @return 目标地址指针
 *
 * @warning 源和目标区域不得重叠，重叠区域应使用 memmove
 */
void *kernel_memcpy(void *dest, const void *src, size_t n);

/**
 * @brief 内存移动（处理重叠区域）
 *
 * @param dest 目标地址
 * @param src  源地址
 * @param n    移动字节数
 *
 * @return 目标地址指针
 */
void *kernel_memmove(void *dest, const void *src, size_t n);

/**
 * @brief 内存填充
 *
 * @param s    目标地址
 * @param c    填充字节值
 * @param n    填充字节数
 *
 * @return 目标地址指针
 */
void *kernel_memset(void *s, int32_t c, size_t n);

/**
 * @brief 内存比较
 *
 * @param s1 第一个内存区域
 * @param s2 第二个内存区域
 * @param n  比较字节数
 *
 * @return 0 相等，<0 s1<s2，>0 s1>s2
 */
int32_t kernel_memcmp(const void *s1, const void *s2, size_t n);

/**
 * @brief 在内存区域中搜索字节
 *
 * @param s 内存区域
 * @param c 搜索的字节值
 * @param n 搜索字节数
 *
 * @return 找到的位置指针，NULL 表示未找到
 */
void *kernel_memchr(const void *s, int32_t c, size_t n);

/**
 * @brief 内存清零
 *
 * @param s 目标地址
 * @param n 清零字节数
 */
void kernel_memzero(void *s, size_t n);

/* ========================================================================
 * 字符串操作函数
 * ======================================================================== */

/**
 * @brief 计算字符串长度
 *
 * @param s 字符串
 *
 * @return 字符串长度（不含终止符）
 */
size_t kernel_strlen(const char *s);

/**
 * @brief 计算字符串长度（带最大长度限制）
 *
 * @param s    字符串
 * @param maxn 最大检查长度
 *
 * @return 字符串长度
 */
size_t kernel_strnlen(const char *s, size_t maxn);

/**
 * @brief 字符串复制
 *
 * @param dest 目标缓冲区
 * @param src  源字符串
 *
 * @return 目标缓冲区指针
 */
char *kernel_strcpy(char *dest, const char *src);

/**
 * @brief 字符串复制（带最大长度限制）
 *
 * @param dest 目标缓冲区
 * @param src  源字符串
 * @param n    最大复制字符数
 *
 * @return 目标缓冲区指针
 */
char *kernel_strncpy(char *dest, const char *src, size_t n);

/**
 * @brief 字符串连接
 *
 * @param dest 目标缓冲区（已有字符串末尾追加）
 * @param src  源字符串
 *
 * @return 目标缓冲区指针
 */
char *kernel_strcat(char *dest, const char *src);

/**
 * @brief 字符串连接（带最大长度限制）
 *
 * @param dest 目标缓冲区
 * @param src  源字符串
 * @param n    最大追加字符数
 *
 * @return 目标缓冲区指针
 */
char *kernel_strncat(char *dest, const char *src, size_t n);

/**
 * @brief 字符串比较
 *
 * @param s1 第一个字符串
 * @param s2 第二个字符串
 *
 * @return 0 相等，<0 s1<s2，>0 s1>s2
 */
int32_t kernel_strcmp(const char *s1, const char *s2);

/**
 * @brief 字符串比较（带最大长度限制）
 *
 * @param s1  第一个字符串
 * @param s2  第二个字符串
 * @param n   最大比较字符数
 *
 * @return 0 相等，<0 s1<s2，>0 s1>s2
 */
int32_t kernel_strncmp(const char *s1, const char *s2, size_t n);

/**
 * @brief 在字符串中查找字符
 *
 * @param s 字符串
 * @param c 搜索的字符
 *
 * @return 找到的位置指针，NULL 表示未找到
 */
char *kernel_strchr(const char *s, int32_t c);

/**
 * @brief 在字符串中从后查找字符
 *
 * @param s 字符串
 * @param c 搜索的字符
 *
 * @return 找到的位置指针，NULL 表示未找到
 */
char *kernel_strrchr(const char *s, int32_t c);

/**
 * @brief 在字符串中查找子串
 *
 * @param haystack 被搜索的字符串
 * @param needle   要查找的子串
 *
 * @return 找到的位置指针，NULL 表示未找到
 */
char *kernel_strstr(const char *haystack, const char *needle);

/* ========================================================================
 * 字符串转数值函数
 * ======================================================================== */

/**
 * @brief 将字符串转换为 32 位整数
 *
 * @param nptr 字符串
 *
 * @return 转换后的整数值
 */
int32_t kernel_strtol(const char *nptr, char **endptr, int32_t base);

/**
 * @brief 将字符串转换为无符号 32 位整数
 *
 * @param nptr 字符串
 *
 * @return 转换后的无符号整数值
 */
uint32_t kernel_strtoul(const char *nptr, char **endptr, int32_t base);

/* ========================================================================
 * 标准名称别名（兼容 <string.h> 调用）
 * ======================================================================== */

#ifndef __KERNEL_STRING_NO_ALIAS

#define memcpy    kernel_memcpy
#define memmove   kernel_memmove
#define memset    kernel_memset
#define memcmp    kernel_memcmp
#define memchr    kernel_memchr

#define strlen    kernel_strlen
#define strnlen   kernel_strnlen
#define strcpy    kernel_strcpy
#define strncpy   kernel_strncpy
#define strcat    kernel_strcat
#define strncat   kernel_strncat
#define strcmp    kernel_strcmp
#define strncmp   kernel_strncmp
#define strchr    kernel_strchr
#define strrchr   kernel_strrchr
#define strstr    kernel_strstr

#define strtol    kernel_strtol
#define strtoul   kernel_strtoul

/* ========================================================================
 * 格式化输出函数（简化的 snprintf）
 * ======================================================================== */

/**
 * @brief 简化的格式化输出（只支持字符串和整数）
 * @param str 目标缓冲区
 * @param size 缓冲区大小
 * @param fmt 格式字符串
 * @param ... 可变参数
 * @return 写入的字符数
 */
int kernel_snprintf(char *str, size_t size, const char *fmt, ...);

/**
 * @brief snprintf 标准名称别名（兼容性）
 */
#define snprintf kernel_snprintf

#endif /* __KERNEL_STRING_NO_ALIAS */

#endif /* KERNEL_STRING_H */
