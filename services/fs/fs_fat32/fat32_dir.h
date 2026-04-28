/**
 * @file    fat32_dir.h
 * @brief   FAT32 目录项解析接口
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 目录项解析接口：
 *          - 8.3 文件名提取
 *          - 目录项属性判断
 *          - 目录遍历和文件查找
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_FAT32_DIR_H
#define FS_FAT32_DIR_H

#include "fat32_types.h"

/**
 * @brief 从目录项提取 8.3 格式文件名
 *
 * @param entry    目录项指针
 * @param name_out 输出文件名缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_dir_extract_name(const fat32_dir_entry_t *entry,
                                char *name_out, uint32_t buf_size);

/**
 * @brief 在指定目录簇中查找文件
 *
 * @param ctx       FAT32 上下文
 * @param dir_clust 目录起始簇号
 * @param name      要查找的文件名（长文件名格式）
 * @param entry     输出匹配的目录项
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_lookup_file(fat32_context_t *ctx, uint32_t dir_clust,
                           const char *name, fat32_dir_entry_t *entry);

#endif /* FS_FAT32_DIR_H */
