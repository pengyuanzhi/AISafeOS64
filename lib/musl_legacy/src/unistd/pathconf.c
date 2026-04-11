/**
 * @file    pathconf.c
 * @brief   pathconf() / fpathconf() 实现
 */
#include <unistd.h>
#include <errno.h>

long pathconf(const char *path, int name)
{
    (void)path;
    if (name == 1) { return 4096; }  /* _PC_PATH_MAX */
    if (name == 2) { return 255; }   /* _PC_NAME_MAX */
    errno = EINVAL;
    return -1;
}

long fpathconf(int fd, int name)
{
    (void)fd;
    return pathconf("", name);
}
