/**
 * @file    syscall.h
 * @brief   系统调用号定义和接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了微内核的系统调用号和用户态桩函数接口：
 *          - ARMv8-A 调用约定（SVC #0）
 *          - x0 = 系统调用号，x1-x6 = 参数，x0 = 返回值
 *          - 线程/IPC/内存/能力/中断管理类系统调用
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: API-001~004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <kernel/types.h>
#include <kernel/errno.h>
#include <stdint.h>

/* ========================================================================
 * 系统调用帧结构（由 exception.S 构造并传递给 syscall_handler）
 * ======================================================================== */

/**
 * @brief 系统调用帧结构
 *
 * @details SVC 异常触发时，由汇编代码将 x0-x8 寄存器保存到此结构中，
 *          猶后将结构体指针传递给 C 函数 syscall_handler()。
 *          返回值通过修改 frame->x0 传回用户态。
 *
 * @note ARM64 约定：x8 = 系统调用号，x0-x6 = 参数，x0 = 返回值
 */
typedef struct
{
    uint64_t x0;    /**< @brief 参数0 / 返回值 */
    uint64_t x1;    /**< @brief 参数1 */
    uint64_t x2;    /**< @brief 参数2 */
    uint64_t x3;    /**< @brief 参数3 */
    uint64_t x4;    /**< @brief 参数4 */
    uint64_t x5;    /**< @brief 参数5 */
    uint64_t x6;    /**< @brief 参数6 */
    uint64_t x7;    /**< @brief 保留（未使用） */
    uint64_t x8;    /**< @brief 系统调用号 */
} syscall_frame_t;

/* ========================================================================
 * 系统调用号定义
 * ======================================================================== */

/* --- 线程管理（0x0000 - 0x00FF） --- */
#define SYS_THREAD_CREATE          0x0001U   /**< @brief 创建线程 */
#define SYS_THREAD_EXIT            0x0002U   /**< @brief 退出线程 */
#define SYS_THREAD_SUSPEND         0x0003U   /**< @brief 挂起线程 */
#define SYS_THREAD_RESUME          0x0004U   /**< @brief 恢复线程 */
#define SYS_THREAD_SET_PRIORITY    0x0005U   /**< @brief 设置优先级 */
#define SYS_THREAD_SET_AFFINITY    0x0006U   /**< @brief 设置 CPU 亲和性 */
#define SYS_THREAD_YIELD           0x0007U   /**< @brief 让出 CPU */
#define SYS_THREAD_GET_ID          0x0008U   /**< @brief 获取线程 ID */

/* --- IPC 操作（0x0100 - 0x01FF） --- */
#define SYS_CHANNEL_CREATE         0x0100U   /**< @brief 创建 IPC 通道 */
#define SYS_CHANNEL_DESTROY        0x0101U   /**< @brief 销毁通道 */
#define SYS_CONNECT_ATTACH         0x0102U   /**< @brief 附加到通道 */
#define SYS_CONNECT_DETACH         0x0103U   /**< @brief 分离连接 */
#define SYS_MSG_SEND               0x0104U   /**< @brief 同步发送消息 */
#define SYS_MSG_RECV               0x0105U   /**< @brief 接收消息 */
#define SYS_MSG_REPLY              0x0106U   /**< @brief 回复消息 */
#define SYS_PULSE_SEND             0x0107U   /**< @brief 发送 Pulse */
#define SYS_NOTIFICATION_SIGNAL    0x0108U   /**< @brief 信号通知 */
#define SYS_NOTIFICATION_WAIT      0x0109U   /**< @brief 等待通知 */
#define SYS_EP_CREATE              0x010AU   /**< @brief 创建 IPC 端点 */

/* --- 内存管理（0x0200 - 0x02FF） --- */
#define SYS_VMSPACE_CREATE         0x0200U   /**< @brief 创建地址空间 */
#define SYS_VMSPACE_DESTROY        0x0201U   /**< @brief 销毁地址空间 */
#define SYS_VM_MAP                 0x0202U   /**< @brief 映射页面（MMIO/DMA，用户态驱动用） */
#define SYS_VM_UNMAP               0x0203U   /**< @brief 解除映射 */
#define SYS_VM_PROTECT             0x0204U   /**< @brief 修改权限 */
#define SYS_VIRT_TO_PHYS           0x0205U   /**< @brief 虚拟地址转物理地址（DMA 用） */

