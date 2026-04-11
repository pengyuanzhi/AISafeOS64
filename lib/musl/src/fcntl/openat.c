/**
 * @file    openat.c
 * @brief   openat() 实现
 */
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>

int openat(int dirfd, const char *pathname, int flags, ...)
{
    (void)dirfd;
    (void)pathname;
    (void)flags;
    errno = ENOSYS;
    return -1;
}
