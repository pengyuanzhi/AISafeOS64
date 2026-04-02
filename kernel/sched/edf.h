/**
 * @file    edf.h
 * @brief   EDF（最早截止时间优先）调度器接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了 SCHED_EDF 调度策略的接口：
 *          - 基于绝对截止时间的红黑树（简化为数组排序）管理
 *          - WCET 预留和可调度性分析
 *          - 支持周期性任务和偶发任务
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SC-003, SC-005~006
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_SCHED_EDF_H
#define KERNEL_SCHED_EDF_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/list.h>
#include <stdint.h>
#include <stdbool.h>
#include "thread.h"

/* ========================================================================
 * EDF 调度常量
 * ======================================================================== */

/** @brief EDF 运行队列最大深度 */
#define EDF_MAX_TASKS             64U

/** @brief 无效截止时间标记 */
#define EDF_DEADLINE_INVALID      0ULL

/** @brief 默认 WCET（最坏执行时间，单位 ticks） */
#define EDF_DEFAULT_WCET          10U

/** @brief 默认周期（单位 ticks） */
#define EDF_DEFAULT_PERIOD        100U

/* ========================================================================
 * EDF 任务参数
 * ======================================================================== */

/**
 * @brief EDF 任务调度参数
 *
 * @details 用于描述 EDF 任务的实时特性：
 *          - wcet    : 最坏情况执行时间（Worst-Case Execution Time）
 *          - period  : 任务周期（周期性任务）
 *          - deadline: 相对截止时间（从周期开始计算）
 *          - runtime : 当前周期内已运行时间
 */
typedef struct
{
    tick_t  wcet;              /**< @brief 最坏情况执行时间（ticks） */
    tick_t  period;            /**< @brief 任务周期（ticks，0=偶发任务） */
    tick_t  relative_deadline; /**< @brief 相对截止时间（ticks） */
    tick_t  runtime;           /**< @brief 当前周期已运行时间 */
    tick_t  absolute_deadline; /**< @brief 绝对截止时间（系统 tick） */
    tick_t  next_release;      /**< @brief 下次释放时间（周期性） */
    uint32_t overrun_count;    /**< @brief 截止时间超限次数 */
    bool    is_periodic;       /**< @brief 是否周期性任务 */
} edf_params_t;

/* ========================================================================
 * EDF 就绪队列
 * ======================================================================== */

/**
 * @brief EDF 就绪队列
 *
 * @details 使用有序数组存储就绪任务，按绝对截止时间排序。
 *          入队时插入排序 O(n)，选下一个 O(1)。
 */
typedef struct
{
    struct list_head tasks;       /**< @brief 就绪任务链表 */
    uint32_t count;              /**< @brief 就绪任务计数 */
    uint32_t lock;               /**< @brief 队列锁 */
    uint64_t total_utilization;  /**< @brief CPU 利用率（千分比） */
} edf_ready_queue_t;

/* ========================================================================
 * EDF 调度器 API
 * ======================================================================== */

/**
 * @brief 初始化 EDF 调度器
 *
 * @details 初始化 EDF 就绪队列，清零统计信息
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t edf_init(void);

/**
 * @brief 设置线程的 EDF 参数
 *
 * @param tid       线程 ID
 * @param params    EDF 参数
 *
 * @return KERNEL_OK 成功，负数错误码失败
 *
 * @retval -ESRCH  线程不存在
 * @retval -EINVAL 参数无效
 */
kernel_status_t edf_set_params(thread_id_t tid, const edf_params_t *params);

/**
 * @brief 获取线程的 EDF 参数
 *
 * @param tid       线程 ID
 * @param[out] params 输出 EDF 参数
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t edf_get_params(thread_id_t tid, edf_params_t *params);

/**
 * @brief 将 EDF 任务加入就绪队列
 *
 * @param thread 线程指针
 */
void edf_enqueue(KThread_t *thread);

/**
 * @brief 从 EDF 就绪队列选取最早截止时间的任务
 *
 * @return 最早截止时间的就绪线程，无就绪线程返回 NULL
 */
KThread_t *edf_pick_next(void);

/**
 * @brief EDF 时钟滴答处理
 *
 * @details 更新运行时间，检查截止时间违规，
 *          处理周期性任务的新周期释放
 */
void edf_tick(void);

/**
 * @brief 计算 EDF 可调度性
 *
 * @details 检查所有 EDF 任务的 CPU 利用率之和是否 <= 1
 *          (Liu & Layland 条件)
 *
 * @return true 可调度，false 不可调度
 */
bool edf_check_schedulability(void);

/**
 * @brief EDF 任务到达新周期
 *
 * @param thread 周期性任务线程指针
 */
void edf_job_release(KThread_t *thread);

/**
 * @brief EDF 任务完成当前作业
 *
 * @param thread 完成作业的线程指针
 */
void edf_job_complete(KThread_t *thread);

#endif /* KERNEL_SCHED_EDF_H */
