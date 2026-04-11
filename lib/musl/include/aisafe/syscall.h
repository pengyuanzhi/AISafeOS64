/**
 * @file    aisafe/syscall.h
 * @brief   统一系统调用映射
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 2.0
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
#include <stdint.h>

/* ========================================================================
 * 系统调用号映射（与 include/kernel/syscall.h 保持一致）
 * ======================================================================== */

/* --- 线程管理（0x0000 - 0x00FF） --- */
#define SYS_THREAD_CREATE        0x0001U
#define SYS_THREAD_EXIT          0x0002U
#define SYS_THREAD_SUSPEND       0x0003U
#define SYS_THREAD_RESUME        0x0004U
#define SYS_THREAD_SET_PRIORITY  0x0005U
#define SYS_THREAD_SET_AFFINITY  0x0006U
#define SYS_THREAD_YIELD         0x0007U
#define SYS_THREAD_GET_ID        0x0008U

/* --- IPC 操作（0x0100 - 0x01FF） --- */
#define SYS_CHANNEL_CREATE       0x0100U
#define SYS_CHANNEL_DESTROY      0x0101U
#define SYS_CONNECT_ATTACH       0x0102U
#define SYS_CONNECT_DETACH       0x0103U
#define SYS_MSG_SEND             0x0104U
#define SYS_MSG_RECV             0x0105U
#define SYS_MSG_REPLY            0x0106U
#define SYS_PULSE_SEND           0x0107U
#define SYS_NOTIFICATION_SIGNAL  0x0108U
#define SYS_NOTIFICATION_WAIT    0x0109U
#define SYS_EP_CREATE            0x010AU

/* --- 内存管理（0x0200 - 0x02FF） --- */
#define SYS_VMSPACE_CREATE       0x0200U
#define SYS_VMSPACE_DESTROY      0x0201U
#define SYS_VM_MAP               0x0202U
#define SYS_VM_UNMAP             0x0203U
#define SYS_VM_PROTECT           0x0204U

/* --- 能力管理（0x0300 - 0x03FF） --- */
#define SYS_CSPACE_CREATE        0x0300U
#define SYS_CAP_COPY             0x0301U
#define SYS_CAP_MOVE             0x0302U
#define SYS_CAP_REVOKE           0x0303U
#define SYS_CAP_DELETE           0x0304U

/* --- 中断管理（0x0400 - 0x04FF） --- */
#define SYS_INTERRUPT_ATTACH     0x0400U
#define SYS_INTERRUPT_DETACH     0x0401U

/* --- 调试/信息（0x0500 - 0x05FF） --- */
#define SYS_DEBUG_PRINT          0x0500U
#define SYS_SYSTEM_INFO          0x0501U

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

/**
 * @brief 5 参数系统调用
 * @param nr 系统调用号
 * @param a0 参数 0
 * @param a1 参数 1
 * @param a2 参数 2
 * @param a3 参数 3
 * @param a4 参数 4
 * @return 返回值
 */
int64_t syscall5(uint32_t nr, uint64_t a0, uint64_t a1,
                 uint64_t a2, uint64_t a3, uint64_t a4);

#endif /* AISAFE_SYSCALL_H */
