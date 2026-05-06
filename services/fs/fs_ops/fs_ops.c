/**
 * @file    fs_ops.c
 * @brief   文件系统抽象层管理实现
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 2.0
 *
 * @details 文件系统抽象层管理实现：
 *          - 文件系统注册管理
 *          - 挂载点管理
 *          - 文件系统操作分发
 *          - 文件锁管理
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

/** @brief 最大挂载点数量 */
#define FS_MAX_MOUNTS         8U

/** @brief 最大文件锁数量 */
#define FS_MAX_LOCKS          128U

/* ========================================================================
 * 文件系统操作表
 * ======================================================================== */

/** @brief 文件系统操作表 */
static const fs_ops_t *s_fs_ops[(uint32_t)FS_FSTYPE_DEVFS + 1U];

/** @brief 挂载点表 */
static fs_mount_t s_mounts[FS_MAX_MOUNTS];

/** @brief 挂载点使用标记 */
static bool s_mount_used[FS_MAX_MOUNTS];

/** @brief 下一个挂载点 ID */
static uint32_t s_next_mount_id;

/** @brief 初始化标志 */
static bool s_initialized;

/* ========================================================================
 * 文件锁管理
 * ======================================================================== */

/**
 * @brief 文件锁表项
 */
typedef struct
{
    uint32_t        mount_id;       /**< @brief 挂载点 ID */
    uint32_t        ino;            /**< @brief inode 编号 */
    fs_file_lock_t  lock;           /**< @brief 锁状态 */
    bool            in_use;         /**< @brief 使用标记 */
} fs_lock_entry_t;

/** @brief 文件锁表 */
static fs_lock_entry_t s_locks[FS_MAX_LOCKS];

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 初始化文件系统抽象层
 */
static void fs_ops_init(void)
{
    uint32_t i;

    if (!s_initialized)
    {
        for (i = 0U; i < (uint32_t)FS_FSTYPE_DEVFS + 1U; i++)
        {
            s_fs_ops[i] = NULL;
        }

        for (i = 0U; i < FS_MAX_MOUNTS; i++)
        {
            (void)memset(&s_mounts[i], 0, sizeof(fs_mount_t));
            s_mount_used[i] = false;
        }

        for (i = 0U; i < FS_MAX_LOCKS; i++)
        {
            (void)memset(&s_locks[i], 0, sizeof(fs_lock_entry_t));
        }

        s_next_mount_id = 0U;
        s_initialized = true;
    }
}

/**
 * @brief 查找文件锁
 */
static fs_lock_entry_t *find_lock(uint32_t mount_id, uint32_t ino)
{
    uint32_t i;

    for (i = 0U; i < FS_MAX_LOCKS; i++)
    {
        if (s_locks[i].in_use &&
            s_locks[i].mount_id == mount_id &&
            s_locks[i].ino == ino)
        {
            return &s_locks[i];
        }
    }

    return NULL;
}

/**
 * @brief 分配文件锁表项
 */
static fs_lock_entry_t *alloc_lock(void)
{
    uint32_t i;

    for (i = 0U; i < FS_MAX_LOCKS; i++)
    {
        if (!s_locks[i].in_use)
        {
            (void)memset(&s_locks[i], 0, sizeof(fs_lock_entry_t));
            return &s_locks[i];
        }
    }

    return NULL;
}

/* ========================================================================
 * 文件系统管理接口实现
 * ======================================================================== */

/**
 * @brief 注册文件系统
 */
int32_t fs_register_fs(fs_fstype_t fstype, const fs_ops_t *ops)
{
    fs_ops_init();

    if (ops == NULL)
    {
        return -1;
    }

    if ((uint32_t)fstype > (uint32_t)FS_FSTYPE_DEVFS)
    {
        return -1;
    }

    s_fs_ops[(uint32_t)fstype] = ops;

    return 0;
}

/**
 * @brief 挂载文件系统
 */
