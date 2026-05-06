/**
 * @file    ext4_inode.c
 * @brief   Ext4 Inode 管理实现
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 Inode 管理实现：
 *          - 读取和写入 Inode
 *          - 分配和释放 Inode
 *          - Inode 缓存
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "ext4_inode.h"
#include <string.h>

/* ========================================================================
 * 内部变量
 * ======================================================================== */

/** @brief 最大 Inode 数量 */
#define EXT4_MAX_INODES         1000000U

/** @brief 最大 Inode 编号 */
#define EXT4_MAX_INO            EXT4_MAX_INODES

/** @brief Inode 表大小 */
#define EXT4_INODE_TABLE_SIZE   EXT4_MAX_INODES * EXT4_INODE_SIZE

/** @brief Mock Inode 表 */
static ext4_inode_t s_inode_table[EXT4_MAX_INODES];
static uint32_t      s_next_ino = 1U;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 获取块组号
 *
 * @param ino    Inode 编号
 *
 * @return 块组号
 */
static uint32_t get_block_group(uint32_t ino, uint32_t bg_size)
{
    return (ino - 1U) / bg_size;
}

/**
 * @brief 从块组获取块偏移
 *
 * @param ino    Inode 编号
 * @param bg_size 块组大小
 *
 * @return 块偏移
 */
static uint64_t get_block_offset(uint32_t ino, uint32_t bg_size)
{
    return (uint64_t)bg_size * get_block_group(ino, bg_size);
}

/**
 * @brief 从磁盘读取块（模拟）
 *
 * @param offset 偏移量
 * @param size   大小
 * @param buf    输出缓冲区
 *
 * @return 0 成功，<0 失败
 */
static int32_t mock_block_read_offset(uint64_t offset, uint32_t size, void *buf)
{
    if (buf == NULL)
    {
        return -22; /* EINVAL */
    }

    /* Inode 表在块 1-100 */
    if (offset >= EXT4_BLOCK_SIZE && offset < EXT4_BLOCK_SIZE + EXT4_INODE_TABLE_SIZE)
    {
        uint32_t inode_idx = (offset - EXT4_BLOCK_SIZE) / EXT4_INODE_SIZE;
        if (inode_idx < EXT4_MAX_INODES)
        {
            (void)memcpy(buf, &s_inode_table[inode_idx], size);
            return 0;
        }
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
static int32_t mock_block_write_offset(uint64_t offset, uint32_t size, const void *buf)
{
    if (buf == NULL)
    {
        return -22; /* EINVAL */
    }

    /* Inode 表在块 1-100 */
    if (offset >= EXT4_BLOCK_SIZE && offset < EXT4_BLOCK_SIZE + EXT4_INODE_TABLE_SIZE)
    {
        uint32_t inode_idx = (offset - EXT4_BLOCK_SIZE) / EXT4_INODE_SIZE;
        if (inode_idx < EXT4_MAX_INODES)
        {
            (void)memcpy(&s_inode_table[inode_idx], buf, size);
            return 0;
        }
    }

    return -5; /* EIO */
}

/* ========================================================================
 * Inode 接口实现
 * ======================================================================== */

/**
 * @brief 读取 Inode
 *
 * @param ino      Inode 编号
 * @param inode    输出 Inode
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_read_inode(uint32_t ino, ext4_inode_t *inode)
{
    uint64_t offset;
    int32_t ret;

    if (ino == 0 || ino > EXT4_MAX_INO)
    {
        return -22; /* EINVAL */
    }

    if (inode == NULL)
    {
        return -22; /* EINVAL */
    }

    /* 查找缓存 */
    if (ino < EXT4_MAX_INODES && s_inode_table[ino].in_use)
    {
        (void)memcpy(inode, &s_inode_table[ino], sizeof(ext4_inode_t));
        return 0;
    }

    /* 读取磁盘 */
    offset = get_block_offset(ino, EXT4_INODE_TABLE_SIZE);
    ret = mock_block_read_offset(offset, sizeof(ext4_inode_t), inode);

    if (ret == 0)
    {
        if (ino < EXT4_MAX_INODES)
        {
            s_inode_table[ino].in_use = true;
        }
    }

    return ret;
}

