/**
 * @file    strtol.c
 * @brief   strtol 实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 字符串转换为长整数，支持 2-36 进制
 *          自动检测 0x（十六进制）和 0（八进制）前缀
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdlib.h>
#include <errno.h>
#include <limits.h>

/* ========================================================================
 * 内部辅助宏
 * ======================================================================== */

/** @brief 判断字符是否为空白 */
#define IS_SPACE(c) (((c) == ' ') || ((c) == '\t') || ((c) == '\n') || \
                     ((c) == '\r') || ((c) == '\f') || ((c) == '\v'))

/**
 * @brief 字符转数字值
 * @param c 输入字符
 * @return 数字值（0-35），非法字符返回 -1
 */
static int char_to_val(int c)
{
    if ((c >= '0') && (c <= '9'))
    {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'z'))
    {
        return c - 'a' + 10;
    }
    if ((c >= 'A') && (c <= 'Z'))
    {
        return c - 'A' + 10;
    }
    return -1;
}

/**
 * @brief 字符串转换为长整数
 * @param nptr 输入字符串
 * @param endptr 停止扫描位置（可为 NULL）
 * @param base 进制（2-36，或 0 表示自动检测）
 * @return 转换后的长整数值
 */
long strtol(const char *nptr, char **endptr, int base)
{
    const char *s = nptr;
    long result = 0L;
    int negative = 0;
    int any_digits = 0;

    /* 跳过前导空白 */
    while (IS_SPACE((int)*s))
    {
        s++;
    }

    /* 处理符号 */
    if (*s == '-')
    {
        negative = 1;
        s++;
    }
    else if (*s == '+')
    {
        s++;
    }

    /* 自动检测进制 */
    if ((base == 0) || (base == 16))
    {
        if ((s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X')))
        {
            if ((base == 0) || (s[2] != '\0'))
            {
                base = 16;
                s += 2;
            }
        }
    }

    if (base == 0)
    {
        if (*s == '0')
        {
            base = 8;
        }
        else
        {
            base = 10;
        }
    }

    /* 检查进制合法性 */
    if ((base < 2) || (base > 36))
    {
        if (endptr != NULL)
        {
            *endptr = (char *)nptr;
        }
        return 0L;
    }

    /* 转换数字 */
    for (;;)
    {
        int val = char_to_val((int)*s);
        if ((val < 0) || (val >= base))
        {
            break;
        }

        /* 溢出检测 */
        if (result > (LONG_MAX - (long)val) / (long)base)
        {
            /* 上溢 */
            if (negative)
            {
                result = LONG_MIN;
            }
            else
            {
                result = LONG_MAX;
            }
            errno = ERANGE;

            /* 跳过剩余数字 */
            for (;;)
            {
                val = char_to_val((int)*s);
                if ((val < 0) || (val >= base))
                {
                    break;
                }
                s++;
            }
            any_digits = 1;
            break;
        }

        result = result * (long)base + (long)val;
        s++;
        any_digits = 1;
    }

    if (negative)
    {
        result = -result;
    }

    if (endptr != NULL)
    {
        if (any_digits)
        {
            *endptr = (char *)s;
        }
        else
        {
            *endptr = (char *)nptr;
        }
    }

    return result;
}
