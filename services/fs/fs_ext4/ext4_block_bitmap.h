/**
 * @file    ext4_block_bitmap.h
 * @brief   Ext4 块位图管理头文件
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 块位图管理接口：
 *          - 块分配
 *          - 块释放
 *          - 位图读取/写入
 *          - 位图缓存
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef EXT4_BLOCK_BITMAP_H
#define EXT4_BLOCK_BITMAP_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 块组配置
 * ======================================================================== */

/** @brief 块组块数 */
#define EXT4_BLOCKS_PER_GROUP    32768U

/** @brief 块组 Inode 数 */
#define EXT4_INODES_PER_GROUP    8192U

/** @brief 位图块数 */
#define EXT4_BITMAP_BLOCK_COUNT  2U

/** @brief 块位图在块组中的偏移 */
#define EXT4_BLOCK_BITMAP_OFFSET(block_id)   \
    ((block_id) + EXT4_BITMAP_BLOCK_COUNT)

/** @brief Inode 位图在块组中的偏移 */
#define EXT4_INODE_BITMAP_OFFSET(block_id)    \
    ((block_id) + EXT4_BITMAP_BLOCK_COUNT + 1U)

/* ========================================================================
 * 块位图接口
 * ======================================================================== */

/**
 * @brief 分配块
 *
 * @param block_id    块组 ID
 * @param block_nr    输出块号
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_alloc_block(uint32_t block_id, uint32_t *block_nr);

/**
 * @brief 释放块
 *
 * @param block_id    块组 ID
 * @param block_nr    块号
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_free_block(uint32_t block_id, uint32_t block_nr);

/**
 * @brief 检查块是否被占用
 *
 * @param block_id    块组 ID
 * @param block_nr    块号
 *
 * @return true 已占用，false 空闲
 */
bool ext4_is_block_used(uint32_t block_id, uint32_t block_nr);

/**
 * @brief 获取空闲块数
 *
 * @param block_id    块组 ID
 *
 * @return 空闲块数
 */
uint32_t ext4_get_free_blocks(uint32_t block_id);

/**
 * @brief 获取使用块数
 *
 * @param block_id    块组 ID
 *
 * @return 使用块数
 */
uint32_t ext4_get_used_blocks(uint32_t block_id);

/**
 * @brief 获取空闲 Inode 数
 *
 * @param block_id    块组 ID
 *
 * @return 空闲 Inode 数
 */
uint32_t ext4_get_free_inodes(uint32_t block_id);

/**
 * @brief 获取使用 Inode 数
 *
 * @param block_id    块组 ID
 *
 * @return 使用 Inode 数
 */
uint32_t ext4_get_used_inodes(uint32_t block_id);

#endif /* EXT4_BLOCK_BITMAP_H */
