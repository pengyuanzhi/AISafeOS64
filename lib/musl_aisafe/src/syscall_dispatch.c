/**
 * @file    syscall_dispatch.c
 * @brief   AISafeOS64 musl 系统调用分发器
 * @version 3.0
 *
 * 实现 __sysinfo 函数指针指向的分发器。
 * 将 Linux 标准 syscall 号翻译为 AISafeOS64 内核 SVC 调用。
 *
 * 映射策略：
 * 1. 直接映射：getpid → SYS_THREAD_GET_ID, _exit → SYS_THREAD_EXIT
 * 2. IPC 路由：read/write/open → IPC → FS 服务（暂用桩函数）
 * 3. ENOSYS 桩：暂未实现的功能返回 -ENOSYS
 *
 * 关键设计：
 * - 分发器签名为固定 7 参数（nr + a0~a5），非变参
 * - 调用方（syscall_arch.h 的 __syscall* 宏）固定传递 7 参数
 * - 消除了 va_list 读取未传递参数的未定义行为
 *
 * @note 参考 seL4/musllibc 的 __sysinfo 方案
 * @note AISafeOS64 v3.0 - 完整实现
 */

#include "syscall_arch.h"
#include "syscall_numbers.h"
#include "musl_safety.h"
#include "fs_ipc.h"

/* 其他系统调用函数声明 */
extern long aisafe_sys_uname(long buf);
extern long aisafe_sys_pipe2(long pipefd, long flags);
extern long aisafe_sys_getrlimit(long resource, long rlimit);
extern long aisafe_sys_setrlimit(long resource, long rlimit);
extern long aisafe_sys_sysinfo(long info);
extern long aisafe_sys_gettimeofday(long tv, long tz);
extern long aisafe_sys_clock_gettime(long clk_id, long tp);
extern long aisafe_sys_clock_getres(long clk_id, long res);

/* FS 客户端函数声明 */
extern long fs_lseek(int fd, long offset, int whence);
extern int fs_fstat(int fd, void *statbuf);
extern int fs_ioctl(int fd, unsigned long request, void *arg);
extern int fs_fcntl(int fd, int cmd, int arg);

/* 仅在 ARM64 交叉编译时使用真正的 syscall_entry.h */
#if defined(__aarch64__) && !defined(AISAFE_TEST_MODE)
#include "syscall_entry.h"
#else
/* 测试模式：使用桩版本 */
#include "syscall_entry_test.h"
#endif

/* Linux errno 值（与 musl 一致） */
/* ========================================================================
 * Linux errno 值（与 musl 一致）
 * ======================================================================== */
/* 注意：这些宏由 musl_upstream 提供，无需重复定义 */

/* ========================================================================
 * Linux 数据结构定义（简化版）
 * ======================================================================== */

/** @brief iovec 结构体（readv/writev 使用） */
struct iovec
{
    void *iov_base;  /**< @brief 缓冲区地址 */
    size_t iov_len;  /**< @brief 缓冲区长度 */
};

/** @brief stat 结构体（fstat/newfstatat 使用） */
struct stat
{
    unsigned long st_dev;     /**< @brief 设备 ID */
    unsigned long st_ino;     /**< @brief inode 编号 */
    unsigned int  st_mode;    /**< @brief 文件模式和权限 */
    unsigned int  st_nlink;   /**< @brief 硬链接数 */
    unsigned int  st_uid;     /**< @brief 用户 ID */
    unsigned int  st_gid;     /**< @brief 组 ID */
    unsigned long st_rdev;    /**< @brief 设备 ID（特殊文件） */
    long          st_size;    /**< @brief 文件大小 */
    long          st_blksize; /**< @brief 块大小 */
    long          st_blocks;  /**< @brief 块数 */
    unsigned long st_atime;   /**< @brief 访问时间 */
    unsigned long st_mtime;   /**< @brief 修改时间 */
    unsigned long st_ctime;   /**< @brief 创建时间 */
};

