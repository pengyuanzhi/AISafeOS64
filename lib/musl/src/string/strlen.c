/**
 * @file    strlen.c
 * @brief   计算字符串长度实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 计算以空字符结尾的字符串长度（不包含终止符）。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <string.h>

/**
 * @brief 计算字符串长度
 *
 * @details 遍历字符串直到遇到终止符 '\0'，返回字符个数。
 *
 * @param s 输入字符串
 *
 * @return 字符串长度（不含终止符）
 */
size_t strlen(const char *s)
{
    size_t len = 0U;

    while (s[len] != '\0')
    {
        len++;
    }

    return len;
}
