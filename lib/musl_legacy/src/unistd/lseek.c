/**
 * @file    lseek.c
 * @brief   lseek() 实现
 */
#include <unistd.h>
#include <errno.h>

off_t lseek(int fd, off_t offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;
    errno = ENOSYS;
    return -1;
}