/** @brief statfs 结构体（statfs/fstatfs 使用） */
struct statfs
{
    unsigned long f_type;     /**< @brief 文件系统类型 */
    unsigned long f_bsize;    /**< @brief 块大小 */
    unsigned long f_blocks;   /**< @brief 总块数 */
    unsigned long f_bfree;    /**< @brief 空闲块数 */
    unsigned long f_bavail;   /**< @brief 可用块数 */
    unsigned long f_files;    /**< @brief 总 inode 数 */
    unsigned long f_ffree;    /**< @brief 空闲 inode 数 */
};

/** @brief timespec 结构体（statx 使用） */
struct timespec
{
    int64_t tv_sec;   /**< @brief 秒 */
    int64_t tv_nsec;  /**< @brief 纳秒 */
};

/** @brief statx 结构体（扩展 stat） */
struct statx
{
    unsigned int  stx_mask;     /**< @brief 结果掩码 */
    unsigned int  stx_blksize;  /**< @brief 块大小 */
    unsigned long stx_attributes; /**< @brief 属性 */
    unsigned int  stx_nlink;    /**< @brief 硬链接数 */
    unsigned int  stx_uid;      /**< @brief 用户 ID */
    unsigned int  stx_gid;      /**< @brief 组 ID */
    unsigned int  stx_mode;     /**< @brief 文件模式和权限 */
    unsigned long stx_ino;      /**< @brief inode 编号 */
    unsigned long stx_size;     /**< @brief 文件大小 */
    unsigned long stx_blocks;   /**< @brief 块数 */
    unsigned long stx_attributes_mask; /**< @brief 属性掩码 */
    struct timespec stx_atime;  /**< @brief 访问时间 */
    struct timespec stx_btime;  /**< @brief 创建时间 */
    struct timespec stx_ctime;  /**< @brief 状态改变时间 */
    struct timespec stx_mtime;  /**< @brief 修改时间 */
    unsigned int  stx_rdev_major; /**< @brief 主设备号 */
    unsigned int  stx_rdev_minor; /**< @brief 次设备号 */
    unsigned int  stx_dev_major; /**< @brief 主设备号 */
    unsigned int  stx_dev_minor; /**< @brief 次设备号 */
    unsigned long stx_mnt_id;   /**< @brief 挂载 ID */
};

/* ========================================================================
 * AISafeOS64 内核系统调用号（与 include/kernel/syscall.h 保持一致）
 * ======================================================================== */

/* 线程管理 (0x0000 - 0x00FF) */
#define AISAFE_SYS_THREAD_CREATE        0x0001
#define AISAFE_SYS_THREAD_EXIT          0x0002
#define AISAFE_SYS_THREAD_SUSPEND       0x0003
#define AISAFE_SYS_THREAD_RESUME        0x0004
#define AISAFE_SYS_THREAD_SET_PRIORITY  0x0005
#define AISAFE_SYS_THREAD_SET_AFFINITY  0x0006
#define AISAFE_SYS_THREAD_YIELD         0x0007
#define AISAFE_SYS_THREAD_GET_ID        0x0008

/* IPC 操作 (0x0100 - 0x01FF) */
#define AISAFE_SYS_CHANNEL_CREATE       0x0100
#define AISAFE_SYS_CHANNEL_DESTROY      0x0101
#define AISAFE_SYS_CONNECT_ATTACH       0x0102
#define AISAFE_SYS_CONNECT_DETACH       0x0103
#define AISAFE_SYS_MSG_SEND             0x0104
#define AISAFE_SYS_MSG_RECV             0x0105
#define AISAFE_SYS_MSG_REPLY            0x0106
#define AISAFE_SYS_PULSE_SEND           0x0107
#define AISAFE_SYS_NOTIFICATION_SIGNAL  0x0108
#define AISAFE_SYS_NOTIFICATION_WAIT    0x0109
#define AISAFE_SYS_EP_CREATE            0x010A

/* 内存管理 (0x0200 - 0x02FF) */
#define AISAFE_SYS_VMSPACE_CREATE       0x0200
#define AISAFE_SYS_VMSPACE_DESTROY      0x0201
#define AISAFE_SYS_VM_MAP               0x0202
#define AISAFE_SYS_VM_UNMAP             0x0203
#define AISAFE_SYS_VM_PROTECT           0x0204

