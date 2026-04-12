/**
 * @file    syscall_entry.h
 * @brief   AISafeOS64 ARM64 SVC 调用桩函数
 * @version 1.0
 *
 * 提供 ARM64 架构相关的 SVC 系统调用入口。
 * 仅在 ARM64 交叉编译时使用。
 *
 * AISafeOS64 内核 SVC 调用约定（ARM64）：
 *   x8 = syscall number, x0-x5 = arguments, x0 = return value
 */

#ifndef SYSCALL_ENTRY_H
#define SYSCALL_ENTRY_H

/**
 * @brief 通用 SVC 调用（6 参数）
 * @param nr   系统调用号
 * @param a0-a5 参数
 * @return 系统调用返回值
 */
static inline long aisafe_svc_call(long nr, long a0, long a1, long a2,
                                    long a3, long a4, long a5)
{
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x3 __asm__("x3") = a3;
    register long x4 __asm__("x4") = a4;
    register long x5 __asm__("x5") = a5;
    __asm__ __volatile__("svc #0"
        : "=r"(x0)
        : "r"(x8), "0"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
        : "memory", "cc");
    return x0;
}

/** @brief 0 参数 SVC 调用 */
static inline long aisafe_svc0(long nr)
{
    return aisafe_svc_call(nr, 0, 0, 0, 0, 0, 0);
}

/** @brief 1 参数 SVC 调用 */
static inline long aisafe_svc1(long nr, long a0)
{
    return aisafe_svc_call(nr, a0, 0, 0, 0, 0, 0);
}

/** @brief 2 参数 SVC 调用 */
static inline long aisafe_svc2(long nr, long a0, long a1)
{
    return aisafe_svc_call(nr, a0, a1, 0, 0, 0, 0);
}

/** @brief 3 参数 SVC 调用 */
static inline long aisafe_svc3(long nr, long a0, long a1, long a2)
{
    return aisafe_svc_call(nr, a0, a1, a2, 0, 0, 0);
}

#endif /* SYSCALL_ENTRY_H */
