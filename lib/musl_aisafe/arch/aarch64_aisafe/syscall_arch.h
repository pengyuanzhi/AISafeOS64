/**
 * @file    syscall_arch.h
 * @brief   AISafeOS64 musl 适配层 — 系统调用路由
 * @version 1.0
 *
 * 参考 seL4/musllibc 方案：通过 __sysinfo 函数指针路由所有系统调用。
 * musl 的所有 __syscall*() 函数通过 __sysinfo 指针分发到
 * AISafeOS64 的 syscall_dispatch 处理器。
 *
 * 标准的 musl 使用 ARM64 svc 0 指令直接发起 Linux 系统调用，
 * 我们替换为通过函数指针调用 AISafeOS64 的分发器。
 */

#define __SYSCALL_LL_E(x) (x)
#define __SYSCALL_LL_O(x) (x)

/* __sysinfo 函数指针，在 syscall_dispatch.c 中设置 */
extern unsigned long __sysinfo;

/* 通过 __sysinfo 路由系统调用 */
#define CALL_SYSINFO(n, ...) ((long(*)(long,...))__sysinfo)(n, ##__VA_ARGS__)

static inline long __syscall0(long n)
{
    return CALL_SYSINFO(n);
}

static inline long __syscall1(long n, long a)
{
    return CALL_SYSINFO(n, a);
}

static inline long __syscall2(long n, long a, long b)
{
    return CALL_SYSINFO(n, a, b);
}

static inline long __syscall3(long n, long a, long b, long c)
{
    return CALL_SYSINFO(n, a, b, c);
}

static inline long __syscall4(long n, long a, long b, long c, long d)
{
    return CALL_SYSINFO(n, a, b, c, d);
}

static inline long __syscall5(long n, long a, long b, long c, long d, long e)
{
    return CALL_SYSINFO(n, a, b, c, d, e);
}

static inline long __syscall6(long n, long a, long b, long c, long d, long e, long f)
{
    return CALL_SYSINFO(n, a, b, c, d, e, f);
}

/* AISafeOS64 不使用 VDSO */
#undef VDSO_USEFUL

/* AISafeOS64 不使用 IPC_64 */
#define IPC_64 0
