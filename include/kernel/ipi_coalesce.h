/**
 * @file    ipi_coalesce.h
 * @brief   IPI Coalescing（批处理）接口
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 本文件定义了 IPI Coalescing 接口：
 *          - IPI 批处理机制
 *          - IPI Coalesce 状态管理
 *          - IPI Coalesce 定时器
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.5 - IPI 优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_IPI_COALESCE_H
#define KERNEL_IPI_COALESCE_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/spinlock.h>
#include <stdint.h>

/* ========================================================================
 * IPI Coalesce 配置常量
 * ======================================================================== */

/** @brief IPI 批处理最大数量 */
#define IPI_COALESCE_MAX_BATCH   8U

/** @brief IPI Coalesce 超时（微秒） */
#define IPI_COALESCE_TIMEOUT_US  100U

/** @brief IPI Coalesce 超时（计数值） */
#define IPI_COALESCE_TIMEOUT_TICKS  ((uint64_t)(IPI_COALESCE_TIMEOUT_US * CONFIG_TICK_RATE_HZ / 1000000ULL))

/** @brief IPI Coalesce 间隔（微秒） */
#define IPI_COALESCE_INTERVAL_US  50U

/** @brief IPI Coalesce 间隔（计数值） */
#define IPI_COALESCE_INTERVAL_TICKS  ((uint64_t)(IPI_COALESCE_INTERVAL_US * CONFIG_TICK_RATE_HZ / 1000000ULL))

/* ========================================================================
 * IPI Coalesce 状态
 * ======================================================================== */

/**
 * @brief IPI Coalesce 状态
 */
typedef enum
{
    IPI_COALESCE_STATE_IDLE = 0U,        /**< @brief 空闲：无待处理 IPI */
    IPI_COALESCE_STATE_COLLECTING,    /**< @brief 收集：正在收集待处理 IPI */
    IPI_COALESCE_STATE_SENDING,       /**< @brief 发送：正在批量发送 IPI */
    IPI_COALESCE_STATE_WAITING,        /**< @brief 等待：等待发送完成 */
} ipi_coalesce_state_t;

/* ========================================================================
 * IPI Coalesce 批处理结构
 * ======================================================================== */

/**
 * @brief IPI 批处理条目
 *
 * @details 记录一个 IPI 的信息，用于批处理。
 */
typedef struct
{
    uint32_t        target_cpu;     /**< @brief 目标 CPU */
    ipi_type_t      type;           /**< @brief IPI 类型 */
    void           *arg;           /**< @brief IPI 参数 */
} ipi_coalesce_entry_t;

/**
 * @brief IPI Coalesce 批处理器
 *
 * @details IPI Coalesce 批处理器，用于批量处理 IPI。
 */
typedef struct
{
    ipi_coalesce_entry_t entries[IPI_COALESCE_MAX_BATCH]; /**< @brief IPI 条目数组 */
    uint32_t                count;                            /**< @brief 当前条目数量 */
    ipi_coalesce_state_t    state;                            /**< @brief 当前状态 */
    TicketLock_t            lock;                             /**< @brief 批处理器锁 */
} ipi_coalesce_t;

/* ========================================================================
 * IPI Coalesce 操作 API
 * ======================================================================== */

/**
 * @brief 初始化 IPI Coalesce 机制
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t ipi_coalesce_init(void);

/**
 * @brief 尝试添加 IPI 到批处理器
 *
 * @details 如果批处理器未满且状态为 COLLECTING，
 *          则添加 IPI 到批处理器并返回 true。
 *          如果批处理器已满或状态不是 COLLECTING，
 *          则立即发送 IPI 并返回 false。
 *
 * @param target_cpu 目标 CPU
 * @param type        IPI 类型
 * @param arg         IPI 参数
 *
 * @return true 表示添加到批处理器，false 表示立即发送
 */
bool ipi_coalesce_try_add(uint32_t target_cpu, ipi_type_t type, void *arg);

/**
 * @brief 立即发送 IPI（绕过批处理）
 *
 * @details 如果需要立即发送 IPI，则调用此函数。
 *          此函数会刷新批处理器并立即发送 IPI。
 *
 * @param target_cpu 目标 CPU
 * @param type        IPI 类型
 * @param arg         IPI 参数
 */
void ipi_coalesce_send_immediate(uint32_t target_cpu, ipi_type_t type, void *arg);

/**
 * @brief IPI Coalesce 定时器处理
 *
 * @details 定期检查批处理器状态，如果超时则立即发送。
 */
void ipi_coalesce_timer_handler(void);

#endif /* KERNEL_IPI_COALESCE_H */
