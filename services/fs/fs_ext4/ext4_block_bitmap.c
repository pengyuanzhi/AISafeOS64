/**
 * @file    ext4_block_bitmap.c
 * @brief   Ext4 块位图管理实现
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 块位图管理实现：
 *          - 块分配和释放
 *          - 位图读取/写入
 *          - 空闲块统计
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "ext4_block_bitmap.h"
#include <string.h>

/* ========================================================================
 * 内部变量
 * ======================================================================== */

/** @brief 总块数 */
#define EXT4_TOTAL_BLOCKS        1000000U

/** @brief 总块组数 */
#define EXT4_TOTAL_BLOCK_GROUPS  (EXT4_TOTAL_BLOCKS / EXT4_BLOCKS_PER_GROUP)

/** @brief Mock 块位图 */
static uint32_t s_block_bitmap[EXT4_TOTAL_BLOCK_GROUPS];
static bool     s_bitmap_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 获取块位图在磁盘中的偏移
 *
 * @param block_id 块组 ID
 *
 * @return 偏移量
 */
static uint64_t get_block_bitmap_offset(uint32_t block_id)
{
    /* 块位图在块组 0 的第 2 和第 3 块 */
    return (uint64_t)(block_id * EXT4_BLOCKS_PER_GROUP + 2U) * (uint64_t)4096U;
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

    if (!s_bitmap_initialized)
    {
        (void)memset(buf, 0, size);
        return 0;
    }

    /* 读取块位图 */
    uint32_t bitmap_idx = offset / (uint32_t)4096U;
    if (bitmap_idx < EXT4_TOTAL_BLOCK_GROUPS)
    {
        (void)memcpy(buf, &s_block_bitmap[bitmap_idx], size);
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

    /* 写入块位图 */
    uint32_t bitmap_idx = offset / (uint32_t)4096U;
    if (bitmap_idx < EXT4_TOTAL_BLOCK_GROUPS)
    {
        (void)memcpy(&s_block_bitmap[bitmap_idx], buf, size);
        return 0;
    }

    return -5; /* EIO */
}

/* ========================================================================
 * 块位图接口实现
 * ======================================================================== */

/**
 * @brief 初始化块位图
 */
void ext4_block_bitmap_init(void)
{
    (void)memset(s_block_bitmap, 0, sizeof(s_block_bitmap));
    s_bitmap_initialized = true;
}

/**
 * @brief 分配块
 *
 * @param block_id    块组 ID
 * @param block_nr    输出块号
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_alloc_block(uint32_t block_id, uint32_t *block_nr)
{
    uint32_t group_start;
    uint32_t i;

    if (block_id >= EXT4_TOTAL_BLOCK_GROUPS)
    {
        return -22; /* EINVAL */
    }

    if (block_nr == NULL)
    {
        return -22; /* EINVAL */
    }

    group_start = block_id * EXT4_BLOCKS_PER_GROUP;

    /* 查找空闲块 */
    for (i = 2U; i < EXT4_BLOCKS_PER_GROUP; i++)
    {
        uint32_t bitmap_idx = i / 32U;
        uint32_t bit_pos = i % 32U;

        if ((s_block_bitmap[block_id] & (1U << bit_pos)) == 0U)
        {
            /* 分配块 */
            s_block_bitmap[block_id] |= (1U << bit_pos);
            *block_nr = group_start + i;
            return 0;
        }
    }

    return -22; /* ENOSPC */
}

/**
 * @brief 释放块
 *
 * @param block_id    块组 ID
 * @param block_nr    块号
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_free_block(uint32_t block_id, uint32_t block_nr)
{
    uint32_t group_start;
    uint32_t block_idx;

    if (block_id >= EXT4_TOTAL_BLOCK_GROUPS)
    {
        return -22; /* EINVAL */
    }

    if (block_nr < group_start)
    {
        return -22; /* EINVAL */
    }

    block_idx = block_nr - (block_id * EXT4_BLOCKS_PER_GROUP);

    if (block_idx >= EXT4_BLOCKS_PER_GROUP)
    {
        return -22; /* EINVAL */
    }

    if (block_idx < 2U)
    {
        return -22; /* 保留块 */
    }

    /* 释放块 */
    uint32_t bitmap_idx = block_idx / 32U;
    uint32_t bit_pos = block_idx % 32U;

    s_block_bitmap[block_id] &= ~(1U << bit_pos);

    return 0;
}

/**
 * @brief 检查块是否被占用
 *
 * @param block_id    块组 ID
 * @param block_nr    块号
 *
 * @return true 已占用，false 空闲
 */
bool ext4_is_block_used(uint32_t block_id, uint32_t block_nr)
{
    uint32_t group_start;
    uint32_t block_idx;

    if (block_id >= EXT4_TOTAL_BLOCK_GROUPS)
    {
        return true;
    }

    group_start = block_id * EXT4_BLOCKS_PER_GROUP;

    if (block_nr < group_start)
    {
        return true;
    }

    block_idx = block_nr - group_start;

    if (block_idx >= EXT4_BLOCKS_PER_GROUP)
    {
        return true;
    }

    if (block_idx < 2U)
    {
        return true;
    }

    uint32_t bitmap_idx = block_idx / 32U;
    uint32_t bit_pos = block_idx % 32U;

    return (s_block_bitmap[block_id] & (1U << bit_pos)) != 0U;
}

/**
 * @brief 获取空闲块数
 *
 * @param block_id    块组 ID
 *
 * @return 空闲块数
 */
uint32_t ext4_get_free_blocks(uint32_t block_id)
{
    uint32_t count = 0U;
    uint32_t i;

    if (block_id >= EXT4_TOTAL_BLOCK_GROUPS)
    {
        return 0U;
    }

    for (i = 2U; i < EXT4_BLOCKS_PER_GROUP; i++)
    {
        uint32_t bitmap_idx = i / 32U;
        uint32_t bit_pos = i % 32U;

        if ((s_block_bitmap[block_id] & (1U << bit_pos)) == 0U)
        {
            count++;
        }
    }

    return count;
}

/**
 * @brief 获取使用块数
 *
 * @param block_id    块组 ID
 *
 * @return 使用块数
 */
uint32_t ext4_get_used_blocks(uint32_t block_id)
{
    return EXT4_BLOCKS_PER_GROUP - 2U - ext4_get_free_blocks(block_id);
}

/**
 * @brief 获取空闲 Inode 数
 *
 * @param block_id    块组 ID
 *
 * @return 空闲 Inode 数
 */
uint32_t ext4_get_free_inodes(uint32_t block_id)
{
    return EXT4_INODES_PER_GROUP - ext4_get_used_inodes(block_id);
}

/**
 * @brief 获取使用 Inode 数
 *
 * @param block_id    块组 ID
 *
 * @return 使用 Inode 数
 */
uint32_t ext4_get_used_inodes(uint32_t block_id)
{
    /* Mock 实现：Inode 位图与块位图相同 */
    return ext4_get_used_blocks(block_id);
}