/* --- 能力管理（0x0300 - 0x03FF） --- */
#define SYS_CSPACE_CREATE          0x0300U   /**< @brief 创建能力空间 */
#define SYS_CAP_COPY               0x0301U   /**< @brief 复制能力 */
#define SYS_CAP_MOVE               0x0302U   /**< @brief 移动能力 */
#define SYS_CAP_REVOKE             0x0303U   /**< @brief 撤销能力 */
#define SYS_CAP_DELETE             0x0304U   /**< @brief 删除能力 */

/* --- 中断管理（0x0400 - 0x04FF） --- */
#define SYS_INTERRUPT_ATTACH       0x0400U   /**< @brief 绑定中断到通知对象（返回 attach_id） */
#define SYS_INTERRUPT_DETACH       0x0401U   /**< @brief 按 IRQ 号解除所有绑定 */
#define SYS_INTERRUPT_DETACH_BY_ID 0x0402U   /**< @brief 按 attach_id 精确解绑 */
#define SYS_INTERRUPT_MASK         0x0403U   /**< @brief 临时屏蔽中断 */
#define SYS_INTERRUPT_UNMASK       0x0404U   /**< @brief 恢复屏蔽的中断 */
#define SYS_INTERRUPT_GET_STATS    0x0405U   /**< @brief 查询中断统计 */

/* --- 调试/信息（0x0500 - 0x05FF） --- */
#define SYS_DEBUG_PRINT            0x0500U   /**< @brief 调试打印 */
#define SYS_SYSTEM_INFO            0x0501U   /**< @brief 获取系统信息 */

/* ========================================================================
 * 系统调用表配置
 * ======================================================================== */

/**
 * @def SYSCALL_TABLE_MAX
 * @brief 系统调用表最大容量
 *
 * @details 定义系统调用表可容纳的最大系统调用数量。
 *          超出此范围的系统调用号将被视为无效。
 */
#define SYSCALL_TABLE_MAX         256U

/* ========================================================================
 * 系统调用处理函数类型
 * ======================================================================== */

/**
 * @brief 系统调用处理函数指针类型
 *
 * @details 每个系统调用号对应一个处理函数。
 *          处理函数接收 syscall_frame_t 指针，
 *          从中提取参数并返回结果。
 *
 * @param frame 系统调用帧指针（包含参数和调用号）
 *
 * @return 系统调用返回值（成功返回 0 或正数，失败返回负错误码）
 */
typedef int64_t (*syscall_handler_fn)(syscall_frame_t *frame);

/* ========================================================================
 * 系统调用表 API
 * ======================================================================== */

/**
 * @brief 初始化系统调用表
 *
 * @details 将系统调用表的所有槽位清空为 NULL，
 *          重置已注册计数为 0。
 *          必须在使用系统调用表之前调用。
 *
 * @note 对应需求: API-001
 */
void syscall_table_init(void);

/**
 * @brief 注册系统调用处理函数
 *
 * @details 将处理函数注册到指定的系统调用号。
 *          每个系统调用号只能注册一个处理函数。
 *
 * @param nr    系统调用号（必须在 [0, SYSCALL_TABLE_MAX) 范围内）
 * @param handler 处理函数指针（不能为 NULL）
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0          注册成功
 * @retval -EINVAL    参数无效（nr 越界或 handler 为 NULL）
 * @retval -EEXIST    系统调用号已被注册
 *
 * @note 对应需求: API-001
 */
int32_t syscall_register(uint32_t nr, syscall_handler_fn handler);

/**
 * @brief 分发系统调用
 *
 * @details 根据系统调用号查找并调用对应的处理函数。
 *          系统调用号从 frame->x8 中读取。
 *
 * @param frame 系统调用帧指针（不能为 NULL）
 *
 * @return 处理函数的返回值，或错误码
 *
 * @retval 处理函数返回值  系统调用成功执行
 * @retval -EINVAL         frame 为 NULL
 * @retval -ENOSYS         系统调用号未注册
 *
 * @note 对应需求: API-001~004
 */
int64_t syscall_table_dispatch(syscall_frame_t *frame);

/**
 * @brief 获取已注册的系统调用数量
 *
 * @return 当前已注册的系统调用处理函数数量
 */
uint32_t syscall_table_count(void);

/* ========================================================================
 * 系统调用帧结构（内核态使用）
 * ======================================================================== */

/**
 * @brief 系统调用分发处理函数（内核态）
 *
 * @details 由 exception.S 的 .Lsvc_handler 调用，
 *          从 frame 中提取系统调用号和参数，
 *          分发到对应的子系统 API。
 *          返回值写入 frame->x0，由汇编还原到 x0 寄存器。
 *
 * @param frame 系统调用帧指针（指向栈上的 syscall_frame_t）
 *
 * @note 对应需求: API-001~004
 */
