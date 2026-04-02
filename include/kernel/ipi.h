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

/** @brief IPI 类型总数 */
#define IPI_TYPE_COUNT          5U

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
 * IPI API
 * ======================================================================== */

/**
 * @brief 初始化 IPI 子系统
 *
 * @details 注册 IPI 中断处理函数。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: MP-004
 */
kernel_status_t ipi_init(void);

/**
 * @brief 向指定 CPU 发送 IPI
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
 *
 * @param ipi_type IPI 类型
 */
void ipi_handler(uint32_t ipi_type);

#endif /* KERNEL_IPI_H */
