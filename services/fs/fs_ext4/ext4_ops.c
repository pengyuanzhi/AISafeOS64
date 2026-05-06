/**
 * @file    ext4_ops.c
 * @brief   Ext4 文件系统 fs_ops 适配层
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details Ext4 文件系统 fs_ops 接口实现：
 *          - 适配 Ext4 内部接口到 fs_ops
 *          - 支持挂载/卸载/查找/创建/读取/写入/目录操作
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "ext4_ops.h"
#include "ext4_types.h"
#include "ext4_file.h"
#include "ext4_dir.h"
#include "ext4_inode.h"
#include "ext4_superblock.h"
#include "fs_ops.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大 Ext4 实例数 */
#define EXT4_MAX_INSTANCES     4U

/* ========================================================================
 * Ext4 实例管理
 * ======================================================================== */

/**
 * @brief Ext4 实例
 */
typedef struct
{
    ext4_instance_t inst;       /**< @brief Ext4 实例 */
    bool in_use;                /**< @brief 使用标记 */
    uint32_t mount_id;          /**< @brief 挂载点 ID */
} ext4_ctx_t;

/** @brief Ext4 实例表 */
static ext4_ctx_t s_instances[EXT4_MAX_INSTANCES];

/** @brief 初始化标志 */
static bool s_initialized;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 初始化 Ext4 适配层
 */
static void ext4_ops_init(void)
{
    uint32_t i;

    if (!s_initialized)
    {
        for (i = 0U; i < EXT4_MAX_INSTANCES; i++)
        {
            (void)memset(&s_instances[i], 0, sizeof(ext4_ctx_t));
        }

        s_initialized = true;
    }
}

/**
 * @brief 根据 mount_id 查找 Ext4 实例
 */
static ext4_ctx_t *find_instance(uint32_t mount_id)
{
    uint32_t i;

    for (i = 0U; i < EXT4_MAX_INSTANCES; i++)
    {
        if (s_instances[i].in_use && s_instances[i].mount_id == mount_id)
        {
            return &s_instances[i];
        }
    }

    return NULL;
}

/**
 * @brief 分配 Ext4 实例
 */
static ext4_ctx_t *alloc_instance(void)
{
    uint32_t i;

    for (i = 0U; i < EXT4_MAX_INSTANCES; i++)
    {
        if (!s_instances[i].in_use)
        {
            (void)memset(&s_instances[i], 0, sizeof(ext4_ctx_t));
            return &s_instances[i];
        }
    }

    return NULL;
}

/* ========================================================================
 * fs_ops 接口实现
 * ======================================================================== */

/**
 * @brief 挂载 Ext4 文件系统
 */
static int32_t ext4_mount(fs_mount_t *mnt, const char *device)
{
    ext4_ctx_t *ctx;
    int32_t ret;

    (void)device; /* Ext4 暂不使用设备参数 */

    ext4_ops_init();

    ctx = alloc_instance();
    if (ctx == NULL)
    {
        return -1;
    }

    /* 初始化 Ext4 实例 */
    ret = ext4_instance_init(&ctx->inst);
    if (ret < 0)
    {
        return -2;
    }

    ctx->mount_id = mnt->mount_id;
    ctx->in_use = true;

    mnt->private_data = &ctx->inst;

    return 0;
}

/**
 * @brief 卸载 Ext4 文件系统
 */
static int32_t ext4_unmount(fs_mount_t *mnt)
{
    ext4_ctx_t *ctx;

    if (mnt == NULL || mnt->private_data == NULL)
    {
        return -1;
    }

    ctx = (ext4_ctx_t *)mnt->private_data;

    /* 清理 Ext4 实例 */
    (void)ext4_instance_cleanup(&ctx->inst);

    ctx->in_use = false;

    return 0;
}

/**
 * @brief 查找文件
 */
static int32_t ext4_lookup(uint32_t mount_id, const char *path,
                           fs_inode_t *inode)
{
    ext4_ctx_t *ctx;
    ext4_inode_t ei;
    int32_t ret;

    if (path == NULL || inode == NULL)
    {
        return -1;
    }

    ctx = find_instance(mount_id);
    if (ctx == NULL)
    {
        return -1;
    }

    /* 查找 inode */
    ret = ext4_inode_lookup(&ctx->inst, path, &ei);
    if (ret < 0)
    {
        return -1;
    }

    /* 填充 inode */
    inode->ino = ei.inode_no;
    inode->size = ei.size;
    inode->mode = ei.mode;
    inode->nlinks = ei.links_count;
    inode->atime = ei.atime;
    inode->mtime = ei.mtime;
    inode->ctime = ei.ctime;

    /* 根据模式判断文件类型 */
    if ((ei.mode & EXT4_S_IFDIR) != 0U)
    {
        inode->type = FS_TYPE_DIRECTORY;
    }
    else if ((ei.mode & EXT4_S_IFREG) != 0U)
    {
        inode->type = FS_TYPE_REGULAR;
    }
    else if ((ei.mode & EXT4_S_IFLNK) != 0U)
    {
        inode->type = FS_TYPE_SYMLINK;
    }
    else
    {
        inode->type = FS_TYPE_DEVICE;
    }

    return 0;
}

