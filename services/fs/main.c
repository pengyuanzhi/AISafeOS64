/**
 * @file    main.c
 * @brief   虚拟文件系统（VFS）服务实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details VFS 核心服务实现：
 *          - 文件描述符表管理（每进程）
 *          - 挂载点管理
 *          - 路径解析与 inode 查找
 *          - 文件系统操作分发
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: FS-001~005
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/vfs.h>
#include <kernel/config.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * VFS 全局状态
 * ======================================================================== */

/** @brief 挂载点表 */
static vfs_mount_t s_mounts[VFS_MAX_MOUNTS];

/** @brief 挂载点使用标记 */
static bool s_mount_used[VFS_MAX_MOUNTS];

/** @brief 文件描述符表（简化：全局单进程） */
static vfs_fd_t s_fd_table[VFS_MAX_FDS];

/** @brief inode 缓存（简化：全局静态数组） */
#define VFS_INODE_CACHE_SIZE    128U
static vfs_inode_t s_inode_cache[VFS_INODE_CACHE_SIZE];
static bool s_inode_used[VFS_INODE_CACHE_SIZE];

/** @brief 文件系统操作表 */
static const vfs_ops_t *s_fs_ops[VFS_FS_DEVFS + 1U];

/** @brief 下一个 inode 编号 */
static uint32_t s_next_ino;

/** @brief 初始化标志 */
static bool s_initialized;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 分配 inode
 *
 * @return inode 编号，0 表示失败
 */
static uint32_t vfs_alloc_inode(void)
{
    uint32_t i;
    uint32_t ino;

    for (i = 0U; i < VFS_INODE_CACHE_SIZE; i++)
    {
        if (!s_inode_used[i])
        {
            ino = s_next_ino;
            s_next_ino++;
            s_inode_cache[i].ino = ino;
            s_inode_cache[i].ref_count = 1U;
            s_inode_cache[i].dirty = false;
            s_inode_used[i] = true;
            return ino;
        }
    }

    return 0U;
}

/**
 * @brief 查找 inode 缓存
 *
 * @param ino inode 编号
 *
 * @return inode 指针，NULL 表示未找到
 */
static vfs_inode_t *vfs_find_inode(uint32_t ino)
{
    uint32_t i;

    for (i = 0U; i < VFS_INODE_CACHE_SIZE; i++)
    {
        if (s_inode_used[i] && (s_inode_cache[i].ino == ino))
        {
            return &s_inode_cache[i];
        }
    }

    return NULL;
}

/**
 * @brief 释放 inode
 *
 * @param ino inode 编号
 */
static void vfs_release_inode(uint32_t ino)
{
    uint32_t i;

    for (i = 0U; i < VFS_INODE_CACHE_SIZE; i++)
    {
        if (s_inode_used[i] && (s_inode_cache[i].ino == ino))
        {
            s_inode_cache[i].ref_count--;
            if (s_inode_cache[i].ref_count == 0U)
            {
                s_inode_used[i] = false;
            }
            break;
        }
    }
}

/**
 * @brief 分配文件描述符
 *
 * @return 文件描述符索引，负数表示失败
 */
static int32_t vfs_alloc_fd(void)
{
    uint32_t i;

    for (i = 0U; i < VFS_MAX_FDS; i++)
    {
        if (!s_fd_table[i].in_use)
        {
            s_fd_table[i].fd = i;
            s_fd_table[i].in_use = true;
            s_fd_table[i].offset = 0U;
            return (int32_t)i;
        }
    }

    return -(int32_t)24; /* -EMFILE */
}

/**
 * @brief 通过路径查找挂载点
 *
 * @param path 文件路径
 *
 * @return 挂载点指针，NULL 表示未找到
 */
static vfs_mount_t *vfs_find_mount(const char *path)
{
    uint32_t i;
    uint32_t best_len = 0U;
    vfs_mount_t *best = NULL;

    for (i = 0U; i < VFS_MAX_MOUNTS; i++)
    {
        if (s_mount_used[i] && s_mounts[i].mounted)
        {
            uint32_t j;
            uint32_t len = 0U;

            /* 计算路径前缀匹配长度 */
            for (j = 0U; j < VFS_PATH_MAX; j++)
            {
                if (s_mounts[i].path[j] == '\0')
                {
                    break;
                }
                if (s_mounts[i].path[j] != path[j])
                {
                    len = 0U;
                    break;
                }
                len++;
            }

            /* 选择最长匹配 */
            if ((len > best_len) && (len > 0U))
            {
                best_len = len;
                best = &s_mounts[i];
            }
        }
    }

    return best;
}

