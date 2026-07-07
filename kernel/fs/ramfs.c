/**
 * @file    ramfs.c
 * @brief   内核 RAMFS 实现（最小文件系统）
 * @author  AISafe64 Team
 * @date    2026-07-07
 * @version 1.0
 *
 * @details 内存文件系统，支持：
 *          - open/close/read/write/lseek
 *          - 文件描述符表（每进程独立）
 *          - /dev/console（stdout/stderr 重定向到 klog）
 *
 *          当前为最小实现：
 *          - 不支持目录层级
 *          - 不支持 unlink/rename
 *          - 文件数据存储在固定大小缓冲区中
 *
 * @note    后续迁移到用户态 FS 服务后，此模块可移除或保留为早期引导用
 *
 * @revision history
 * v1.0 2026-07-07 初始版本
 */

#include <kernel/ramfs.h>
#include <kernel/config.h>
#include <kernel/errno.h>
#include <kernel/klog.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef RAMFS_MAX_FILES
#define RAMFS_MAX_FILES 32U
#endif

#ifndef RAMFS_FILE_SIZE
#define RAMFS_FILE_SIZE 4096U
#endif

#ifndef RAMFS_MAX_FDS
#define RAMFS_MAX_FDS 16U
#endif

/* ========================================================================
 * 数据结构
 * ======================================================================== */

/**
 * @brief RAMFS 文件节点
 */
typedef struct
{
    bool    in_use;             /**< @brief 是否被使用 */
    char    name[32];           /**< @brief 文件名 */
    uint8_t data[RAMFS_FILE_SIZE]; /**< @brief 文件数据 */
    uint32_t size;              /**< @brief 当前文件大小 */
} ramfs_file_t;

/**
 * @brief 文件描述符
 */
typedef struct
{
    bool       in_use;          /**< @brief 是否被使用 */
    uint32_t   file_idx;        /**< @brief 关联的文件索引 */
    uint32_t   pos;             /**< @brief 当前读/写位置 */
    bool       writable;        /**< @brief 是否可写 */
} ramfs_fd_t;

/** @brief 文件节点表 */
static ramfs_file_t s_files[RAMFS_MAX_FILES];

/** @brief 全局文件描述符表 */
static ramfs_fd_t s_fds[RAMFS_MAX_FDS];

/** @brief 初始化标志 */
static bool s_initialized = false;

/* ========================================================================
 * 内部辅助
 * ======================================================================== */

/**
 * @brief 按文件名查找文件节点
 *
 * @param name 文件名
 * @return 文件索引，未找到返回 -1
 */
static int32_t ramfs_find_file(const char *name)
{
    uint32_t i;
    if (name == NULL)
    {
        return -1;
    }
    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (s_files[i].in_use &&
            (strcmp(s_files[i].name, name) == 0))
        {
            return (int32_t)i;
        }
    }
    return -1;
}

/**
 * @brief 分配空闲文件节点
 *
 * @return 文件索引，无空闲返回 -1
 */
