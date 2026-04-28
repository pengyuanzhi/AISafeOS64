/**
 * @file    ramfs.h
 * @brief   RAMFS 内存文件系统头文件
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details RAMFS 内存文件系统接口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_RAMFS_H
#define FS_RAMFS_H

#include "fs_ops.h"

/**
 * @brief 获取 RAMFS 操作接口
 *
 * @return RAMFS 操作接口指针
 */
const fs_ops_t *ramfs_get_ops(void);

#endif /* FS_RAMFS_H */
