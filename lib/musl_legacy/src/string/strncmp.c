/**
 * @file    strncmp.c
 * @brief   字符串比较（限制长度）实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 逐字符比较两个字符串，最多比较 n 个字符。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <string.h>

/**
 * @brief 字符串比较（限制长度）
 *
 * @details 逐字符比较 s1 和 s2 的前 n 个字符。
 *          若在 n 个字符内遇到终止符或发现差异则停止。
 *
 * @param s1 第一个字符串
 * @param s2 第二个字符串
 * @param n  最大比较字符数
 *
 * @return 0 表示相等，负数表示 s1 小于 s2，正数表示 s1 大于 s2
 */
int strncmp(const char *s1, const char *s2, size_t n)
{
    size_t i = 0U;

    if (n == 0U)
    {
        return 0;
    }

    while ((i < n) && (s1[i] != '\0') && (s1[i] == s2[i]))
    {
        i++;
    }

    if (i == n)
    {
        return 0;
    }

    return (int)(unsigned char)s1[i] - (int)(unsigned char)s2[i];
}
