/**
 * @file    strncat.c
 * @brief   字符串连接（限制长度）实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 将源字符串的最多 n 个字符追加到目标字符串末尾。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <string.h>

/**
 * @brief 字符串连接（限制长度）
 *
 * @details 将 src 指向的字符串中最多 n 个字符追加到 dst 末尾，
 *          然后添加终止符 '\0'。
 *
 * @param dst 目标字符串
 * @param src 要追加的字符串
 * @param n   最大追加字符数
 *
 * @return 目标地址 dst
 */
char *strncat(char *dst, const char *src, size_t n)
{
    size_t dst_len = strlen(dst);
    size_t i = 0U;

    while ((i < n) && (src[i] != '\0'))
    {
        dst[dst_len + i] = src[i];
        i++;
    }

    dst[dst_len + i] = '\0';

    return dst;
}