int32_t fs_mount(const char *path, fs_fstype_t fstype,
                  const char *device, uint32_t flags)
{
    uint32_t i;
    fs_mount_t *mnt;
    int32_t ret;
    uint32_t mount_id;

    fs_ops_init();

    /* 参数检查 */
    if (path == NULL)
    {
        return -1;
    }

    if ((uint32_t)fstype > (uint32_t)FS_FSTYPE_DEVFS)
    {
        return -1;
    }

    if (s_fs_ops[(uint32_t)fstype] == NULL)
    {
        return -1;
    }

    /* 检查挂载点是否已存在 */
    for (i = 0U; i < FS_MAX_MOUNTS; i++)
    {
        if (s_mount_used[i] && strcmp(s_mounts[i].path, path) == 0)
        {
            return -1;
        }
    }

    /* 分配挂载点 */
    for (i = 0U; i < FS_MAX_MOUNTS; i++)
    {
        if (!s_mount_used[i])
        {
            break;
        }
    }

    if (i >= FS_MAX_MOUNTS)
    {
        return -1;
    }

    mnt = &s_mounts[i];
    mount_id = s_next_mount_id++;

    /* 填充挂载点信息 */
    mnt->mount_id = mount_id;
    mnt->fstype = fstype;
    mnt->flags = flags;
    (void)strncpy(mnt->path, path, 255U);
    mnt->path[255U] = '\0';
    mnt->private_data = NULL;

    /* 调用文件系统的挂载函数 */
    if (s_fs_ops[(uint32_t)fstype]->mount != NULL)
    {
        ret = s_fs_ops[(uint32_t)fstype]->mount(mnt, device);
        if (ret < 0)
        {
            return ret;
        }
    }

    s_mount_used[i] = true;

    return (int32_t)mount_id;
}

/**
 * @brief 卸载文件系统
 */
int32_t fs_unmount(uint32_t mount_id)
{
    uint32_t i;
    fs_mount_t *mnt;
    int32_t ret;

    fs_ops_init();

    /* 查找挂载点 */
    for (i = 0U; i < FS_MAX_MOUNTS; i++)
    {
        if (s_mount_used[i] && s_mounts[i].mount_id == mount_id)
        {
            break;
        }
    }

    if (i >= FS_MAX_MOUNTS)
    {
        return -1;
    }

    mnt = &s_mounts[i];

    /* 调用文件系统的卸载函数 */
    if (s_fs_ops[(uint32_t)mnt->fstype]->unmount != NULL)
    {
        ret = s_fs_ops[(uint32_t)mnt->fstype]->unmount(mnt);
        if (ret < 0)
        {
            return ret;
        }
    }

    /* 清理文件锁 */
    for (i = 0U; i < FS_MAX_LOCKS; i++)
    {
        if (s_locks[i].in_use && s_locks[i].mount_id == mount_id)
        {
            s_locks[i].in_use = false;
        }
    }

    /* 清空挂载点 */
    (void)memset(mnt, 0, sizeof(fs_mount_t));
    s_mount_used[i] = false;

    return 0;
}

/* ========================================================================
 * 文件锁接口实现
 * ======================================================================== */

/**
 * @brief 文件锁操作
 */
int32_t fs_flock(uint32_t mount_id, uint32_t ino,
                  fs_lock_type_t lock_type, uint32_t owner_tid)
{
    fs_lock_entry_t *lock;

    fs_ops_init();

    /* 查找现有锁 */
    lock = find_lock(mount_id, ino);

    if (lock_type == FS_LOCK_UN)
    {
        /* 解锁 */
        if (lock == NULL)
        {
            return -1;
        }

        if (lock->lock.owner_tid != owner_tid)
        {
            return -1;
        }

        lock->lock.lock_count--;

        if (lock->lock.lock_count == 0U)
        {
            lock->in_use = false;
        }

        return 0;
    }
    else if (lock_type == FS_LOCK_SH || lock_type == FS_LOCK_EX)
    {
        /* 加锁 */
        if (lock != NULL)
        {
            /* 已有锁，检查是否可以嵌套 */
            if (lock->lock.owner_tid == owner_tid)
            {
                /* 同一线程，允许嵌套锁 */
                if (lock->lock.lock_type != lock_type)
                {
                    /* 锁类型不兼容 */
                    return -1;
                }

                lock->lock.lock_count++;
                return 0;
            }
            else
            {
                /* 不同线程的锁冲突 */
                return -1;
            }
        }

        /* 分配新锁 */
        lock = alloc_lock();
        if (lock == NULL)
        {
            return -1;
        }

        lock->mount_id = mount_id;
        lock->ino = ino;
        lock->lock.locked = true;
        lock->lock.lock_type = lock_type;
        lock->lock.owner_tid = owner_tid;
        lock->lock.lock_count = 1U;
        lock->in_use = true;

        return 0;
    }

    return -1;
}

/**
 * @brief 获取挂载点（内部函数）
 */