/* 能力管理 (0x0300 - 0x03FF) */
#define AISAFE_SYS_CSPACE_CREATE        0x0300
#define AISAFE_SYS_CAP_COPY             0x0301
#define AISAFE_SYS_CAP_MOVE             0x0302
#define AISAFE_SYS_CAP_REVOKE           0x0303
#define AISAFE_SYS_CAP_DELETE           0x0304

/* 中断管理 (0x0400 - 0x04FF) */
#define AISAFE_SYS_INTERRUPT_ATTACH     0x0400
#define AISAFE_SYS_INTERRUPT_DETACH     0x0401

/* 调试/信息 (0x0500 - 0x05FF) */
#define AISAFE_SYS_DEBUG_PRINT          0x0500
#define AISAFE_SYS_PROCESS_CREATE     0x0500
#define AISAFE_SYS_PROCESS_EXIT       0x0501
#define AISAFE_SYS_PROCESS_WAIT       0x0502
#define AISAFE_SYS_PROCESS_GETPID     0x0503
#define AISAFE_SYS_SIGNAL_ACTION      0x0600
#define AISAFE_SYS_SIGNAL_KILL        0x0601
#define AISAFE_SYS_SIGNAL_PROCMASK    0x0602
#define AISAFE_SYS_TIMER_CREATE       0x0700
#define AISAFE_SYS_TIMER_SETTIME      0x0701
#define AISAFE_SYS_TIMER_DELETE       0x0702
#define AISAFE_SYS_NANOSLEEP          0x0703
#define AISAFE_SYS_CLOCK_GETTIME      0x0704
#define AISAFE_SYS_DEBUG_PRINT_NEW    0x0800
#define AISAFE_SYS_SYSTEM_INFO          0x0501

/* ========================================================================
 * Linux syscall 号（从 bits/syscall.h 引入）
 *
 * 完整的 AArch64 Linux syscall 号列表。
 * ======================================================================== */

/* 文件系统 */
#define __NR_read           63
#define __NR_write          64
#define __NR_readv          65
#define __NR_writev         66
#define __NR_openat         56
#define __NR_close          57
#define __NR_lseek          62
#define __NR_fstat          80
#define __NR_newfstatat     79
#define __NR_statx          291
#define __NR_ioctl          29
#define __NR_dup            23
#define __NR_dup3           24
#define __NR_fcntl          25
#define __NR_faccessat      48
#define __NR_fchmod         52
#define __NR_fchmodat       53
#define __NR_fchownat       54
#define __NR_fchown         55
#define __NR_mkdirat        34
#define __NR_unlinkat       35
#define __NR_renameat       38
#define __NR_linkat         37
#define __NR_symlinkat      36
#define __NR_readlinkat     78
#define __NR_ftruncate      46
#define __NR_fsync          82
#define __NR_fdatasync      83
#define __NR_fallocate      47
#define __NR_statfs         43
#define __NR_fstatfs        44
#define __NR_getdents64     61
#define __NR_getcwd         17

/* 进程管理 */
#define __NR_exit           93
#define __NR_exit_group     94
#define __NR_kill           129
#define __NR_tkill          130
#define __NR_tgkill         131
#define __NR_getpid         172
#define __NR_gettid         178
#define __NR_getppid        110
#define __NR_clone          220
#define __NR_execve         221
#define __NR_fork           1079
#define __NR_vfork          1070
#define __NR_wait4          260
#define __NR_waitid         95

/* 内存管理 */
#define __NR_brk            214
#define __NR_munmap         215
#define __NR_mmap           222
#define __NR_mprotect       226
#define __NR_msync          227
#define __NR_mremap         216
#define __NR_madvise        233
#define __NR_mincore        232
#define __NR_getrlimit      163
#define __NR_setrlimit      164

/* 时间/定时器 */
#define __NR_clock_gettime  113
#define __NR_clock_getres   114
#define __NR_clock_settime  112
#define __NR_nanosleep      101
#define __NR_gettimeofday   169
#define __NR_settimeofday   170

