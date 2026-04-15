/**
 * @file    syscall_numbers.h
 * @brief   Linux syscall 号定义（从 syscall_dispatch.c 提取）
 * @version 1.0
 *
 * 供测试和分发器使用。
 */

#ifndef SYSCALL_NUMBERS_H
#define SYSCALL_NUMBERS_H

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

#endif /* SYSCALL_NUMBERS_H */
