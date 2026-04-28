/**
 * @file    fat32_path.h
 * @brief   FAT32 路径解析接口
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 路径解析接口：
 *          - 路径分割（提取路径组件）
 *          - 8.3 文件名匹配
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_FAT32_PATH_H
#define FS_FAT32_PATH_H

#include "fat32_types.h"

/**
 * @brief 提取路径中的下一个组件
 *
 * @param path      输入/输出：路径指针（会被修改）
 * @param component 输出：提取的路径组件
 * @param comp_size 组件缓冲区大小
 *
 * @return 0 路径结束，1 成功提取组件，<0 失败
 */
int32_t fat32_path_next_component(const char **path, char *component,
                                   uint32_t comp_size);

/**
 * @brief 8.3 格式文件名匹配（大小写不敏感）
 *
 * @param name83   8.3 格式文件名（11 字节）
 * @param longname 长文件名
 *
 * @return true 匹配，false 不匹配
 */
bool fat32_name_match_83(const char *name83, const char *longname);

#endif /* FS_FAT32_PATH_H */