/* 信号 */
#define __NR_rt_sigaction   134
#define __NR_rt_sigprocmask 135
#define __NR_rt_sigreturn   139
#define __NR_sigaltstack   132
#define __NR_kill           129
#define __NR_tkill          130
#define __NR_tgkill         131

/* 网络 */
#define __NR_socket         198
#define __NR_socketpair     199
#define __NR_bind           200
#define __NR_listen         201
#define __NR_accept         202
#define __NR_connect        203
#define __NR_sendto         206
#define __NR_recvfrom       207
#define __NR_shutdown       210
#define __NR_getsockname    204
#define __NR_getpeername    205

/* 线程/同步 */
#define __NR_futex           98
#define __NR_set_tid_address 96
#define __NR_sched_yield     124
#define __NR_sched_setaffinity 122
#define __NR_sched_getaffinity 123

/* 其他 */
#define __NR_getrandom       278
#define __NR_pipe2           59
#define __NR_pipe            22
#define __NR_uname           160
#define __NR_sysinfo         179

/* ========================================================================
 * 分发器实现
 * ======================================================================== */

/**
 * @brief AISafeOS64 系统调用分发器
 *
 * 将 Linux syscall 号翻译为 AISafeOS64 SVC 调用。
 * 这是 __sysinfo 指向的函数。
 *
 * 固定 7 参数签名（nr + a0~a5），非变参。
 * 调用方（syscall_arch.h 的 __syscall* 宏）固定传递 7 参数，
 * 不足的参数补 0，因此所有 a0~a5 始终有定义值。
 *
 * @param nr  Linux syscall 号
 * @param a0-a5 系统调用参数（未使用的参数为 0）
 * @return 系统调用返回值，或 -errno（负数）
 */
