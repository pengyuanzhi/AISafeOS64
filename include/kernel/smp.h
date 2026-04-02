/**
 * @file    smp.h
 * @brief   SMP 多核管理接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了 SMP（对称多处理）管理接口：
 *          - 多核启动（主核启动从核）
 *          - 每 CPU 数据区管理
 *          - CPU 状态管理
 *          - 核心间同步
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: MP-001~005
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_SMP_H
#define KERNEL_SMP_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/spinlock.h>
#include <kernel/list.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * SMP 配置常量
 * ======================================================================== */

/** @brief 最大 CPU 核心数量 */
#define SMP_MAX_CPUS           CONFIG_MAX_CPUS

/** @brief 无效 CPU ID */
#define SMP_CPU_INVALID        0xFFFFFFFFU

/** @brief 主核 ID（通常为 0） */
#define SMP_BOOT_CPU           0U

/* ========================================================================
 * CPU 状态枚举
 * ======================================================================== */

/**
 * @brief CPU 状态
 */
typedef enum
{
    CPU_STATE_OFFLINE = 0U,      /**< @brief 离线（未启动） */
    CPU_STATE_BOOTING,           /**< @brief 启动中 */
    CPU_STATE_RUNNING,           /**< @brief 正常运行 */
    CPU_STATE_STOPPING,          /**< @brief 正在停止 */
    CPU_STATE_STOPPED            /**< @brief 已停止 */
} cpu_state_t;

/* ========================================================================
 * 每 CPU 数据结构
 * ======================================================================== */

/**
 * @brief 每 CPU 数据区
 *
 * @details 每个 CPU 核心独有的数据，避免伪共享。
 *          使用 TPIDR_EL0 或数组索引访问。
 */
typedef struct
{
    uint32_t        cpu_id;             /**< @brief CPU 核心编号 */
    cpu_state_t     state;              /**< @brief CPU 状态 */
    uint32_t        irq_depth;          /**< @brief 中断嵌套深度 */
    uint32_t        preempt_count;      /**< @brief 抢占计数 */
    uint64_t        schedule_count;     /**< @brief 调度次数统计 */
    uint64_t        ipi_count;          /**< @brief IPI 接收计数 */
    void           *current_thread;     /**< @brief 当前运行线程 */
    void           *idle_thread;        /**< @brief 空闲线程 */
    void           *stack_base;         /**< @brief 内核栈基地址 */
    uint64_t        reserved[4];        /**< @brief 预留对齐到缓存行 */
} percpu_t;

/* ========================================================================
 * SMP 管理 API
 * ======================================================================== */

/**
 * @brief 初始化 SMP 子系统
 *
 * @details 初始化主核数据、设置每 CPU 数据区。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: MP-001
 */
kernel_status_t smp_init(void);

/**
 * @brief 启动所有从核
 *
 * @details 通过 PSCI（Power State Coordination Interface）
 *          或自定义 SMP 协议启动从核。
 *
 * @return KERNEL_OK 成功
 * @return -EBUSY CPU 已在线
 *
 * @note 对应需求: MP-002
 */
kernel_status_t smp_boot_secondary(void);

/**
 * @brief 停止指定 CPU
 *
 * @param cpu_id 要停止的 CPU 编号
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: MP-003
 */
kernel_status_t smp_cpu_stop(uint32_t cpu_id);

/**
 * @brief 获取当前 CPU 编号
 *
 * @return CPU 编号（0 ~ SMP_MAX_CPUS-1）
 */
uint32_t smp_get_cpu_id(void);

/**
 * @brief 获取当前 CPU 的每 CPU 数据
 *
 * @return 每 CPU 数据指针
 */
percpu_t *smp_get_percpu(void);

/**
 * @brief 获取指定 CPU 的每 CPU 数据
 *
 * @param cpu_id CPU 编号
 *
 * @return 每 CPU 数据指针，无效返回 NULL
 */
percpu_t *smp_get_percpu_by_id(uint32_t cpu_id);

/**
 * @brief 获取在线 CPU 数量
 *
 * @return 在线 CPU 数量
 */
uint32_t smp_get_online_count(void);

/**
 * @brief 检查 CPU 是否在线
 *
 * @param cpu_id CPU 编号
 *
 * @return true 在线
 */
bool smp_cpu_online(uint32_t cpu_id);

/**
 * @brief 从核入口函数
 *
 * @details 从核被唤醒后执行的入口函数。
 *          完成从核初始化后进入调度器。
 *
 * @param cpu_id CPU 编号
 */
void smp_secondary_entry(uint32_t cpu_id);

#endif /* KERNEL_SMP_H */
