/**
 * @file    kernel_string.c
 * @brief   内核字符串和内存操作函数实现
 * @author  AISafe64 Team
 * @date    2026-04-04
#include <stdarg.h>
#ifndef __VA_LIST_DEFINED
#define __VA_LIST_DEFINED
typedef void *va_list;
#endif/
 * @version 1.0
 *
 * @details 本文件实现了内核所需的字符串和内存操作函数，
 *          替代标准库 string.h，用于 freestanding 环境：
 *          - 内存复制/移动/填充/比较/搜索
 *          - 字符串长度/复制/连接/比较/搜索
 *          - 字符串转数值
 *
 * @note MISRA-C:2012 合规
 * @note 仅使用 C11 标准特性
 * @note 所有指针参数均进行 NULL 检查
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ========================================================================
 * 内存操作函数实现
 * ======================================================================== */

/**
 * @brief 内存复制（不处理重叠区域）
 *
 * @param dest 目标地址
 * @param src  源地址
 * @param n    复制字节数
 *
 * @return 目标地址指针
 */
void *kernel_memcpy(void *dest, const void *src, size_t n)
{
    uint8_t       *d;
    const uint8_t *s;
    size_t         i;

    if ((dest == NULL) || (src == NULL))
    {
        return dest;
    }

    d = (uint8_t *)dest;
    s = (const uint8_t *)src;

    /* 使用 8 字节对齐复制优化 */
    while ((n >= sizeof(uint64_t)) &&
           (((uintptr_t)d & 0x7U) == ((uintptr_t)s & 0x7U)))
    {
        if (((uintptr_t)d & 0x7U) == 0U)
        {
            break;
        }
        *d++ = *s++;
        n--;
    }

    /* 8 字节块复制 */
    while (n >= sizeof(uint64_t))
    {
        uint64_t val;
        const uint64_t *src64 = (const uint64_t *)(uintptr_t)s;
        uint64_t *dst64 = (uint64_t *)(uintptr_t)d;

        val = *src64;
        *dst64 = val;

        d += sizeof(uint64_t);
        s += sizeof(uint64_t);
        n -= sizeof(uint64_t);
    }

    /* 剩余字节复制 */
    for (i = 0U; i < n; i++)
    {
        d[i] = s[i];
    }

    return dest;
}

/**
 * @brief 内存移动（处理重叠区域）
 *
 * @param dest 目标地址
 * @param src  源地址
 * @param n    移动字节数
 *
 * @return 目标地址指针
 */
void *kernel_memmove(void *dest, const void *src, size_t n)
{
    uint8_t       *d;
    const uint8_t *s;
    size_t         i;

    if ((dest == NULL) || (src == NULL))
    {
        return dest;
    }

    d = (uint8_t *)dest;
    s = (const uint8_t *)src;

    if (d == s)
    {
        return dest;
    }

    if (d < s)
    {
        /* 向前复制 */
        for (i = 0U; i < n; i++)
        {
            d[i] = s[i];
        }
    }
    else
    {
        /* 向后复制（避免重叠问题） */
        i = n;
        while (i > 0U)
        {
            i--;
            d[i] = s[i];
        }
    }

    return dest;
}

/**
 * @brief 内存填充
 *
 * @param s 目标地址
 * @param c 填充字节值
 * @param n 填充字节数
 *
 * @return 目标地址指针
 */
void *kernel_memset(void *s, int32_t c, size_t n)
{
    uint8_t *p;
    uint8_t  val;
    size_t   i;

    if (s == NULL)
    {
        return s;
    }

    p = (uint8_t *)s;
    val = (uint8_t)(c & 0xFFU);

    for (i = 0U; i < n; i++)
    {
        p[i] = val;
    }

    return s;
}

/**
 * @brief 内存比较
 *
 * @param s1 第一个内存区域
 * @param s2 第二个内存区域
 * @param n  比较字节数
 *
 * @return 0 相等，<0 s1<s2，>0 s1>s2
 */
int32_t kernel_memcmp(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1;
    const uint8_t *p2;
    size_t         i;
    int32_t        diff;

    if ((s1 == NULL) && (s2 == NULL))
    {
        return 0;
    }

    if (s1 == NULL)
    {
        return -1;
    }

    if (s2 == NULL)
    {
        return 1;
    }

    p1 = (const uint8_t *)s1;
    p2 = (const uint8_t *)s2;

    for (i = 0U; i < n; i++)
    {
        diff = (int32_t)p1[i] - (int32_t)p2[i];
        if (diff != 0)
        {
            return diff;
        }
    }

    return 0;
}

