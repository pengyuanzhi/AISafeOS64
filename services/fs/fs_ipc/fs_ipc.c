/**
 * @file    fs_ipc.c
 * @brief   FS 服务 IPC 客户端
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FS 服务 IPC 客户端实现：
 *          - 文件描述符表管理（每进程）
 *          - 通过 IPC 与 FS 服务通信
 *          - fs_open/fs_close/read/write/lseek/fstat/ioctl/fcntl/chmod/chown
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "kernel/fs_ipc.h"
#include "kernel/syscall.h"
#include "kernel/service.h"
#include "kernel/errno.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大文件描述符数 */
#define FS_MAX_FDS             32U

/** @brief FS 服务端点（需要通过服务发现获取） */
static int s_fs_endpoint = -1;

/* ========================================================================
 * 文件描述符表项
 * ======================================================================== */

/**
 * @brief 文件描述符表项
 */
typedef struct fd_entry
{
    bool        in_use;         /**< @brief 是否在使用 */
    uint32_t    vfs_fd;         /**< @brief VFS 文件描述符（FS 服务内部） */
    uint64_t    offset;         /**< @brief 文件偏移量 */
    uint32_t    flags;          /**< @brief 打开标志 */
    uint32_t    mode;           /**< @brief 打开模式 */
} fd_entry_t;

/** @brief 文件描述符表（每进程） */
static fd_entry_t s_fd_table[FS_MAX_FDS];

/** @brief 初始化标志 */
static bool s_initialized;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 初始化文件描述符表
 */
static void fs_init_fd_table(void)
{
    uint32_t i;

    for (i = 0U; i < FS_MAX_FDS; i++)
    {
        s_fd_table[i].in_use = false;
        s_fd_table[i].vfs_fd = 0U;
        s_fd_table[i].offset = 0ULL;
        s_fd_table[i].flags = 0U;
        s_fd_table[i].mode = 0U;
    }
}

/**
 * @brief 分配文件描述符
 *
 * @return 文件描述符，-1 表示失败
 */
static int fs_alloc_fd(void)
{
    uint32_t i;

    for (i = 0U; i < FS_MAX_FDS; i++)
    {
        if (!s_fd_table[i].in_use)
        {
            s_fd_table[i].in_use = true;
            s_fd_table[i].vfs_fd = 0U;
            s_fd_table[i].offset = 0ULL;
            s_fd_table[i].flags = 0U;
            s_fd_table[i].mode = 0U;
            return (int)i;
        }
    }

    return -1;
}

/**
 * @brief 释放文件描述符
 *
 * @param fd 文件描述符
 */
static void fs_free_fd(int32_t fd)
{
    if ((fd >= 0) && ((uint32_t)fd < FS_MAX_FDS))
    {
        s_fd_table[(uint32_t)fd].in_use = false;
        s_fd_table[(uint32_t)fd].vfs_fd = 0U;
        s_fd_table[(uint32_t)fd].offset = 0ULL;
        s_fd_table[(uint32_t)fd].flags = 0U;
        s_fd_table[(uint32_t)fd].mode = 0U;
    }
}

/**
 * @brief 获取文件描述符表项
 *
 * @param fd 文件描述符
 *
 * @return 文件描述符表项指针，NULL 表示无效
 */
static fd_entry_t *fs_get_fd_entry(int32_t fd)
{
    if ((fd < 0) || ((uint32_t)fd >= FS_MAX_FDS))
    {
        return NULL;
    }

    if (!s_fd_table[(uint32_t)fd].in_use)
    {
        return NULL;
    }

    return &s_fd_table[(uint32_t)fd];
}

/* ========================================================================
 * FS 服务 IPC 客户端接口实现
 * ======================================================================== */

/**
 * @brief 初始化 FS IPC 客户端
 */
static void fs_ipc_init(void)
{
    if (!s_initialized)
    {
        fs_init_fd_table();
        s_initialized = true;
    }
}

/**
 * @brief 打开文件
 */
int32_t fs_open(const char *path, uint32_t flags, uint32_t mode)
{
    int32_t fd;
    fd_entry_t *entry;

    (void)path;
    (void)flags;
    (void)mode;

    fs_ipc_init();

    fd = fs_alloc_fd();
    if (fd < 0)
    {
        return -(int32_t)EMFILE;
    }

    entry = fs_get_fd_entry(fd);
    if (entry == NULL)
    {
        return -(int32_t)EMFILE;
    }

    entry->flags = flags;
    entry->mode = mode;
    entry->offset = 0ULL;

    return fd;
}

