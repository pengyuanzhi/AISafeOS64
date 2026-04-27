/**
 * @file    misra_wrapper.h
 * @brief   musl 公共 API 的 MISRA C:2012 合规包装
 * @version 1.0
 *
 * @details 对标准 musl 的公共 API 提供 MISRA C:2012 合规的薄包装。
 *          包装层不改变 API 语义，只添加必要的参数验证和安全检查。
 *
 *          主要功能：
 *          - 参数验证：指针非空、对齐、大小验证
 *          - 边界检查：缓冲区溢出防护
 *          - NULL 终止保证：strncpy 等
 *          - MISRA Rule 合规：消除 Rule 11.3/11.4/11.5/18.2 等违规
 *
 * @note MISRA-C:2012 合规
 * @note 参考 ISO 26262 ASIL-D 要求
 */

#ifndef AISAFE_MISRA_WRAPPER_H
#define AISAFE_MISRA_WRAPPER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大字符串长度（防止整数溢出） */
#define MISRA_MAX_STRING_LEN (1024UL * 1024UL)  /* 1MB */

/** @brief 最大内存拷贝大小 */
#define MISRA_MAX_MEMCPY_LEN (1024UL * 1024UL * 256UL)  /* 256MB */

/** @brief 最大文件路径长度 */
#define MISRA_MAX_PATH_LEN (4096UL)

/* ========================================================================
 * 字符串操作包装（string.h）
 * ======================================================================== */

/**
 * @brief 安全的 memcpy 包装（MISRA 合规）
 *
 * @param dest 目标指针
 * @param src 源指针
 * @param n 拷贝字节数
 * @return dest（成功），NULL（失败）
 *
 * @details 包装标准 memcpy，添加参数验证：
 *          - 检查 dest/src 非空（MISRA Rule 11.9）
 *          - 检查 n 不超过最大限制（防止整数溢出）
 *          - 检查内存不重叠（防止未定义行为）
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - 大小参数已检查（Rule 10.1）
 *       - 内存重叠检查（Rule 18.2）
 */
void *misra_memcpy(void *dest, const void *src, size_t n);

/**
 * @brief 安全的 memset 包装（MISRA 合规）
 *
 * @param s 目标指针
 * @param c 填充字节（int 但仅使用低 8 位）
 * @param n 填充字节数
 * @return s（成功），NULL（失败）
 *
 * @details 包装标准 memset，添加参数验证：
 *          - 检查 s 非空（MISRA Rule 11.9）
 *          - 检查 n 不超过最大限制（防止整数溢出）
 *          - 参数类型验证（c 转换为 unsigned char）
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - 大小参数已检查（Rule 10.1）
 *       - 类型转换安全（Rule 10.8）
 */
void *misra_memset(void *s, int c, size_t n);

/**
 * @brief 安全的 memcmp 包装（MISRA 合规）
 *
 * @param s1 比较字符串 1
 * @param s2 比较字符串 2
 * @param n 比较字节数
 * @return 比较结果（<0, =0, >0）
 *
 * @details 包装标准 memcmp，添加参数验证：
 *          - 检查 s1/s2 非空（MISRA Rule 11.9）
 *          - 检查 n 不超过最大限制（防止整数溢出）
 *          - 无副作用（纯函数）
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - 纯函数，无副作用（Dir 4.9）
 */
int misra_memcmp(const void *s1, const void *s2, size_t n);

/**
 * @brief 安全的 strlen 包装（MISRA 合规）
 *
 * @param s 字符串指针
 * @param max_len 最大长度限制
 * @return 字符串长度（不包括 NULL 终止符）
 *
 * @details 包装标准 strlen，添加参数验证和长度限制：
 *          - 检查 s 非空（MISRA Rule 11.9）
 *          - 检查字符串以 NULL 终止（防止缓冲区溢出）
 *          - 长度不超过 max_len（防止无限扫描）
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - NULL 终止保证（Rule 21.1）
 *       - 边界检查（Rule 18.2）
 */
size_t misra_strlen(const char *s, size_t max_len);

/**
 * @brief 安全的 strcmp 包装（MISRA 合规）
 *
 * @param s1 比较字符串 1
 * @param s2 比较字符串 2
 * @param max_len 最大长度限制
 * @return 比较结果（<0, =0, >0）
 *
 * @details 包装标准 strcmp，添加参数验证和长度限制：
 *          - 检查 s1/s2 非空（MISRA Rule 11.9）
 *          - 检查字符串以 NULL 终止（防止缓冲区溢出）
 *          - 长度不超过 max_len（防止无限扫描）
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - NULL 终止保证（Rule 21.1）
 *       - 边界检查（Rule 18.2）
 */
int misra_strcmp(const char *s1, const char *s2, size_t max_len);

/**
 * @brief 安全的 strncmp 包装（MISRA 合规）
 *
 * @param s1 比较字符串 1
 * @param s2 比较字符串 2
 * @param n 比较字节数
 * @return 比较结果（<0, =0, >0）
 *
 * @details 包装标准 strncmp，添加参数验证：
 *          - 检查 s1/s2 非空（MISRA Rule 11.9）
 *          - 检查 n 不超过最大限制（防止整数溢出）
 *          - 正确处理 NULL 终止（n 内或 NULL 终止时停止）
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - 边界检查（Rule 18.2）
 *       - 确定性行为（Rule 1.1）
 */
