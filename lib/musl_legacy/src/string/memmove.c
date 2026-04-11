/**
 * @file    memmove.c
 * @brief   内存移动实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 将源内存区域的内容移动到目标区域，支持重叠区域。
 *          当源地址小于目标地址时从后向前拷贝，避免数据覆盖。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <string.h>

/**
 * @brief 内存移动（支持重叠区域）
 *
 * @details 将 src 指向的前 n 个字节移动到 dst 指向的内存区域。
 *          当 src 和 dst 重叠时，通过选择拷贝方向确保数据正确。
 *
 * @param dst 目标地址
 * @param src 源地址
 * @param n   移动字节数
 *
 * @return 目标地址 dst
 */
void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;

    if (d == s)
    {
        /* 地址相同，无需操作 */
        return dst;
    }

    if (d < s)
    {
        /* 目标在源前面，从前向后拷贝 */
        for (i = 0U; i < n; i++)
        {
            d[i] = s[i];
        }
    }
    else
    {
        /* 目标在源后面，从后向前拷贝 */
        for (i = n; i > 0U; i--)
        {
            d[i - 1U] = s[i - 1U];
        }
    }

    return dst;
}
