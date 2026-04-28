/**
 * @file    fs_ops.c
 * @brief   文件系统抽象层管理实现
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details 文件系统抽象层管理实现：
 *          - 文件系统注册管理
 *          - 挂载点管理
 *          - 文件系统操作分发
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

        s_next_mount_id = 0U;
        s_initialized = true;
    }
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
    const fs_ops_t *ops;
    int32_t ret;

    fs_ops_init();

    if (path == NULL)
    {
        return -1;
    }

    if ((uint32_t)fstype > (uint32_t)FS_FSTYPE_DEVFS)
    {
        return -1;
    }

    ops = s_fs_ops[(uint32_t)fstype];
    if (ops == NULL)
    {
        return -1;
    }

    /* 查找空闲挂载点 */
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

    (void)memset(mnt, 0, sizeof(fs_mount_t));
    mnt->mount_id = s_next_mount_id++;
    mnt->fstype = fstype;
    mnt->flags = flags;
    mnt->private_data = NULL;

    /* 复制挂载路径 */
    for (i = 0U; (i < 255U) && (path[i] != '\0'); i++)
    {
        mnt->path[i] = path[i];
    }
    mnt->path[i] = '\0';

    /* 调用文件系统 mount */
    if (ops->mount != NULL)
    {
        ret = ops->mount(mnt, device);
        if (ret != 0)
        {
            return ret;
        }
    }

    s_mount_used[(uint32_t)mnt->mount_id] = true;

    return (int32_t)mnt->mount_id;
}

/**
 * @brief 卸载文件系统
 */
int32_t fs_unmount(uint32_t mount_id)
{
    fs_mount_t *mnt;
    const fs_ops_t *ops;

    fs_ops_init();

    if (mount_id >= FS_MAX_MOUNTS)
    {
        return -1;
    }

    if (!s_mount_used[mount_id])
    {
        return -1;
    }

    mnt = &s_mounts[mount_id];

    ops = s_fs_ops[(uint32_t)mnt->fstype];
    if ((ops != NULL) && (ops->unmount != NULL))
    {
        (void)ops->unmount(mnt);
    }

    s_mount_used[mount_id] = false;

    return 0;
}
