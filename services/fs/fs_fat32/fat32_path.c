/**
 * @file    fat32_path.c
 * @brief   FAT32 路径解析实现
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 路径解析实现：
 *          - 路径分割（按 '/' 分隔符）
 *          - 跳过前导 '/' 字符
 *          - 8.3 文件名大小写不敏感匹配
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fat32_path.h"
#include <string.h>
#include <stdint.h>

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 大写转小写
 *
 * @param c 输入字符
 *
 * @return 转换后的字符
 */
static char to_lower_char(char c)
{
    if ((c >= 'A') && (c <= 'Z'))
    {
        return (char)((int32_t)c + 32);
    }
    return c;
}

/* ========================================================================
 * 路径解析实现
 * ======================================================================== */

/**
 * @brief 提取路径中的下一个组件
 */
int32_t fat32_path_next_component(const char **path, char *component,
                                   uint32_t comp_size)
{
    const char *p;
    uint32_t pos;

    if ((path == NULL) || (*path == NULL) || (component == NULL))
    {
        return -22; /* -EINVAL */
    }

    p = *path;

    /* 跳过前导 '/' */
    while (*p == '/')
    {
        p++;
    }

    /* 检查是否到达路径末尾 */
    if (*p == '\0')
    {
        component[0] = '\0';
        *path = p;
        return 0;
    }

    /* 提取组件 */
    pos = 0U;
    while ((*p != '\0') && (*p != '/'))
    {
        if (pos < (comp_size - 1U))
        {
            component[pos] = *p;
            pos++;
        }
        p++;
    }

    component[pos] = '\0';

    /* 跳过尾随 '/' */
    while (*p == '/')
    {
        p++;
    }

    *path = p;

    return 1;
}

/* ========================================================================
 * 文件名匹配实现
 * ======================================================================== */

/**
 * @brief 8.3 格式文件名匹配（大小写不敏感）
 */
bool fat32_name_match_83(const char *name83, const char *longname)
{
    char composed[13];
    uint32_t i;
    uint32_t pos;

    if ((name83 == NULL) || (longname == NULL))
    {
        return false;
    }

    /* 组合 8.3 格式为 "NAME.EXT" 格式 */
    pos = 0U;

    /* 主文件名 */
    for (i = 0U; (i < 8U) && (pos < 12U); i++)
    {
        if (name83[i] == ' ')
        {
            break;
        }
        composed[pos] = name83[i];
        pos++;
    }

    /* 扩展名 */
    if (name83[8] != ' ')
    {
        if (pos < 12U)
        {
            composed[pos] = '.';
            pos++;
        }
        for (i = 8U; (i < 11U) && (pos < 12U); i++)
        {
            if (name83[i] == ' ')
            {
                break;
            }
            composed[pos] = name83[i];
            pos++;
        }
    }

    composed[pos] = '\0';

    /* 长度检查 */
    if (strlen(longname) != strlen(composed))
    {
        return false;
    }

    /* 大小写不敏感比较 */
    for (i = 0U; i < pos; i++)
    {
        if (to_lower_char(composed[i]) != to_lower_char(longname[i]))
        {
            return false;
        }
    }

    return true;
}
