/**
 * @file    ipi.h
 * @brief   核心间中断（IPI）接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了核心间中断（IPI）接口：
 *          - IPI 类型定义
 *          - IPI 发送和接收
 *          - IPI 处理函数注册
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: MP-004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_IPI_H
#define KERNEL_IPI_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * IPI 类型定义
 * ======================================================================== */

/** @brief IPI 类型：重新调度 */
#define IPI_TYPE_RESCHEDULE     0U

/** @brief IPI 类型：停止 CPU */
#define IPI_TYPE_STOP           1U

/** @brief IPI 类型：函数调用（通用） */
#define IPI_TYPE_CALL_FUNC      2U

/** @brief IPI 类型：TLB 刷新 */
#define IPI_TYPE_TLB_FLUSH      3U

/** @brief IPI 类型：缓存维护 */
#define IPI_TYPE_CACHE_MAINT    4U

/** @brief IPI 类型：能力撤销广播（通知其他核使能力缓存失效） */
#define IPI_TYPE_CAP_REVOKE     5U

/** @brief IPI 类型：ASID 刷新广播 */
#define IPI_TYPE_ASID_FLUSH     6U

/** @brief IPI 类型总数 */
#define IPI_TYPE_COUNT          7U

/* ========================================================================
 * IPI 函数调用结构
 * ======================================================================== */

/**
 * @brief IPI 函数调用信息
 */
typedef struct
{
    void    (*func)(void *arg);     /**< @brief 要执行的函数 */
    void    *arg;                   /**< @brief 函数参数 */
    volatile uint32_t done;         /**< @brief 完成标志 */
} ipi_call_func_t;

/* ========================================================================
 * IPI 延迟统计结构
 * ======================================================================== */

/**
 * @brief IPI 延迟统计信息
 *
 * @details 记录每个 CPU 的 IPI 延迟统计：
 *          - min_ns: 最小延迟（纳秒）
 *          - max_ns: 最大延迟（纳秒）
 *          - avg_ns: 平均延迟（纳秒）
 *          - count: 采样次数
 */
typedef struct
{
    uint64_t min_ns;    /**< @brief 最小延迟（纳秒） */
    uint64_t max_ns;    /**< @brief 最大延迟（纳秒） */
    uint64_t avg_ns;    /**< @brief 平均延迟（纳秒） */
    uint64_t count;     /**< @brief 采样次数 */
} ipi_latency_stats_t;

/* ========================================================================
 * IPI API
 * ======================================================================== */

/**
 * @brief 初始化 IPI 子系统
 *
 * @details 注册 IPI 中断处理函数，初始化批处理位图和延迟统计。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: MP-004
 */
kernel_status_t ipi_init(void);

/**
 * @brief 向指定 CPU 发送 IPI
 *
 * @details 将 IPI 标记到目标 CPU 的待处理位图中。
 *          如需立即发送，调用 ipi_flush_pending()。
 *          批处理模式下不立即触发 SGI，减少 GIC MMIO 写入。
 *
 * @param target_cpu 目标 CPU 编号
 * @param ipi_type   IPI 类型
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: MP-004
 */
kernel_status_t ipi_send(uint32_t target_cpu, uint32_t ipi_type);

/**
 * @brief 向所有其他 CPU 广播 IPI
 *
 * @param ipi_type IPI 类型
 * @param exclude_self 是否排除自身
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t ipi_broadcast(uint32_t ipi_type, bool exclude_self);

/**
 * @brief 向指定 CPU 发送函数调用 IPI
 *
 * @details 目标 CPU 在 IPI 上下文中执行指定函数。
 *
 * @param target_cpu 目标 CPU 编号
 * @param func       要执行的函数
 * @param arg        函数参数
 * @param wait       是否等待完成
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t ipi_call_func(uint32_t target_cpu,
                                void (*func)(void *arg),
                                void *arg,
                                bool wait);

/**
 * @brief IPI 中断处理入口
 *
 * @details 在中断上下文中被调用，根据 IPI 类型分发处理。
 *          处理完成后自动清除该 CPU 的待处理位图。
 *
 * @param ipi_type IPI 类型
 */
void ipi_handler(uint32_t ipi_type);

/**
 * @brief 刷新指定 CPU 的所有待处理 IPI
 *
 * @details 将目标 CPU 的待处理 IPI 位图合并为一次 SGI 发送，
 *          减少 GIC MMIO 写入次数（从 N 次 SGI → 1 次 SGI）。
 *          在调度器返回用户态前、中断返回前调用。
 *
 * @param cpu_id 目标 CPU 编号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: MP-004
 */
kernel_status_t ipi_flush_pending(uint32_t cpu_id);

/**
 * @brief 刷新当前 CPU 的所有待处理 IPI
 *
 * @details ipi_flush_pending() 的便利封装，自动获取当前 CPU ID。
 */
void ipi_flush_pending_self(void);

/**
 * @brief 获取指定 CPU 的 IPI 延迟统计
 *
 * @param cpu_id  CPU 编号
 * @param stats   输出统计信息（调用者提供缓冲区）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t ipi_get_latency_stats(uint32_t cpu_id, ipi_latency_stats_t *stats);

#endif /* KERNEL_IPI_H */