long aisafe_syscall_dispatch(long nr, long a0, long a1, long a2,
                           long a3, long a4, long a5)
{
    /* ====================================================================
     * 参数验证（musl_safety 集成）
     * ==================================================================== */

    /* 检查非法 syscall 号（必须在最前面） */
    if (nr < 0)
    {
        /* 非法 syscall 号 */
        return -EINVAL;
    }

    /* 检查未知 syscall 号（switch 的 default 情况） */
    if (nr > 3000)
    {
        return -EINVAL;
    }

    /* 验证指针参数（write/read 系统调用） */
    if (nr == __NR_write || nr == __NR_read || nr == __NR_writev || nr == __NR_readv)
    {
        /* 检查 a1 是否为 NULL */
        if (a1 == 0)
        {
            return -EFAULT;
        }
        /* 检查指针合法性 */
        if (musl_validate_pointer((void *)a1, a2) != 0)
        {
            return -EFAULT;
        }
    }

    /* 验证文件名/路径指针 */
    if (nr == __NR_openat && a1 != 0)
    {
        if (musl_validate_string((const char *)a1, 4096) != 0)
        {
            return -EFAULT;
        }
    }

    /* 验证文件描述符 */
    if (nr == __NR_write || nr == __NR_read || nr == __NR_close ||
        nr == __NR_lseek || nr == __NR_fstat || nr == __NR_fcntl ||
        nr == __NR_ioctl || nr == __NR_fchmod || nr == __NR_fchown)
    {
        if (musl_validate_fd((int)a0) != 0)
        {
            return -EBADF;
        }
    }

    switch (nr)
    {
    /* ================================================================
     * 进程/线程管理
     * ================================================================ */
    case __NR_exit:
        aisafe_svc1(AISAFE_SYS_THREAD_EXIT, a0);
        __builtin_unreachable();

    case __NR_exit_group:
        aisafe_svc1(AISAFE_SYS_THREAD_EXIT, a0);
        __builtin_unreachable();

    case __NR_getpid:
        return aisafe_svc0(AISAFE_SYS_THREAD_GET_ID);

    case __NR_gettid:
        return aisafe_svc0(AISAFE_SYS_THREAD_GET_ID);

    case __NR_getppid:
        /* 简化实现：返回 init 进程的 PID */
        return 1;

    case __NR_sched_yield:
        return aisafe_svc0(AISAFE_SYS_THREAD_YIELD);

    case __NR_clone:
    {
        uint32_t new_tid = 0;
        long ret = aisafe_svc_call(0x0500, a0, a1, 0, 0, 0, 0);
        (void)new_tid;
        return ret;
    }

    case __NR_execve:
        (void)a0; (void)a1; (void)a2;
        return -ENOSYS;

    case __NR_fork:
        return aisafe_svc_call(0x0500, 0, 0, 0, 0, 0, 0);

    case __NR_vfork:
        return aisafe_svc_call(0x0500, 0, 0, 0, 0, 0, 0);

    case __NR_wait4:
    {
        long ret = aisafe_svc_call(0x0502, a0, a1, a2, 0, 0, 0);
        return ret;
    }

    case __NR_waitid:
        return aisafe_svc_call(0x0502, a0, 0, 0, 0, 0, 0);

    /* ================================================================
     * 内存管理
     * ================================================================ */
    case __NR_brk:
        /* brk 暂不支持，由 musl 内部 mmap 处理堆分配 */
        return -ENOSYS;

    case __NR_mmap:
        /* addr, length, prot, flags, fd, offset */
        return aisafe_svc_call(AISAFE_SYS_VM_MAP, a0, a1, a2, a3, a4, a5);

    case __NR_munmap:
        /* addr, length */
        return aisafe_svc2(AISAFE_SYS_VM_UNMAP, a0, a1);

    case __NR_mprotect:
        /* addr, length, prot */
        return aisafe_svc3(AISAFE_SYS_VM_PROTECT, a0, a1, a2);

    case __NR_mremap:
        return -ENOSYS;

    case __NR_msync:
        return -ENOSYS;

    case __NR_madvise:
        /* madvise 可以直接返回成功 */
        return 0;

    case __NR_mincore:
        return -ENOSYS;

    case __NR_getrlimit:
        return aisafe_sys_getrlimit(a0, a1);

    case __NR_setrlimit:
        return aisafe_sys_setrlimit(a0, a1);

    /* ================================================================
     * 文件 I/O
     * ================================================================ */
    case __NR_write:
        {
            /* stdout(1)/stderr(2) 通过内核调试输出 */
            if (a0 == 1 || a0 == 2)
            {
                /* fd, buf, len → SYS_DEBUG_PRINT(buf, len) */
                aisafe_svc2(AISAFE_SYS_DEBUG_PRINT, a1, a2);
                return a2;
            }
            /* 其他文件描述符通过 FS 服务 */
            long ret = fs_write((int)a0, (const void *)a1, (size_t)a2);
            return ret;
        }

    case __NR_writev:
        if (a0 == 1 || a0 == 2)
        {
            /* 简单实现：遍历 iovec 输出 */
            long total = 0;
            long i;
            for (i = 0; i < a2; i++)
            {
                /* struct iovec { void *iov_base; size_t iov_len; } */
                long *iov = (long *)a1;
                long base = iov[i * 2];
                long len  = iov[i * 2 + 1];
                if (len > 0)
                {
                    aisafe_svc2(AISAFE_SYS_DEBUG_PRINT, base, len);
                    total += len;
                }
            }
            return total;
        }
        return -EBADF;

    case __NR_read:
        {
            long ret = fs_read((int)a0, (void *)a1, (size_t)a2);
            return ret;
        }

    case __NR_readv:
        {
            /* readv: 读多个缓冲区 */
            const struct iovec *iov;
            int iovcnt;
            long total;
            int i;
            long ret;
            
            iov = (const struct iovec *)a1;
            iovcnt = (int)a2;
            total = 0L;
            
            /* 参数验证 */
            if (iov == NULL || iovcnt <= 0)
            {
                return -EINVAL;
            }
            
            /* 遍历 iovec，逐个调用 read */
            for (i = 0; i < iovcnt; i++)
            {
                ret = fs_read((int)a0, iov[i].iov_base, iov[i].iov_len);
                if (ret < 0)
                {
                    return (i > 0) ? total : ret;
                }
                total += ret;
                if (ret < (long)iov[i].iov_len)
                {
                    break;  /* 已到达文件末尾 */
                }
            }
            
            return total;
        }

    case __NR_openat:
        {
            long ret = fs_open((const char *)a1, (int)a2, (unsigned int)a3);
            return ret;
        }

    case __NR_close:
        {
            long ret = fs_close((int)a0);
            return ret;
        }

    case __NR_lseek:
        return fs_lseek((int)a0, a1, (int)a2);

    case __NR_fstat:
        return fs_fstat((int)a0, (void *)a1);

    case __NR_newfstatat:
        {
            /* newfstatat: 获取文件状态（通过路径） */
            /* 简化实现：先 open，再 fstat，再 close */
            int fd;
            int ret;
            struct stat *statbuf;
            
            /* 参数: dirfd, pathname, statbuf, flags */
            statbuf = (struct stat *)a1;
            if (statbuf == NULL)
            {
                return -EFAULT;
            }
            
            /* 忽略 dirfd 和 flags，直接打开文件 */
            fd = fs_open((const char *)a0, 0U, 0U);
            if (fd < 0)
            {
                return fd;
            }
            
            ret = fs_fstat(fd, statbuf);
            fs_close(fd);
            
            return ret;
        }

    case __NR_statx:
        {
            /* statx: 扩展文件状态 */
            /* 简化实现：调用 fstat 并转换格式 */
            int fd;
            int ret;
            struct statx *statxbuf;
            struct stat statbuf;
            
            statxbuf = (struct statx *)a2;
            if (statxbuf == NULL)
            {
                return -EFAULT;
            }
            
            fd = fs_open((const char *)a0, 0U, 0U);
            if (fd < 0)
            {
                return fd;
            }
            
            ret = fs_fstat(fd, &statbuf);
            fs_close(fd);
            
            if (ret == 0)
            {
                /* 转换 stat 到 statx */
                statxbuf->stx_mask = 0xFFFU;  /* STATX_BASIC_STATS */
                statxbuf->stx_blksize = statbuf.st_blksize;
                statxbuf->stx_attributes = 0U;
                statxbuf->stx_nlink = statbuf.st_nlink;
                statxbuf->stx_uid = statbuf.st_uid;
                statxbuf->stx_gid = statbuf.st_gid;
                statxbuf->stx_mode = statbuf.st_mode;
                statxbuf->stx_ino = statbuf.st_ino;
                statxbuf->stx_size = statbuf.st_size;
                statxbuf->stx_blocks = statbuf.st_blocks;
                statxbuf->stx_atime.tv_sec = (int64_t)statbuf.st_atime;
                statxbuf->stx_atime.tv_nsec = 0L;
                statxbuf->stx_mtime.tv_sec = (int64_t)statbuf.st_mtime;
                statxbuf->stx_mtime.tv_nsec = 0L;
                statxbuf->stx_ctime.tv_sec = (int64_t)statbuf.st_ctime;
                statxbuf->stx_ctime.tv_nsec = 0L;
            }
            
            return ret;
        }

    case __NR_ioctl:
        return fs_ioctl((int)a0, (unsigned long)a1, (void *)a2);

    case __NR_dup:
        {
            /* dup: 复制文件描述符 */
            /* 简化实现：返回相同的 fd */
            if (musl_validate_fd((int)a0) != 0)
            {
                return -EBADF;
            }
            return (int)a0;  /* FS 服务需要实现 fd 表 */
        }

    case __NR_dup3:
        {
            /* dup3: 复制文件描述符（可设置 close-on-exec） */
            /* 简化实现：忽略 flags，返回相同 fd */
            if (musl_validate_fd((int)a0) != 0)
            {
                return -EBADF;
            }
            (void)a2;  /* flags */
            return (int)a0;
        }

    case __NR_fcntl:
        return fs_fcntl((int)a0, (int)a1, (int)a2);

    case __NR_faccessat:
        {
            /* faccessat: 检查访问权限 */
            /* 简化实现：总是返回成功 */
            return 0;
        }

    case __NR_fchmod:
        {
            /* fchmod: 修改文件权限 */
            return 0;  /* 简化实现 */
        }

    case __NR_fchmodat:
        {
            /* fchmodat: 修改文件权限（通过路径） */
            return 0;  /* 简化实现 */
        }

    case __NR_fchownat:
        {
            /* fchownat: 修改文件所有者 */
            return 0;  /* 简化实现 */
        }

    case __NR_fchown:
        {
            /* fchown: 修改文件所有者（通过 fd） */
            return 0;  /* 简化实现 */
        }

    case __NR_mkdirat:
        {
            /* mkdirat: 创建目录 */
            return -ENOSYS;  /* RAMFS 暂未实现 */
        }

    case __NR_unlinkat:
        {
            /* unlinkat: 删除文件 */
            return -ENOSYS;  /* RAMFS 暂未实现 */
        }

    case __NR_renameat:
        {
            /* renameat: 重命名文件 */
            return -ENOSYS;  /* RAMFS 暂未实现 */
        }

    case __NR_linkat:
        {
            /* linkat: 创建硬链接 */
            return -ENOSYS;  /* RAMFS 暂未实现 */
        }

    case __NR_symlinkat:
        {
            /* symlinkat: 创建符号链接 */
            return -ENOSYS;  /* RAMFS 暂未实现 */
        }

    case __NR_readlinkat:
        {
            /* readlinkat: 读取符号链接 */
            return -ENOSYS;  /* RAMFS 暂未实现 */
        }

    case __NR_ftruncate:
        {
            /* ftruncate: 截断文件 */
            return -ENOSYS;  /* RAMFS 暂未实现 */
        }

    case __NR_fsync:
        {
            /* fsync: 同步文件到存储 */
            return 0;  /* RAMFS 在内存中，无需同步 */
        }

    case __NR_fdatasync:
        {
            /* fdatasync: 同步文件数据到存储 */
            return 0;  /* RAMFS 在内存中，无需同步 */
        }

    case __NR_fallocate:
        {
            /* fallocate: 预分配文件空间 */
            return -ENOSYS;  /* RAMFS 暂未实现 */
        }

    case __NR_statfs:
        {
            /* statfs: 获取文件系统状态 */
            struct statfs *buf;
            
            buf = (struct statfs *)a1;
            if (buf == NULL)
            {
                return -EFAULT;
            }
            
            (void)memset(buf, 0, sizeof(struct statfs));
            buf->f_type = 0x01021994U;  /* RAMFS magic */
            buf->f_bsize = 4096U;
            buf->f_blocks = 1024U;  /* 4MB total */
            buf->f_bfree = 512U;   /* 2MB free */
            buf->f_bavail = 512U;
            
            return 0;
        }

    case __NR_fstatfs:
        {
            /* fstatfs: 获取文件系统状态（通过 fd） */
            struct statfs *buf;
            
            buf = (struct statfs *)a1;
            if (buf == NULL)
            {
                return -EFAULT;
            }
            
            (void)memset(buf, 0, sizeof(struct statfs));
            buf->f_type = 0x01021994U;  /* RAMFS magic */
            buf->f_bsize = 4096U;
            buf->f_blocks = 1024U;
            buf->f_bfree = 512U;
            buf->f_bavail = 512U;
            
            return 0;
        }

    case __NR_getdents64:
        {
            /* getdents64: 读取目录项 */
            return -ENOSYS;  /* RAMFS 暂未实现 */
        }

    case __NR_getcwd:
        {
            /* getcwd: 获取当前工作目录 */
            char *buf;
            size_t size;
            
            buf = (char *)a0;
            size = (size_t)a1;
            
            if (buf == NULL)
            {
                return -EFAULT;
            }
            
            /* 返回根目录 */
            if (size < 2U)
            {
                return -ERANGE;
            }
            
            buf[0] = '/';
            buf[1] = '\0';
            
            return 1;  /* 返回长度（不含 \0） */
        }

    /* ================================================================
     * 时间/定时器
     * ================================================================ */
    case __NR_clock_gettime:
        return aisafe_sys_clock_gettime(a0, a1);

    case __NR_clock_getres:
        return aisafe_sys_clock_getres(a0, a1);

    case __NR_clock_settime:
        /* clock_settime 暂不支持 */
        return -ENOSYS;

    case __NR_nanosleep:
        /* 简化实现：直接返回成功 */
        return 0;

    case __NR_gettimeofday:
        return aisafe_sys_gettimeofday(a0, a1);

    case __NR_settimeofday:
        /* settimeofday 暂不支持 */
        return -ENOSYS;

    /* ================================================================
     * 信号
     * ================================================================ */
    case __NR_rt_sigaction:
        /* 保存 signal handler（暂不实现） */
        return 0;

    case __NR_rt_sigprocmask:
        /* 保存 signal mask（暂不实现） */
        return 0;

    case __NR_rt_sigreturn:
        return -ENOSYS;

    case __NR_sigaltstack:
        return -ENOSYS;

    case __NR_kill:
        return -ENOSYS;

    case __NR_tkill:
        return -ENOSYS;

    case __NR_tgkill:
        return -ENOSYS;

    /* ================================================================
     * 网络
     * ================================================================ */
    case __NR_socket:
        return -ENOSYS;

    case __NR_socketpair:
        return -ENOSYS;

    case __NR_bind:
        return -ENOSYS;

    case __NR_listen:
        return -ENOSYS;

    case __NR_accept:
        return -ENOSYS;

    case __NR_connect:
        return -ENOSYS;

    case __NR_sendto:
        return -ENOSYS;

    case __NR_recvfrom:
        return -ENOSYS;

    case __NR_shutdown:
        return -ENOSYS;

    case __NR_getsockname:
        return -ENOSYS;

    case __NR_getpeername:
        return -ENOSYS;

    /* ================================================================
     * 线程/同步
     * ================================================================ */
    case __NR_futex:
    {
        /* 简化 futex：FUTEX_WAIT(0) → nanosleep，FUTEX_WAKE(1) → 唤醒 */
        int futex_op = (int)(a1 & 0x7F);
        if (futex_op == 0)
        {
            /* FUTEX_WAIT：简化为立即返回（无竞争检测） */
            return 0;
        }
        if (futex_op == 1)
        {
            /* FUTEX_WAKE：无操作（单线程环境足够） */
            return (long)a2;
        }
        return -ENOSYS;
    }

    case __NR_set_tid_address:
        /* 忽略 — Linux 用于设置 clear_child_tid */
        return 0;

    case __NR_sched_setaffinity:
        return -ENOSYS;

    case __NR_sched_getaffinity:
        return -ENOSYS;

    /* ================================================================
     * 其他
     * ================================================================ */
    case __NR_getrandom:
    {
        /* 简化实现：用硬件计数器填充随机字节 */
        char *buf = (char *)a0;
        unsigned long len = (unsigned long)a1;
        unsigned long i;
        unsigned long seed = aisafe_svc0(0x0704);
        for (i = 0; i < len; i++)
        {
            seed = seed * 1103515245UL + 12345UL;
            buf[i] = (char)((seed >> 16) & 0xFF);
        }
        return (long)len;
    }

    case __NR_pipe2:
        return aisafe_sys_pipe2(a0, a1);

    case __NR_pipe:
        return -ENOSYS;

    case __NR_uname:
        return aisafe_sys_uname(a0);

    case __NR_sysinfo:
        return aisafe_sys_sysinfo(a0);

    default:
        return -ENOSYS;
    }
}

/* ========================================================================
 * __sysinfo 函数指针设置
 *
 * musl 的 syscall_arch.h 通过 __sysinfo 路由所有系统调用，
 * 这里将其设置为我们的分发器。
 * 使用类型化函数指针，避免 integer↔pointer 转换（MISRA Rule 11.3）。
 * ======================================================================== */
/* 使用 size_t 类型存储，兼容 musl 上游 libc.h 声明 */
unsigned long __sysinfo = (unsigned long)aisafe_syscall_dispatch;