/**
 * @brief 在内存区域中搜索字节
 *
 * @param s 内存区域
 * @param c 搜索的字节值
 * @param n 搜索字节数
 *
 * @return 找到的位置指针，NULL 表示未找到
 */
void *kernel_memchr(const void *s, int32_t c, size_t n)
{
    const uint8_t *p;
    uint8_t        val;
    size_t         i;

    if (s == NULL)
    {
        return NULL;
    }

    p = (const uint8_t *)s;
    val = (uint8_t)(c & 0xFFU);

    for (i = 0U; i < n; i++)
    {
        if (p[i] == val)
        {
            return (void *)(uintptr_t)&p[i];
        }
    }

    return NULL;
}

/**
 * @brief 内存清零
 *
 * @param s 目标地址
 * @param n 清零字节数
 */
void kernel_memzero(void *s, size_t n)
{
    (void)kernel_memset(s, 0, n);
}

/* ========================================================================
 * 字符串操作函数实现
 * ======================================================================== */

/**
 * @brief 计算字符串长度
 *
 * @param s 字符串
 *
 * @return 字符串长度（不含终止符）
 */
size_t kernel_strlen(const char *s)
{
    size_t len;

    if (s == NULL)
    {
        return 0U;
    }

    len = 0U;
    while (s[len] != '\0')
    {
        len++;
    }

    return len;
}

/**
 * @brief 计算字符串长度（带最大长度限制）
 *
 * @param s    字符串
 * @param maxn 最大检查长度
 *
 * @return 字符串长度
 */
size_t kernel_strnlen(const char *s, size_t maxn)
{
    size_t len;

    if (s == NULL)
    {
        return 0U;
    }

    len = 0U;
    while ((len < maxn) && (s[len] != '\0'))
    {
        len++;
    }

    return len;
}

/**
 * @brief 字符串复制
 *
 * @param dest 目标缓冲区
 * @param src  源字符串
 *
 * @return 目标缓冲区指针
 */
