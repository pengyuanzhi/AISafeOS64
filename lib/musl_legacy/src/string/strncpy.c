/**
 * @file    strncpy.c
 * @brief   字符串拷贝（限制长度）实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 将源字符串拷贝到目标缓冲区，最多拷贝 n 个字符。
 *          若 src 长度不足 n，剩余部分填充 '\0'。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <string.h>

/**
 * @brief 字符串拷贝（限制长度）
 *
 * @details 将 src 指向的字符串拷贝到 dst，最多拷贝 n 个字符。
 *          若 src 长度小于 n，则在 dst 末尾补 '\0' 直到写满 n 个字符。
 *          若 src 长度大于等于 n，则 dst 不会以 '\0' 结尾。
 *
 * @param dst 目标缓冲区
 * @param src 源字符串
 * @param n   最大拷贝字符数
 *
 * @return 目标地址 dst
 */
char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0U;

    while ((i < n) && (src[i] != '\0'))
    {
        dst[i] = src[i];
        i++;
    }

    while (i < n)
    {
        dst[i] = '\0';
        i++;
    }

    return dst;
}
