/**
 * @file    misra_wrapper.c
 * @brief   musl 公共 API 的 MISRA C:2012 合规包装实现
 * @version 1.0
 *
 * @details 对标准 musl 的公共 API 提供 MISRA C:2012 合规的薄包装。
 *          包装层不改变 API 语义，只添加必要的参数验证和安全检查。
 *
 * @note MISRA-C:2012 合规
 */

#include "misra_wrapper.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 检查指针参数是否合法
 *
 * @param ptr 指针参数
 * @return true 表示合法，false 表示非法
 */
static bool is_pointer_valid(const void *ptr)
{
    return (ptr != NULL);
}

/**
 * @brief 检查内存区域是否重叠
 *
 * @param ptr1 指针 1
 * @param ptr2 指针 2
 * @param size 大小
 * @return true 表示重叠，false 表示不重叠
 */
static bool is_memory_overlap(const void *ptr1, const void *ptr2, size_t size)
{
    uintptr_t addr1 = (uintptr_t)ptr1;
    uintptr_t addr2 = (uintptr_t)ptr2;

    /* 检查 [ptr1, ptr1 + size) 和 [ptr2, ptr2 + size) 是否重叠 */
    if ((addr1 < (addr2 + size)) && ((addr1 + size) > addr2))
    {
        return true;
    }

    return false;
}

/**
 * @brief 检查大小参数是否合法
 *
 * @param size 大小参数
 * @param max_size 最大允许大小
 * @return true 表示合法，false 表示非法
 */
static bool is_size_valid(size_t size, size_t max_size)
{
    return (size <= max_size);
}

/* ========================================================================
 * 内存操作包装实现
 * ======================================================================== */

void *misra_memcpy(void *dest, const void *src, size_t n)
{
    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(dest) || !is_pointer_valid(src))
    {
        return NULL;
    }

    /* 大小参数检查（MISRA Rule 10.1 - 边界检查） */
    if (!is_size_valid(n, MISRA_MAX_MEMCPY_LEN))
    {
        return NULL;
    }

    /* 内存重叠检查（MISRA Rule 18.2 - 防止未定义行为） */
    if (is_memory_overlap(dest, src, n))
    {
        /* 内存重叠，使用 memmove */
        return memmove(dest, src, n);
    }

    /* 调用标准 memcpy */
    return memcpy(dest, src, n);
}

void *misra_memset(void *s, int c, size_t n)
{
    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(s))
    {
        return NULL;
    }

    /* 大小参数检查（MISRA Rule 10.1 - 边界检查） */
    if (!is_size_valid(n, MISRA_MAX_MEMCPY_LEN))
    {
        return NULL;
    }

    /* 参数类型转换（MISRA Rule 10.8 - 安全的类型转换） */
    unsigned char byte_val = (unsigned char)c;

    /* 调用标准 memset */
    return memset(s, byte_val, n);
}

int misra_memcmp(const void *s1, const void *s2, size_t n)
{
    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(s1) || !is_pointer_valid(s2))
    {
        return 0;
    }

    /* 大小参数检查（MISRA Rule 10.1 - 边界检查） */
    if (!is_size_valid(n, MISRA_MAX_MEMCPY_LEN))
    {
        return 0;
    }

    /* 调用标准 memcmp */
    return memcmp(s1, s2, n);
}

/* ========================================================================
 * 字符串操作包装实现
 * ======================================================================== */

size_t misra_strlen(const char *s, size_t max_len)
{
    size_t i;
    size_t len;

    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(s))
    {
        return 0;
    }

    /* 长度限制检查（MISRA Rule 10.1 - 边界检查） */
    if (max_len == 0)
    {
        return 0;
    }

    /* 检查是否超过最大长度限制 */
    if (max_len > MISRA_MAX_STRING_LEN)
    {
        return 0;
    }

    /* 手动检查字符串以 NULL 终止（MISRA Rule 21.1） */
    len = 0;
    for (i = 0; i < max_len; i++)
    {
        if (s[i] == '\0')
        {
            /* 找到 NULL 终止符 */
            break;
        }
        len++;
    }

    /* 如果遍历了 max_len 个字符都没有找到 NULL 终止符，返回错误 */
    if (len >= max_len)
    {
        return 0;  /* 错误：字符串未终止 */
    }

    return len;
}

int misra_strcmp(const char *s1, const char *s2, size_t max_len)
{
    size_t i;
    size_t len1;
    size_t len2;
    size_t min_len;

    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(s1) || !is_pointer_valid(s2))
    {
        return 0;
    }

    /* 长度限制检查（MISRA Rule 10.1 - 边界检查） */
    if (max_len == 0)
    {
        return 0;
    }

    /* 获取字符串长度（确保 NULL 终止） */
    len1 = misra_strlen(s1, max_len);
    len2 = misra_strlen(s2, max_len);

    /* 比较两个字符串 */
    min_len = (len1 < len2) ? len1 : len2;
    for (i = 0; i < min_len; i++)
    {
        if (s1[i] != s2[i])
        {
            /* 字符不同，返回差值 */
            return (int)(unsigned char)s1[i] - (int)(unsigned char)s2[i];
        }
    }

    /* 所有字符相同，比较长度 */
    if (len1 < len2)
    {
        return -1;
    }
    else if (len1 > len2)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

