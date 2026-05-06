/**
 * @file    ext4_permission.c
 * @brief   Ext4 权限管理实现
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 权限管理实现：
 *          - chmod: 修改权限
 *          - chown: 修改所有者
 *          - 权限检查
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "ext4_types.h"
#include "ext4_permission.h"
#include <string.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

#define EXT4_MAX_FILES         64U

/* ========================================================================
 * Mock Inode 表（简化版）
 * ======================================================================== */

typedef struct
{
    uint32_t ino;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    bool     in_use;
} ext4_mock_inode_t;

static ext4_mock_inode_t s_inodes[EXT4_MAX_FILES];
static uint32_t            s_next_ino = 1U;

/* ========================================================================
 * 权限接口实现
 * ======================================================================== */

/**
 * @brief 修改文件权限
 *
 * @param ino  Inode 编号
 * @param mode 新权限模式
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_chmod(uint32_t ino, uint32_t mode)
{
    uint32_t i;

    if (ino == 0 || ino > EXT4_MAX_FILES)
    {
        return -22; /* EINVAL */
    }

    for (i = 0U; i < EXT4_MAX_FILES; i++)
    {
        if (s_inodes[i].in_use && s_inodes[i].ino == ino)
        {
            s_inodes[i].mode = mode;
            return 0;
        }
    }

    return -2; /* ENOENT */
}

/**
 * @brief 修改文件所有者
 *
 * @param ino  Inode 编号
 * @param uid  新用户 ID
 * @param gid  新组 ID
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_chown(uint32_t ino, uint32_t uid, uint32_t gid)
{
    uint32_t i;

    if (ino == 0 || ino > EXT4_MAX_FILES)
    {
        return -22; /* EINVAL */
    }

    for (i = 0U; i < EXT4_MAX_FILES; i++)
    {
        if (s_inodes[i].in_use && s_inodes[i].ino == ino)
        {
            s_inodes[i].uid = uid;
            s_inodes[i].gid = gid;
            return 0;
        }
    }

    return -2; /* ENOENT */
}

/**
 * @brief 检查访问权限
 *
 * @param ino  Inode 编号
 * @param uid  用户 ID
 * @param gid  组 ID
 * @param mode 访问模式（读/写/执行）
 *
 * @return true 允许，false 拒绝
 */
bool ext4_check_permission(uint32_t ino, uint32_t uid,
                            uint32_t gid, uint32_t mode)
{
    uint32_t i;
    uint32_t file_mode;
    uint32_t file_uid;
    uint32_t file_gid;

    if (ino == 0 || ino > EXT4_MAX_FILES)
    {
        return false;
    }

    /* 查找 Inode */
    for (i = 0U; i < EXT4_MAX_FILES; i++)
    {
        if (s_inodes[i].in_use && s_inodes[i].ino == ino)
        {
            file_mode = s_inodes[i].mode;
            file_uid = s_inodes[i].uid;
            file_gid = s_inodes[i].gid;
            break;
        }
    }

    /* Root 用户（UID 0）总是允许 */
    if (uid == 0U)
    {
        return true;
    }

    /* 检查用户权限 */
    if (uid == file_uid)
    {
        uint32_t user_perms = (file_mode >> 6U) & 07U;

        if ((mode & user_perms) == mode)
        {
            return true;
        }
    }

    /* 检查组权限 */
    if (gid == file_gid)
    {
        uint32_t group_perms = (file_mode >> 3U) & 07U;

        if ((mode & group_perms) == mode)
        {
            return true;
        }
    }

    /* 检查其他权限 */
    uint32_t other_perms = file_mode & 07U;

    if ((mode & other_perms) == mode)
    {
        return true;
    }

    return false;
}

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 分配 Inode
 *
 * @param mode 权限模式
 * @param uid  用户 ID
 * @param gid  组 ID
 *
 * @return Inode 编号，0 表示失败
 */
uint32_t ext4_inode_alloc(uint32_t mode, uint32_t uid, uint32_t gid)
{
    uint32_t i;

    for (i = 0U; i < EXT4_MAX_FILES; i++)
    {
        if (!s_inodes[i].in_use)
        {
            s_inodes[i].ino = s_next_ino++;
            s_inodes[i].mode = mode;
            s_inodes[i].uid = uid;
            s_inodes[i].gid = gid;
            s_inodes[i].in_use = true;
            return s_inodes[i].ino;
        }
    }

    return 0U;
}

/**
 * @brief 释放 Inode
 *
 * @param ino  Inode 编号
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_inode_free(uint32_t ino)
{
    uint32_t i;

    if (ino == 0 || ino > EXT4_MAX_FILES)
    {
        return -22; /* EINVAL */
    }

    for (i = 0U; i < EXT4_MAX_FILES; i++)
    {
        if (s_inodes[i].in_use && s_inodes[i].ino == ino)
        {
            s_inodes[i].in_use = false;
            return 0;
        }
    }

    return -2; /* ENOENT */
}
