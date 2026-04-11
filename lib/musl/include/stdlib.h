/**
 * @file    stdlib.h
 * @brief   标准库函数声明
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 提供 POSIX 标准库函数：
 *          - 字符串转数值：atoi, strtol, strtoul
 *          - 动态内存管理：malloc, calloc, realloc, free
 *          - 程序控制：exit, abort, atexit
 *
 *          malloc 使用简单 bump allocator，64KB 静态内存池
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef AISAFE_STDLIB_H
#define AISAFE_STDLIB_H

#include <sys/types.h>

/* ========================================================================
 * 数值转换函数
 * ======================================================================== */

/**
 * @brief 字符串转换为整数
 * @param nptr 输入字符串
 * @return 转换后的整数值
 */
int atoi(const char *nptr);

/**
 * @brief 字符串转换为长整数
 * @param nptr 输入字符串
 * @param endptr 停止扫描位置（可为 NULL）
 * @param base 进制（2-36，或 0 表示自动检测）
 * @return 转换后的长整数值
 */
long strtol(const char *nptr, char **endptr, int base);

/**
 * @brief 字符串转换为无符号长整数
 * @param nptr 输入字符串
 * @param endptr 停止扫描位置（可为 NULL）
 * @param base 进制（2-36，或 0 表示自动检测）
 * @return 转换后的无符号长整数值
 */
unsigned long strtoul(const char *nptr, char **endptr, int base);

/* ========================================================================
 * 动态内存管理
 * ======================================================================== */

/**
 * @brief 分配内存
 * @param size 请求的字节数
 * @return 成功返回分配的内存指针，失败返回 NULL
 */
void *malloc(size_t size);

/**
 * @brief 分配并清零内存
 * @param nmemb 元素数量
 * @param size 每个元素大小
 * @return 成功返回分配的内存指针，失败返回 NULL
 */
void *calloc(size_t nmemb, size_t size);

/**
 * @brief 重新分配内存
 * @param ptr 原有内存指针（可为 NULL）
 * @param size 新的大小
 * @return 成功返回新的内存指针，失败返回 NULL
 */
void *realloc(void *ptr, size_t size);

/**
 * @brief 释放内存
 * @param ptr 要释放的内存指针（可为 NULL）
 */
void free(void *ptr);

/* ========================================================================
 * 程序控制
 * ======================================================================== */

/** @brief 程序退出处理函数类型 */
typedef void (*atexit_fn)(void);

/**
 * @brief 正常终止程序
 * @param status 退出状态码
 */
void exit(int status);

/**
 * @brief 异常终止程序
 */
void abort(void);

/**
 * @brief 注册退出处理函数
 * @param func 处理函数指针
 * @return 成功返回 0，失败返回非零
 */
int atexit(void (*func)(void));

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief exit 成功状态码 */
#define EXIT_SUCCESS 0

/** @brief exit 失败状态码 */
#define EXIT_FAILURE 1

#endif /* AISAFE_STDLIB_H */