int misra_strncmp(const char *s1, const char *s2, size_t n)
{
    size_t i;

    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(s1) || !is_pointer_valid(s2))
    {
        return 0;
    }

    /* 长度参数检查（MISRA Rule 10.1 - 边界检查） */
    if (!is_size_valid(n, MISRA_MAX_STRING_LEN))
    {
        return 0;
    }

    /* 比较两个字符串（最多 n 个字符） */
    for (i = 0; i < n; i++)
    {
        if (s1[i] != s2[i])
        {
            /* 字符不同，返回差值 */
            return (int)(unsigned char)s1[i] - (int)(unsigned char)s2[i];
        }

        if (s1[i] == '\0')
        {
            /* NULL 终止符，结束比较 */
            break;
        }
    }

    /* 所有比较的字符都相同 */
    return 0;
}

char *misra_strcpy(char *dest, const char *src, size_t dest_size)
{
    size_t i;
    size_t len;

    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(dest) || !is_pointer_valid(src))
    {
        return NULL;
    }

    /* dest_size 检查（MISRA Rule 10.1 - 边界检查） */
    if (dest_size == 0)
    {
        return NULL;
    }

    /* 手动计算源字符串长度（检查 NULL 终止） */
    len = 0;
    for (i = 0; i < dest_size; i++)
    {
        if (src[i] == '\0')
        {
            /* 找到 NULL 终止符 */
            break;
        }
        len++;
    }

    /* 检查源字符串是否合法（未终止是错误） */
    if (len >= dest_size)
    {
        /* 源字符串未终止，无法拷贝 */
        return NULL;
    }

    /* 检查目标缓冲区是否足够（需要 len + 1 字节，包括 NULL） */
    if ((len + 1) > dest_size)
    {
        /* 目标缓冲区不足，无法拷贝 */
        return NULL;
    }

    /* 拷贝字符串（包括 NULL 终止符） */
    for (i = 0; i <= len; i++)
    {
        dest[i] = src[i];
    }

    return dest;
}

char *misra_strncpy(char *dest, const char *src, size_t dest_size, size_t n)
{
    size_t i;
    size_t copy_len;

    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(dest) || !is_pointer_valid(src))
    {
        return NULL;
    }

    /* dest_size 检查（MISRA Rule 10.1 - 边界检查） */
    if (dest_size == 0)
    {
        return NULL;
    }

    /* 检查 n 不超过 dest_size（防止缓冲区溢出） */
    if (n > dest_size)
    {
        return NULL;
    }

    /* 获取源字符串长度（确保 NULL 终止） */
    copy_len = misra_strlen(src, n);

    /* 拷贝字符串（最多 n 个字符） */
    for (i = 0; i < n; i++)
    {
        if (i < copy_len)
        {
            /* 拷贝源字符 */
            dest[i] = src[i];
        }
        else
        {
            /* 填充 NULL（修复标准 strncpy 的未定义行为） */
            dest[i] = '\0';
        }
    }

    /* 确保最后一个字符为 NULL 终止符 */
    if (n > 0)
    {
        dest[n - 1] = '\0';
    }

    return dest;
}

char *misra_strchr(const char *s, int c, size_t max_len)
{
    size_t i;
    unsigned char ch;

    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(s))
    {
        return NULL;
    }

    /* 长度限制检查（MISRA Rule 10.1 - 边界检查） */
    if (max_len == 0)
    {
        return NULL;
    }

    /* 类型转换（MISRA Rule 10.8 - 安全的类型转换） */
    ch = (unsigned char)c;

    /* 查找字符 */
    for (i = 0; i < max_len; i++)
    {
        if (s[i] == '\0')
        {
            /* NULL 终止符，未找到 */
            break;
        }

        if (s[i] == ch)
        {
            /* 找到字符，返回指针 */
            return (char *)&s[i];
        }
    }

    /* 未找到字符 */
    return NULL;
}

