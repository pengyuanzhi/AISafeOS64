/**
 * @file    extra_syscalls.c
 * @brief   AISafeOS64 额外系统调用实现
 * @author  AISafe64 Team
 * @date    2026-04-30
 * @version 1.0
 *
 * @details 实现一些辅助性的系统调用功能：
 *          - 时间相关：clock_gettime, clock_getres, gettimeofday
 *          - 系统信息：uname, sysinfo
 *          - 其他：pipe2
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdint.h>
#include <string.h>
#include <stddef.h>

/* ========================================================================
 * 类型定义
 * ======================================================================== */

/** @brief time 结构体 */
struct timespec
{
    int64_t tv_sec;   /**< @brief 秒 */
    int64_t tv_nsec;  /**< @brief 纳秒 */
};

/** @brief timeval 结构体 */
struct timeval
{
    int64_t tv_sec;   /**< @brief 秒 */
    int64_t tv_usec;  /**< @brief 微秒 */
};

/** @brief utsname 结构体 */
struct utsname
{
    char sysname[65];   /**< @brief 系统名称 */
    char nodename[65];  /**< @brief 节点名称 */
    char release[65];   /**< @brief 内核版本 */
    char version[65];   /**< @brief 内核详细信息 */
    char machine[65];   /**< @brief 硬件类型 */
    char domainname[65]; /**< @brief NIS 域名 */
};

/** @brief sysinfo 结构体 */
struct sysinfo
{
    int64_t uptime;      /**< @brief 启动时间（秒） */
    uint64_t loads[3];   /**< @brief 负载平均值 */
    uint64_t totalram;   /**< @brief 总内存 */
    uint64_t freeram;    /**< @brief 空闲内存 */
    uint64_t sharedram;  /**< @brief 共享内存 */
    uint64_t bufferram;  /**< @brief 缓冲内存 */
    uint64_t totalswap;  /**< @brief 总交换空间 */
    uint64_t freeswap;   /**< @brief 空闲交换空间 */
    uint16_t procs;      /**< @brief 进程数 */
    uint64_t totalhigh;  /**< @brief 总高端内存 */
    uint64_t freehigh;   /**< @brief 空闲高端内存 */
    uint32_t mem_unit;   /**< @brief 内存单位 */
};

/* ========================================================================
 * 错误码定义
 * ======================================================================== */

#define EINVAL 22

/* ========================================================================
 * 外部内核接口声明
 * ======================================================================== */

extern int64_t aisafe_svc0(uint64_t syscall_nr);

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 安全字符串复制
 */
static void safe_strcpy(char *dst, const char *src, uint32_t n)
{
    uint32_t i;

    if ((dst == NULL) || (src == NULL) || (n == 0U))
    {
        return;
    }

    for (i = 0U; i < (n - 1U) && (src[i] != '\0'); i++)
    {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* ========================================================================
 * 时间相关系统调用
 * ======================================================================== */

/**
 * @brief 获取时钟时间
 */
long aisafe_sys_clock_gettime(long clk_id, long tp)
{
    struct timespec *tp_struct;

    (void)clk_id;  /* 暂不支持多种时钟 */

    tp_struct = (struct timespec *)tp;
    if (tp_struct == NULL)
    {
        return -EINVAL;
    }

    /* 简化实现：返回固定时间 */
    tp_struct->tv_sec = 1714500000L;
    tp_struct->tv_nsec = 0L;

    return 0;
}

/**
 * @brief 获取时钟分辨率
 */
long aisafe_sys_clock_getres(long clk_id, long res)
{
    struct timespec *res_struct;

    (void)clk_id;

    res_struct = (struct timespec *)res;
    if (res_struct == NULL)
    {
        return -EINVAL;
    }

    /* 返回 1ms 分辨率 */
    res_struct->tv_sec = 0L;
    res_struct->tv_nsec = 1000000L;

    return 0;
}

/**
 * @brief 获取当前时间
 */
long aisafe_sys_gettimeofday(long tv, long tz)
{
    struct timeval *tv_struct;

    (void)tz;  /* 暂不支持时区 */

    tv_struct = (struct timeval *)tv;
    if (tv_struct == NULL)
    {
        return -EINVAL;
    }

    /* 简化实现：返回固定时间 */
    tv_struct->tv_sec = 1714500000L;
    tv_struct->tv_usec = 0L;

    return 0;
}

/* ========================================================================
 * 系统信息相关系统调用
 * ======================================================================== */

/**
 * @brief 获取系统名称和版本信息
 */
long aisafe_sys_uname(long buf)
{
    struct utsname *uts;

    if (buf == 0L)
    {
        return -EINVAL;
    }

    uts = (struct utsname *)buf;

    safe_strcpy(uts->sysname, "AISafeOS64", 65);
    safe_strcpy(uts->nodename, "aisafe64", 65);
    safe_strcpy(uts->release, "1.0.0", 65);
    safe_strcpy(uts->version, "AISafeOS64 v1.0.0 - ARMv8-A Microkernel RTOS", 65);
    safe_strcpy(uts->machine, "aarch64", 65);
    safe_strcpy(uts->domainname, "(none)", 65);

    return 0;
}

/**
 * @brief 获取系统统计信息
 */
long aisafe_sys_sysinfo(long info)
{
    struct sysinfo *info_struct;

    if (info == 0L)
    {
        return -EINVAL;
    }

    info_struct = (struct sysinfo *)info;

    (void)memset(info_struct, 0, sizeof(struct sysinfo));

    /* 简化实现：返回固定值 */
    info_struct->uptime = 3600LL;  /* 1 小时 */
    info_struct->totalram = 1024ULL * 1024ULL;  /* 1GB */
    info_struct->freeram = 512ULL * 1024ULL;   /* 512MB */
    info_struct->mem_unit = 1U;
    info_struct->procs = 4U;

    return 0;
}

/* ========================================================================
 * 其他系统调用
 * ======================================================================== */

/**
 * @brief 创建管道
 */
long aisafe_sys_pipe2(long pipefd, long flags)
{
    (void)pipefd;
    (void)flags;

    /* 简化实现：管道暂未实现 */
    return -EINVAL;
}

/* ========================================================================
 * 资源限制相关系统调用
 * ======================================================================== */

/** @brief rlimit 结构体 */
struct rlimit
{
    int64_t rlim_cur;  /**< @brief 当前限制 */
    int64_t rlim_max;  /**< @brief 最大限制 */
};

/** @brief 获取资源限制 */
long aisafe_sys_getrlimit(long resource, long rlimit)
{
    struct rlimit *rlim;

    (void)resource;  /* 暂不支持多种资源 */

    rlim = (struct rlimit *)rlimit;
    if (rlim == NULL)
    {
        return -EINVAL;
    }

    /* 返回默认限制 */
    rlim->rlim_cur = 8192LL;  /* 8KB */
    rlim->rlim_max = 65536LL;  /* 64KB */

    return 0;
}

/** @brief 设置资源限制 */
long aisafe_sys_setrlimit(long resource, long rlimit)
{
    struct rlimit *rlim;

    (void)resource;  /* 暂不支持多种资源 */

    rlim = (struct rlimit *)rlimit;
    if (rlim == NULL)
    {
        return -EINVAL;
    }

    /* 简化实现：总是返回成功 */
    (void)rlim;
    return 0;
}
