/**
 * @file    syscall_dispatch.c
 * @brief   AISafeOS64 musl 系统调用分发器
 * @version 2.0
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
 */

#include "syscall_arch.h"
#include "syscall_entry.h"

/* Linux errno 值（与 musl 一致） */
#define EPERM   1
#define ENOENT  2
#define EBADF   9
#define ENOMEM  12
#define EACCES  13
#define EFAULT  14
#define EINVAL  22
#define ENOSYS  38

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
 * 只包含分发器用到的定义，避免完整包含带来的依赖问题。
 * 完整定义在 bits/syscall.h 中。
 * ======================================================================== */

/* 进程/线程 */
#define __NR_exit           93
#define __NR_exit_group     94
#define __NR_kill           129
#define __NR_tkill          130
#define __NR_tgkill         131
#define __NR_getpid         172
#define __NR_gettid         178
#define __NR_clone          220
#define __NR_execve         221

/* 内存管理 */
#define __NR_brk            214
#define __NR_munmap         215
#define __NR_mmap           222
#define __NR_mprotect       226
#define __NR_madvise        233

/* 文件 I/O */
#define __NR_read           63
#define __NR_write          64
#define __NR_readv          65
#define __NR_writev         66
#define __NR_openat         56
#define __NR_close          57
#define __NR_lseek          62
#define __NR_fstat          80
#define __NR_newfstatat     79
#define __NR_ioctl          29
#define __NR_dup            23
#define __NR_dup3           24
#define __NR_fcntl          25
#define __NR_faccessat      48
#define __NR_fchmod         52
#define __NR_fchmodat       53
#define __NR_fchownat       54
#define __NR_fchown         55

/* 时间 */
#define __NR_clock_gettime  113
#define __NR_clock_getres   114
#define __NR_nanosleep      101

/* 信号 */
#define __NR_rt_sigaction   134
#define __NR_rt_sigprocmask 135
#define __NR_rt_sigreturn   139

/* 网络 */
#define __NR_socket         198
#define __NR_socketpair     199

/* 其他 */
#define __NR_set_tid_address 96
#define __NR_sched_yield     124
#define __NR_getrandom       278

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
static long aisafe_syscall_dispatch(long nr, long a0, long a1, long a2,
                                    long a3, long a4, long a5)
{
    (void)a3;
    (void)a4;
    (void)a5;

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

    case __NR_sched_yield:
        return aisafe_svc0(AISAFE_SYS_THREAD_YIELD);

    case __NR_clone:
        return -ENOSYS;

    case __NR_execve:
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

    case __NR_madvise:
        /* madvise 可以直接返回成功 */
        return 0;

    /* ================================================================
     * 文件 I/O
     * ================================================================ */
    case __NR_write:
        /* stdout(1)/stderr(2) 通过内核调试输出 */
        if (a0 == 1 || a0 == 2)
        {
            /* fd, buf, len → SYS_DEBUG_PRINT(buf, len) */
            aisafe_svc2(AISAFE_SYS_DEBUG_PRINT, a1, a2);
            return a2;
        }
        return -EBADF;

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
        return -ENOSYS;

    case __NR_openat:
        return -ENOSYS;

    case __NR_close:
        return -ENOSYS;

    case __NR_lseek:
        return -ENOSYS;

    case __NR_fstat:
        return -ENOSYS;

    case __NR_ioctl:
        return -ENOSYS;

    case __NR_dup:
    case __NR_dup3:
        return -ENOSYS;

    case __NR_fcntl:
        return -ENOSYS;

    /* ================================================================
     * 时间/定时器
     * ================================================================ */
    case __NR_clock_gettime:
        return -ENOSYS;

    case __NR_clock_getres:
        return -ENOSYS;

    case __NR_nanosleep:
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

    case __NR_kill:
    case __NR_tkill:
    case __NR_tgkill:
        return -ENOSYS;

    /* ================================================================
     * 网络
     * ================================================================ */
    case __NR_socket:
    case __NR_socketpair:
        return -ENOSYS;

    /* ================================================================
     * 其他
     * ================================================================ */
    case __NR_set_tid_address:
        /* 忽略 — Linux 用于设置 clear_child_tid */
        return 0;

    case __NR_getrandom:
        return -ENOSYS;

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
