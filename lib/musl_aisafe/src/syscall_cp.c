/**
 * @file    syscall_cp.c
 * @brief   AISafeOS64 musl 适配层 — 取消点系统调用包装
 * @version 1.0
 *
 * @details 实现 musl 的线程取消机制所需的 __syscall_cp 函数。
 *          AISafeOS64 不支持线程取消，因此简化实现。
 *
 * @note MISRA-C:2012 合规
 */

#include "syscall_arch.h"
#include <stdint.h>

/* ========================================================================
 * __syscall_cp 实现
 * ======================================================================== */

/**
 * @brief 带取消点的系统调用
 *
 * @param cancelptr 取消标志指针（AISafeOS64 不使用）
 * @param nr 系统调用号
 * @param a0-a5 参数
 *
 * @return 系统调用返回值
 *
 * @note AISafeOS64 不支持线程取消，因此直接调用底层系统调用
 * @note 参考 musl 的 __syscall_cp 实现
 */
long __syscall_cp(volatile long *cancelptr, long nr,
                 long a0, long a1, long a2,
                 long a3, long a4, long a5)
{
    (void)cancelptr;  /* AISafeOS64 不支持取消 */

    /* 直接调用底层系统调用 */
    return __syscall6(nr, a0, a1, a2, a3, a4, a5);
}
