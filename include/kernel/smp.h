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

/* ========================================================================
 * 多核调度 API
 * ======================================================================== */

/**
 * @brief 初始化多核调度器
 *
 * @details 初始化每 CPU 就绪队列、亲和性表。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: MP-004
 */
kernel_status_t smp_sched_init(void);

/**
 * @brief 向指定 CPU 的就绪队列添加线程
 *
 * @param cpu_id   目标 CPU 编号
 * @param priority 线程优先级（0~255）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t smp_enqueue(uint32_t cpu_id, uint32_t priority);

/**
 * @brief 从指定 CPU 的就绪队列移除线程
 *
 * @param cpu_id   目标 CPU 编号
 * @param priority 线程优先级（0~255）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t smp_dequeue(uint32_t cpu_id, uint32_t priority);

/**
 * @brief 获取指定 CPU 的就绪队列负载
 *
 * @param cpu_id CPU 编号
 *
 * @return 就绪队列中的线程数
 */
uint32_t smp_get_load(uint32_t cpu_id);

/**
 * @brief 执行负载均衡
 *
 * @details 检测负载不均衡并迁移线程。
 *          支持批量迁移，尊重 CPU 亲和性约束。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: MP-004
 */
kernel_status_t smp_load_balance(void);

/**
 * @brief 设置线程的 CPU 亲和性
 *
 * @details 将线程绑定到 cpu_mask 指定的 CPU 集合。
 *          cpu_mask 为 0 表示无约束。
 *
 * @param thread_id 线程 ID
 * @param cpu_mask  允许运行的 CPU 位掩码
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: MP-005
 */
kernel_status_t smp_set_affinity(uint32_t thread_id, uint32_t cpu_mask);

/**
 * @brief 获取线程的 CPU 亲和性掩码
 *
 * @param thread_id 线程 ID
 *
 * @return CPU 亲和性位掩码，0 表示无约束
 */
uint32_t smp_get_affinity(uint32_t thread_id);

/**
 * @brief 检查线程是否允许在指定 CPU 上运行
 *
 * @param thread_id 线程 ID
 * @param cpu_id    目标 CPU 编号
 *
 * @return true 允许，false 不允许
 */
bool smp_affinity_allowed(uint32_t thread_id, uint32_t cpu_id);

/**
 * @brief 根据亲和性选择最佳目标 CPU
 *
 * @param thread_id   线程 ID
 * @param exclude_cpu 排除的 CPU
 *
 * @return 目标 CPU 编号
 */
uint32_t smp_select_target_cpu(uint32_t thread_id, uint32_t exclude_cpu);

/**
 * @brief 基于亲和性的线程入队选择
 *
 * @param thread_id 线程 ID
 * @param hint_cpu  建议 CPU（通常为当前 CPU）
 *
 * @return 选中的 CPU 编号
 */
uint32_t smp_select_enqueue_cpu(uint32_t thread_id, uint32_t hint_cpu);

/**
 * @brief 调度器时钟检查（周期性触发负载均衡）
 *
 * @details 在每次调度器时钟中断中调用。当调度次数达到
 *          LOAD_BALANCE_INTERVAL 的整数倍时触发一次负载均衡检查。
 *
 * @param cpu_id 当前 CPU 编号
 *
 * @note 对应需求: MP-004
 */
void smp_tick_check_balance(uint32_t cpu_id);

/**
 * @brief 调度器时钟周期性检查（内部使用）
 *
 * @details 递增调度计数并在达到间隔时触发负载均衡。
 *          由 smp_tick_check_balance 内部调用。
 *
 * @param cpu_id 当前 CPU 编号
 */
void smp_sched_tick(uint32_t cpu_id);

/**
 * @brief 将线程从一个 CPU 迁移到另一个 CPU
 *
 * @details 显式迁移，检查亲和性约束后执行迁移并发送 IPI 通知。
 *
 * @param src_cpu   源 CPU 编号
 * @param dst_cpu   目标 CPU 编号
 * @param priority  线程优先级（0~255）
 * @param thread_id 线程 ID（用于亲和性检查）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EPERM 亲和性不允许
 *
 * @note 对应需求: MP-004, MP-005
 */
kernel_status_t smp_migrate_thread(uint32_t src_cpu,
                                     uint32_t dst_cpu,
                                     uint32_t priority,
                                     uint32_t thread_id);

/**
 * @brief 向目标 CPU 发送重新调度 IPI
 *
 * @details 封装 IPI 发送，用于通知目标 CPU 重新调度。
 *
 * @param target_cpu 目标 CPU 编号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 目标 CPU 无效
 *
 * @note 对应需求: MP-004
 */
kernel_status_t smp_send_reschedule(uint32_t target_cpu);

/**
 * @brief 工作窃取 - 空闲 CPU 从忙碌 CPU 窃取线程
 *
 * @details 当当前 CPU 没有就绪线程时调用。
 *          从负载最高的 CPU 窃取最多 1 个低优先级线程。
 *          窃取时检查亲和性约束。
 *
 * @param my_cpu 当前 CPU 编号（窃取发起方）
 *
 * @return KERNEL_OK 窃取成功
 * @return -EINVAL 参数无效或当前 CPU 有就绪线程
 * @return -ENOENT 无可窃取的线程
 *
 * @note 对应需求: MP-004
 */
kernel_status_t smp_work_steal(uint32_t my_cpu);

#endif /* KERNEL_SMP_H */