void syscall_handler(syscall_frame_t *frame);

/* ========================================================================
 * 系统调用桩函数（内联汇编）
 * ======================================================================== */

/**
 * @brief 系统调用底层封装
 *
 * @details ARMv8-A 系统调用约定：
 *          - x0 = 系统调用号
 *          - x1-x6 = 参数（最多 6 个）
 *          - x0 = 返回值
 *
 * @param syscall_nr 系统调用号
 * @param arg0 参数 0
 * @param arg1 参数 1
 * @param arg2 参数 2
 * @param arg3 参数 3
 * @param arg4 参数 4
 * @param arg5 参数 5
 *
 * @return 系统调用返回值
 */
static inline int64_t syscall0(uint32_t syscall_nr)
{
    int64_t ret;
    __asm__ volatile(
        "mov x8, %1\n"
        "svc #0\n"
        "mov %0, x0\n"
        : "=r"(ret)
        : "r"((uint64_t)syscall_nr)
        : "x0", "x8", "memory"
    );
    return ret;
}

static inline int64_t syscall1(uint32_t syscall_nr, uint64_t arg0)
{
    int64_t ret;
    __asm__ volatile(
        "mov x8, %1\n"
        "mov x0, %2\n"
        "svc #0\n"
        "mov %0, x0\n"
        : "=r"(ret)
        : "r"((uint64_t)syscall_nr), "r"(arg0)
        : "x0", "x8", "memory"
    );
    return ret;
}

static inline int64_t syscall2(uint32_t syscall_nr, uint64_t arg0,
                                 uint64_t arg1)
{
    int64_t ret;
    __asm__ volatile(
        "mov x8, %[nr]\n"
        "mov x0, %[a0]\n"
        "mov x1, %[a1]\n"
        "svc #0\n"
        "mov %[ret], x0\n"
        : [ret] "=r"(ret)
        : [nr] "r"((uint64_t)syscall_nr),
          [a0] "r"(arg0), [a1] "r"(arg1)
        : "x0", "x1", "x8", "memory"
    );
    return ret;
}

static inline int64_t syscall3(uint32_t syscall_nr, uint64_t arg0,
                                 uint64_t arg1, uint64_t arg2)
{
    int64_t ret;
    __asm__ volatile(
        "mov x8, %[nr]\n"
        "mov x0, %[a0]\n"
        "mov x1, %[a1]\n"
        "mov x2, %[a2]\n"
        "svc #0\n"
        "mov %[ret], x0\n"
        : [ret] "=r"(ret)
        : [nr] "r"((uint64_t)syscall_nr),
          [a0] "r"(arg0), [a1] "r"(arg1), [a2] "r"(arg2)
        : "x0", "x1", "x2", "x8", "memory"
    );
    return ret;
}

static inline int64_t syscall4(uint32_t syscall_nr, uint64_t arg0,
                                 uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    int64_t ret;
    __asm__ volatile(
        "mov x8, %[nr]\n"
        "mov x0, %[a0]\n"
        "mov x1, %[a1]\n"
        "mov x2, %[a2]\n"
        "mov x3, %[a3]\n"
        "svc #0\n"
        "mov %[ret], x0\n"
        : [ret] "=r"(ret)
        : [nr] "r"((uint64_t)syscall_nr),
          [a0] "r"(arg0), [a1] "r"(arg1), [a2] "r"(arg2), [a3] "r"(arg3)
        : "x0", "x1", "x2", "x3", "x8", "memory"
    );
    return ret;
}

static inline int64_t syscall5(uint32_t syscall_nr, uint64_t arg0,
                                uint64_t arg1, uint64_t arg2,
                                uint64_t arg3, uint64_t arg4)
{
    int64_t ret;
    __asm__ volatile(
        "mov x8, %[nr]\n"
        "mov x0, %[a0]\n"
        "mov x1, %[a1]\n"
        "mov x2, %[a2]\n"
        "mov x3, %[a3]\n"
        "mov x4, %[a4]\n"
        "svc #0\n"
        "mov %[ret], x0\n"
        : [ret] "=r"(ret)
        : [nr] "r"((uint64_t)syscall_nr),
          [a0] "r"(arg0), [a1] "r"(arg1), [a2] "r"(arg2),
          [a3] "r"(arg3), [a4] "r"(arg4)
        : "x0", "x1", "x2", "x3", "x4", "x8", "memory"
    );
    return ret;
}

#endif /* KERNEL_SYSCALL_H */
