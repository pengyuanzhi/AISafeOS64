/**
 * @file    ext4_inode_bitmap.c
 * @brief   Ext4 Inode 位图管理实现
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 Inode 位图管理实现：
 *          - Inode 分配和释放
 *          - 位图读取/写入
 *          - 空闲 Inode 统计
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "ext4_inode_bitmap.h"
#include <string.h>

/* ========================================================================
 * 内部变量
 * ======================================================================== */

/** @brief Mock Inode 位图 */
static uint32_t s_inode_bitmap[EXT4_TOTAL_BLOCK_GROUPS];
static bool     s_inode_bitmap_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 获取 Inode 位图在磁盘中的偏移
 *
 * @param block_id 块组 ID
 *
 * @return 偏移量
 */
static uint64_t get_inode_bitmap_offset(uint32_t block_id)
{
    /* Inode 位图在块组 0 的第 4 块 */
    return (uint64_t)(block_id * EXT4_BLOCKS_PER_GROUP + 4U) * (uint64_t)4096U;
}

/**
 * @brief 模拟读取块
 *
 * @param offset 偏移量
 * @param size   大小
 * @param buf    输出缓冲区
 *
 * @return 0 成功，<0 失败
 */
static int32_t mock_block_read(uint64_t offset, uint32_t size, void *buf)
{
    if (buf == NULL)
    {
        return -22; /* EINVAL */
    }

    if (!s_inode_bitmap_initialized)
    {
        (void)memset(buf, 0, size);
        return 0;
    }

    /* 读取 Inode 位图 */
    uint32_t bitmap_idx = offset / (uint32_t)4096U;
    if (bitmap_idx < EXT4_TOTAL_BLOCK_GROUPS)
    {
        (void)memcpy(buf, &s_inode_bitmap[bitmap_idx], size);
        return 0;
    }

    return -5; /* EIO */
}

/**
 * @brief 模拟写入块
 *
 * @param offset 偏移量
 * @param size   大小
 * @param buf    输入缓冲区
 *
 * @return 0 成功，<0 失败
 */
static int32_t mock_block_write(uint64_t offset, uint32_t size, const void *buf)
{
    if (buf == NULL)
    {
        return -22; /* EINVAL */
    }

    /* 写入 Inode 位图 */
    uint32_t bitmap_idx = offset / (uint32_t)4096U;
    if (bitmap_idx < EXT4_TOTAL_BLOCK_GROUPS)
    {
        (void)memcpy(&s_inode_bitmap[bitmap_idx], buf, size);
        return 0;
    }

    return -5; /* EIO */
}

/* ========================================================================
 * Inode 位图接口实现
 * ======================================================================== */

/**
 * @brief 初始化 Inode 位图
 */
void ext4_inode_bitmap_init(void)
{
    (void)memset(s_inode_bitmap, 0, sizeof(s_inode_bitmap));
    s_inode_bitmap_initialized = true;
}

/**
 * @brief 分配 Inode
 *
 * @param block_id    块组 ID
 * @param inode_nr    输出 Inode 编号
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_alloc_inode_bitmap(uint32_t block_id, uint32_t *inode_nr)
{
    uint32_t group_start;
    uint32_t i;

    if (block_id >= EXT4_TOTAL_BLOCK_GROUPS)
    {
        return -22; /* EINVAL */
    }

    if (inode_nr == NULL)
    {
        return -22; /* EINVAL */
    }

    group_start = block_id * EXT4_INODES_PER_GROUP;

    /* 查找空闲 Inode */
    for (i = 0U; i < EXT4_INODES_PER_GROUP; i++)
    {
        uint32_t bitmap_idx = i / 32U;
        uint32_t bit_pos = i % 32U;

        if ((s_inode_bitmap[block_id] & (1U << bit_pos)) == 0U)
        {
            /* 分配 Inode */
            s_inode_bitmap[block_id] |= (1U << bit_pos);
            *inode_nr = group_start + i + 1U;
            return 0;
        }
    }

    return -22; /* ENOSPC */
}

/**
 * @brief 释放 Inode
 *
 * @param block_id    块组 ID
 * @param inode_nr    Inode 编号
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_free_inode_bitmap(uint32_t block_id, uint32_t inode_nr)
{
    uint32_t group_start;
    uint32_t inode_idx;

    if (block_id >= EXT4_TOTAL_BLOCK_GROUPS)
    {
        return -22; /* EINVAL */
    }

    group_start = block_id * EXT4_INODES_PER_GROUP;

    if (inode_nr <= group_start || inode_nr > group_start + EXT4_INODES_PER_GROUP)
    {
        return -22; /* EINVAL */
    }

    inode_idx = inode_nr - group_start - 1U;

    if (inode_idx >= EXT4_INODES_PER_GROUP)
    {
        return -22; /* EINVAL */
    }

    /* 释放 Inode */
    uint32_t bitmap_idx = inode_idx / 32U;
    uint32_t bit_pos = inode_idx % 32U;

    s_inode_bitmap[block_id] &= ~(1U << bit_pos);

    return 0;
}

/**
 * @brief 检查 Inode 是否被占用
 *
 * @param block_id    块组 ID
 * @param inode_nr    Inode 编号
 *
 * @return true 已占用，false 空闲
 */
bool ext4_is_inode_used(uint32_t block_id, uint32_t inode_nr)
{
    uint32_t group_start;
    uint32_t inode_idx;

    if (block_id >= EXT4_TOTAL_BLOCK_GROUPS)
    {
        return true;
    }

    group_start = block_id * EXT4_INODES_PER_GROUP;

    if (inode_nr <= group_start || inode_nr > group_start + EXT4_INODES_PER_GROUP)
    {
        return true;
    }

    inode_idx = inode_nr - group_start - 1U;

    if (inode_idx >= EXT4_INODES_PER_GROUP)
    {
        return true;
    }

    uint32_t bitmap_idx = inode_idx / 32U;
    uint32_t bit_pos = inode_idx % 32U;

    return (s_inode_bitmap[block_id] & (1U << bit_pos)) != 0U;
}

/**
 * @brief 获取空闲 Inode 数
 *
 * @param block_id    块组 ID
 *
 * @return 空闲 Inode 数
 */
uint32_t ext4_get_free_inodes_bitmap(uint32_t block_id)
{
    uint32_t count = 0U;
    uint32_t i;

    if (block_id >= EXT4_TOTAL_BLOCK_GROUPS)
    {
        return 0U;
    }

    for (i = 0U; i < EXT4_INODES_PER_GROUP; i++)
    {
        uint32_t bitmap_idx = i / 32U;
        uint32_t bit_pos = i % 32U;

        if ((s_inode_bitmap[block_id] & (1U << bit_pos)) == 0U)
        {
            count++;
        }
    }

    return count;
}

/**
 * @brief 获取使用 Inode 数
 *
 * @param block_id    块组 ID
 *
 * @return 使用 Inode 数
 */
uint32_t ext4_get_used_inodes_bitmap(uint32_t block_id)
{
    return EXT4_INODES_PER_GROUP - ext4_get_free_inodes_bitmap(block_id);
}
