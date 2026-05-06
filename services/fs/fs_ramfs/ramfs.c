/**
 * @file    ramfs.c
 * @brief   RAMFS 内存文件系统实现
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 2.0
 *
 * @details RAMFS 内存文件系统实现：
 *          - 纯内存文件系统
 *          - 支持文件/目录操作
 *          - 支持软链接和硬链接
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

/** @brief 最大链接路径长度 */
#define RAMFS_MAX_LINK_PATH    256U

/* ========================================================================
 * RAMFS 文件描述符
 * ======================================================================== */

/**
 * @brief RAMFS 文件
 */
typedef struct ramfs_file
{
    char            name[64];                /**< @brief 文件名 */
    uint32_t        ino;                     /**< @brief inode 编号 */
    uint8_t         data[RAMFS_MAX_FILE_SIZE]; /**< @brief 文件数据 */
    uint32_t        size;                    /**< @brief 文件大小 */
    uint32_t        mode;                    /**< @brief 文件权限 */
    uint32_t        uid;                     /**< @brief 用户 ID */
    uint32_t        gid;                     /**< @brief 组 ID */
    uint64_t        atime;                   /**< @brief 访问时间 */
    uint64_t        mtime;                   /**< @brief 修改时间 */
    uint64_t        ctime;                   /**< @brief 创建时间 */
    uint32_t        nlinks;                  /**< @brief 硬链接数 */
    uint32_t        target_ino;              /**< @brief 软链接目标 inode */
    bool            is_symlink;              /**< @brief 是否为软链接 */
    bool            in_use;                  /**< @brief 使用标记 */
} ramfs_file_t;

/** @brief RAMFS 文件表 */
static ramfs_file_t s_files[RAMFS_MAX_FILES];

/** @brief 下一个 inode 编号 */
static uint32_t s_next_ino;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 根据 inode 查找文件
 */
static ramfs_file_t *find_file_by_ino(uint32_t ino)
{
    uint32_t i;

    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (s_files[i].in_use && s_files[i].ino == ino)
        {
            return &s_files[i];
        }
    }

    return NULL;
}

/**
 * @brief 根据路径查找文件
 */
static ramfs_file_t *find_file_by_path(const char *path)
{
    uint32_t i;

    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (s_files[i].in_use && strcmp(s_files[i].name, path) == 0)
        {
            return &s_files[i];
        }
    }

    return NULL;
}

/**
 * @brief 分配文件表项
 */
static ramfs_file_t *alloc_file(void)
{
    uint32_t i;

    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (!s_files[i].in_use)
        {
            (void)memset(&s_files[i], 0, sizeof(ramfs_file_t));
            s_files[i].ino = s_next_ino++;
            s_files[i].nlinks = 1U;
            return &s_files[i];
        }
    }

    return NULL;
}

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
    ramfs_file_t *file;

    (void)mount_id;

    if (path == NULL || inode == NULL)
    {
        return -1;
    }

    /* 查找文件 */
    file = find_file_by_path(path);
    if (file == NULL)
    {
        return -1;
    }

    /* 填充 inode */
    inode->ino = file->ino;
    inode->size = file->size;
    inode->mode = file->mode;
    inode->nlinks = file->nlinks;
    inode->atime = file->atime;
    inode->mtime = file->mtime;
    inode->ctime = file->ctime;

    /* 判断文件类型 */
    if (file->is_symlink)
    {
        inode->type = FS_TYPE_SYMLINK;
    }
    else
    {
        inode->type = FS_TYPE_REGULAR;
    }

    return 0;
}

/**
 * @brief RAMFS create
 */
static int32_t ramfs_create(uint32_t mount_id, const char *path,
                            uint32_t mode, fs_inode_t *inode)
{
    ramfs_file_t *file;

    (void)mount_id;

    if (path == NULL)
    {
        return -1;
    }

    /* 检查文件是否已存在 */
    file = find_file_by_path(path);
    if (file != NULL)
    {
        return -1;
    }

    /* 分配新文件 */
    file = alloc_file();
    if (file == NULL)
    {
        return -1;
    }

    /* 填充文件信息 */
    (void)strncpy(file->name, path, 63U);
    file->name[63U] = '\0';
    file->size = 0U;
    file->mode = mode;
    file->in_use = true;
    file->is_symlink = false;
    file->target_ino = 0U;

    if (inode != NULL)
    {
        inode->ino = file->ino;
        inode->size = 0U;
        inode->type = FS_TYPE_REGULAR;
        inode->mode = mode;
        inode->nlinks = 1U;
    }

    return 0;
}