/* ========================================================================
 * 初始化
 * ======================================================================== */

kernel_status_t vfs_init(void)
{
    uint32_t i;

    (void)memset(s_mounts, 0, sizeof(s_mounts));
    (void)memset(s_mount_used, 0, sizeof(s_mount_used));
    (void)memset(s_fd_table, 0, sizeof(s_fd_table));
    (void)memset(s_inode_cache, 0, sizeof(s_inode_cache));
    (void)memset(s_inode_used, 0, sizeof(s_inode_used));
    (void)memset(s_fs_ops, 0, sizeof(s_fs_ops));

    for (i = 0U; i < VFS_MAX_FDS; i++)
    {
        s_fd_table[i].in_use = false;
    }

    s_next_ino = 1U; /* 0 为无效 */
    s_initialized = true;

    return KERNEL_OK;
}

/* ========================================================================
 * 注册文件系统
 * ======================================================================== */

kernel_status_t vfs_register_fs(vfs_fstype_t fstype, const vfs_ops_t *ops)
{
    if (ops == NULL)
    {
        return -(int32_t)22;
    }

    if ((uint32_t)fstype > (uint32_t)VFS_FS_DEVFS)
    {
        return -(int32_t)22;
    }

    s_fs_ops[(uint32_t)fstype] = ops;

    return KERNEL_OK;
}

/* ========================================================================
 * 挂载/卸载
 * ======================================================================== */

int32_t vfs_mount(const char *path, vfs_fstype_t fstype,
                   const char *device, uint32_t flags)
{
    uint32_t i;
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;
    int32_t ret;

    if (!s_initialized)
    {
        return -(int32_t)22;
    }

    if (path == NULL)
    {
        return -(int32_t)22;
    }

    if ((uint32_t)fstype > (uint32_t)VFS_FS_DEVFS)
    {
        return -(int32_t)22;
    }

    ops = s_fs_ops[(uint32_t)fstype];
    if (ops == NULL)
    {
        return -(int32_t)38; /* -ENOSYS */
    }

    /* 查找空闲挂载槽 */
    for (i = 0U; i < VFS_MAX_MOUNTS; i++)
    {
        if (!s_mount_used[i])
        {
            break;
        }
    }

    if (i >= VFS_MAX_MOUNTS)
    {
        return -(int32_t)12;
    }

    mnt = &s_mounts[i];

    (void)memset(mnt, 0, sizeof(vfs_mount_t));
    mnt->mount_id = i;
    mnt->fstype = fstype;
    mnt->flags = flags;

    /* 复制挂载路径 */
    uint32_t j;
    for (j = 0U; (j < (VFS_PATH_MAX - 1U)) && (path[j] != '\0'); j++)
    {
        mnt->path[j] = path[j];
    }
    mnt->path[j] = '\0';

    /* 调用文件系统挂载操作 */
    if (ops->mount != NULL)
    {
        ret = ops->mount(mnt, device);
        if (ret != 0)
        {
            return ret;
        }
    }

    mnt->mounted = true;
    s_mount_used[i] = true;

    return (int32_t)i;
}

kernel_status_t vfs_unmount(uint32_t mount_id)
{
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;

    if (mount_id >= VFS_MAX_MOUNTS)
    {
        return -(int32_t)22;
    }

    if (!s_mount_used[mount_id])
    {
        return -(int32_t)2;
    }

    mnt = &s_mounts[mount_id];

    /* 检查是否有打开的文件 */
    uint32_t i;
    for (i = 0U; i < VFS_MAX_FDS; i++)
    {
        if (s_fd_table[i].in_use && (s_fd_table[i].mount_id == mount_id))
        {
            return -(int32_t)16; /* -EBUSY */
        }
    }

    /* 调用文件系统卸载操作 */
    ops = s_fs_ops[(uint32_t)mnt->fstype];
    if ((ops != NULL) && (ops->unmount != NULL))
    {
        (void)ops->unmount(mnt);
    }

    mnt->mounted = false;
    s_mount_used[mount_id] = false;

    return KERNEL_OK;
}

/* ========================================================================
 * 文件操作
 * ======================================================================== */

