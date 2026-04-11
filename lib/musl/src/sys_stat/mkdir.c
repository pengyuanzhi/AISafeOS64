/**
 * @file    mkdir.c
 * @brief   mkdir() 实现
 */
#include <sys/stat.h>
#include <errno.h>

int mkdir(const char *pathname, mode_t mode)
{
    (void)pathname;
    (void)mode;
    errno = ENOSYS;
    return -1;
}
