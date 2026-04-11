/**
 * @file    read.c
 * @brief   read() 实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 从文件描述符读取数据。
 *          当前通过 IPC 请求 FS 服务，宿主机测试返回 ENOSYS。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <unistd.h>
#include <errno.h>

/**
 * @brief 从文件描述符读取数据
 *
 * @param fd    文件描述符
 * @param buf   接收缓冲区
 * @param count 读取字节数
 *
 * @return 成功返回读取字节数，失败返回 -1
 */
ssize_t read(int fd, void *buf, size_t count)
{
    (void)fd;
    (void)buf;
    (void)count;
    errno = ENOSYS;
    return -1;
}