static int32_t ramfs_alloc_file(void)
{
    uint32_t i;
    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (!s_files[i].in_use)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

/**
 * @brief 分配空闲文件描述符
 *
 * @return fd（>= 3），无空闲返回 -1
 */
static int32_t ramfs_alloc_fd(void)
{
    uint32_t i;
    /* fd 0/1/2 预留给 stdin/stdout/stderr */
    for (i = 3U; i < RAMFS_MAX_FDS; i++)
    {
        if (!s_fds[i].in_use)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

/* ========================================================================
 * 公共 API
 * ======================================================================== */

void ramfs_init(void)
{
    uint32_t i;

    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        s_files[i].in_use = false;
        s_files[i].name[0] = '\0';
        s_files[i].size = 0U;
    }

    for (i = 0U; i < RAMFS_MAX_FDS; i++)
    {
        s_fds[i].in_use = false;
        s_fds[i].file_idx = 0U;
        s_fds[i].pos = 0U;
        s_fds[i].writable = false;
    }

    /* fd 0 = stdin（只读空设备） */
    s_fds[0].in_use = true;

    /* fd 1 = stdout（写到 klog） */
    s_fds[1].in_use = true;
    s_fds[1].writable = true;

    /* fd 2 = stderr（写到 klog） */
    s_fds[2].in_use = true;
    s_fds[2].writable = true;

    s_initialized = true;
}

int32_t ramfs_open(const char *path, uint32_t flags)
{
    int32_t file_idx;
    int32_t fd;

    if ((path == NULL) || !s_initialized)
    {
        return -(int32_t)EINVAL;
    }

    /* /dev/console 映射到 fd 1（stdout） */
    if (strcmp(path, "/dev/console") == 0)
    {
        return 1;
    }

    /* 查找或创建文件 */
    file_idx = ramfs_find_file(path);
    if (file_idx < 0)
    {
        /* 文件不存在，检查是否允许创建 */
        if ((flags & RAMFS_O_CREAT) == 0U)
        {
            return -(int32_t)ENOENT;
        }

        file_idx = ramfs_alloc_file();
        if (file_idx < 0)
        {
            return -(int32_t)ENOMEM;
        }

        s_files[file_idx].in_use = true;
        s_files[file_idx].name[0] = '\0';
        (void)strncpy(s_files[file_idx].name, path, sizeof(s_files[file_idx].name) - 1U);
        s_files[file_idx].name[sizeof(s_files[file_idx].name) - 1U] = '\0';
        s_files[file_idx].size = 0U;
    }

    fd = ramfs_alloc_fd();
    if (fd < 0)
    {
        return -(int32_t)EMFILE;
    }

    s_fds[fd].in_use = true;
    s_fds[fd].file_idx = (uint32_t)file_idx;
    s_fds[fd].pos = 0U;
    s_fds[fd].writable = ((flags & RAMFS_O_WRONLY) != 0U) ||
                         ((flags & RAMFS_O_RDWR) != 0U);

    return fd;
}

int32_t ramfs_close(int32_t fd)
{
    if ((fd < 0) || ((uint32_t)fd >= RAMFS_MAX_FDS))
    {
        return -(int32_t)EINVAL;
    }

    if (!s_fds[fd].in_use)
    {
        return -(int32_t)EBADF;
    }

    /* stdin/stdout/stderr 不关闭 */
    if ((uint32_t)fd < 3U)
    {
        return 0;
    }

    s_fds[fd].in_use = false;
    return 0;
}

int32_t ramfs_read(int32_t fd, void *buf, uint32_t count)
{
    ramfs_fd_t *f;
    ramfs_file_t *file;
    uint32_t available;
    uint32_t to_read;

    if ((buf == NULL) || (fd < 0) || ((uint32_t)fd >= RAMFS_MAX_FDS))
    {
        return -(int32_t)EINVAL;
    }

    if (!s_fds[fd].in_use)
    {
        return -(int32_t)EBADF;
    }

    /* fd 0 (stdin)：暂无输入 */
    if (fd == 0)
    {
        return 0;
    }

    /* fd 1/2 (stdout/stderr)：不可读 */
    if (fd <= 2)
    {
        return -(int32_t)EBADF;
    }

    f = &s_fds[fd];
    file = &s_files[f->file_idx];

    available = file->size - f->pos;
    to_read = (count < available) ? count : available;

    if (to_read > 0U)
    {
        (void)memcpy(buf, &file->data[f->pos], to_read);
        f->pos += to_read;
    }

    return (int32_t)to_read;
}

int32_t ramfs_write(int32_t fd, const void *buf, uint32_t count)
{
    if ((buf == NULL) || (fd < 0) || ((uint32_t)fd >= RAMFS_MAX_FDS))
    {
        return -(int32_t)EINVAL;
    }

    if (!s_fds[fd].in_use)
    {
        return -(int32_t)EBADF;
    }

    /* fd 1/2 (stdout/stderr)：写到 klog */
    if ((fd == 1) || (fd == 2))
    {
        uint32_t i;
        const char *str = (const char *)buf;
        for (i = 0U; i < count; i++)
        {
            klog_putc(str[i]);
        }
        return (int32_t)count;
    }

    /* fd 0 (stdin)：不可写 */
    if (fd == 0)
    {
        return -(int32_t)EBADF;
    }

    /* 普通文件写入 */
    {
        ramfs_fd_t *f = &s_fds[fd];
        ramfs_file_t *file = &s_files[f->file_idx];
        uint32_t space_left = RAMFS_FILE_SIZE - f->pos;
        uint32_t to_write = (count < space_left) ? count : space_left;

        if (!f->writable)
        {
            return -(int32_t)EBADF;
        }

        if (to_write > 0U)
        {
            (void)memcpy(&file->data[f->pos], buf, to_write);
            f->pos += to_write;
            if (f->pos > file->size)
            {
                file->size = f->pos;
            }
        }

        return (int32_t)to_write;
    }
}

int32_t ramfs_lseek(int32_t fd, int32_t offset, uint32_t whence)
{
    ramfs_fd_t *f;
    uint32_t new_pos;

    if ((fd < 0) || ((uint32_t)fd >= RAMFS_MAX_FDS))
    {
        return -(int32_t)EINVAL;
    }

    if (!s_fds[fd].in_use)
    {
        return -(int32_t)EBADF;
    }

    if (fd <= 2)
    {
        return -(int32_t)ESPIPE;
    }

    f = &s_fds[fd];

    switch (whence)
    {
        case 0U: /* SEEK_SET */
            new_pos = (uint32_t)offset;
            break;
        case 1U: /* SEEK_CUR */
            new_pos = f->pos + (uint32_t)offset;
            break;
        case 2U: /* SEEK_END */
            new_pos = s_files[f->file_idx].size + (uint32_t)offset;
            break;
        default:
            return -(int32_t)EINVAL;
    }

    f->pos = new_pos;
    return (int32_t)new_pos;
}

int32_t ramfs_fstat(int32_t fd, void *statbuf)
{
    ramfs_fd_t *f;
    uint32_t *stat;

    if ((statbuf == NULL) || (fd < 0) || ((uint32_t)fd >= RAMFS_MAX_FDS))
    {
        return -(int32_t)EINVAL;
    }

    if (!s_fds[fd].in_use)
    {
        return -(int32_t)EBADF;
    }

    if (fd <= 2)
    {
        /* stdin/stdout/stderr 是字符设备 */
        stat = (uint32_t *)statbuf;
        memset(statbuf, 0, 64U); /* struct stat 简化清零 */
        stat[0] = 0x2190U; /* mode: 字符设备 */
        return 0;
    }

    f = &s_fds[fd];
    stat = (uint32_t *)statbuf;
    memset(statbuf, 0, 64U);
    stat[0] = 0x81A4U; /* mode: 常规文件 0644 */
    stat[8] = s_files[f->file_idx].size; /* size */

    return 0;
}
