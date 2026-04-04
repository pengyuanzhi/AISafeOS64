/**
 * @file    types.h
 * @brief   内核基础类型定义
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 本文件定义了 AISafeOS64 微内核所使用的所有基础数据类型，
 *          包括：
 *          - 错误码类型（kernel_status_t）
 *          - 内核对象标识符类型（kobj_id_t）
 *          - 线程标识符类型（thread_id_t）
 *          - CPU 标识符类型（cpu_id_t）
 *          - 优先级类型（priority_t）
 *          - 系统滴答类型（tick_t）
 *          - 物理地址与虚拟地址类型（paddr_t / vaddr_t）
 *          - 常用工具宏（NULL、ARRAY_SIZE、container_of 等）
 *
 * @note MISRA-C:2012 合规
 * @note 仅使用 C11 标准特性，不依赖编译器扩展
 * @warning 本文件仅供内核代码使用，用户态代码不得包含
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_TYPES_H
#define KERNEL_TYPES_H

/*
 * C11 标准固定宽度整数类型
 * 内核代码必须使用这些类型，禁止使用 char/short/int/long
 */
#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * 错误码类型定义
 * ======================================================================== */

/**
 * @brief 内核状态码类型
 *
 * @details 用于表示内核函数的执行结果
 *          - 0 表示成功（KERNEL_OK）
 *          - 负数表示错误（遵循 POSIX 惯例）
 *
 * @note 具体错误码值定义在 errno.h 中
 */
typedef int32_t kernel_status_t;

/** @brief 内核操作成功 */
#define KERNEL_OK       ((kernel_status_t)0)

/** @brief 通用错误 */
#define KERNEL_ERROR    ((kernel_status_t)(-1))

/* ========================================================================
 * 内核对象标识符类型
 * ======================================================================== */

/**
 * @brief 内核对象 ID 类型
 *
 * @details 用于唯一标识一个内核对象（如信号量、互斥锁、消息队列等）
 *          值 0 表示无效对象 ID
 *
 * @note 由内核对象管理器统一分配，全局唯一
 */
typedef uint32_t kobj_id_t;

/** @brief 无效的内核对象 ID */
#define KOBJ_ID_INVALID ((kobj_id_t)0U)

/* ========================================================================
 * 线程标识符类型
 * ======================================================================== */

/**
 * @brief 线程 ID 类型
 *
 * @details 用于唯一标识一个内核线程
 *          值 0 表示无效线程 ID
 *
 * @note 线程 ID 在创建时由调度器分配
 */
typedef uint32_t thread_id_t;

/** @brief 无效的线程 ID（超出有效范围 [0, CONFIG_MAX_THREADS)） */
#define THREAD_ID_INVALID ((thread_id_t)0xFFFFFFFFU)

/* ========================================================================
 * CPU 标识符类型
 * ======================================================================== */

/**
 * @brief CPU ID 类型
 *
 * @details 用于标识多核系统中的各个 CPU 核心
 *          取值范围：[0, CONFIG_MAX_CPUS)
 *
 * @note 通过读取 MPIDR_EL1 寄存器获取当前 CPU ID
 */
typedef uint32_t cpu_id_t;

/* ========================================================================
 * 调度相关类型
 * ======================================================================== */

/**
 * @brief 线程优先级类型
 *
 * @details 表示线程的调度优先级
 *          - 0 为最低优先级
 *          - 255 为最高优先级
 *          - 值越大优先级越高
 *
 * @note 有效范围：[0, CONFIG_PRIORITY_LEVELS)
 */
typedef uint8_t priority_t;

/** @brief 最低优先级 */
#define PRIORITY_MIN    ((priority_t)0U)

/** @brief 最高优先级 */
#define PRIORITY_MAX    ((priority_t)255U)

/* ========================================================================
 * 时间相关类型
 * ======================================================================== */

/**
 * @brief 系统滴答类型
 *
 * @details 表示系统时间滴答计数
 *          - 64 位宽度，避免溢出
 *          - 单位为 1 / CONFIG_TICK_RATE_HZ 秒
 *
 * @note 滴答频率由 CONFIG_TICK_RATE_HZ 定义（默认 1000Hz，即 1ms 精度）
 */
