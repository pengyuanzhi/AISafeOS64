/**
 * @file    strcpy.c
 * @brief   字符串拷贝实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 将源字符串（含终止符）拷贝到目标缓冲区。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <string.h>

/**
 * @brief 字符串拷贝
 *
 * @details 将 src 指向的字符串（包括终止符 '\0'）拷贝到 dst。
 *          调用者必须确保 dst 有足够空间。
 *
 * @param dst 目标缓冲区
 * @param src 源字符串
 *
 * @return 目标地址 dst
 */
char *strcpy(char *dst, const char *src)
{
    size_t i = 0U;

    while (src[i] != '\0')
    {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';

    return dst;
}
