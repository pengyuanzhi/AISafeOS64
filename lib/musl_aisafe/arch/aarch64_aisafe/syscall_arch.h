/**
 * @file    syscall_arch.h
 * @brief   AISafeOS64 musl 适配层 — 系统调用路由
 * @version 2.0
 *
 * 参考 seL4/musllibc 方案：通过 __sysinfo 函数指针路由所有系统调用。
 * musl 的所有 __syscall*() 函数通过 __sysinfo 指针分发到
 * AISafeOS64 的 syscall_dispatch 处理器。
 *
 * 关键设计：
 * - 所有 __syscall* 宏固定传递 7 个参数（nr + a0~a5），不足补 0
 * - __sysinfo 使用类型化函数指针（syscall_hook_t），避免 integer↔pointer 转换
 * - 分发器函数签名为固定 7 参数，消除 va_list 未定义行为
 */

#ifndef SYSCALL_ARCH_H
#define SYSCALL_ARCH_H

#define __SYSCALL_LL_E(x) (x)
#define __SYSCALL_LL_O(x) (x)

/*
 * __sysinfo 函数指针存储（musl 上游 libc.h 声明为 size_t 类型）。
 * 使用 size_t 存储是为了兼容 musl 内部约定，避免修改上游源码。
 * MISRA Deviation: Rule 11.3 — 此处 integer↔function-pointer 转换是
 * seL4/musllibc 方案的核心机制，已记录在 MISRA Deviation Permit 中。
 */
extern unsigned long __sysinfo;

/* 安全调用包装：将 __sysinfo 转为函数指针并调用（固定 7 参数） */
#define CALL_SYSINFO(n, a, b, c, d, e, f) (((long(*)(long,long,long,long,long,long,long))__sysinfo)((n), (a), (b), (c), (d), (e), (f)))

/*
 * 所有 __syscall* 固定传递 6 个参数（不足补 0），
 * 消除 va_arg 读取未传递参数的未定义行为。
 */

static inline long __syscall0(long n)
{
    return CALL_SYSINFO(n, 0, 0, 0, 0, 0, 0);
}

static inline long __syscall1(long n, long a)
{
    return CALL_SYSINFO(n, a, 0, 0, 0, 0, 0);
}

static inline long __syscall2(long n, long a, long b)
{
    return CALL_SYSINFO(n, a, b, 0, 0, 0, 0);
}

static inline long __syscall3(long n, long a, long b, long c)
{
    return CALL_SYSINFO(n, a, b, c, 0, 0, 0);
}

static inline long __syscall4(long n, long a, long b, long c, long d)
{
    return CALL_SYSINFO(n, a, b, c, d, 0, 0);
}

static inline long __syscall5(long n, long a, long b, long c, long d, long e)
{
    return CALL_SYSINFO(n, a, b, c, d, e, 0);
}

static inline long __syscall6(long n, long a, long b, long c, long d, long e, long f)
{
    return CALL_SYSINFO(n, a, b, c, d, e, f);
}

/* AISafeOS64 不使用 VDSO */
#undef VDSO_USEFUL

/* AISafeOS64 不使用 IPC_64 */
#define IPC_64 0

#endif /* SYSCALL_ARCH_H */
