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
#include <kernel/spinlock.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大挂载点数量 */
#define FS_MAX_MOUNTS         8U

/** @brief 最大文件锁数量 */
#define FS_MAX_LOCKS          128U

/** @brief 分片锁数量（必须是 2 的幂次方） */
#define FS_LOCK_SHARDS_COUNT  8U

/** @brief 每个分片的锁表大小 */
#define FS_LOCKS_PER_SHARD    (FS_MAX_LOCKS / FS_LOCK_SHARDS_COUNT)

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

/**
 * @brief 文件锁分片结构
 *
 * @details 每个分片独立管理一部分锁表，使用 TicketLock 保护，
 *          不同分片的锁操作互不干扰。
 */
typedef struct
{
    TicketLock_t      shard_lock;                         /**< @brief 分片自旋锁 */
    fs_lock_entry_t   locks[FS_LOCKS_PER_SHARD];         /**< @brief 分片锁表 */
    uint32_t          lock_count;                         /**< @brief 分片内锁计数 */
} fs_lock_shard_t;

/** @brief 文件锁分片数组 */
static fs_lock_shard_t s_lock_shards[FS_LOCK_SHARDS_COUNT];

/* ========================================================================
 * 分片锁辅助函数
 * ======================================================================== */

/**
 * @brief 根据挂载点 ID 选择锁分片
 *
 * @details 使用取模运算将挂载点映射到分片，
 *          相同挂载点的所有锁操作路由到同一分片。
 *
 * @param mount_id 挂载点 ID
 *
 * @return 分片索引 [0, FS_LOCK_SHARDS_COUNT)
 */
static inline uint32_t select_lock_shard(uint32_t mount_id)
{
    return mount_id & (FS_LOCK_SHARDS_COUNT - 1U);
}

/**
 * @brief 获取挂载点对应的锁分片
 *
 * @param mount_id 挂载点 ID
 *
 * @return 锁分片指针
 */
static inline fs_lock_shard_t *get_lock_shard(uint32_t mount_id)
{
    return &s_lock_shards[select_lock_shard(mount_id)];
}

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 初始化文件系统抽象层
 *
 * @details 初始化文件系统操作表、挂载点表和分片锁表。
 *          每个分片独立初始化 TicketLock 和锁表。
 */
static void fs_ops_init(void)
{
    uint32_t i;
    uint32_t j;

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

        /* 初始化每个锁分片 */
        for (i = 0U; i < FS_LOCK_SHARDS_COUNT; i++)
        {
            ticket_lock_init(&s_lock_shards[i].shard_lock);
            s_lock_shards[i].lock_count = 0U;

            for (j = 0U; j < FS_LOCKS_PER_SHARD; j++)
            {
                (void)memset(&s_lock_shards[i].locks[j], 0, sizeof(fs_lock_entry_t));
            }
        }

        s_next_mount_id = 0U;
        s_initialized = true;
    }
}

/**
 * @brief 查找文件锁（分片内查找，需在持锁状态下调用）
 *
 * @details 在指定分片内查找匹配的锁表项。
 *          调用方必须持有分片锁。
 *
 * @param shard    锁分片指针
 * @param mount_id 挂载点 ID
 * @param ino      inode 编号
 *
 * @return 锁表项指针，未找到返回 NULL
 */
static fs_lock_entry_t *find_lock_in_shard(fs_lock_shard_t *shard,
                                            uint32_t mount_id, uint32_t ino)
{
    uint32_t i;

    for (i = 0U; i < FS_LOCKS_PER_SHARD; i++)
    {
        if (shard->locks[i].in_use &&
            shard->locks[i].mount_id == mount_id &&
            shard->locks[i].ino == ino)
        {
            return &shard->locks[i];
        }
    }

    return NULL;
}

/**
 * @brief 分配文件锁表项（分片内分配，需在持锁状态下调用）
 *
 * @details 在指定分片内查找空闲槽位并分配。
 *          调用方必须持有分片锁。
 *
 * @param shard 锁分片指针
 *
 * @return 锁表项指针，无空闲槽位返回 NULL
 */
static fs_lock_entry_t *alloc_lock_in_shard(fs_lock_shard_t *shard)
{
    uint32_t i;

    for (i = 0U; i < FS_LOCKS_PER_SHARD; i++)
    {
        if (!shard->locks[i].in_use)
        {
            (void)memset(&shard->locks[i], 0, sizeof(fs_lock_entry_t));
            return &shard->locks[i];
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

    /* 清理该挂载点对应分片中的文件锁 */
    {
        fs_lock_shard_t *shard = get_lock_shard(mount_id);
        uint32_t j;

        ticket_lock_acquire(&shard->shard_lock);

        for (j = 0U; j < FS_LOCKS_PER_SHARD; j++)
        {
            if (shard->locks[j].in_use && shard->locks[j].mount_id == mount_id)
            {
                shard->locks[j].in_use = false;
            }
        }

        ticket_lock_release(&shard->shard_lock);
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
 *
 * @details 使用分片锁保护文件锁操作：
 *          - 根据挂载点 ID 选择对应分片
 *          - 获取分片锁后执行锁操作
 *          - 操作完成后释放分片锁
 *
 * @param mount_id  挂载点 ID
 * @param ino       inode 编号
 * @param lock_type 锁类型（LOCK_SH/LOCK_EX/LOCK_UN）
 * @param owner_tid 线程 ID
 *
 * @return 0 成功，<0 失败
 */
int32_t fs_flock(uint32_t mount_id, uint32_t ino,
                  fs_lock_type_t lock_type, uint32_t owner_tid)
{
    fs_lock_shard_t *shard;
    fs_lock_entry_t *lock;
    int32_t ret;

    fs_ops_init();

    /* 选择并锁定对应分片 */
    shard = get_lock_shard(mount_id);
    ticket_lock_acquire(&shard->shard_lock);

    /* 查找现有锁 */
    lock = find_lock_in_shard(shard, mount_id, ino);

    if (lock_type == FS_LOCK_UN)
    {
        /* 解锁 */
        if (lock == NULL)
        {
            ticket_lock_release(&shard->shard_lock);
            return -1;
        }

        if (lock->lock.owner_tid != owner_tid)
        {
            ticket_lock_release(&shard->shard_lock);
            return -1;
        }

        lock->lock.lock_count--;

        if (lock->lock.lock_count == 0U)
        {
            lock->in_use = false;
        }

        ret = 0;
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
                    ticket_lock_release(&shard->shard_lock);
                    return -1;
                }

                lock->lock.lock_count++;
                ret = 0;
            }
            else
            {
                /* 不同线程的锁冲突 */
                ticket_lock_release(&shard->shard_lock);
                return -1;
            }
        }
        else
        {
            /* 分配新锁 */
            lock = alloc_lock_in_shard(shard);
            if (lock == NULL)
            {
                ticket_lock_release(&shard->shard_lock);
                return -1;
            }

            lock->mount_id = mount_id;
            lock->ino = ino;
            lock->lock.locked = true;
            lock->lock.lock_type = lock_type;
            lock->lock.owner_tid = owner_tid;
            lock->lock.lock_count = 1U;
            lock->in_use = true;

            ret = 0;
        }
    }
    else
    {
        ret = -1;
    }

    ticket_lock_release(&shard->shard_lock);
    return ret;
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
