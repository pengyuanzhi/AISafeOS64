/**
 * @file    ramfs.c
 * @brief   RAMFS 内存文件系统实现
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details RAMFS 内存文件系统实现：
 *          - 纯内存文件系统
 *          - 支持文件/目录操作
 *          - 简化实现，用于测试
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fs_ops.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief RAMFS 最大文件数 */
#define RAMFS_MAX_FILES        64U

/** @brief RAMFS 最大目录数 */
#define RAMFS_MAX_DIRS         16U

/** @brief 最大文件大小（简化版） */
#define RAMFS_MAX_FILE_SIZE    4096U

/* ========================================================================
 * RAMFS 文件描述符
 * ======================================================================== */

/**
 * @brief RAMFS 文件
 */
typedef struct ramfs_file
{
    char            name[64];        /**< @brief 文件名 */
    uint32_t        ino;             /**< @brief inode 编号 */
    uint8_t         data[RAMFS_MAX_FILE_SIZE]; /**< @brief 文件数据 */
    uint32_t        size;            /**< @brief 文件大小 */
    uint32_t        mode;            /**< @brief 文件权限 */
    uint32_t        uid;             /**< @brief 用户 ID */
    uint32_t        gid;             /**< @brief 组 ID */
    uint64_t        atime;           /**< @brief 访问时间 */
    uint64_t        mtime;           /**< @brief 修改时间 */
    uint64_t        ctime;           /**< @brief 创建时间 */
    bool            in_use;          /**< @brief 使用标记 */
} ramfs_file_t;

/** @brief RAMFS 文件表 */
static ramfs_file_t s_files[RAMFS_MAX_FILES];

/** @brief 下一个 inode 编号 */
static uint32_t s_next_ino;

/* ========================================================================
 * RAMFS 文件系统操作
 * ======================================================================== */

/**
 * @brief RAMFS mount
 */
static int32_t ramfs_mount(fs_mount_t *mnt, const char *device)
{
    (void)mnt;
    (void)device;

    s_next_ino = 1U;

    return 0;
}

/**
 * @brief RAMFS unmount
 */
static int32_t ramfs_unmount(fs_mount_t *mnt)
{
    (void)mnt;

    return 0;
}

/**
 * @brief RAMFS lookup
 */
static int32_t ramfs_lookup(uint32_t mount_id, const char *path,
                            fs_inode_t *inode)
{
    (void)mount_id;

    if (path == NULL || inode == NULL)
    {
        return -1;
    }

    /* 简化实现：假设根目录 '/' 返回成功 */
    if (path[0] == '/' && path[1] == '\0')
    {
        (void)memset(inode, 0, sizeof(fs_inode_t));
        inode->ino = 1U;
        inode->type = FS_TYPE_DIRECTORY;
        inode->mode = 0755U;
        return 0;
    }

    return -1;
}

/**
 * @brief RAMFS create
 */
static int32_t ramfs_create(uint32_t mount_id, const char *path,
                            uint32_t mode, fs_inode_t *inode)
{
    uint32_t i;

    (void)mount_id;
    (void)path;

    if (inode == NULL)
    {
        return -1;
    }

    /* 分配文件槽 */
    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (!s_files[i].in_use)
        {
            s_files[i].in_use = true;
            s_files[i].ino = s_next_ino++;
            s_files[i].size = 0U;
            s_files[i].mode = mode;
            s_files[i].uid = 0U;
            s_files[i].gid = 0U;
            s_files[i].atime = 0ULL;
            s_files[i].mtime = 0ULL;
            s_files[i].ctime = 0ULL;

            (void)memset(inode, 0, sizeof(fs_inode_t));
            inode->ino = s_files[i].ino;
            inode->type = FS_TYPE_REGULAR;
            inode->mode = mode;

            return 0;
        }
    }

    return -1;
}

/**
 * @brief RAMFS read
 */
static int64_t ramfs_read(uint32_t mount_id, uint32_t ino,
                           uint64_t offset, void *buf, uint64_t size)
{
    uint32_t i;

    (void)mount_id;

    if (buf == NULL)
    {
        return -1;
    }

    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (s_files[i].in_use && s_files[i].ino == ino)
        {
            if (offset >= s_files[i].size)
            {
                return 0;
            }

            if ((offset + size) > s_files[i].size)
            {
                size = s_files[i].size - offset;
            }

            (void)memcpy(buf, &s_files[i].data[(uint32_t)offset],
                         (size_t)size);

            return (int64_t)size;
        }
    }

    return -1;
}

/**
 * @brief RAMFS write
 */
static int64_t ramfs_write(uint32_t mount_id, uint32_t ino,
                            uint64_t offset, const void *buf, uint64_t size)
{
    uint32_t i;

    (void)mount_id;

    if (buf == NULL)
    {
        return -1;
    }

    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (s_files[i].in_use && s_files[i].ino == ino)
        {
            if ((offset + size) > RAMFS_MAX_FILE_SIZE)
            {
                return -1;
            }

            (void)memcpy(&s_files[i].data[(uint32_t)offset], buf,
                         (size_t)size);
            s_files[i].size = (uint32_t)(offset + size);

            return (int64_t)size;
        }
    }

    return -1;
}

/**
 * @brief RAMFS mkdir
 */
static int32_t ramfs_mkdir(uint32_t mount_id, const char *path,
                            uint32_t mode)
{
    (void)mount_id;
    (void)path;
    (void)mode;

    return 0;
}

/**
 * @brief RAMFS unlink
 */
static int32_t ramfs_unlink(uint32_t mount_id, const char *path)
{
    uint32_t i;

    (void)mount_id;
    (void)path;

    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (s_files[i].in_use)
        {
            s_files[i].in_use = false;
            s_files[i].size = 0U;
            return 0;
        }
    }

    return -1;
}

/**
 * @brief RAMFS sync
 */
static int32_t ramfs_sync(uint32_t mount_id)
{
    (void)mount_id;

    return 0;
}

/* ========================================================================
 * RAMFS 操作接口
 * ======================================================================== */

/** @brief RAMFS 操作接口 */
static const fs_ops_t s_ramfs_ops =
{
    .mount   = ramfs_mount,
    .unmount = ramfs_unmount,
    .lookup  = ramfs_lookup,
    .create  = ramfs_create,
    .read    = ramfs_read,
    .write   = ramfs_write,
    .mkdir   = ramfs_mkdir,
    .unlink  = ramfs_unlink,
    .sync    = ramfs_sync
};

/**
 * @brief 获取 RAMFS 操作接口
 *
 * @return RAMFS 操作接口指针
 */
const fs_ops_t *ramfs_get_ops(void)
{
    return &s_ramfs_ops;
}
