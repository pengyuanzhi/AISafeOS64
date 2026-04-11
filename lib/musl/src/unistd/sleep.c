/**
 * @file    sleep.c
 * @brief   sleep() 实现
 */
#include <unistd.h>
#include <aisafe/syscall.h>

unsigned int sleep(unsigned int seconds)
{
    /* 简单忙等待 — 后续改用定时器 */
    (void)seconds;
    return 0;
}
