/**
 * @file    ext4.h
 * @brief   Ext4 文件系统公共头文件
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 文件系统公共接口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef EXT4_H
#define EXT4_H

#include "ext4_types.h"

/**
 * @brief 获取 Ext4 操作接口
 *
 * @return Ext4 操作接口指针
 */
const void *ext4_get_ops(void);

#endif /* EXT4_H */
