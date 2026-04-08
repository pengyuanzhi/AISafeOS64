/**
 * @file    config.h
 * @brief   内核配置常量定义
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 本文件定义了 AISafeOS64 微内核的所有编译时配置常量，
 *          包括：
 *          - SMP 多核配置（最大 CPU 数量）
 *          - 线程与调度配置（线程数、优先级级数、时间片）
 *          - 内存管理配置（栈大小、页大小、堆大小）
 *          - 内核对象管理配置（对象类型数、能力空间大小）
 *          - 地址空间配置（内核虚拟基址）
 *
 *          所有配置值均通过编译时静态断言进行合法性检查，
 *          确保配置值满足系统约束条件。
 *
 * @note MISRA-C:2012 合规
 * @note 本文件中的值为默认值，可通过构建系统覆盖
 * @warning 修改本文件中的配置值前必须充分评估对系统的影响
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_CONFIG_H
#define KERNEL_CONFIG_H

#include <stdint.h>

/* ========================================================================
 * SMP 多核配置
 * ======================================================================== */

/**
 * @def CONFIG_MAX_CPUS
 * @brief 系统支持的最大 CPU 核心数量
 *
 * @details 定义系统最多可管理的 CPU 核心数。
 *          ARMv8-A 架构支持最多 256 个核心（通过 MPIDR_EL1 标识），
 *          本系统限制为 8 核以满足典型嵌入式实时应用需求。
 *
 * @note 有效范围：[1, 256]
 * @note 取值应为 2 的幂次方以优化位图操作
 */
#define CONFIG_MAX_CPUS                 8U

/* ========================================================================
 * 线程与调度配置
 * ======================================================================== */

/**
 * @def CONFIG_MAX_THREADS
 * @brief 系统支持的最大线程数量
 *
 * @details 定义系统可同时存在的最大线程数（包含内核线程和用户线程）。
 *          每个线程占用一个线程控制块（TCB），
 *          总内存消耗为 CONFIG_MAX_THREADS * sizeof(TCB_t)。
 *
 * @note 有效范围：[1, 65535]
 * @note 增大此值会增加内核静态内存占用
 */
#define CONFIG_MAX_THREADS              256U

/**
 * @def CONFIG_PRIORITY_LEVELS
 * @brief 调度器支持的优先级级数
 *
 * @details 定义优先级的总级数。值越大，调度粒度越细。
 *          使用 256 级优先级位图调度，可实现 O(1) 时间复杂度的
 *          最高优先级查找。
 *
 * @note 必须为 256（固定值，与位图调度算法耦合）
 * @note 优先级值范围：[0, 255]，值越大优先级越高
 */
#define CONFIG_PRIORITY_LEVELS          256U

/**
 * @def CONFIG_TIME_SLICE_MS
 * @brief 默认时间片长度（毫秒）
 *
 * @details 同优先级线程之间的轮转调度时间片。
 *          当线程用尽时间片后，调度器将选择同优先级的下一个就绪线程。
 *
 * @note 有效范围：[1, 1000]
 * @note 实际精度取决于 CONFIG_TICK_RATE_HZ
 */
#define CONFIG_TIME_SLICE_MS            10U

/**
 * @def CONFIG_TICK_RATE_HZ
 * @brief 系统定时器滴答频率（Hz）
 *
 * @details 定义系统定时器中断频率。
 *          - 1000Hz 表示 1ms 一次时钟中断
 *          - 100Hz 表示 10ms 一次时钟中断
 *
 * @note 常见取值：100、1000、10000
 * @note 更高的频率提供更精细的时间管理，但会增加中断开销
 */
#define CONFIG_TICK_RATE_HZ             1000U

/* ========================================================================
 * 内存管理配置
 * ======================================================================== */

/**
 * @def CONFIG_STACK_SIZE_DEFAULT
 * @brief 线程默认栈大小（字节）
 *
 * @details 新创建线程的默认栈空间大小。
 *          栈空间从内核堆中分配，必须满足 16 字节对齐要求（ARM64 ABI）。
 *
 * @note 有效范围：[CONFIG_STACK_SIZE_MIN, CONFIG_STACK_SIZE_MAX]
 * @note 必须为 16 的整数倍
 */
#define CONFIG_STACK_SIZE_DEFAULT       8192U

