/**
 * @file    sched_class.h
 * @brief   调度类（sched_class）框架接口
 * @author  AISafe64 Team
 * @date    2026-07-04
 * @version 3.0
 *
 * @details 每种调度策略实现一个 sched_class 实例（虚函数表）。
 *          schedule() 按类优先级（数值大的先查询）遍历调度类链表，
 *          调用各调度类的 pick_next 选择下一个线程。
 *
 *          新增调度策略只需实现并注册一个新的 sched_class，
 *          不需要修改 schedule()，实现策略可插拔。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_SCHED_CLASS_H
#define KERNEL_SCHED_CLASS_H

#include "thread.h"
#include <stdbool.h>

/* ========================================================================
 * 调度类结构体
 * ======================================================================== */

/**
 * @brief 调度类（策略可插拔）
 *
 * @details 每种调度策略实现一个 sched_class 实例。
 *          schedule() 按优先级遍历调度类链表调用 pick_next。
 *          新增策略只需注册一个新的 sched_class，不需要修改 schedule()。
 */
struct sched_class
{
    const char *name;                                /**< @brief 策略名称 */
    int priority;                                    /**< @brief 类优先级（数值大的先查询） */
    void (*enqueue)(struct KThread *thread);         /**< @brief 线程就绪 */
    void (*dequeue)(struct KThread *thread);         /**< @brief 线程离开就绪 */
    struct KThread *(*pick_next)(void);              /**< @brief 选择下一个线程 */
    void (*tick)(struct KThread *current);           /**< @brief 时钟 tick 处理 */
    struct sched_class *next;                        /**< @brief 调度类链表 */
};

/* ========================================================================
 * 调度类框架 API
 * ======================================================================== */

/**
 * @brief 注册调度类
 *
 * @details 将调度类按优先级插入到调度类链表（数值大的在前）。
 *          scheduler_init 中调用。
 *
 * @param cls 调度类实例
 */
void sched_class_register(struct sched_class *cls);

/**
 * @brief 设置默认调度类
 *
 * @details sched_class 等框架函数在 thread->sched_class 为 NULL 时
 *          回退到默认调度类（通常为 sched_rr）。
 *
 * @param cls 默认调度类实例
 */
void sched_class_set_default(struct sched_class *cls);

/**
 * @brief 获取默认调度类
 *
 * @return 默认调度类指针
 */
struct sched_class *sched_class_default(void);

/**
 * @brief 按线程所属调度类将线程加入就绪队列
 *
 * @details 调用 thread->sched_class 的 enqueue 方法。
 *          若线程未指定调度类，则回退到默认（rr）调度类。
 *
 * @param thread 要加入的线程
 */
void sched_class_enqueue(struct KThread *thread);

/**
 * @brief 按线程所属调度类将线程移出就绪队列
 *
 * @param thread 要移除的线程
 */
void sched_class_dequeue(struct KThread *thread);

/**
 * @brief 遍历调度类链表选择下一个线程
 *
 * @details 按类优先级从高到低调用各调度类的 pick_next，
 *          返回第一个非 NULL 的结果。无就绪线程返回 NULL。
 *
 * @return 选中的线程，无就绪线程返回 NULL
 */
struct KThread *sched_class_pick_next(void);

/**
 * @brief 遍历调度类链表执行 tick
 *
 * @details 调用当前线程所属调度类的 tick 方法。
 *
 * @param current 当前运行的线程
 */
void sched_class_tick(struct KThread *current);

/* ========================================================================
 * RR 调度类（默认实现，定义在 sched_rr.c）
 * ======================================================================== */

/** @brief RR 调度类实例（基础调度类） */
extern struct sched_class sched_rr;

/**
 * @brief 初始化并注册 RR 调度类
 *
 * @details 在 scheduler_init 中调用：注册 sched_rr 为默认调度类。
 */
void sched_rr_init(void);

#endif /* KERNEL_SCHED_CLASS_H */
