/**
 * @file    ext4_ops.h
 * @brief   Ext4 文件系统 fs_ops 适配层头文件
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details Ext4 文件系统 fs_ops 适配层接口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef EXT4_OPS_H
#define EXT4_OPS_H

#include "fs_ops.h"

/**
 * @brief 获取 Ext4 操作接口
 *
 * @return Ext4 操作接口指针
 */
const fs_ops_t *ext4_get_ops(void);

#endif /* EXT4_OPS_H */