/**
 * @def CONFIG_STACK_SIZE_MIN
 * @brief 线程最小栈大小（字节）
 *
 * @details 线程栈空间的最小允许值。
 *          此值需保证能满足最坏情况下的栈使用需求，
 *          包括函数调用链、局部变量、中断上下文保存等。
 *
 * @note 不得小于 4096 字节
 */
#define CONFIG_STACK_SIZE_MIN           4096U

/**
 * @def CONFIG_STACK_SIZE_MAX
 * @brief 线程最大栈大小（字节）
 *
 * @details 线程栈空间的最大允许值。
 *          用于防止因配置错误导致的内存浪费。
 *
 * @note 不得超过 CONFIG_KERNEL_HEAP_SIZE 的 50%
 */
#define CONFIG_STACK_SIZE_MAX           65536U

/**
 * @def CONFIG_KERNEL_HEAP_SIZE
 * @brief 内核堆空间大小（字节）
 *
 * @details 内核动态内存池的总大小。
 *          用于线程栈分配、内核对象分配等。
 *          默认 2MB（0x200000）。
 *
 * @note 必须为页大小（CONFIG_PAGE_SIZE）的整数倍
 * @note 实际可用空间会因内存管理元数据开销而略小
 */
#define CONFIG_KERNEL_HEAP_SIZE         ((uint64_t)0x200000ULL)

/**
 * @def CONFIG_PAGE_SIZE
 * @brief 内存页大小（字节）
 *
 * @details ARMv8-A MMU 的标准页大小。
 *          使用 4KB 标准页作为最小内存映射单位。
 *
 * @note 固定为 4096 字节（ARM64 标准页大小）
 * @note 必须为 2 的幂次方
 */
#define CONFIG_PAGE_SIZE                4096U

/**
 * @def CONFIG_HUGE_PAGE_SIZE
 * @brief 大页大小（字节）
 *
 * @details ARMv8-A MMU 支持的 2MB 大页（块映射）。
 *          用于大块连续内存的映射，可减少 TLB 压力。
 *
 * @note 固定为 2097152 字节（2MB）
 * @note 必须为 CONFIG_PAGE_SIZE 的整数倍
 */
#define CONFIG_HUGE_PAGE_SIZE           2097152U

/**
 * @def CONFIG_KERNEL_VADDR_BASE
 * @brief 内核虚拟地址空间基地址
 *
 * @details ARM64 虚拟地址空间中内核区域的起始地址。
 *          采用高地址方案，将内核映射在虚拟地址空间的高半部分。
 *          用户空间占据低地址区域 [0, CONFIG_KERNEL_VADDR_BASE)。
 *
 * @note ARM64canonical high address 格式
 * @note 用户态不得直接访问此地址以上的区域（由 MMU 保护）
 */
#define CONFIG_KERNEL_VADDR_BASE        ((uint64_t)0xFFFF000000000000ULL)

/* ========================================================================
 * 内核对象管理配置
 * ======================================================================== */

/**
 * @def CONFIG_MAX_KOBJ_TYPES
 * @brief 内核支持的最大对象类型数量
 *
 * @details 微内核中可注册的内核对象类型上限。
 *          每种对象类型（如信号量、互斥锁、消息队列等）
 *          在内核对象管理器中占有一个类型描述符槽位。
 *
 * @note 有效范围：[8, 256]
 * @note 增大此值会增加类型描述符表的内存占用
 */
#define CONFIG_MAX_KOBJ_TYPES           16U

/**
 * @def CONFIG_CSPACE_SIZE
 * @brief 能力空间（CNode）的大小
 *
 * @details 每个线程的能力空间（Capability Space）中
 *          可容纳的能力（Capability）槽位数量。
 *          能力用于控制线程对内核对象的访问权限。
 *
 * @note 有效范围：[16, 65536]
 * @note 必须为 2 的幂次方（用于快速索引计算）
 * @note 增大此值会增加每个线程的内存开销
 */
#define CONFIG_CSPACE_SIZE              256U

/**
 * @def CONFIG_MAX_CSPACES
 * @brief 系统支持的最大能力空间（CSpace）数量
 *
 * @details 定义系统可同时存在的最大 CSpace 实例数。
 *          每个 CSpace 对应一个进程的能力空间。
 *          使用静态池预分配，避免运行时动态内存分配。
 *
 * @note 有效范围：[1, 256]
 * @note 增大此值会增加内核静态内存占用
 */
