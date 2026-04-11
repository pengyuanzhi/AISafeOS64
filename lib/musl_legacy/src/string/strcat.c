/**
 * @file    strcat.c
 * @brief   字符串连接实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 将源字符串追加到目标字符串末尾。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <string.h>

/**
 * @brief 字符串连接
 *
 * @details 将 src 指向的字符串追加到 dst 末尾，
 *          覆盖 dst 的终止符并在末尾添加新的终止符。
 *
 * @param dst 目标字符串（已有内容后追加）
 * @param src 要追加的字符串
 *
 * @return 目标地址 dst
 */
char *strcat(char *dst, const char *src)
{
    size_t dst_len = strlen(dst);
    size_t i = 0U;

    while (src[i] != '\0')
    {
        dst[dst_len + i] = src[i];
        i++;
    }

    dst[dst_len + i] = '\0';

    return dst;
}