/**
 * @brief 创建文件
 */
static int32_t ext4_create(uint32_t mount_id, const char *path,
                            uint32_t mode, fs_inode_t *inode)
{
    ext4_ctx_t *ctx;
    ext4_inode_t ei;
    int32_t ret;

    if (path == NULL)
    {
        return -1;
    }

    ctx = find_instance(mount_id);
    if (ctx == NULL)
    {
        return -1;
    }

    /* 创建文件 */
    ret = ext4_inode_create(&ctx->inst, path, mode | EXT4_S_IFREG);
    if (ret < 0)
    {
        return -1;
    }

    /* 查找新创建的文件 */
    if (inode != NULL)
    {
        ret = ext4_inode_lookup(&ctx->inst, path, &ei);
        if (ret < 0)
        {
            return -1;
        }

        inode->ino = ei.inode_no;
        inode->size = 0;
        inode->type = FS_TYPE_REGULAR;
        inode->mode = ei.mode;
        inode->nlinks = ei.links_count;
    }

    return 0;
}

/**
 * @brief 读取文件
 */
static int64_t ext4_read(uint32_t mount_id, uint32_t ino,
                          uint64_t offset, void *buf, uint64_t size)
{
    ext4_ctx_t *ctx;
    int64_t ret;

    if (buf == NULL)
    {
        return -1;
    }

    ctx = find_instance(mount_id);
    if (ctx == NULL)
    {
        return -1;
    }

    /* 读取文件数据 */
    ret = ext4_file_read(&ctx->inst, ino, offset, buf, size);

    return ret;
}

/**
 * @brief 写入文件
 */
static int64_t ext4_write(uint32_t mount_id, uint32_t ino,
                           uint64_t offset, const void *buf, uint64_t size)
{
    ext4_ctx_t *ctx;
    int64_t ret;

    if (buf == NULL)
    {
        return -1;
    }

    ctx = find_instance(mount_id);
    if (ctx == NULL)
    {
        return -1;
    }

    /* 写入文件数据 */
    ret = ext4_file_write(&ctx->inst, ino, offset, buf, size);

    return ret;
}

/**
 * @brief 创建目录
 */
static int32_t ext4_mkdir(uint32_t mount_id, const char *path, uint32_t mode)
{
    ext4_ctx_t *ctx;
    int32_t ret;

    if (path == NULL)
    {
        return -1;
    }

    ctx = find_instance(mount_id);
    if (ctx == NULL)
    {
        return -1;
    }

    /* 创建目录 */
    ret = ext4_inode_create(&ctx->inst, path, mode | EXT4_S_IFDIR);

    return ret;
}

/**
 * @brief 删除文件
 */
static int32_t ext4_unlink(uint32_t mount_id, const char *path)
{
    ext4_ctx_t *ctx;
    int32_t ret;

    if (path == NULL)
    {
        return -1;
    }

    ctx = find_instance(mount_id);
    if (ctx == NULL)
    {
        return -1;
    }

    /* 删除文件或目录 */
    ret = ext4_inode_delete(&ctx->inst, path);

    return ret;
}

/**
 * @brief 同步文件系统
 */
static int32_t ext4_sync(uint32_t mount_id)
{
    ext4_ctx_t *ctx;

    ctx = find_instance(mount_id);
    if (ctx == NULL)
    {
        return -1;
    }

    /* Ext4 日志同步 */
    /* TODO: 实现 Ext4 日志同步 */

    return 0;
}

/* ========================================================================
 * fs_ops 接口
 * ======================================================================== */

static const fs_ops_t s_ext4_ops =
{
    .mount   = ext4_mount,
    .unmount = ext4_unmount,
    .lookup  = ext4_lookup,
    .create  = ext4_create,
    .read    = ext4_read,
    .write   = ext4_write,
    .mkdir   = ext4_mkdir,
    .unlink  = ext4_unlink,
    .sync    = ext4_sync
};

/* ========================================================================
 * 公共接口
 * ======================================================================== */

/**
 * @brief 获取 Ext4 操作接口
 *
 * @return Ext4 操作接口指针
 */
const fs_ops_t *ext4_get_ops(void)
{
    return &s_ext4_ops;
}
