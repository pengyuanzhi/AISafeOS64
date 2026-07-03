/**
 * @file    ext4_inode.h
 * @brief   Ext4 Inode 管理头文件
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 Inode 管理接口：
 *          - 读取 Inode
 *          - 写入 Inode
 *          - 分配 Inode
 *          - 释放 Inode
 *          - Inode 缓存
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef EXT4_INODE_H
#define EXT4_INODE_H

#include <stdint.h>
#include <stdbool.h>
#include "ext4_types.h"

/* ========================================================================
 * Inode 管理常量
 * ======================================================================== */

/** @brief Ext4 Inode 表偏移（基于块号） */
#define EXT4_INODE_TABLE_OFFSET(block_id) \
    (block_id)

/* ========================================================================
 * Inode 状态
 * ======================================================================== */

/** @brief Inode 状态 */
typedef enum
{
    EXT4_INODE_FREE     = 0U,
    EXT4_INODE_USED     = 1U,
    EXT4_INODE_ORPHAN   = 2U
} ext4_inode_state_t;

/* ========================================================================
 * Inode 接口
 * ======================================================================== */

/**
 * @brief 读取 Inode
 *
 * @param ino      Inode 编号
 * @param inode    输出 Inode
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_read_inode(uint32_t ino, ext4_inode_t *inode);

/**
 * @brief 写入 Inode
 *
 * @param ino    Inode 编号
 * @param inode  Inode
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_write_inode(uint32_t ino, const ext4_inode_t *inode);

/**
 * @brief 分配 Inode
 *
 * @param mode    文件模式
 * @param uid     用户 ID
 * @param gid     组 ID
 *
 * @return Inode 编号（>=0 成功），<0 失败
 */
uint32_t ext4_alloc_inode(uint32_t mode, uint32_t uid, uint32_t gid);

/**
 * @brief 释放 Inode
 *
 * @param ino    Inode 编号
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_free_inode(uint32_t ino);

/**
 * @brief 获取文件类型
 *
 * @param ino    Inode 编号
 * @param type   输出文件类型
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_get_file_type(uint32_t ino, ext4_file_type_t *type);

/**
 * @brief 获取文件大小
 *
 * @param ino    Inode 编号
 * @param size   输出文件大小
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_get_file_size(uint32_t ino, uint32_t *size);

/**
 * @brief 获取块数量
 *
 * @param ino    Inode 编号
 * @param count  输出块数量
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_get_block_count(uint32_t ino, uint32_t *count);

#endif /* EXT4_INODE_H */
