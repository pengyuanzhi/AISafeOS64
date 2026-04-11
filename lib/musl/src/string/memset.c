/**
 * @file    memset.c
 * @brief   内存填充实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 将目标内存区域的每个字节设置为指定值。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <string.h>

/**
 * @brief 内存填充
 *
 * @details 将 dst 指向的前 n 个字节设置为值 c（转换为 unsigned char）。
 *
 * @param dst 目标地址
 * @param c   填充值（低 8 位有效）
 * @param n   填充字节数
 *
 * @return 目标地址 dst
 */
void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    unsigned char uc = (unsigned char)c;
    size_t i;

    for (i = 0U; i < n; i++)
    {
        d[i] = uc;
    }

    return dst;
}
