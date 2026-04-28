/**
 * @file    fat32_fat.h
 * @brief   FAT32 FAT 表和簇管理头文件
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 FAT 表解析和簇管理接口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_FAT32_FAT_H
#define FS_FAT32_FAT_H

#include "fat32_bpb.h"
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * FAT32 FAT 表和簇管理接口
 * ======================================================================== */

/**
 * @brief FAT 表上下文
 */
typedef struct
{
    const fat32_volume_info_t *volume_info;  /**< @brief 卷信息 */
    uint32_t                     free_clusters;  /**< @brief 自由簇数 */
    uint32_t                     next_free;      /**< @brief 下一个自由簇 */
    bool                         initialized;    /**< @brief 初始化标志 */
} fat32_fat_context_t;

/**
 * @brief 初始化 FAT 表上下文
 *
 * @param context     FAT 表上下文
 * @param volume_info 卷信息
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_fat_init(fat32_fat_context_t *context,
                       const fat32_volume_info_t *volume_info);

/**
 * @brief 读取 FAT 表项
 *
 * @param context FAT 表上下文
 * @param cluster 簇号
 *
 * @return 下一簇号，0xFFFFFFFF 表示错误
 */
uint32_t fat32_read_fat_entry(const fat32_fat_context_t *context,
                                uint32_t cluster);

/**
 * @brief 写入 FAT 表项
 *
 * @param context FAT 表上下文
 * @param cluster 簇号
 * @param value   下一簇号
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_write_fat_entry(const fat32_fat_context_t *context,
                                uint32_t cluster,
                                uint32_t value);

/**
 * @brief 检查簇是否是链的末尾
 *
 * @param cluster 簇号
 *
 * @return true 是末尾，false 不是
 */
bool fat32_is_eoc(uint32_t cluster);

/**
 * @brief 检查簇是否损坏
 *
 * @param cluster 簇号
 *
 * @return true 损坏，false 正常
 */
bool fat32_is_bad(uint32_t cluster);

/**
 * @brief 分配一个自由簇
 *
 * @param context FAT 表上下文
 *
 * @return 分配的簇号，0xFFFFFFFF 表示失败
 */
uint32_t fat32_alloc_cluster(fat32_fat_context_t *context);

/**
 * @brief 释放簇
 *
 * @param context FAT 表上下文
 * @param cluster 簇号
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_free_cluster(fat32_fat_context_t *context,
                           uint32_t cluster);

#endif /* FS_FAT32_FAT_H */
