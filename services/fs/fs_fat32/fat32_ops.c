/**
 * @file    fat32_ops.c
 * @brief   FAT32 文件系统 fs_ops 适配层
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details FAT32 文件系统 fs_ops 接口实现：
 *          - 适配 FAT32 内部接口到 fs_ops
 *          - 支持挂载/卸载/查找/创建/读取/写入/目录操作
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fat32_ops.h"
#include "fat32_types.h"
#include "fat32_file.h"
#include "fat32_dir.h"
#include "fat32_path.h"
#include "fs_ops.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大 FAT32 实例数 */
#define FAT32_MAX_INSTANCES     4U

/* ========================================================================
 * FAT32 实例管理
 * ======================================================================== */

/**
 * @brief FAT32 实例
 */
typedef struct
{
    fat32_instance_t inst;       /**< @brief FAT32 实例 */
    bool in_use;                /**< @brief 使用标记 */
    uint32_t mount_id;           /**< @brief 挂载点 ID */
} fat32_ctx_t;

/** @brief FAT32 实例表 */
static fat32_ctx_t s_instances[FAT32_MAX_INSTANCES];

/** @brief 初始化标志 */
static bool s_initialized;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 初始化 FAT32 适配层
 */
static void fat32_ops_init(void)
{
    uint32_t i;

    if (!s_initialized)
    {
        for (i = 0U; i < FAT32_MAX_INSTANCES; i++)
        {
            (void)memset(&s_instances[i], 0, sizeof(fat32_ctx_t));
        }

        s_initialized = true;
    }
}

/**
 * @brief 根据 mount_id 查找 FAT32 实例
 */
static fat32_ctx_t *find_instance(uint32_t mount_id)
{
    uint32_t i;

    for (i = 0U; i < FAT32_MAX_INSTANCES; i++)
    {
        if (s_instances[i].in_use && s_instances[i].mount_id == mount_id)
        {
            return &s_instances[i];
        }
    }

    return NULL;
}

/**
 * @brief 分配 FAT32 实例
 */
static fat32_ctx_t *alloc_instance(void)
{
    uint32_t i;

    for (i = 0U; i < FAT32_MAX_INSTANCES; i++)
    {
        if (!s_instances[i].in_use)
        {
            (void)memset(&s_instances[i], 0, sizeof(fat32_ctx_t));
            return &s_instances[i];
        }
    }

    return NULL;
}

/* ========================================================================
 * fs_ops 接口实现
 * ======================================================================== */

/**
 * @brief 挂载 FAT32 文件系统
 */