int32_t vfs_open(const char *path, uint32_t flags, uint32_t mode)
{
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;
    vfs_inode_t inode;
    int32_t fd;
    int32_t ret;

    if (path == NULL)
    {
        return -(int32_t)22;
    }

    /* 查找挂载点 */
    mnt = vfs_find_mount(path);
    if (mnt == NULL)
    {
        return -(int32_t)2;
    }

    ops = s_fs_ops[(uint32_t)mnt->fstype];
    if (ops == NULL)
    {
        return -(int32_t)38;
    }

    /* 分配文件描述符 */
    fd = vfs_alloc_fd();
    if (fd < 0)
    {
        return fd;
    }

    /* 查找或创建 inode */
    if ((flags & (uint32_t)VFS_O_CREAT) != 0U)
    {
        /* 尝试创建 */
        if (ops->create != NULL)
        {
            ret = ops->create(mnt->mount_id, path, mode, &inode);
            if (ret < 0)
            {
                /* 创建失败，尝试查找已有文件 */
                if (ops->lookup == NULL)
                {
                    s_fd_table[(uint32_t)fd].in_use = false;
                    return ret;
                }
                ret = ops->lookup(mnt->mount_id, path, &inode);
                if (ret < 0)
                {
                    s_fd_table[(uint32_t)fd].in_use = false;
                    return ret;
                }
            }
        }
    }
    else
    {
        /* 查找已有文件 */
        if (ops->lookup == NULL)
        {
            s_fd_table[(uint32_t)fd].in_use = false;
            return -(int32_t)38;
        }

        ret = ops->lookup(mnt->mount_id, path, &inode);
        if (ret < 0)
        {
            s_fd_table[(uint32_t)fd].in_use = false;
            return ret;
        }
    }

    /* 填充文件描述符 */
    s_fd_table[(uint32_t)fd].ino = inode.ino;
    s_fd_table[(uint32_t)fd].flags = flags;
    s_fd_table[(uint32_t)fd].offset = 0U;
    s_fd_table[(uint32_t)fd].mount_id = mnt->mount_id;

    return fd;
}

kernel_status_t vfs_close(uint32_t fd)
{
    if (fd >= VFS_MAX_FDS)
    {
        return -(int32_t)9; /* -EBADF */
    }

    if (!s_fd_table[fd].in_use)
    {
        return -(int32_t)9;
    }

    vfs_release_inode(s_fd_table[fd].ino);

    s_fd_table[fd].in_use = false;
    s_fd_table[fd].ino = 0U;
    s_fd_table[fd].offset = 0U;

    return KERNEL_OK;
}

int64_t vfs_read(uint32_t fd, void *buf, uint64_t size)
{
    vfs_fd_t *f;
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;
    int64_t bytes;

    if (fd >= VFS_MAX_FDS)
    {
        return -(int64_t)9;
    }

    f = &s_fd_table[fd];

    if (!f->in_use)
    {
        return -(int64_t)9;
    }

    if ((f->flags & (uint32_t)VFS_O_WRONLY) == (uint32_t)VFS_O_WRONLY)
    {
        return -(int64_t)22; /* 只写文件不可读 */
    }

    if (buf == NULL)
    {
        return -(int64_t)22;
    }

    if (f->mount_id >= VFS_MAX_MOUNTS)
    {
        return -(int64_t)22;
    }

    mnt = &s_mounts[f->mount_id];
    ops = s_fs_ops[(uint32_t)mnt->fstype];

    if ((ops == NULL) || (ops->read == NULL))
    {
        return -(int64_t)38;
    }

    bytes = ops->read(f->mount_id, f->ino, f->offset, buf, size);

    if (bytes > 0)
    {
        f->offset += (uint64_t)bytes;
    }

    return bytes;
}

int64_t vfs_write(uint32_t fd, const void *buf, uint64_t size)
{
    vfs_fd_t *f;
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;
    int64_t bytes;

    if (fd >= VFS_MAX_FDS)
    {
        return -(int64_t)9;
    }

    f = &s_fd_table[fd];

    if (!f->in_use)
    {
        return -(int64_t)9;
    }

    if ((f->flags & (uint32_t)VFS_O_RDONLY) == (uint32_t)VFS_O_RDONLY)
    {
        return -(int64_t)22; /* 只读文件不可写 */
    }

    if (buf == NULL)
    {
        return -(int64_t)22;
    }

    if (f->mount_id >= VFS_MAX_MOUNTS)
    {
        return -(int64_t)22;
    }

    mnt = &s_mounts[f->mount_id];
    ops = s_fs_ops[(uint32_t)mnt->fstype];

    if ((ops == NULL) || (ops->write == NULL))
    {
        return -(int64_t)38;
    }

    /* 追加模式 */
    if ((f->flags & (uint32_t)VFS_O_APPEND) != 0U)
    {
        vfs_inode_t *inode = vfs_find_inode(f->ino);
        if (inode != NULL)
        {
            f->offset = inode->size;
        }
    }

    bytes = ops->write(f->mount_id, f->ino, f->offset, buf, size);

    if (bytes > 0)
    {
        f->offset += (uint64_t)bytes;
    }

    return bytes;
}