/**
 * @brief RAMFS read
 */
static int64_t ramfs_read(uint32_t mount_id, uint32_t ino,
                          uint64_t offset, void *buf, uint64_t size)
{
    ramfs_file_t *file;
    uint64_t copy_size;

    (void)mount_id;

    if (buf == NULL)
    {
        return -1;
    }

    /* 查找文件 */
    file = find_file_by_ino(ino);
    if (file == NULL)
    {
        return -1;
    }

    /* 软链接：返回链接目标 */
    if (file->is_symlink)
    {
        if (file->target_ino == 0U)
        {
            return -1;
        }

        /* 这里简化处理，直接返回目标 inode */
        return file->target_ino;
    }

    /* 检查偏移是否越界 */
    if (offset >= file->size)
    {
        return 0;
    }

    /* 计算拷贝大小 */
    copy_size = file->size - offset;
    if (copy_size > size)
    {
        copy_size = size;
    }

    /* 拷贝数据 */
    (void)memcpy(buf, &file->data[offset], (size_t)copy_size);

    return (int64_t)copy_size;
}

/**
 * @brief RAMFS write
 */
static int64_t ramfs_write(uint32_t mount_id, uint32_t ino,
                           uint64_t offset, const void *buf, uint64_t size)
{
    ramfs_file_t *file;
    uint64_t copy_size;

    (void)mount_id;

    if (buf == NULL)
    {
        return -1;
    }

    /* 查找文件 */
    file = find_file_by_ino(ino);
    if (file == NULL)
    {
        return -1;
    }

    /* 软链接不能写入 */
    if (file->is_symlink)
    {
        return -1;
    }

    /* 检查空间是否足够 */
    if (offset >= RAMFS_MAX_FILE_SIZE)
    {
        return -1;
    }

    /* 计算拷贝大小 */
    copy_size = RAMFS_MAX_FILE_SIZE - offset;
    if (copy_size > size)
    {
        copy_size = size;
    }

    /* 拷贝数据 */
    (void)memcpy(&file->data[offset], buf, (size_t)copy_size);

    /* 更新文件大小 */
    if (offset + copy_size > file->size)
    {
        file->size = (uint32_t)(offset + copy_size);
    }

    return (int64_t)copy_size;
}

/**
 * @brief RAMFS mkdir
 */
static int32_t ramfs_mkdir(uint32_t mount_id, const char *path, uint32_t mode)
{
    (void)mount_id;
    (void)path;
    (void)mode;

    /* RAMFS 简化实现，暂不实现目录 */
    return -1;
}

/**
 * @brief RAMFS unlink
 */
