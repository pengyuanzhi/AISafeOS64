/**
 * @file    fat32.h
 * @brief   FAT32 文件系统公共头文件
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details FAT32 文件系统公共接口：
 *          - 提供 fs_ops 抽象层适配
 *          - 挂载/卸载/读写等操作
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_FAT32_H
#define FS_FAT32_H

#include "fs_ops.h"

/**
 * @brief 获取 FAT32 操作接口
 *
 * @return FAT32 操作接口指针
 */
const fs_ops_t *fat32_get_ops(void);

#endif /* FS_FAT32_H */