char *kernel_strcpy(char *dest, const char *src)
{
    size_t i;

    if ((dest == NULL) || (src == NULL))
    {
        return dest;
    }

    i = 0U;
    while (src[i] != '\0')
    {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';

    return dest;
}

/**
 * @brief 字符串复制（带最大长度限制）
 *
 * @param dest 目标缓冲区
 * @param src  源字符串
 * @param n    最大复制字符数
 *
 * @return 目标缓冲区指针
 */
char *kernel_strncpy(char *dest, const char *src, size_t n)
{
    size_t i;

    if ((dest == NULL) || (src == NULL))
    {
        return dest;
    }

    for (i = 0U; (i < n) && (src[i] != '\0'); i++)
    {
        dest[i] = src[i];
    }

    for (; i < n; i++)
    {
        dest[i] = '\0';
    }

    return dest;
}

/**
 * @brief 字符串连接
 *
 * @param dest 目标缓冲区（已有字符串末尾追加）
 * @param src  源字符串
 *
 * @return 目标缓冲区指针
 */
char *kernel_strcat(char *dest, const char *src)
{
    size_t dest_len;
    size_t i;

    if ((dest == NULL) || (src == NULL))
    {
        return dest;
    }

    dest_len = kernel_strlen(dest);

    i = 0U;
    while (src[i] != '\0')
    {
        dest[dest_len + i] = src[i];
        i++;
    }
    dest[dest_len + i] = '\0';

    return dest;
}

/**
 * @brief 字符串连接（带最大长度限制）
 *
 * @param dest 目标缓冲区
 * @param src  源字符串
 * @param n    最大追加字符数
 *
 * @return 目标缓冲区指针
 */
char *kernel_strncat(char *dest, const char *src, size_t n)
{
    size_t dest_len;
    size_t i;

    if ((dest == NULL) || (src == NULL))
    {
        return dest;
    }

    dest_len = kernel_strlen(dest);

    for (i = 0U; (i < n) && (src[i] != '\0'); i++)
    {
        dest[dest_len + i] = src[i];
    }
    dest[dest_len + i] = '\0';

    return dest;
}

/**
 * @brief 字符串比较
 *
 * @param s1 第一个字符串
 * @param s2 第二个字符串
 *
 * @return 0 相等，<0 s1<s2，>0 s1>s2
 */
int32_t kernel_strcmp(const char *s1, const char *s2)
{
    size_t  i;
    uint8_t c1;
    uint8_t c2;

    if ((s1 == NULL) && (s2 == NULL))
    {
        return 0;
    }

    if (s1 == NULL)
    {
        return -1;
    }

    if (s2 == NULL)
    {
        return 1;
    }

    i = 0U;
    do
    {
        c1 = (uint8_t)s1[i];
        c2 = (uint8_t)s2[i];

        if (c1 != c2)
        {
            return (int32_t)c1 - (int32_t)c2;
        }

        if (c1 == 0U)
        {
            return 0;
        }

        i++;
    } while (true);
}

/**
 * @brief 字符串比较（带最大长度限制）
 *
 * @param s1  第一个字符串
 * @param s2  第二个字符串
 * @param n   最大比较字符数
 *
 * @return 0 相等，<0 s1<s2，>0 s1>s2
 */
int32_t kernel_strncmp(const char *s1, const char *s2, size_t n)
{
    size_t  i;
    uint8_t c1;
    uint8_t c2;

    if (n == 0U)
    {
        return 0;
    }

    if ((s1 == NULL) && (s2 == NULL))
    {
        return 0;
    }

    if (s1 == NULL)
    {
        return -1;
    }

    if (s2 == NULL)
    {
        return 1;
    }

    for (i = 0U; i < n; i++)
    {
        c1 = (uint8_t)s1[i];
        c2 = (uint8_t)s2[i];

        if (c1 != c2)
        {
            return (int32_t)c1 - (int32_t)c2;
        }

        if (c1 == 0U)
        {
            return 0;
        }
    }

    return 0;
}

/**
 * @brief 在字符串中查找字符
 *
 * @param s 字符串
 * @param c 搜索的字符
 *
 * @return 找到的位置指针，NULL 表示未找到
 */
char *kernel_strchr(const char *s, int32_t c)
{
    uint8_t val;
    size_t  i;

    if (s == NULL)
    {
        return NULL;
    }

    val = (uint8_t)(c & 0xFFU);

    i = 0U;
    while (s[i] != '\0')
    {
        if ((uint8_t)s[i] == val)
        {
            return (char *)(uintptr_t)&s[i];
        }
        i++;
    }

    /* 检查是否搜索终止符 */
    if (val == 0U)
    {
        return (char *)(uintptr_t)&s[i];
    }

    return NULL;
}

/**
 * @brief 在字符串中从后查找字符
 *
 * @param s 字符串
 * @param c 搜索的字符
 *
 * @return 找到的位置指针，NULL 表示未找到
 */
char *kernel_strrchr(const char *s, int32_t c)
{
    uint8_t       val;
    size_t        len;
    size_t        i;
    char         *result;

    if (s == NULL)
    {
        return NULL;
    }

    val = (uint8_t)(c & 0xFFU);
    len = kernel_strlen(s);
    result = NULL;

    for (i = 0U; i <= len; i++)
    {
        if ((uint8_t)s[i] == val)
        {
            result = (char *)(uintptr_t)&s[i];
        }
    }

    return result;
}

/**
 * @brief 在字符串中查找子串
 *
 * @param haystack 被搜索的字符串
 * @param needle   要查找的子串
 *
 * @return 找到的位置指针，NULL 表示未找到
 */
char *kernel_strstr(const char *haystack, const char *needle)
{
    size_t h_len;
    size_t n_len;
    size_t i;
    size_t j;
    bool   match;

    if ((haystack == NULL) || (needle == NULL))
    {
        return NULL;
    }

    n_len = kernel_strlen(needle);
    if (n_len == 0U)
    {
        return (char *)(uintptr_t)haystack;
    }

    h_len = kernel_strlen(haystack);

    if (n_len > h_len)
    {
        return NULL;
    }

    for (i = 0U; i <= (h_len - n_len); i++)
    {
        match = true;
        for (j = 0U; j < n_len; j++)
        {
            if (haystack[i + j] != needle[j])
            {
                match = false;
                break;
            }
        }

        if (match)
        {
            return (char *)(uintptr_t)&haystack[i];
        }
    }

    return NULL;
}

/* ========================================================================
 * 字符串转数值函数实现
 * ======================================================================== */

/**
 * @brief 跳过前导空白字符
 *
 * @param str 字符串指针
 *
 * @return 跳过空白后的指针
 */
static const char *skip_spaces(const char *str)
{
    while ((*str == ' ') || (*str == '\t') || (*str == '\n') ||
           (*str == '\r') || (*str == '\v') || (*str == '\f'))
    {
        str++;
    }

    return str;
}

/**
 * @brief 将字符转换为数字值
 *
 * @param c    字符
 * @param base 基数
 *
 * @return 数字值，-1 表示无效
 */
static int32_t char_to_digit(char c, int32_t base)
{
    int32_t val;

    if ((c >= '0') && (c <= '9'))
    {
        val = (int32_t)c - (int32_t)'0';
    }
    else if ((c >= 'a') && (c <= 'z'))
    {
        val = (int32_t)c - (int32_t)'a' + 10;
    }
    else if ((c >= 'A') && (c <= 'Z'))
    {
        val = (int32_t)c - (int32_t)'A' + 10;
    }
    else
    {
        return -1;
    }

    if (val >= base)
    {
        return -1;
    }

    return val;
}

/**
 * @brief 将字符串转换为 32 位有符号整数
 *
 * @param nptr  字符串
 * @param endptr 转换结束位置（可为 NULL）
 * @param base   基数（0 自动检测，2-36）
 *
 * @return 转换后的整数值
 */
int32_t kernel_strtol(const char *nptr, char **endptr, int32_t base)
{
    const char *p;
    bool        negative;
    int32_t     result;
    int32_t     digit;

    if (nptr == NULL)
    {
        if (endptr != NULL)
        {
            *endptr = NULL;
        }
        return 0;
    }

    p = skip_spaces(nptr);
    negative = false;

    if (*p == '-')
    {
        negative = true;
        p++;
    }
    else if (*p == '+')
    {
        p++;
    }
    else
    {
        /* 无符号 */
    }

    /* 自动检测基数 */
    if (base == 0)
    {
        if (*p == '0')
        {
            if ((p[1] == 'x') || (p[1] == 'X'))
            {
                base = 16;
                p += 2;
            }
            else
            {
                base = 8;
                p++;
            }
        }
        else
        {
            base = 10;
        }
    }
    else if (base == 16)
    {
        if ((p[0] == '0') && ((p[1] == 'x') || (p[1] == 'X')))
        {
            p += 2;
        }
    }
    else
    {
        /* 使用指定基数 */
    }

    result = 0;
    while (*p != '\0')
    {
        digit = char_to_digit(*p, base);
        if (digit < 0)
        {
            break;
        }

        result = result * base + digit;
        p++;
    }

    if (negative)
    {
        result = -result;
    }

    if (endptr != NULL)
    {
        *endptr = (char *)(uintptr_t)p;
    }

    return result;
}

/**
 * @brief 将字符串转换为 32 位无符号整数
 *
 * @param nptr   字符串
 * @param endptr 转换结束位置（可为 NULL）
 * @param base   基数（0 自动检测，2-36）
 *
 * @return 转换后的无符号整数值
 */
uint32_t kernel_strtoul(const char *nptr, char **endptr, int32_t base)
{
    const char *p;
    bool        negative;
    uint32_t    result;
    int32_t     digit;

    if (nptr == NULL)
    {
        if (endptr != NULL)
        {
            *endptr = NULL;
        }
        return 0U;
    }

    p = skip_spaces(nptr);
    negative = false;

    if (*p == '-')
    {
        negative = true;
        p++;
    }
    else if (*p == '+')
    {
        p++;
    }
    else
    {
        /* 无符号 */
    }

    if (base == 0)
    {
        if (*p == '0')
        {
            if ((p[1] == 'x') || (p[1] == 'X'))
            {
                base = 16;
                p += 2;
            }
            else
            {
                base = 8;
                p++;
            }
        }
        else
        {
            base = 10;
        }
    }
    else if (base == 16)
    {
        if ((p[0] == '0') && ((p[1] == 'x') || (p[1] == 'X')))
        {
            p += 2;
        }
    }
    else
    {
        /* 使用指定基数 */
    }

    result = 0U;
    while (*p != '\0')
    {
        digit = char_to_digit(*p, base);
        if (digit < 0)
        {
            break;
        }

        result = (uint32_t)((uint64_t)result * (uint64_t)base + (uint64_t)digit);
        p++;
    }

    if (negative)
    {
        result = (uint32_t)(-(int32_t)result);
    }

    if (endptr != NULL)
    {
        *endptr = (char *)(uintptr_t)p;
    }

    return result;
}

/**
 * @brief 简化的 snprintf 实现（只支持格式化字符串，不支持浮点）
 */

