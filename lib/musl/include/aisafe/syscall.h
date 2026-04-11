/**
 * @file    aisafe/syscall.h
 * @brief   统一系统调用映射
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 将 AISafeOS64 内核系统调用号映射为 musl libc 可用的接口。
 *          用户态服务通过此头文件访问底层系统调用桩函数。
 *
 *          在宿主机测试环境中，系统调用桩函数不可用，
 *          此头文件仅做声明，不提供实现。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef AISAFE_SYSCALL_H
#define AISAFE_SYSCALL_H

#include <sys/types.h>

/* ========================================================================
 * 系统调用号映射（与 include/kernel/syscall.h 保持一致）
 * ======================================================================== */

/* 线程管理 */
#define SYS_THREAD_CREATE        0x0001U
#define SYS_THREAD_EXIT          0x0002U
#define SYS_THREAD_YIELD         0x0007U
#define SYS_THREAD_GET_ID        0x0008U

/* IPC 操作 */
#define SYS_MSG_SEND             0x0104U
#define SYS_MSG_RECV             0x0105U
#define SYS_MSG_REPLY            0x0106U
#define SYS_DEBUG_PRINT          0x0500U

/* ========================================================================
 * 系统调用桩函数声明（ARM64 内联汇编实现）
 * ======================================================================== */

/**
 * @brief 0 参数系统调用
 * @param nr 系统调用号
 * @return 返回值
 */
int64_t syscall0(uint32_t nr);

/**
 * @brief 1 参数系统调用
 * @param nr 系统调用号
 * @param a0 参数 0
 * @return 返回值
 */
int64_t syscall1(uint32_t nr, uint64_t a0);

/**
 * @brief 2 参数系统调用
 * @param nr 系统调用号
 * @param a0 参数 0
 * @param a1 参数 1
 * @return 返回值
 */
int64_t syscall2(uint32_t nr, uint64_t a0, uint64_t a1);

/**
 * @brief 3 参数系统调用
 * @param nr 系统调用号
 * @param a0 参数 0
 * @param a1 参数 1
 * @param a2 参数 2
 * @return 返回值
 */
int64_t syscall3(uint32_t nr, uint64_t a0, uint64_t a1, uint64_t a2);

#endif /* AISAFE_SYSCALL_H */