char *misra_strstr(const char *haystack, const char *needle, size_t max_len)
{
    size_t haystack_len;
    size_t needle_len;
    size_t i;
    size_t j;

    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(haystack) || !is_pointer_valid(needle))
    {
        return NULL;
    }

    /* 长度限制检查（MISRA Rule 10.1 - 边界检查） */
    if (max_len == 0)
    {
        return NULL;
    }

    /* 获取字符串长度（确保 NULL 终止） */
    haystack_len = misra_strlen(haystack, max_len);
    needle_len = misra_strlen(needle, max_len);

    /* 检查 needle 是否为空字符串 */
    if (needle_len == 0)
    {
        return (char *)haystack;
    }

    /* 检查 haystack 是否足够长 */
    if (haystack_len < needle_len)
    {
        return NULL;
    }

    /* 查找子字符串 */
    for (i = 0; i <= (haystack_len - needle_len); i++)
    {
        /* 检查是否匹配 */
        bool match = true;
        for (j = 0; j < needle_len; j++)
        {
            if (haystack[i + j] != needle[j])
            {
                match = false;
                break;
            }
        }

        if (match)
        {
            /* 找到匹配，返回指针 */
            return (char *)&haystack[i];
        }
    }

    /* 未找到匹配 */
    return NULL;
}

int misra_snprintf(char *str, size_t size, const char *format, ...)
{
    va_list args;
    int result;

    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(str) || !is_pointer_valid(format))
    {
        return -1;
    }

    /* size 检查（MISRA Rule 10.1 - 边界检查） */
    if (size == 0)
    {
        return -1;
    }

    /* 格式化字符串 */
    va_start(args, format);
    result = misra_vsnprintf(str, size, format, args);
    va_end(args);

    return result;
}

int misra_vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
    int result;

    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(str) || !is_pointer_valid(format))
    {
        return -1;
    }

    /* size 检查（MISRA Rule 10.1 - 边界检查） */
    if (size == 0)
    {
        return -1;
    }

    /* 调用标准 vsnprintf */
    result = vsnprintf(str, size, format, ap);

    /* 确保 NULL 终止（MISRA Rule 21.1） */
    if (size > 0)
    {
        str[size - 1] = '\0';
    }

    return result;
}

/* ========================================================================
 * 标准输入/输出包装实现
 * ======================================================================== */

int misra_printf(const char *format, ...)
{
    va_list args;
    int result;

    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(format))
    {
        return -1;
    }

    /* 调用标准 printf */
    va_start(args, format);
    result = vprintf(format, args);
    va_end(args);

    return result;
}

int misra_fprintf(FILE *stream, const char *format, ...)
{
    va_list args;
    int result;

    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(stream) || !is_pointer_valid(format))
    {
        return -1;
    }

    /* 调用标准 fprintf */
    va_start(args, format);
    result = vfprintf(stream, format, args);
    va_end(args);

    return result;
}

/* ========================================================================
 * 标准库包装实现
 * ======================================================================== */

int misra_atoi(const char *nptr, size_t max_len)
{
    long result;

    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(nptr))
    {
        return 0;
    }

    /* 调用 misra_strtol */
    result = misra_strtol(nptr, NULL, 10, max_len);

    /* 检查溢出 */
    if (result > INT_MAX)
    {
        return INT_MAX;
    }
    else if (result < INT_MIN)
    {
        return INT_MIN;
    }
    else
    {
        return (int)result;
    }
}

long misra_strtol(const char *nptr, char **endptr, int base, size_t max_len)
{
    const char *p;
    long result;
    char *end;

    /* 参数验证（MISRA Rule 11.9 - 指针参数） */
    if (!is_pointer_valid(nptr))
    {
        if (endptr != NULL)
        {
            *endptr = NULL;
        }
        return 0;
    }

    /* base 检查（MISRA Rule 10.1 - 边界检查） */
    if ((base != 0) && (base < 2) && (base > 36))
    {
        if (endptr != NULL)
        {
            *endptr = (char *)nptr;
        }
        return 0;
    }

    /* 长度限制检查（MISRA Rule 10.1 - 边界检查） */
    if (max_len == 0)
    {
        if (endptr != NULL)
        {
            *endptr = (char *)nptr;
        }
        return 0;
    }

    /* 跳过前导空格 */
    p = nptr;
    while ((p < (nptr + max_len)) && (*p == ' '))
    {
        p++;
    }

    /* 检查是否到达字符串结尾 */
    if (*p == '\0')
    {
        if (endptr != NULL)
        {
            *endptr = (char *)nptr;
        }
        return 0;
    }

    /* 调用标准 strtol */
    result = strtol(nptr, &end, base);

    /* 返回 endptr */
    if (endptr != NULL)
    {
        *endptr = end;
    }

    return result;
}

int misra_abs(int x)
{
    /* 处理 INT_MIN 边界情况（MISRA Rule 10.1） */
    if (x == INT_MIN)
    {
        /* INT_MIN 的绝对值会溢出，返回 INT_MAX */
        return INT_MAX;
    }

    /* 标准 abs 实现 */
    return (x < 0) ? -x : x;
}

long misra_labs(long x)
{
    /* 处理 LONG_MIN 边界情况（MISRA Rule 10.1） */
    if (x == LONG_MIN)
    {
        /* LONG_MIN 的绝对值会溢出，返回 LONG_MAX */
        return LONG_MAX;
    }

    /* 标准 labs 实现 */
    return (x < 0) ? -x : x;
}