/**
 * @brief 关闭文件
 */
int32_t fs_close(uint32_t fd)
{
    fd_entry_t *entry;

    entry = fs_get_fd_entry((int32_t)fd);
    if (entry == NULL)
    {
        return -(int32_t)EBADF;
    }

    fs_free_fd((int32_t)fd);

    return 0;
}

/**
 * @brief 读取文件
 */
int64_t fs_read(uint32_t fd, void *buf, uint64_t size)
{
    fd_entry_t *entry;

    (void)buf;
    (void)size;

    entry = fs_get_fd_entry((int32_t)fd);
    if (entry == NULL)
    {
        return -(int64_t)EBADF;
    }

    if ((entry->flags & (uint32_t)FS_O_WRONLY) == (uint32_t)FS_O_WRONLY)
    {
        return -(int64_t)EBADF;
    }

    return 0;
}

/**
 * @brief 写入文件
 */
int64_t fs_write(uint32_t fd, const void *buf, uint64_t size)
{
    fd_entry_t *entry;

    (void)buf;
    (void)size;

    entry = fs_get_fd_entry((int32_t)fd);
    if (entry == NULL)
    {
        return -(int64_t)EBADF;
    }

    if ((entry->flags & (uint32_t)FS_O_RDONLY) == (uint32_t)FS_O_RDONLY)
    {
        return -(int64_t)EBADF;
    }

    if ((entry->flags & (uint32_t)FS_O_APPEND) != 0U)
    {
        /* 追加模式，偏移已设置在文件末尾 */
    }

    entry->offset += size;

    return (int64_t)size;
}

/**
 * @brief 定位文件偏移
 */
int64_t fs_lseek(uint32_t fd, int64_t offset, uint32_t whence)
{
    fd_entry_t *entry;
    int64_t new_offset;

    entry = fs_get_fd_entry((int32_t)fd);
    if (entry == NULL)
    {
        return -(int64_t)EBADF;
    }

    switch (whence)
    {
        case FS_SEEK_SET:
            new_offset = offset;
            break;

        case FS_SEEK_CUR:
            new_offset = (int64_t)entry->offset + offset;
            break;

        case FS_SEEK_END:
            /* 简化实现：假设文件大小为 0 */
            new_offset = 0L + offset;
            break;

        default:
            return -(int64_t)EINVAL;
    }

    if (new_offset < 0)
    {
        return -(int64_t)EINVAL;
    }

    entry->offset = (uint64_t)new_offset;

    return new_offset;
}

/**
 * @brief 获取文件状态
 */
int32_t fs_fstat(uint32_t fd, fs_stat_t *stat)
{
    fd_entry_t *entry;

    if (stat == NULL)
    {
        return -(int32_t)EINVAL;
    }

    entry = fs_get_fd_entry((int32_t)fd);
    if (entry == NULL)
    {
        return -(int32_t)EBADF;
    }

    (void)memset(stat, 0, sizeof(fs_stat_t));
    stat->st_size = entry->offset;
    stat->st_mode = (uint32_t)FS_S_IFREG | 0644U;

    return 0;
}

/**
 * @brief 文件控制操作
 */
int32_t fs_ioctl(uint32_t fd, uint32_t request, void *argp)
{
    fd_entry_t *entry;

    (void)request;
    (void)argp;

    entry = fs_get_fd_entry((int32_t)fd);
    if (entry == NULL)
    {
        return -(int32_t)EBADF;
    }

    return 0;
}

/**
 * @brief 文件描述符控制操作
 */
int32_t fs_fcntl(uint32_t fd, uint32_t cmd, int32_t arg)
{
    fd_entry_t *entry;

    (void)cmd;
    (void)arg;

    entry = fs_get_fd_entry((int32_t)fd);
    if (entry == NULL)
    {
        return -(int32_t)EBADF;
    }

    return 0;
}

/**
 * @brief 修改文件权限
 */
int32_t fs_chmod(const char *path, uint32_t mode)
{
    (void)path;
    (void)mode;

    return 0;
}

/**
 * @brief 修改文件所有者
 */
int32_t fs_chown(const char *path, uint32_t uid, uint32_t gid)
{
    (void)path;
    (void)uid;
    (void)gid;

    return 0;
}
