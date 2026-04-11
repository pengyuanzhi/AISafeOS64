/**
 * @file    stat.c
 * @brief   stat() / fstat() / lstat() 实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 通过 IPC 请求 FS 服务获取文件状态
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <sys/stat.h>
#include <errno.h>
#include <string.h>

/**
 * @brief 获取文件状态
 */
int stat(const char *pathname, struct stat *statbuf)
{
    if ((pathname == NULL) || (statbuf == NULL))
    {
        errno = EFAULT;
        return -1;
    }

    /* 通过 IPC 请求 FS 服务 — 当前返回 ENOSYS */
    memset(statbuf, 0, sizeof(struct stat));
    errno = ENOSYS;
    return -1;
}

/**
 * @brief 通过文件描述符获取文件状态
 */
int fstat(int fd, struct stat *statbuf)
{
    if (statbuf == NULL)
    {
        errno = EFAULT;
        return -1;
    }

    /* 标准 fd 返回字符设备信息 */
    if ((fd >= 0) && (fd <= 2))
    {
        memset(statbuf, 0, sizeof(struct stat));
        statbuf->st_mode = 0x2000 | 0666;  /* S_IFCHR | rw-rw-rw- */
        statbuf->st_blksize = 4096;
        return 0;
    }

    memset(statbuf, 0, sizeof(struct stat));
    errno = ENOSYS;
    return -1;
}

/**
 * @brief 获取文件状态（不跟随符号链接）
 */
int lstat(const char *pathname, struct stat *statbuf)
{
    /* 当前不支持符号链接，等同于 stat() */
    return stat(pathname, statbuf);
}
