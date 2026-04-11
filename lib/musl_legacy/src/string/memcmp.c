/**
 * @file    memcmp.c
 * @brief   内存比较实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 比较两块内存区域的内容。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <string.h>

/**
 * @brief 内存比较
 *
 * @details 逐字节比较 s1 和 s2 指向的前 n 个字节。
 *
 * @param s1 第一块内存
 * @param s2 第二块内存
 * @param n  比较字节数
 *
 * @return 0 表示相等，负数表示 s1 小于 s2，正数表示 s1 大于 s2
 */
int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    size_t i;

    for (i = 0U; i < n; i++)
    {
        if (p1[i] != p2[i])
        {
            return (int)p1[i] - (int)p2[i];
        }
    }

    return 0;
}