int misra_strncmp(const char *s1, const char *s2, size_t n);

/**
 * @brief 安全的 strcpy 包装（MISRA 合规）
 *
 * @param dest 目标缓冲区
 * @param src 源字符串
 * @param dest_size 目标缓冲区大小（字节）
 * @return dest（成功），NULL（失败）
 *
 * @details 包装标准 strcpy，添加参数验证和边界检查：
 *          - 检查 dest/src 非空（MISRA Rule 11.9）
 *          - 检查 src 以 NULL 终止（Rule 21.1）
 *          - 检查字符串长度 < dest_size（防止缓冲区溢出）
 *          - 保证 dest 以 NULL 终止（Rule 21.1）
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - 边界检查（Rule 18.2）
 *       - NULL 终止保证（Rule 21.1）
 */
char *misra_strcpy(char *dest, const char *src, size_t dest_size);

/**
 * @brief 安全的 strncpy 包装（MISRA 合规）
 *
 * @param dest 目标缓冲区
 * @param src 源字符串
 * @param dest_size 目标缓冲区大小（字节）
 * @param n 拷贝字节数
 * @return dest（成功），NULL（失败）
 *
 * @details 包装标准 strncpy，修复 NULL 终止问题：
 *          - 检查 dest/src 非空（MISRA Rule 11.9）
 *          - 检查 n <= dest_size（防止缓冲区溢出）
 *          - 保证 dest 以 NULL 终止（修复 Rule 21.1 违规）
 *          - 如果 strlen(src) >= n，强制 NULL 终止在 n-1 位置
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - 边界检查（Rule 18.2）
 *       - NULL 终止保证（Rule 21.1）
 *       - 修复标准 strncpy 的未定义行为
 */
char *misra_strncpy(char *dest, const char *src, size_t dest_size, size_t n);

/**
 * @brief 安全的 strchr 包装（MISRA 合规）
 *
 * @param s 字符串指针
 * @param c 查找字符
 * @param max_len 最大长度限制
 * @return 找到的字符指针（成功），NULL（未找到）
 *
 * @details 包装标准 strchr，添加参数验证和长度限制：
 *          - 检查 s 非空（MISRA Rule 11.9）
 *          - 检查字符串以 NULL 终止（防止缓冲区溢出）
 *          - 长度不超过 max_len（防止无限扫描）
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - 边界检查（Rule 18.2）
 *       - NULL 终止保证（Rule 21.1）
 */
char *misra_strchr(const char *s, int c, size_t max_len);

/**
 * @brief 安全的 strstr 包装（MISRA 合规）
 *
 * @param haystack 主字符串
 * @param needle 子字符串
 * @param max_len 最大长度限制
 * @return 找到的子字符串指针（成功），NULL（未找到）
 *
 * @details 包装标准 strstr，添加参数验证和长度限制：
 *          - 检查 haystack/needle 非空（MISRA Rule 11.9）
 *          - 检查 haystack 以 NULL 终止（防止缓冲区溢出）
 *          - 长度不超过 max_len（防止无限扫描）
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - 边界检查（Rule 18.2）
 *       - NULL 终止保证（Rule 21.1）
 */
char *misra_strstr(const char *haystack, const char *needle, size_t max_len);

/**
 * @brief 安全的 snprintf 包装（MISRA 合规）
 *
 * @param str 目标缓冲区
 * @param size 目标缓冲区大小
 * @param format 格式化字符串
 * @param ... 变参
 * @return 写入的字符数（不包括 NULL 终止符）
 *
 * @details 包装标准 snprintf，添加参数验证：
 *          - 检查 str/format 非空（MISRA Rule 11.9）
 *          - 检查 size > 0（缓冲区必须有空间）
 *          - 保证 str 以 NULL 终止（Rule 21.1）
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - 边界检查（Rule 18.2）
 *       - NULL 终止保证（Rule 21.1）
 */
int misra_snprintf(char *str, size_t size, const char *format, ...);

/**
 * @brief 安全的 vsnprintf 包装（MISRA 合规）
 *
 * @param str 目标缓冲区
 * @param size 目标缓冲区大小
 * @param format 格式化字符串
 * @param ap 参参数列表
 * @return 写入的字符数（不包括 NULL 终止符）
 *
 * @details 包装标准 vsnprintf，添加参数验证：
 *          - 检查 str/format 非空（MISRA Rule 11.9）
 *          - 检查 size > 0（缓冲区必须有空间）
 *          - 保证 str 以 NULL 终止（Rule 21.1）
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - 边界检查（Rule 18.2）
 *       - NULL 终止保证（Rule 21.1）
 */
int misra_vsnprintf(char *str, size_t size, const char *format, va_list ap);

/* ========================================================================
 * 标准输入/输出包装（stdio.h）
 * ======================================================================== */

