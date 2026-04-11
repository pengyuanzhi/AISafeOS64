/**
 * @file    getppid.c
 * @brief   getppid() 实现
 */
#include <unistd.h>
#include <errno.h>

pid_t getppid(void)
{
    /* 微内核 init 进程 PID=1 为所有用户态服务父进程 */
    return (pid_t)1;
}