#define CONFIG_MAX_CSPACES              32U

/* ========================================================================
 * IPC 子系统配置
 * ======================================================================== */

/**
 * @def CONFIG_IPC_MAX_ENDPOINTS
 * @brief 系统支持的最大 IPC 端点数量
 *
 * @details 每个 IPC 端点代表一个消息传递的端接点。
 *          服务端线程通过端点接收消息。
 *
 * @note 有效范围：[1, 65535]
 */
#define CONFIG_IPC_MAX_ENDPOINTS        128U

/**
 * @def CONFIG_IPC_MAX_CHANNELS
 * @brief 系统支持的最大 IPC 通道数量
 *
 * @details 通道是 QNX 风格消息传递的服务端入口点。
 *          客户端通过连接（Connection）附加到通道。
 *
 * @note 有效范围：[1, 65535]
 */
#define CONFIG_IPC_MAX_CHANNELS         64U

/**
 * @def CONFIG_IPC_MAX_CONNECTIONS
 * @brief 系统支持的最大 IPC 连接数量
 *
 * @details 连接代表客户端到通道的附着关系。
 *          每个连接关联一个客户端线程和一个通道。
 *
 * @note 有效范围：[1, 65535]
 */
#define CONFIG_IPC_MAX_CONNECTIONS      256U

/**
 * @def CONFIG_IPC_MAX_NOTIFICATIONS
 * @brief 系统支持的最大通知对象数量
 *
 * @details 通知对象用于异步事件信号传递。
 *          常用于中断到线程的通知投递。
 *
 * @note 有效范围：[1, 65535]
 */
#define CONFIG_IPC_MAX_NOTIFICATIONS    128U

/**
 * @def CONFIG_IPC_MAX_PULSE_QUEUE
 * @brief 每个通道的 Pulse 队列最大深度
 *
 * @details Pulse 是轻量级异步消息，按优先级排队。
 *
 * @note 有效范围：[1, 1024]
 */
#define CONFIG_IPC_MAX_PULSE_QUEUE      32U

/**
 * @def CONFIG_IPC_MSG_MAX_SIZE
 * @brief 单条 IPC 消息的最大有效负载大小（字节）
 *
 * @details 超过此大小的消息应使用共享内存传输。
 *
 * @note 有效范围：[64, 65536]
 */
#define CONFIG_IPC_MSG_MAX_SIZE         4096U

/**
 * @def CONFIG_IPC_REG_MSG_WORDS
 * @brief 寄存器传递消息的最大字数（uint64_t）
 *
 * @details 小消息（≤ 4 个 uint64_t）通过寄存器直接传递，
 *          避免内存拷贝，实现快速路径优化。
 *
 * @note 固定为 4（使用 x2-x5 寄存器）
 */
#define CONFIG_IPC_REG_MSG_WORDS       4U

/* ========================================================================
 * 驱动子系统配置
 * ======================================================================== */

/**
 * @def CONFIG_MAX_DRIVERS
 * @brief 系统支持的最大驱动数量
 *
 * @details 定义系统可同时注册的驱动描述符上限。
 *          每个驱动描述符占用一个 driver_desc_t 槽位。
 *
 * @note 有效范围：[1, 256]
 */
#define CONFIG_MAX_DRIVERS              16U

/**
 * @def CONFIG_MAX_DEVICES
 * @brief 系统支持的最大设备数量
 *
 * @details 定义系统可同时注册的设备描述符上限。
 *          每个设备描述符占用一个 device_desc_t 槽位。
 *
 * @note 有效范围：[1, 256]
 */
#define CONFIG_MAX_DEVICES              32U

/* ========================================================================
 * 编译时配置合法性检查
 * ======================================================================== */

/**
 * @name 配置值静态断言
 * @brief 在编译时验证所有配置值的合法性
 * @{
 */

/* 验证：最大 CPU 数量必须在 [1, 256] 范围内 */
_Static_assert(
    (CONFIG_MAX_CPUS >= 1U) && (CONFIG_MAX_CPUS <= 256U),
    "CONFIG_MAX_CPUS must be in range [1, 256]"
);

/* 验证：最大线程数量必须在 [1, 65535] 范围内 */
_Static_assert(
    (CONFIG_MAX_THREADS >= 1U) && (CONFIG_MAX_THREADS <= 65535U),
    "CONFIG_MAX_THREADS must be in range [1, 65535]"
);

