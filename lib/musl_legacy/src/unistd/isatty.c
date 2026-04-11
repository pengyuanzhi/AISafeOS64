/**
 * @file    isatty.c
 * @brief   isatty() 实现
 */
#include <unistd.h>

/**
 * @brief 检查是否为终端
 * @return STDIN/STDOUT/STDERR 返回 1，否则返回 0
 */
int isatty(int fd)
{
    if ((fd >= 0) && (fd <= 2))
    {
        return 1;
    }
    return 0;
}
