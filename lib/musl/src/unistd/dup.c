/**
 * @file    dup.c
 * @brief   dup() / dup2() 实现
 */
#include <unistd.h>
#include <errno.h>

int dup(int oldfd)
{
    (void)oldfd;
    errno = ENOSYS;
    return -1;
}

int dup2(int oldfd, int newfd)
{
    (void)oldfd;
    (void)newfd;
    errno = ENOSYS;
    return -1;
}
