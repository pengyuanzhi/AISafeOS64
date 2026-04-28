/**
 * @file    fat32_fat.h
 * @brief   FAT32 FAT 表操作接口
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 FAT 表操作接口：
 *          - FAT 表项读取和写入
 *          - 空闲簇分配和释放
 *          - 簇链解析
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_FAT32_FAT_H
#define FS_FAT32_FAT_H

#include "fat32_types.h"

/**
 * @brief 读取 FAT 表项
 *
 * @param ctx     FAT32 上下文
 * @param cluster 簇号
 * @param value   输出 FAT 表项值
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_fat_read_entry(fat32_context_t *ctx, uint32_t cluster,
                              uint32_t *value);

/**
 * @brief 写入 FAT 表项（更新所有副本）
 *
 * @param ctx     FAT32 上下文
 * @param cluster 簇号
 * @param value   要写入的 FAT 表项值
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_fat_write_entry(fat32_context_t *ctx, uint32_t cluster,
                               uint32_t value);

/**
 * @brief 分配一个空闲簇
 *
 * @param ctx     FAT32 上下文
 * @param cluster 输出分配的簇号
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_alloc_cluster(fat32_context_t *ctx, uint32_t *cluster);

/**
 * @brief 释放一个簇
 *
 * @param ctx     FAT32 上下文
 * @param cluster 要释放的簇号
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_free_cluster(fat32_context_t *ctx, uint32_t cluster);

/**
 * @brief 获取簇链
 *
 * @param ctx       FAT32 上下文
 * @param start     起始簇号
 * @param chain     簇链输出缓冲区
 * @param max_len   缓冲区最大长度
 * @param out_len   输出簇链实际长度
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_get_cluster_chain(fat32_context_t *ctx, uint32_t start,
                                 uint32_t *chain, uint32_t max_len,
                                 uint32_t *out_len);

#endif /* FS_FAT32_FAT_H */
