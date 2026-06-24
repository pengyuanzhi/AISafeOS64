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
 * Forward declarations（ext4 内部模块接口）
 * ======================================================================== */
int32_t ext4_instance_init(ext4_instance_t *inst);
int32_t ext4_instance_cleanup(ext4_instance_t *inst);
int32_t ext4_inode_lookup(ext4_instance_t *inst, const char *path,
                          uint32_t *ino, ext4_inode_t *inode);
int32_t ext4_inode_create(ext4_instance_t *inst, const char *path,
                          uint32_t mode);
int32_t ext4_inode_delete(ext4_instance_t *inst, const char *path);
int64_t ext4_file_read(ext4_instance_t *inst, uint32_t ino,
                        uint64_t offset, void *buf, uint64_t size);
int64_t ext4_file_write(ext4_instance_t *inst, uint32_t ino,
                         uint64_t offset, const void *buf, uint64_t size);

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
static int32_t ext4_fs_lookup(uint32_t mount_id, const char *path,
                              fs_inode_t *inode)
{
    ext4_ctx_t *ctx;
    ext4_inode_t ei;
    uint32_t ino_no;
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

    /* 查找 inode，ext4_inode_lookup 输出 inode 号和 inode 数据 */
    ret = ext4_inode_lookup(&ctx->inst, path, &ino_no, &ei);
    if (ret < 0)
    {
        return -1;
    }

    /* 填充 inode */
    inode->ino = ino_no;
    inode->size = (uint64_t)ei.i_size;
    inode->mode = (uint32_t)ei.i_mode;
    inode->nlinks = (uint32_t)ei.i_links_count;
    inode->atime = (uint64_t)ei.i_atime;
    inode->mtime = (uint64_t)ei.i_mtime;
    inode->ctime = (uint64_t)ei.i_ctime;

    /* 根据模式判断文件类型 */
    if (((uint32_t)ei.i_mode & EXT4_S_IFDIR) != 0U)
    {
        inode->type = FS_TYPE_DIRECTORY;
    }
    else if (((uint32_t)ei.i_mode & EXT4_S_IFREG) != 0U)
    {
        inode->type = FS_TYPE_REGULAR;
    }
    else if (((uint32_t)ei.i_mode & EXT4_S_IFLNK) != 0U)
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
static int32_t ext4_fs_create(uint32_t mount_id, const char *path,
                              uint32_t mode, fs_inode_t *inode)
{
    ext4_ctx_t *ctx;
    ext4_inode_t ei;
    uint32_t ino_no;
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
        ret = ext4_inode_lookup(&ctx->inst, path, &ino_no, &ei);
        if (ret < 0)
        {
            return -1;
        }

        inode->ino = ino_no;
        inode->size = 0U;
        inode->type = FS_TYPE_REGULAR;
        inode->mode = (uint32_t)ei.i_mode;
        inode->nlinks = (uint32_t)ei.i_links_count;
    }

    return 0;
}

/**
 * @brief 读取文件
 */
static int64_t ext4_fs_read(uint32_t mount_id, uint32_t ino,
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
static int64_t ext4_fs_write(uint32_t mount_id, uint32_t ino,
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
static int32_t ext4_fs_mkdir(uint32_t mount_id, const char *path, uint32_t mode)
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
static int32_t ext4_fs_unlink(uint32_t mount_id, const char *path)
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
    .lookup  = ext4_fs_lookup,
    .create  = ext4_fs_create,
    .read    = ext4_fs_read,
    .write   = ext4_fs_write,
    .mkdir   = ext4_fs_mkdir,
    .unlink  = ext4_fs_unlink,
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

/* ========================================================================
 * Stub 实现（待 ext4 内部模块完善后替换）
 *
 * 这些函数提供弱实现，使 fs.elf 能链接通过。
 * 后续将 ext4_inode.c / ext4_file.c 等纳入构建时自动覆盖。
 * ======================================================================== */

__attribute__((weak)) int32_t ext4_instance_init(ext4_instance_t *inst)
{
    (void)inst;
    return -1;
}

__attribute__((weak)) int32_t ext4_instance_cleanup(ext4_instance_t *inst)
{
    (void)inst;
    return 0;
}

__attribute__((weak)) int32_t ext4_inode_lookup(ext4_instance_t *inst,
    const char *path, uint32_t *ino, ext4_inode_t *inode)
{
    (void)inst; (void)path; (void)ino; (void)inode;
    return -1;
}

__attribute__((weak)) int32_t ext4_inode_create(ext4_instance_t *inst,
    const char *path, uint32_t mode)
{
    (void)inst; (void)path; (void)mode;
    return -1;
}

__attribute__((weak)) int32_t ext4_inode_delete(ext4_instance_t *inst,
    const char *path)
{
    (void)inst; (void)path;
    return -1;
}

__attribute__((weak)) int64_t ext4_file_read(ext4_instance_t *inst,
    uint32_t ino, uint64_t offset, void *buf, uint64_t size)
{
    (void)inst; (void)ino; (void)offset; (void)buf; (void)size;
    return -1;
}

__attribute__((weak)) int64_t ext4_file_write(ext4_instance_t *inst,
    uint32_t ino, uint64_t offset, const void *buf, uint64_t size)
{
    (void)inst; (void)ino; (void)offset; (void)buf; (void)size;
    return -1;
}
