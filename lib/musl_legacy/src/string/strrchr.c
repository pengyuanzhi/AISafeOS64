/**
 * @file    strrchr.c
 * @brief   查找字符最后一次出现位置实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 在字符串中查找指定字符最后一次出现的位置。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <string.h>

/**
 * @brief 查找字符最后一次出现的位置
 *
 * @details 从字符串末尾向前查找字符 c 最后一次出现的位置。
 *
 * @param s 输入字符串
 * @param c 要查找的字符
 *
 * @return 找到返回指向该字符的指针，未找到返回 NULL
 */
char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    unsigned char uc = (unsigned char)c;

    for (;;)
    {
        if ((unsigned char)*s == uc)
        {
            last = s;
        }

        if (*s == '\0')
        {
            break;
        }

        s++;
    }

    return (char *)last;
}
