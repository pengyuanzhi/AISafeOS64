/**
 * @file    strcmp.c
 * @brief   字符串比较实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 逐字符比较两个以空字符结尾的字符串。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <string.h>

/**
 * @brief 字符串比较
 *
 * @details 逐字符比较 s1 和 s2，直到发现差异或遇到终止符。
 *
 * @param s1 第一个字符串
 * @param s2 第二个字符串
 *
 * @return 0 表示相等，负数表示 s1 小于 s2，正数表示 s1 大于 s2
 */
int strcmp(const char *s1, const char *s2)
{
    size_t i = 0U;

    while ((s1[i] != '\0') && (s1[i] == s2[i]))
    {
        i++;
    }

    return (int)(unsigned char)s1[i] - (int)(unsigned char)s2[i];
}