/**
 * @brief 写入 Inode
 *
 * @param ino    Inode 编号
 * @param inode  Inode
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_write_inode(uint32_t ino, const ext4_inode_t *inode)
{
    uint64_t offset;
    int32_t ret;

    if (ino == 0 || ino > EXT4_MAX_INO)
    {
        return -22; /* EINVAL */
    }

    if (inode == NULL)
    {
        return -22; /* EINVAL */
    }

    /* 更新缓存 */
    if (ino < EXT4_MAX_INODES)
    {
        (void)memcpy(&s_inode_table[ino], inode, sizeof(ext4_inode_t));
        s_inode_table[ino].in_use = true;
    }

    /* 写入磁盘 */
    offset = get_block_offset(ino, EXT4_INODE_TABLE_SIZE);
    ret = mock_block_write_offset(offset, sizeof(ext4_inode_t), inode);

    return ret;
}

/**
 * @brief 分配 Inode
 *
 * @param mode    文件模式
 * @param uid     用户 ID
 * @param gid     组 ID
 *
 * @return Inode 编号（>=0 成胜），<0 失败
 */
uint32_t ext4_alloc_inode(uint32_t mode, uint32_t uid, uint32_t gid)
{
    uint32_t i;

    /* 在缓存中查找空闲 Inode */
    for (i = 0U; i < EXT4_MAX_INODES; i++)
    {
        if (!s_inode_table[i].in_use)
        {
            (void)memset(&s_inode_table[i], 0, sizeof(ext4_inode_t));

            s_inode_table[i].i_mode = mode;
            s_inode_table[i].i_uid = uid;
            s_inode_table[i].i_atime = 0U;
            s_inode_table[i].i_ctime = 0U;
            s_inode_table[i].i_mtime = 0U;
            s_inode_table[i].i_links_count = 0U;
            s_inode_table[i].i_blocks = 0U;
            s_inode_table[i].in_use = true;

            return s_next_ino++;
        }
    }

    return 0U;
}

/**
 * @brief 释放 Inode
 *
 * @param ino    Inode 编号
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_free_inode(uint32_t ino)
{
    if (ino == 0 || ino > EXT4_MAX_INO)
    {
        return -22; /* EINVAL */
    }

    if (ino < EXT4_MAX_INODES)
    {
        s_inode_table[ino].in_use = false;
        (void)memset(&s_inode_table[ino], 0, sizeof(ext4_inode_t));
    }

    return 0;
}

/**
 * @brief 获取文件类型
 *
 * @param ino    Inode 编号
 * @param type   输出文件类型
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_get_file_type(uint32_t ino, ext4_file_type_t *type)
{
    ext4_inode_t inode;
    int32_t ret;

    if (ino == 0 || ino > EXT4_MAX_INO)
    {
        return -22; /* EINVAL */
    }

    if (type == NULL)
    {
        return -22; /* EINVAL */
    }

    ret = ext4_read_inode(ino, &inode);

    if (ret == 0)
    {
        if ((inode.i_mode & EXT4_S_IFMT) == EXT4_S_IFDIR)
        {
            *type = EXT4_FT_DIR;
        }
        else if ((inode.i_mode & EXT4_S_IFMT) == EXT4_S_IFREG)
        {
            *type = EXT4_FT_REG_FILE;
        }
        else
        {
            *type = EXT4_FT_UNKNOWN;
        }
    }

    return ret;
}

/**
 * @brief 获取文件大小
 *
 * @param ino    Inode 编号
 * @param size   输出文件大小
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_get_file_size(uint32_t ino, uint32_t *size)
{
    ext4_inode_t inode;
    int32_t ret;

    if (ino == 0 || ino > EXT4_MAX_INO)
    {
        return -22; /* EINVAL */
    }

    if (size == NULL)
    {
        return -22; /* EINVAL */
    }

    ret = ext4_read_inode(ino, &inode);

    if (ret == 0)
    {
        *size = inode.i_size;
    }

    return ret;
}

/**
 * @brief 获取块数量
 *
 * @param ino    Inode 编号
 * @param count  输出块数量
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_get_block_count(uint32_t ino, uint32_t *count)
{
    ext4_inode_t inode;
    int32_t ret;

    if (ino == 0 || ino > EXT4_MAX_INO)
    {
        return -22; /* EINVAL */
    }

    if (count == NULL)
    {
        return -22; /* EINVAL */
    }

    ret = ext4_read_inode(ino, &inode);

    if (ret == 0)
    {
        *count = inode.i_blocks;
    }

    return ret;
}

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 初始化 Inode 表
 */
void ext4_inode_table_init(void)
{
    (void)memset(s_inode_table, 0, sizeof(s_inode_table));
    s_next_ino = 1U;
}
