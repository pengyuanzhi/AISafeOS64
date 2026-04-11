/**
 * @file    memcpy.c
 * @brief   内存拷贝实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 将源内存区域的内容拷贝到目标区域。
 *          调用者必须确保源和目标区域不重叠。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <string.h>

/**
 * @brief 内存拷贝
 *
 * @details 将 src 指向的前 n 个字节拷贝到 dst 指向的内存区域。
 *          源和目标区域不得重叠，重叠区域应使用 memmove。
 *
 * @param dst 目标地址
 * @param src 源地址
 * @param n   拷贝字节数
 *
 * @return 目标地址 dst
 */
void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;

    for (i = 0U; i < n; i++)
    {
        d[i] = s[i];
    }

    return dst;
}
