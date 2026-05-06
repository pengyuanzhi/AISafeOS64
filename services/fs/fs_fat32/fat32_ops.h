/**
 * @file    fat32_ops.h
 * @brief   FAT32 文件系统 fs_ops 适配层头文件
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details FAT32 文件系统 fs_ops 适配层接口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_FAT32_OPS_H
#define FS_FAT32_OPS_H

#include "fs_ops.h"

/**
 * @brief 获取 FAT32 操作接口
 *
 * @return FAT32 操作接口指针
 */
const fs_ops_t *fat32_get_ops(void);

#endif /* FS_FAT32_OPS_H */
