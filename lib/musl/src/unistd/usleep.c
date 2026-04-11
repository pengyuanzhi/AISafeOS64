/**
 * @file    usleep.c
 * @brief   usleep() 实现
 */
#include <unistd.h>
#include <errno.h>

int usleep(unsigned int usec)
{
    (void)usec;
    return 0;
}
