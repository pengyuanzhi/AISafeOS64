/**
 * @file    fcntl.c
 * @brief   fcntl() 实现
 */
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>

int fcntl(int fd, int cmd, ...)
{
    (void)fd;
    (void)cmd;
    errno = ENOSYS;
    return -1;
}
