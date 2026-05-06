/**
 * @file    ext4_inode_bitmap.h
 * @brief   Ext4 Inode 位图管理头文件
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 Inode 位图管理接口：
 *          - Inode 分配
 *          - Inode 释放
 *          - 位图读取/写入
 *          - 空闲 Inode 统计
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef EXT4_INODE_BITMAP_H
#define EXT4_INODE_BITMAP_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 块组配置
 * ======================================================================== */

/** @brief 块组块数 */
#define EXT4_BLOCKS_PER_GROUP    32768U

/** @brief 块组 Inode 数 */
#define EXT4_INODES_PER_GROUP    8192U

/** @brief Inode 位图在块组中的偏移 */
#define EXT4_INODE_BITMAP_OFFSET(block_id)    \
    ((block_id) + EXT4_BITMAP_BLOCK_COUNT + 1U)

/* ========================================================================
 * Inode 位图接口
 * ======================================================================== */

/**
 * @brief 分配 Inode
 *
 * @param block_id    块组 ID
 * @param inode_nr    输出 Inode 编号
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_alloc_inode_bitmap(uint32_t block_id, uint32_t *inode_nr);

/**
 * @brief 释放 Inode
 *
 * @param block_id    块组 ID
 * @param inode_nr    Inode 编号
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_free_inode_bitmap(uint32_t block_id, uint32_t inode_nr);

/**
 * @brief 检查 Inode 是否被占用
 *
 * @param block_id    块组 ID
 * @param inode_nr    Inode 编号
 *
 * @return true 已占用，false 空闲
 */
bool ext4_is_inode_used(uint32_t block_id, uint32_t inode_nr);

/**
 * @brief 获取空闲 Inode 数
 *
 * @param block_id    块组 ID
 *
 * @return 空闲 Inode 数
 */
uint32_t ext4_get_free_inodes_bitmap(uint32_t block_id);

/**
 * @brief 获取使用 Inode 数
 *
 * @param block_id    块组 ID
 *
 * @return 使用 Inode 数
 */
uint32_t ext4_get_used_inodes_bitmap(uint32_t block_id);

#endif /* EXT4_INODE_BITMAP_H */
