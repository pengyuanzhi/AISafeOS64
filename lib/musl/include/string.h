/**
 * @file    string.h
 * @brief   字符串操作函数声明
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 提供 POSIX 标准字符串操作函数：
 *          - 内存操作：memcpy, memset, memmove, memcmp, memchr
 *          - 字符串操作：strlen, strcmp, strncmp, strcpy, strncpy
 *          - 字符串连接：strcat, strncat
 *          - 字符串查找：strchr, strrchr, strstr
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef AISAFE_STRING_H
#define AISAFE_STRING_H

#include <sys/types.h>

/* ========================================================================
 * 内存操作函数
 * ======================================================================== */

/**
 * @brief 内存拷贝
 * @param dst 目标地址
 * @param src 源地址
 * @param n 拷贝字节数
 * @return 目标地址
 */
void *memcpy(void *dst, const void *src, size_t n);

/**
 * @brief 内存填充
 * @param dst 目标地址
 * @param c 填充值
 * @param n 填充字节数
 * @return 目标地址
 */
void *memset(void *dst, int c, size_t n);

/**
 * @brief 内存移动（支持重叠区域）
 * @param dst 目标地址
 * @param src 源地址
 * @param n 移动字节数
 * @return 目标地址
 */
void *memmove(void *dst, const void *src, size_t n);

/**
 * @brief 内存比较
 * @param s1 第一块内存
 * @param s2 第二块内存
 * @param n 比较字节数
 * @return 0=相等, <0=s1<s2, >0=s1>s2
 */
int memcmp(const void *s1, const void *s2, size_t n);

/**
 * @brief 内存中查找字符
 * @param s 内存起始地址
 * @param c 要查找的字符
 * @param n 搜索字节数
 * @return 找到返回指针，未找到返回 NULL
 */
void *memchr(const void *s, int c, size_t n);

/* ========================================================================
 * 字符串操作函数
 * ======================================================================== */

/**
 * @brief 计算字符串长度
 * @param s 输入字符串
 * @return 字符串长度（不含终止符）
 */
size_t strlen(const char *s);

/**
 * @brief 字符串比较
 * @param s1 第一个字符串
 * @param s2 第二个字符串
 * @return 0=相等, <0=s1<s2, >0=s1>s2
 */
int strcmp(const char *s1, const char *s2);

/**
 * @brief 字符串比较（限制长度）
 * @param s1 第一个字符串
 * @param s2 第二个字符串
 * @param n 最大比较字符数
 * @return 0=相等, <0=s1<s2, >0=s1>s2
 */
int strncmp(const char *s1, const char *s2, size_t n);

/**
 * @brief 字符串拷贝
 * @param dst 目标缓冲区
 * @param src 源字符串
 * @return 目标地址
 */
char *strcpy(char *dst, const char *src);

/**
 * @brief 字符串拷贝（限制长度）
 * @param dst 目标缓冲区
 * @param src 源字符串
 * @param n 最大拷贝字符数
 * @return 目标地址
 */
char *strncpy(char *dst, const char *src, size_t n);

/**
 * @brief 字符串连接
 * @param dst 目标字符串（已有内容后追加）
 * @param src 要追加的字符串
 * @return 目标地址
 */
char *strcat(char *dst, const char *src);

/**
 * @brief 字符串连接（限制长度）
 * @param dst 目标字符串
 * @param src 要追加的字符串
 * @param n 最大追加字符数
 * @return 目标地址
 */
char *strncat(char *dst, const char *src, size_t n);

/**
 * @brief 查找字符首次出现的位置
 * @param s 输入字符串
 * @param c 要查找的字符
 * @return 找到返回指针，未找到返回 NULL
 */
char *strchr(const char *s, int c);

/**
 * @brief 查找字符最后一次出现的位置
 * @param s 输入字符串
 * @param c 要查找的字符
 * @return 找到返回指针，未找到返回 NULL
 */
char *strrchr(const char *s, int c);

/**
 * @brief 查找子字符串
 * @param haystack 主字符串
 * @param needle 要查找的子串
 * @return 找到返回位置指针，未找到返回 NULL
 */
char *strstr(const char *haystack, const char *needle);

#endif /* AISAFE_STRING_H */