int64_t vfs_seek(uint32_t fd, int64_t offset, uint32_t whence)
{
    vfs_fd_t *f;
    vfs_inode_t *inode;
    int64_t new_offset;

    if (fd >= VFS_MAX_FDS)
    {
        return -(int64_t)9;
    }

    f = &s_fd_table[fd];

    if (!f->in_use)
    {
        return -(int64_t)9;
    }

    inode = vfs_find_inode(f->ino);
    if (inode == NULL)
    {
        return -(int64_t)22;
    }

    switch (whence)
    {
        case 0U: /* SEEK_SET */
            new_offset = offset;
            break;

        case 1U: /* SEEK_CUR */
            new_offset = (int64_t)f->offset + offset;
            break;

        case 2U: /* SEEK_END */
            new_offset = (int64_t)inode->size + offset;
            break;

        default:
            return -(int64_t)22;
    }

    if (new_offset < 0)
    {
        return -(int64_t)22;
    }

    f->offset = (uint64_t)new_offset;

    return new_offset;
}

/* ========================================================================
 * 目录操作
 * ======================================================================== */

kernel_status_t vfs_stat(const char *path, vfs_inode_t *stat)
{
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;
    int32_t ret;

    if ((path == NULL) || (stat == NULL))
    {
        return -(int32_t)22;
    }

    mnt = vfs_find_mount(path);
    if (mnt == NULL)
    {
        return -(int32_t)2;
    }

    ops = s_fs_ops[(uint32_t)mnt->fstype];
    if ((ops == NULL) || (ops->lookup == NULL))
    {
        return -(int32_t)38;
    }

    ret = ops->lookup(mnt->mount_id, path, stat);

    return (ret < 0) ? (kernel_status_t)ret : KERNEL_OK;
}

kernel_status_t vfs_mkdir(const char *path, uint32_t mode)
{
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;

    if (path == NULL)
    {
        return -(int32_t)22;
    }

    mnt = vfs_find_mount(path);
    if (mnt == NULL)
    {
        return -(int32_t)2;
    }

    ops = s_fs_ops[(uint32_t)mnt->fstype];
    if ((ops == NULL) || (ops->mkdir == NULL))
    {
        return -(int32_t)38;
    }

    return (kernel_status_t)ops->mkdir(mnt->mount_id, path, mode);
}

kernel_status_t vfs_unlink(const char *path)
{
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;

    if (path == NULL)
    {
        return -(int32_t)22;
    }

    mnt = vfs_find_mount(path);
    if (mnt == NULL)
    {
        return -(int32_t)2;
    }

    ops = s_fs_ops[(uint32_t)mnt->fstype];
    if ((ops == NULL) || (ops->unlink == NULL))
    {
        return -(int32_t)38;
    }

    return (kernel_status_t)ops->unlink(mnt->mount_id, path);
}

kernel_status_t vfs_sync(int32_t mount_id)
{
    uint32_t i;

    if (mount_id < 0)
    {
        /* 同步所有挂载点 */
        for (i = 0U; i < VFS_MAX_MOUNTS; i++)
        {
            if (s_mount_used[i] && s_mounts[i].mounted)
            {
                const vfs_ops_t *ops = s_fs_ops[(uint32_t)s_mounts[i].fstype];
                if ((ops != NULL) && (ops->sync != NULL))
                {
                    (void)ops->sync(i);
                }
            }
        }
    }
    else
    {
        if ((uint32_t)mount_id >= VFS_MAX_MOUNTS)
        {
            return -(int32_t)22;
        }

        if (!s_mount_used[(uint32_t)mount_id])
        {
            return -(int32_t)2;
        }

        const vfs_ops_t *ops = s_fs_ops[(uint32_t)s_mounts[(uint32_t)mount_id].fstype];
        if ((ops != NULL) && (ops->sync != NULL))
        {
            (void)ops->sync((uint32_t)mount_id);
        }
    }

    return KERNEL_OK;
}
