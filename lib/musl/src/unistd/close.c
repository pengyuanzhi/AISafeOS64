/**
 * @file    close.c
 * @brief   close() 实现
 */
#include <unistd.h>
#include <errno.h>

int close(int fd)
{
    (void)fd;
    /* 标准 fd 不需要关闭 */
    errno = ENOSYS;
    return -1;
}
