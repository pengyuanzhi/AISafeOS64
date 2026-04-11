/**
 * @file    pipe.c
 * @brief   pipe() 实现
 */
#include <unistd.h>
#include <errno.h>

int pipe(int pipefd[2])
{
    (void)pipefd;
    errno = ENOSYS;
    return -1;
}