/* 验证：优先级级数必须固定为 256（与位图调度器耦合） */
_Static_assert(
    CONFIG_PRIORITY_LEVELS == 256U,
    "CONFIG_PRIORITY_LEVELS must be exactly 256 (coupled with bitmap scheduler)"
);

/* 验证：时间片必须大于 0 且不超过 1000ms */
_Static_assert(
    (CONFIG_TIME_SLICE_MS >= 1U) && (CONFIG_TIME_SLICE_MS <= 1000U),
    "CONFIG_TIME_SLICE_MS must be in range [1, 1000]"
);

/* 验证：滴答频率必须在 [10, 100000] 范围内 */
_Static_assert(
    (CONFIG_TICK_RATE_HZ >= 10U) && (CONFIG_TICK_RATE_HZ <= 100000U),
    "CONFIG_TICK_RATE_HZ must be in range [10, 100000]"
);

/* 验证：最小栈大小不小于 4096 字节 */
_Static_assert(
    CONFIG_STACK_SIZE_MIN >= 4096U,
    "CONFIG_STACK_SIZE_MIN must be at least 4096"
);

/* 验证：栈大小关系：MIN <= DEFAULT <= MAX */
_Static_assert(
    (CONFIG_STACK_SIZE_MIN <= CONFIG_STACK_SIZE_DEFAULT) &&
    (CONFIG_STACK_SIZE_DEFAULT <= CONFIG_STACK_SIZE_MAX),
    "Stack size order must be: MIN <= DEFAULT <= MAX"
);

/* 验证：默认栈大小必须为 16 的整数倍（ARM64 ABI 栈对齐要求） */
_Static_assert(
    (CONFIG_STACK_SIZE_DEFAULT % 16U) == 0U,
    "CONFIG_STACK_SIZE_DEFAULT must be a multiple of 16"
);

/* 验证：最小栈大小必须为 16 的整数倍 */
_Static_assert(
    (CONFIG_STACK_SIZE_MIN % 16U) == 0U,
    "CONFIG_STACK_SIZE_MIN must be a multiple of 16"
);

/* 验证：最大栈大小必须为 16 的整数倍 */
_Static_assert(
    (CONFIG_STACK_SIZE_MAX % 16U) == 0U,
    "CONFIG_STACK_SIZE_MAX must be a multiple of 16"
);

/* 验证：页大小必须为 2 的幂次方 */
_Static_assert(
    (CONFIG_PAGE_SIZE != 0U) &&
    ((CONFIG_PAGE_SIZE & (CONFIG_PAGE_SIZE - 1U)) == 0U),
    "CONFIG_PAGE_SIZE must be a power of 2"
);

/* 验证：大页大小必须为页大小的整数倍 */
_Static_assert(
    (CONFIG_HUGE_PAGE_SIZE % CONFIG_PAGE_SIZE) == 0U,
    "CONFIG_HUGE_PAGE_SIZE must be a multiple of CONFIG_PAGE_SIZE"
);

/* 验证：内核堆大小必须为页大小的整数倍 */
_Static_assert(
    (CONFIG_KERNEL_HEAP_SIZE % (uint64_t)CONFIG_PAGE_SIZE) == 0ULL,
    "CONFIG_KERNEL_HEAP_SIZE must be a multiple of CONFIG_PAGE_SIZE"
);

/* 验证：能力空间大小必须为 2 的幂次方 */
_Static_assert(
    (CONFIG_CSPACE_SIZE != 0U) &&
    ((CONFIG_CSPACE_SIZE & (CONFIG_CSPACE_SIZE - 1U)) == 0U),
    "CONFIG_CSPACE_SIZE must be a power of 2"
);

/* 验证：最大 CSpace 数量必须在 [1, 256] 范围内 */
_Static_assert(
    (CONFIG_MAX_CSPACES >= 1U) && (CONFIG_MAX_CSPACES <= 256U),
    "CONFIG_MAX_CSPACES must be in range [1, 256]"
);

/* 验证：内核对象类型数量至少为 8 */
_Static_assert(
    CONFIG_MAX_KOBJ_TYPES >= 8U,
    "CONFIG_MAX_KOBJ_TYPES must be at least 8"
);

/** @} */

#endif /* KERNEL_CONFIG_H */