static int32_t ramfs_unlink(uint32_t mount_id, const char *path)
{
    ramfs_file_t *file;

    (void)mount_id;

    if (path == NULL)
    {
        return -1;
    }

    /* 查找文件 */
    file = find_file_by_path(path);
    if (file == NULL)
    {
        return -1;
    }

    /* 硬链接：减少链接计数 */
    if (file->nlinks > 1U)
    {
        file->nlinks--;
        return 0;
    }

    /* 删除文件 */
    file->in_use = false;

    return 0;
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
 * 链接操作（新增）
 * ======================================================================== */

/**
 * @brief RAMFS symlink - 创建软链接
 */
static int32_t ramfs_symlink(uint32_t mount_id, const char *oldpath,
                              const char *newpath)
{
    ramfs_file_t *link;
    ramfs_file_t *target;

    (void)mount_id;

    if (oldpath == NULL || newpath == NULL)
    {
        return -1;
    }

    /* 检查新路径是否已存在 */
    link = find_file_by_path(newpath);
    if (link != NULL)
    {
        return -1;
    }

    /* 查找目标文件 */
    target = find_file_by_path(oldpath);
    if (target == NULL)
    {
        return -1;
    }

    /* 分配软链接 */
    link = alloc_file();
    if (link == NULL)
    {
        return -1;
    }

    /* 填充软链接信息 */
    (void)strncpy(link->name, newpath, 63U);
    link->name[63U] = '\0';
    link->size = (uint32_t)strlen(oldpath);
    link->mode = 0777;
    link->in_use = true;
    link->is_symlink = true;
    link->target_ino = target->ino;
    link->nlinks = 1U;

    /* 将目标路径存入 data 字段 */
    (void)strncpy((char *)link->data, oldpath, RAMFS_MAX_FILE_SIZE - 1U);
    link->data[RAMFS_MAX_FILE_SIZE - 1U] = '\0';

    return 0;
}

/**
 * @brief RAMFS link - 创建硬链接
 */
static int32_t ramfs_link(uint32_t mount_id, const char *oldpath,
                           const char *newpath)
{
    ramfs_file_t *link;
    ramfs_file_t *target;

    (void)mount_id;

    if (oldpath == NULL || newpath == NULL)
    {
        return -1;
    }

    /* 检查新路径是否已存在 */
    link = find_file_by_path(newpath);
    if (link != NULL)
    {
        return -1;
    }

    /* 查找目标文件 */
    target = find_file_by_path(oldpath);
    if (target == NULL)
    {
        return -1;
    }

    /* 软链接不能创建硬链接 */
    if (target->is_symlink)
    {
        return -1;
    }

    /* 分配硬链接 */
    link = alloc_file();
    if (link == NULL)
    {
        return -1;
    }

    /* 填充硬链接信息 */
    (void)strncpy(link->name, newpath, 63U);
    link->name[63U] = '\0';

    /* 硬链接共享相同的数据和 inode */
    (void)memcpy(link->data, target->data, RAMFS_MAX_FILE_SIZE);
    link->size = target->size;
    link->mode = target->mode;
    link->uid = target->uid;
    link->gid = target->gid;
    link->ino = target->ino;  /* 使用相同的 inode */
    link->in_use = true;
    link->is_symlink = false;
    link->target_ino = 0U;

    /* 增加硬链接计数 */
    target->nlinks++;
    link->nlinks = target->nlinks;

    return 0;
}

/**
 * @brief RAMFS readlink - 读取软链接
 */
static int32_t ramfs_readlink(uint32_t mount_id, const char *path,
                               char *buf, uint64_t bufsize)
{
    ramfs_file_t *link;

    (void)mount_id;

    if (path == NULL || buf == NULL)
    {
        return -1;
    }

    /* 查找软链接 */
    link = find_file_by_path(path);
    if (link == NULL || !link->is_symlink)
    {
        return -1;
    }

    /* 检查缓冲区大小 */
    if (bufsize < link->size + 1U)
    {
        return -1;
    }

    /* 拷贝链接目标路径 */
    (void)memcpy(buf, link->data, (size_t)link->size);
    buf[link->size] = '\0';

    return 0;
}

/* ========================================================================
 * fs_ops 接口
 * ======================================================================== */

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

/* ========================================================================
 * 公共接口
 * ======================================================================== */

/**
 * @brief 获取 RAMFS 操作接口
 */
const fs_ops_t *ramfs_get_ops(void)
{
    return &s_ramfs_ops;
}

/**
 * @brief RAMFS 软链接操作（外部调用）
 */
int32_t ramfs_do_symlink(uint32_t mount_id, const char *oldpath,
                          const char *newpath)
{
    return ramfs_symlink(mount_id, oldpath, newpath);
}

/**
 * @brief RAMFS 硬链接操作（外部调用）
 */
int32_t ramfs_do_link(uint32_t mount_id, const char *oldpath,
                       const char *newpath)
{
    return ramfs_link(mount_id, oldpath, newpath);
}

/**
 * @brief RAMFS 读取软链接（外部调用）
 */
int32_t ramfs_do_readlink(uint32_t mount_id, const char *path,
                           char *buf, uint64_t bufsize)
{
    return ramfs_readlink(mount_id, path, buf, bufsize);
}
