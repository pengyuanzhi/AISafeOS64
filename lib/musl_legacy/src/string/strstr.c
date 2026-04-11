/**
 * @file    strstr.c
 * @brief   查找子字符串实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 在主字符串中查找子字符串首次出现的位置。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <string.h>

/**
 * @brief 查找子字符串
 *
 * @details 在 haystack 指向的字符串中查找 needle 指向的子串。
 *          若 needle 为空字符串，则返回 haystack。
 *
 * @param haystack 主字符串
 * @param needle   要查找的子串
 *
 * @return 找到返回指向子串起始位置的指针，未找到返回 NULL
 */
char *strstr(const char *haystack, const char *needle)
{
    size_t needle_len;
    size_t haystack_len;
    size_t i;

    needle_len = strlen(needle);

    if (needle_len == 0U)
    {
        return (char *)haystack;
    }

    haystack_len = strlen(haystack);

    for (i = 0U; i + needle_len <= haystack_len; i++)
    {
        if (memcmp(&haystack[i], needle, needle_len) == 0)
        {
            return (char *)&haystack[i];
        }
    }

    return NULL;
}