/**
 * @brief 安全的 printf 包装（MISRA 合规）
 *
 * @param format 格式化字符串
 * @param ... 变参
 * @return 输出的字符数
 *
 * @details 包装标准 printf，添加参数验证：
 *          - 检查 format 非空（MISRA Rule 11.9）
 *          - 参数类型检查（通过编译时警告）
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - 格式化字符串验证（Rule 21.1）
 */
int misra_printf(const char *format, ...);

/**
 * @brief 安全的 fprintf 包装（MISRA 合规）
 *
 * @param stream 文件流
 * @param format 格式化字符串
 * @param ... 变参
 * @return 输出的字符数
 *
 * @details 包装标准 fprintf，添加参数验证：
 *          - 检查 stream/format 非空（MISRA Rule 11.9）
 *          - 参数类型检查（通过编译时警告）
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - 格式化字符串验证（Rule 21.1）
 */
int misra_fprintf(FILE *stream, const char *format, ...);

/* ========================================================================
 * 标准库包装（stdlib.h）
 * ======================================================================== */

/**
 * @brief 安全的 atoi 包装（MISRA 合规）
 *
 * @param nptr 字符串指针
 * @param max_len 最大长度限制
 * @return 转换后的整数
 *
 * @details 包装标准 atoi，添加参数验证：
 *          - 检查 nptr 非空（MISRA Rule 11.9）
 *          - 检查字符串以 NULL 终止（防止缓冲区溢出）
 *          - 长度不超过 max_len（防止无限扫描）
 *          - 错误处理：非数字字符返回 0
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - 边界检查（Rule 18.2）
 *       - NULL 终止保证（Rule 21.1）
 */
int misra_atoi(const char *nptr, size_t max_len);

/**
 * @brief 安全的 strtol 包装（MISRA 合规）
 *
 * @param nptr 字符串指针
 * @param endptr 指向转换结束位置的指针（可为 NULL）
 * @param base 基数（0, 2-36）
 * @param max_len 最大长度限制
 * @return 转换后的长整数
 *
 * @details 包装标准 strtol，添加参数验证：
 *          - 检查 nptr 非空（MISRA Rule 11.9）
 *          - 检查 base 在合法范围内（0, 2-36）
 *          - 检查字符串以 NULL 终止（防止缓冲区溢出）
 *          - 长度不超过 max_len（防止无限扫描）
 *          - 错误处理：溢出返回 LONG_MAX/LONG_MIN
 *
 * @note MISRA C:2012 合规：
 *       - 指针参数已验证（Rule 11.9）
 *       - 边界检查（Rule 18.2）
 *       - NULL 终止保证（Rule 21.1）
 *       - 溢出检测（Rule 10.1）
 */
long misra_strtol(const char *nptr, char **endptr, int base, size_t max_len);

/**
 * @brief 安全的 abs 包装（MISRA 合规）
 *
 * @param x 整数
 * @return 绝对值
 *
 * @details 包装标准 abs，处理 INT_MIN 边界情况：
 *          - INT_MIN 的情况：返回 INT_MAX（防止未定义行为）
 *          - 避免负数溢出
 *
 * @note MISRA C:2012 合规：
 *       - 边界条件处理（Rule 10.1）
 *       - 避免未定义行为（Rule 1.1）
 */
int misra_abs(int x);

/**
 * @brief 安全的 labs 包装（MISRA 合规）
 *
 * @param x 长整数
 * @return 绝对值
 *
 * @details 包装标准 labs，处理 LONG_MIN 边界情况：
 *          - LONG_MIN 的情况：返回 LONG_MAX（防止未定义行为）
 *          - 避免负数溢出
 *
 * @note MISRA C:2012 合规：
 *       - 边界条件处理（Rule 10.1）
 *       - 避免未定义行为（Rule 1.1）
 */
long misra_labs(long x);

/* ========================================================================
 * 辅助宏定义
 * ======================================================================== */

/**
 * @brief 字符串拷贝宏（自动计算缓冲区大小）
 *
 * @param dest 目标缓冲区
 * @param src 源字符串
 * @return dest（成功），NULL（失败）
 *
 * @details 自动计算 dest 缓冲区大小，调用 misra_strcpy
 */
#define MISRA_STRCPY(dest, src) \
    misra_strcpy(dest, src, sizeof(dest))

/**
 * @brief 字符串长度宏（使用默认最大长度）
 *
 * @param s 字符串指针
 * @return 字符串长度
 *
 * @details 使用默认最大长度（1MB）调用 misra_strlen
 */
#define MISRA_STRLEN(s) \
    misra_strlen(s, MISRA_MAX_STRING_LEN)

/**
 * @brief 字符串比较宏（使用默认最大长度）
 *
 * @param s1 字符串 1
 * @param s2 字符串 2
 * @return 比较结果（<0, =0, >0）
 *
 * @details 使用默认最大长度（1MB）调用 misra_strcmp
 */
#define MISRA_STRCMP(s1, s2) \
    misra_strcmp(s1, s2, MISRA_MAX_STRING_LEN)

#ifdef __cplusplus
}
#endif

#endif /* AISAFE_MISRA_WRAPPER_H */
