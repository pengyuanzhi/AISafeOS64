/**
 * @file    memchr.c
 * @brief   内存中查找字符实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 在内存区域中查找指定字符的首次出现位置。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <string.h>

/**
 * @brief 内存中查找字符
 *
 * @details 在 s 指向的前 n 个字节中查找字符 c（转换为 unsigned char）。
 *
 * @param s 内存起始地址
 * @param c 要查找的字符
 * @param n 搜索字节数
 *
 * @return 找到返回指向该字符的指针，未找到返回 NULL
 */
void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char uc = (unsigned char)c;
    size_t i;

    for (i = 0U; i < n; i++)
    {
        if (p[i] == uc)
        {
            return (void *)&p[i];
        }
    }

    return NULL;
}