typedef uint64_t tick_t;

/* ========================================================================
 * 地址类型定义
 * ======================================================================== */

/**
 * @brief 物理地址类型
 *
 * @details 表示 ARM64 物理内存地址
 *          - 64 位宽度，支持最大 2^64 字节物理地址空间
 *          - 用于页表映射、DMA 操作等
 *
 * @note 物理地址不得直接解引用，必须先映射到虚拟地址空间
 */
typedef uint64_t paddr_t;

/**
 * @brief 虚拟地址类型
 *
 * @details 表示 ARM64 虚拟内存地址
 *          - 64 位宽度，支持 48 位或 52 位虚拟地址空间
 *          - 内核空间起始地址由 CONFIG_KERNEL_VADDR_BASE 定义
 *
 * @note 虚拟地址必须在有效的映射区域内方可访问
 */
typedef uint64_t vaddr_t;

/**
 * @brief 指针大小的无符号整数类型
 *
 * @details 用于存放指针值，支持指针与整数之间的安全转换
 */
typedef uintptr_t regval_t;

/* ========================================================================
 * NULL 定义
 * ======================================================================== */

/**
 * @def NULL
 * @brief 空指针常量
 *
 * @details C11 标准定义的空指针常量
 *          如果编译器已定义 NULL 则使用系统定义，
 *          否则定义为 ((void *)0)
 */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* ========================================================================
 * 编译时断言兼容宏
 * ======================================================================== */

/**
 * @def static_assert
 * @brief 编译时断言兼容宏
 *
 * @details C11 标准引入了 _Static_assert 关键字，
 *          本宏提供 static_assert 的兼容定义，
 *          使代码在 C11 模式下可使用更直观的名称。
 *
 * @param expr 编译时常量表达式，必须为非零值
 * @param msg  断言失败时的诊断消息字符串
 *
 * @note 如果表达式为零，编译将在编译时报错并显示 msg
 *
 * @par 示例
 * @code
 * static_assert(sizeof(uint64_t) == 8U, "uint64_t must be 8 bytes");
 * @endcode
 */
#ifndef __cplusplus
#ifndef static_assert
#define static_assert _Static_assert
#endif
#endif

/* ========================================================================
 * 数组大小计算宏
 * ======================================================================== */

/**
 * @def ARRAY_SIZE
 * @brief 计算数组元素的个数
 *
 * @details 在编译时计算给定数组的元素数量。
 *          通过指针算术实现类型安全，
 *          对非数组类型的参数会产生编译警告或错误。
 *
 * @param arr 数组变量名（不得为指针）
 *
 * @return 数组中的元素个数（size_t 类型）
 *
 * @note 参数必须是数组，不能是指针
 *
 * @par 示例
 * @code
 * uint32_t buffer[64];
 * size_t count = ARRAY_SIZE(buffer);   // count = 64
 * @endcode
 */
#define ARRAY_SIZE(arr) ((size_t)(sizeof(arr) / sizeof((arr)[0U])))

/* ========================================================================
 * container_of 宏
 * ======================================================================== */

/**
 * @def container_of
 * @brief 通过结构体成员指针获取所在结构体的指针
 *
 * @details 给定一个指向结构体成员的指针，计算并返回
 *          包含该成员的结构体的首地址。
 *
 * @param ptr     指向结构体成员的指针
 * @param type    结构体类型名
 * @param member  成员在结构体中的字段名
 *
 * @return 指向包含该成员的结构体实例的指针（type * 类型）
 *
 * @note 使用前必须确保 ptr 非空且指向有效的结构体成员
 * @warning ptr 必须是指向 type 类型结构体中 member 成员的指针，
 *          否则行为未定义
 *
 * @par 示例
 * @code
 * typedef struct
 * {
 *     uint32_t id;
 *     list_node_t node;
 * } MyStruct_t;
 *
 * list_node_t *n = get_node();
 * MyStruct_t *obj = container_of(n, MyStruct_t, node);
 * @endcode
 */
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#endif /* KERNEL_TYPES_H */
