/**
 * @file    chmod.c
 * @brief   chmod() / fchmod() 实现
 */
#include <sys/stat.h>
#include <errno.h>

int chmod(const char *pathname, mode_t mode)
{
    (void)pathname;
    (void)mode;
    errno = ENOSYS;
    return -1;
}

int fchmod(int fd, mode_t mode)
{
    (void)fd;
    (void)mode;
    errno = ENOSYS;
    return -1;
}
