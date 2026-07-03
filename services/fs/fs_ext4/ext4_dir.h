/**
 * @file    ext4_dir.h
 * @brief   Ext4 目录操作头文件
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 目录操作接口：
 *          - 创建目录
 *          - 删除目录
 *          - 目录项查找
 *          - 目录列表
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef EXT4_DIR_H
#define EXT4_DIR_H

#include <stdint.h>
#include <stdbool.h>
#include "ext4_types.h"

/* ========================================================================
 * 目录常量（仅在本文件及目录操作源文件中使用）
 * ======================================================================== */

/** @brief 最大文件名长度（与 EXT4_NAME_LEN 保持一致） */
#define EXT4_DIR_NAME_LEN        EXT4_NAME_LEN

/** @brief 最小目录项长度 */
#define EXT4_DIR_MIN_REC_LEN     8U

/* ========================================================================
 * 目录接口
 * ======================================================================== */

/**
 * @brief 创建目录
 *
 * @param parent_ino  父目录 Inode
 * @param name        目录名
 * @param mode        权限模式
 * @param uid         用户 ID
 * @param gid         组 ID
 *
 * @return Inode 编号（>=0 成胜），<0 失败
 */
int32_t ext4_mkdir(uint32_t parent_ino, const char *name,
                    uint32_t mode, uint32_t uid, uint32_t gid);

/**
 * @brief 删除目录
 *
 * @param ino    目录 Inode
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_rmdir(uint32_t ino);

/**
 * @brief 查找目录项
 *
 * @param parent_ino 父目录 Inode
 * @param name       文件名
 * @param entry      输出目录项
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_lookup(uint32_t parent_ino, const char *name,
                     ext4_dir_entry_t *entry);

/**
 * @brief 列出目录内容
 *
 * @param parent_ino 父目录 Inode
 * @param entries    输出目录项数组
 * @param max_count  最大条目数
 *
 * @return 实际条目数（>=0 成胜），<0 失败
 */
int32_t ext4_readdir(uint32_t parent_ino, ext4_dir_entry_t *entries,
                      uint32_t max_count);

/**
 * @brief 检查目录是否为空
 *
 * @param ino    目录 Inode
 *
 * @return true 空，false 非空
 */
bool ext4_is_dir_empty(uint32_t ino);

#endif /* EXT4_DIR_H */
