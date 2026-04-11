/**
 * @file    write.c
 * @brief   write() 实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 向文件描述符写入数据。
 *          STDOUT/STDERR 使用 SYS_DEBUG_PRINT 输出。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <unistd.h>
#include <errno.h>
#include <aisafe/syscall.h>

/**
 * @brief 向文件描述符写入数据
 *
 * @param fd    文件描述符
 * @param buf   写入缓冲区
 * @param count 写入字节数
 *
 * @return 成功返回写入字节数，失败返回 -1
 */
ssize_t write(int fd, const void *buf, size_t count)
{
    if (buf == NULL)
    {
        errno = EFAULT;
        return -1;
    }

    if ((fd == STDOUT_FILENO) || (fd == STDERR_FILENO))
    {
        /* 标准/错误输出通过内核调试打印 */
        int ret = (int)syscall2(SYS_DEBUG_PRINT,
                                (uint64_t)(unsigned long)buf,
                                (uint64_t)count);
        if (ret < 0)
        {
            errno = -ret;
            return -1;
        }
        return (ssize_t)count;
    }

    /* 其他 fd 通过 IPC 请求 FS 服务 */
    errno = ENOSYS;
    return -1;
}
