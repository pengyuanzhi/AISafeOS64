/**
 * @file    strchr.c
 * @brief   查找字符首次出现位置实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 在字符串中查找指定字符首次出现的位置。
 *          支持查找终止符 '\0'。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <string.h>

/**
 * @brief 查找字符首次出现的位置
 *
 * @details 在 s 指向的字符串中查找字符 c 首次出现的位置。
 *          终止符 '\0' 也被视为字符串的一部分，可以查找。
 *
 * @param s 输入字符串
 * @param c 要查找的字符
 *
 * @return 找到返回指向该字符的指针，未找到返回 NULL
 */
char *strchr(const char *s, int c)
{
    unsigned char uc = (unsigned char)c;

    for (;;)
    {
        if ((unsigned char)s[0] == uc)
        {
            return (char *)s;
        }

        if (s[0] == '\0')
        {
            return NULL;
        }

        s++;
    }
}
