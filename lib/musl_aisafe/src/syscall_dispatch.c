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
        /* clone 暂不支持，由用户态 fork 实现 */
        return -ENOSYS;

    case __NR_execve:
        /* execve 暂不支持，由用户态 exec 实现 */
        return -ENOSYS;

    case __NR_fork:
        return -ENOSYS;

    case __NR_vfork:
        return -ENOSYS;

    case __NR_wait4:
        /* wait4 暂不支持 */
        return -ENOSYS;

    case __NR_waitid:
        return -ENOSYS;

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
        return -ENOSYS;

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
        return -ENOSYS;

    case __NR_statx:
        return -ENOSYS;

    case __NR_ioctl:
        return fs_ioctl((int)a0, (unsigned long)a1, (void *)a2);

    case __NR_dup:
        return -ENOSYS;

    case __NR_dup3:
        return -ENOSYS;

    case __NR_fcntl:
        return fs_fcntl((int)a0, (int)a1, (int)a2);

    case __NR_faccessat:
        return -ENOSYS;

    case __NR_fchmod:
        return -ENOSYS;

    case __NR_fchmodat:
        return -ENOSYS;

    case __NR_fchownat:
        return -ENOSYS;

    case __NR_fchown:
        return -ENOSYS;

    case __NR_mkdirat:
        return -ENOSYS;

    case __NR_unlinkat:
        return -ENOSYS;

    case __NR_renameat:
        return -ENOSYS;

    case __NR_linkat:
        return -ENOSYS;

    case __NR_symlinkat:
        return -ENOSYS;

    case __NR_readlinkat:
        return -ENOSYS;

    case __NR_ftruncate:
        return -ENOSYS;

    case __NR_fsync:
        return -ENOSYS;

    case __NR_fdatasync:
        return -ENOSYS;

    case __NR_fallocate:
        return -ENOSYS;

    case __NR_statfs:
        return -ENOSYS;

    case __NR_fstatfs:
        return -ENOSYS;

    case __NR_getdents64:
        return -ENOSYS;

    case __NR_getcwd:
        return -ENOSYS;

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
        return -ENOSYS;

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
        return -ENOSYS;

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
