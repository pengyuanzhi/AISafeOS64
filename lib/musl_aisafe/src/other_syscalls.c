/**
 * @file    other_syscalls.c
 * @brief   AISafeOS64 musl 适配层 — 其他系统调用实现
 * @version 1.0
 *
 * @details 实现其他系统调用：pipe2, uname, sysinfo, getrlimit, setrlimit 等
 *
 * @note MISRA-C:2012 合规
 */

#include "syscall_arch.h"
#include "musl_safety.h"
#include <kernel/syscall.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * Linux errno 值（与 musl 一致）
 * ======================================================================== */
#define EPERM           1
#define ENOENT          2
#define ESRCH           3
#define EINTR           4
#define EIO             5
#define ENXIO           6
#define E2BIG           7
#define ENOEXEC         8
#define EBADF            9
#define ECHILD          10
#define EAGAIN          11
#define ENOMEM          12
#define EACCES          13
#define EFAULT          14
#define ENOTBLK         15
#define EBUSY           16
#define EEXIST          17
#define EXDEV           18
#define ENODEV          19
#define ENOTDIR         20
#define EISDIR          21
#define EINVAL          22
#define ENFILE          23
#define EMFILE          24
#define ENOTTY          25
#define ETXTBSY         26
#define EFBIG           27
#define ENOSPC          28
#define ESPIPE          29
#define EROFS           30
#define EMLINK          31
#define EPIPE           32
#define EDOM            33
#define ERANGE          34
#define EDEADLK         35
#define ENAMETOOLONG    36
#define ENOLCK          37
#define ENOSYS          38
#define ENOTEMPTY       39
#define ELOOP           40
#define ENOMSG          42
#define EIDRM           43
#define ECHRNG          44
#define EL2NSYNC        45
#define EL3HLT          46
#define ENRSTRNG        48
#define ENONET          50
#define ENOTCONN        107
#define ECONNRESET      104
#define ENOTSOCK        88

/* ========================================================================
 * uname 实现
 * ======================================================================== */

/**
 * @brief uname 系统调用
 *
 * @param buf utsname 结构体指针
 *
 * @return 0 表示成功，-1 表示失败
 */
long aisafe_sys_uname(long buf)
{
    /* AISafeOS64 临时实现：返回固定信息 */
    if (buf == 0)
    {
        return -EFAULT;
    }

    /* struct utsname { char sysname[65], nodename[65], release[65], version[65], machine[65] } */
    char *name = (char *)buf;

    (void)memset(name, 0, 65U);
    (void)strncpy(name, "AISafeOS64", 65U);

    (void)memset(name + 65U, 0, 65U);
    (void)strncpy(name + 65U, "aisafe64", 65U);

    (void)memset(name + 130U, 0, 65U);
    (void)strncpy(name + 130U, "1.0.0", 65U);

    (void)memset(name + 195U, 0, 65U);
    (void)strncpy(name + 195U, "AISafeOS64 1.0.0", 65U);

    (void)memset(name + 260U, 0, 65U);
    (void)strncpy(name + 260U, "aarch64", 65U);

    return 0;
}

/* ========================================================================
 * pipe2 实现
 * ======================================================================== */

/**
 * @brief pipe2 系统调用
 *
 * @param pipefd 文件描述符数组（2 个）
 * @param flags 标志（O_CLOEXEC, O_NONBLOCK 等）
 *
 * @return 0 表示成功，-1 表示失败
 */
long aisafe_sys_pipe2(long pipefd, long flags)
{
    /* AISafeOS64 临时实现：返回 -ENOSYS */
    (void)pipefd;
    (void)flags;
    return -ENOSYS;
}

/* ========================================================================
 * getrlimit / setrlimit 实现
 * ======================================================================== */

/**
 * @brief getrlimit 系统调用
 *
 * @param resource 资源类型
 * @param rlimit rlimit 结构体指针
 *
 * @return 0 表示成功，-1 表示失败
 */
long aisafe_sys_getrlimit(long resource, long rlimit)
{
    /* AISafeOS64 临时实现：返回固定值 */
    (void)resource;

    if (rlimit == 0)
    {
        return -EFAULT;
    }

    /* struct rlimit { rlim_t rlim_cur, rlim_max } */
    unsigned long *lim = (unsigned long *)rlimit;
    lim[0] = 0xFFFFFFFFU;  /* RLIM_INFINITY */
    lim[1] = 0xFFFFFFFFU;  /* RLIM_INFINITY */

    return 0;
}

/**
 * @brief setrlimit 系统调用
 *
 * @param resource 资源类型
 * @param rlimit rlimit 结构体指针
 *
 * @return 0 表示成功，-1 表示失败
 */
long aisafe_sys_setrlimit(long resource, long rlimit)
{
    /* AISafeOS64 临时实现：返回 -ENOSYS */
    (void)resource;
    (void)rlimit;
    return -ENOSYS;
}

/* ========================================================================
 * sysinfo 实现
 * ======================================================================== */

/**
 * @brief sysinfo 系统调用
 *
 * @param info sysinfo 结构体指针
 *
 * @return 0 表示成功，-1 表示失败
 */
long aisafe_sys_sysinfo(long info)
{
    /* AISafeOS64 临时实现：返回固定信息 */
    if (info == 0)
    {
        return -EFAULT;
    }

    /* struct sysinfo { long uptime, loads[3], totalram, freeram, sharedram, bufferram, totalswap, freeswap, procs, totalhigh, freehigh, mem_unit } */
    unsigned long *si = (unsigned long *)info;

    si[0] = 100UL;      /* uptime (秒) */
    si[1] = 0UL;        /* loads[0] */
    si[2] = 0UL;        /* loads[1] */
    si[3] = 0UL;        /* loads[2] */
    si[4] = 1024UL * 1024UL;  /* totalram (1GB) */
    si[5] = 512UL * 1024UL;  /* freeram (512MB) */
    si[6] = 0UL;        /* sharedram */
    si[7] = 0UL;        /* bufferram */
    si[8] = 0UL;        /* totalswap */
    si[9] = 0UL;        /* freeswap */
    si[10] = 1UL;       /* procs */
    si[11] = 0UL;       /* totalhigh */
    si[12] = 0UL;       /* freehigh */
    si[13] = 1UL;       /* mem_unit */

    return 0;
}