static int32_t fat32_mount(fs_mount_t *mnt, const char *device)
{
    fat32_ctx_t *ctx;
    int32_t ret;

    (void)device; /* FAT32 暂不使用设备参数 */

    fat32_ops_init();

    ctx = alloc_instance();
    if (ctx == NULL)
    {
        return -1;
    }

    /* 初始化 FAT32 实例 */
    ret = fat32_instance_init(&ctx->inst);
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
 * @brief 卸载 FAT32 文件系统
 */
static int32_t fat32_unmount(fs_mount_t *mnt)
{
    fat32_ctx_t *ctx;

    if (mnt == NULL || mnt->private_data == NULL)
    {
        return -1;
    }

    ctx = (fat32_ctx_t *)mnt->private_data;

    /* 清理 FAT32 实例 */
    (void)fat32_instance_cleanup(&ctx->inst);

    ctx->in_use = false;

    return 0;
}

/**
 * @brief 查找文件
 */
static int32_t fat32_lookup(uint32_t mount_id, const char *path,
                            fs_inode_t *inode)
{
    fat32_ctx_t *ctx;
    fat32_dir_entry_t entry;
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

    /* 查找目录项 */
    ret = fat32_dir_find_entry(&ctx->inst, path, &entry);
    if (ret < 0)
    {
        return -1;
    }

    /* 填充 inode */
    inode->ino = (uint32_t)(((uint32_t)entry.fst_clus_hi << 16) | entry.fst_clus_lo);
    inode->size = entry.file_size;

    /* 根据属性判断文件类型 */
    if ((entry.attr & FAT32_ATTR_DIRECTORY) != 0U)
    {
        inode->type = FS_TYPE_DIRECTORY;
    }
    else
    {
        inode->type = FS_TYPE_REGULAR;
    }

    inode->mode = 0;
    inode->nlinks = 1U;
    inode->atime = 0;
    inode->mtime = 0;
    inode->ctime = 0;

    return 0;
}

/**
 * @brief 创建文件
 */
static int32_t fat32_create(uint32_t mount_id, const char *path,
                             uint32_t mode, fs_inode_t *inode)
{
    fat32_ctx_t *ctx;
    fat32_dir_entry_t entry;
    int32_t ret;

    (void)mode; /* FAT32 暂不使用模式参数 */

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
    ret = fat32_dir_create_file(&ctx->inst, path);
    if (ret < 0)
    {
        return -1;
    }

    /* 查找新创建的文件 */
    ret = fat32_dir_find_entry(&ctx->inst, path, &entry);
    if (ret < 0)
    {
        return -1;
    }

    if (inode != NULL)
    {
        inode->ino = (uint32_t)(((uint32_t)entry.fst_clus_hi << 16) | entry.fst_clus_lo);
        inode->size = 0;
        inode->type = FS_TYPE_REGULAR;
        inode->mode = 0;
        inode->nlinks = 1U;
    }

    return 0;
}

/**
 * @brief 读取文件
 */
static int64_t fat32_fs_read(uint32_t mount_id, uint32_t ino,
                           uint64_t offset, void *buf, uint64_t size)
{
    fat32_ctx_t *ctx;
    int32_t fd;
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

    /* 使用 cluster 作为文件描述符（简化版本） */
    fd = (int32_t)ino;

    /* 定位到指定偏移 */
    ret = fat32_file_lseek(&ctx->inst, fd, offset);
    if (ret < 0)
    {
        return -1;
    }

    /* 读取数据 */
    ret = fat32_read(&ctx->inst, (uint32_t)fd, buf, size);

    return ret;
}

/**
 * @brief 写入文件
 */
static int64_t fat32_fs_write(uint32_t mount_id, uint32_t ino,
                            uint64_t offset, const void *buf, uint64_t size)
{
    fat32_ctx_t *ctx;
    int32_t fd;
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

    /* 使用 cluster 作为文件描述符（简化版本） */
    fd = (int32_t)ino;

    /* 定位到指定偏移 */
    ret = fat32_file_lseek(&ctx->inst, fd, offset);
    if (ret < 0)
    {
        return -1;
    }

    /* 写入数据 */
    ret = fat32_write(&ctx->inst, (uint32_t)fd, buf, size);

    return ret;
}

/**
 * @brief 创建目录
 */
static int32_t fat32_mkdir(uint32_t mount_id, const char *path, uint32_t mode)
{
    fat32_ctx_t *ctx;
    int32_t ret;

    (void)mode; /* FAT32 暂不使用模式参数 */

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
    ret = fat32_dir_create_dir(&ctx->inst, path);

    return ret;
}

/**
 * @brief 删除文件
 */
static int32_t fat32_unlink(uint32_t mount_id, const char *path)
{
    fat32_ctx_t *ctx;
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
    ret = fat32_dir_delete_entry(&ctx->inst, path);

    return ret;
}

/**
 * @brief 同步文件系统
 */
static int32_t fat32_sync(uint32_t mount_id)
{
    fat32_ctx_t *ctx;

    ctx = find_instance(mount_id);
    if (ctx == NULL)
    {
        return -1;
    }

    /* FAT32 缓存同步 */
    /* TODO: 实现 FAT32 缓存同步 */

    return 0;
}

/* ========================================================================
 * fs_ops 接口
 * ======================================================================== */

static const fs_ops_t s_fat32_ops =
{
    .mount   = fat32_mount,
    .unmount = fat32_unmount,
    .lookup  = fat32_lookup,
    .create  = fat32_create,
    .read    = fat32_read,
    .write   = fat32_write,
    .mkdir   = fat32_mkdir,
    .unlink  = fat32_unlink,
    .sync    = fat32_sync
};

/* ========================================================================
 * 公共接口
 * ======================================================================== */

/**
 * @brief 获取 FAT32 操作接口
 *
 * @return FAT32 操作接口指针
 */
const fs_ops_t *fat32_get_ops(void)
{
    return &s_fat32_ops;
}