fs_mount_t *fs_get_mount(uint32_t mount_id)
{
    uint32_t i;

    fs_ops_init();

    for (i = 0U; i < FS_MAX_MOUNTS; i++)
    {
        if (s_mount_used[i] && s_mounts[i].mount_id == mount_id)
        {
            return &s_mounts[i];
        }
    }

    return NULL;
}

/**
 * @brief 获取文件系统操作（内部函数）
 */
const fs_ops_t *fs_get_ops(fs_fstype_t fstype)
{
    fs_ops_init();

    if ((uint32_t)fstype > (uint32_t)FS_FSTYPE_DEVFS)
    {
        return NULL;
    }

    return s_fs_ops[(uint32_t)fstype];
}

/* ========================================================================
 * 符号链接/硬链接接口实现
 * ======================================================================== */

/**
 * @brief 查找路径对应的挂载点
 */
static int32_t find_mount_for_path(const char *path, uint32_t *mount_id)
{
    uint32_t i;
    uint32_t best_len;
    int32_t best_idx;

    best_len = 0U;
    best_idx = -1;

    for (i = 0U; i < FS_MAX_MOUNTS; i++)
    {
        if (s_mount_used[i])
        {
            uint32_t plen = 0U;
            while ((s_mounts[i].path[plen] != '\0') && (plen < 255U))
            {
                plen++;
            }

            if (plen > best_len)
            {
                uint32_t j;
                bool match = true;

                for (j = 0U; j < plen; j++)
                {
                    if (path[j] != s_mounts[i].path[j])
                    {
                        match = false;
                        break;
                    }
                }

                if (match)
                {
                    best_len = plen;
                    best_idx = (int32_t)i;
                }
            }
        }
    }

    if (best_idx < 0)
    {
        return -1;
    }

    *mount_id = s_mounts[(uint32_t)best_idx].mount_id;
    return 0;
}

/**
 * @brief 创建符号链接
 */
int32_t fs_symlink(const char *target, const char *linkpath)
{
    uint32_t mount_id;
    int32_t ret;
    const fs_ops_t *ops;
    fs_mount_t *mnt;

    fs_ops_init();

    if ((target == NULL) || (linkpath == NULL))
    {
        return -1;
    }

    /* 查找链接路径所在的挂载点 */
    ret = find_mount_for_path(linkpath, &mount_id);
    if (ret != 0)
    {
        return -1;
    }

    mnt = fs_get_mount(mount_id);
    if (mnt == NULL)
    {
        return -1;
    }

    ops = s_fs_ops[(uint32_t)mnt->fstype];
    if ((ops == NULL) || (ops->symlink == NULL))
    {
        return -38;  /* ENOSYS */
    }

    return ops->symlink(mount_id, target, linkpath);
}

/**
 * @brief 创建硬链接
 */
int32_t fs_link(const char *oldpath, const char *newpath)
{
    uint32_t mount_id;
    int32_t ret;
    const fs_ops_t *ops;
    fs_mount_t *mnt;

    fs_ops_init();

    if ((oldpath == NULL) || (newpath == NULL))
    {
        return -1;
    }

    /* 查找路径所在的挂载点 */
    ret = find_mount_for_path(oldpath, &mount_id);
    if (ret != 0)
    {
        return -1;
    }

    mnt = fs_get_mount(mount_id);
    if (mnt == NULL)
    {
        return -1;
    }

    ops = s_fs_ops[(uint32_t)mnt->fstype];
    if ((ops == NULL) || (ops->link == NULL))
    {
        return -38;  /* ENOSYS */
    }

    return ops->link(mount_id, oldpath, newpath);
}

/**
 * @brief 读取符号链接目标
 */
int64_t fs_readlink(const char *path, char *buf, uint64_t bufsize)
{
    uint32_t mount_id;
    int32_t ret;
    const fs_ops_t *ops;
    fs_mount_t *mnt;

    fs_ops_init();

    if ((path == NULL) || (buf == NULL) || (bufsize == 0U))
    {
        return -1;
    }

    /* 查找路径所在的挂载点 */
    ret = find_mount_for_path(path, &mount_id);
    if (ret != 0)
    {
        return -1;
    }

    mnt = fs_get_mount(mount_id);
    if (mnt == NULL)
    {
        return -1;
    }

    ops = s_fs_ops[(uint32_t)mnt->fstype];
    if ((ops == NULL) || (ops->readlink == NULL))
    {
        return -38;  /* ENOSYS */
    }

    return ops->readlink(mount_id, path, buf, bufsize);
}
