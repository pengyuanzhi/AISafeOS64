/**
 * @file    scheduler.h
 * @brief   256 级优先级位图调度器接口
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 本文件声明了 AISafeOS64 的 O(1) 优先级位图调度器接口：
 *          - 每 CPU 独立就绪队列（PerCPUReadyQueue_t）
 *          - 256 级优先级位图 + 每优先级链表
 *          - O(1) 最高优先级查找（CLZ 指令）
 *          - SMP 支持（最多 CONFIG_MAX_CPUS 个 CPU）
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SC-001（O(1) 优先级位图调度）、SC-002（上下文切换）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_SCHED_SCHEDULER_H
#define KERNEL_SCHED_SCHEDULER_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/bitmap.h>
#include <kernel/list.h>
#include <kernel/compiler.h>
#include <kernel/alignment.h>
#include <kernel/spinlock.h>
#include <stdbool.h>
#include "thread.h"
#include <kernel/mm/slab.h>

/* ========================================================================
 * 每 CPU 就绪队列
 * ======================================================================== */

/**
 * @brief 每 CPU 就绪队列结构
 *
 * @details 每个 CPU 核心维护独立的就绪队列：
 *          - bitmap : 256 位优先级位图，O(1) 查找最高优先级
 *          - queues : 每优先级一个双向链表，管理同优先级线程
 *          - lock   : 自旋锁（保护队列操作）
 *          - nr_running : 就绪线程计数
 *          - current_thread : 当前正在运行的线程
 *          - idle_thread    : 当前 CPU 的 idle 线程
 */
typedef struct CACHE_ALIGN(64)
{
    bitmap256_t bitmap;                                       /**< @brief 优先级位图 */
    struct list_head queues[CONFIG_PRIORITY_LEVELS];          /**< @brief 每优先级就绪链表 */
    TicketLock_t lock;                                        /**< @brief 队列自旋锁（关中断保护） */
    uint32_t nr_running;                                      /**< @brief 就绪线程总数 */
    KThread_t *current_thread;                                /**< @brief 当前运行线程 */
    KThread_t *idle_thread;                                   /**< @brief idle 线程 */
    volatile uint32_t need_resched;                           /**< @brief 需重新调度标志（中断中置位，出口/闲时检查） */
} PerCPUReadyQueue_t;

/* ========================================================================
 * 全局调度器结构
 * ======================================================================== */

/**
 * @brief 全局调度器实例
 *
 * @details 包含所有 CPU 的就绪队列和全局线程表
 */
/* ========================================================================
 * 线程栈 Slab 缓存配置
 * ======================================================================== */

/**
 * @brief 线程栈大小类别
 */
typedef enum
{
    STACK_SIZE_4KB = 0,         /**< @brief 4KB 栈索引 */
    STACK_SIZE_8KB = 1,         /**< @brief 8KB 栈索引 */
    STACK_SIZE_16KB = 2,        /**< @brief 16KB 栈索引 */
    STACK_SIZE_COUNT            /**< @brief 栈大小类别数量 = 3 */
} stack_size_class_t;

/** @brief 实际栈大小（字节） */
#define STACK_SIZE_4KB_BYTES   4096U
#define STACK_SIZE_8KB_BYTES   8192U
#define STACK_SIZE_16KB_BYTES  16384U

/**
 * @brief 线程栈 Slab 缓存集合
 */
typedef struct
{
    slab_cache_t caches[STACK_SIZE_COUNT]; /**< @brief 不同大小栈的 Slab 缓存 */
    bool initialized;                    /**< @brief Slab 缓存初始化标志 */
} ThreadStackSlab_t;

/* ========================================================================
 * 全局调度器结构
 * ======================================================================== */

/**
 * @brief 全局调度器实例
 *
 * @details 包含所有 CPU 的就绪队列、全局线程表和线程栈 Slab 缓存
 */
typedef struct
{
    PerCPUReadyQueue_t cpu_queues[CONFIG_MAX_CPUS]; /**< @brief 每 CPU 就绪队列 */
    KThread_t thread_table[CONFIG_MAX_THREADS];      /**< @brief 全局线程控制块表 */
    ThreadStackSlab_t stack_slab;                    /**< @brief 线程栈 Slab 缓存 */
    bool initialized;                                 /**< @brief 调度器初始化标志 */
} Scheduler_t;

/* ========================================================================
 * 全局调度器实例声明
 * ======================================================================== */

/** @brief 全局调度器实例（定义在 scheduler.c 中） */
extern Scheduler_t g_scheduler;

/* ========================================================================
 * 调度器 API
 * ======================================================================== */

/**
 * @brief 初始化调度器
 *
 * @details 初始化所有 CPU 的就绪队列，创建每 CPU 的 idle 线程
 *
 * @return 成功返回 KERNEL_OK，失败返回负错误码
 *
 * @retval -ENOMEM 内存不足，无法创建 idle 线程
 */
kernel_status_t scheduler_init(void);

/**
 * @brief 启动调度器（永不返回）
 *
 * @details 切换到第一个任务（idle 线程），开始调度循环
 *
 * @warning 此函数不返回
 */
void NORETURN scheduler_start(void);

/**
 * @brief 从核启动调度器（永不返回）
 *
 * @details 从核完成初始化后调用，选择最高优先级就绪线程并切换。
 *          与 scheduler_start() 类似，但从核的 current_thread 初始为 NULL。
 *
 * @warning 此函数不返回
 */
void NORETURN scheduler_start_secondary(void);

/**
 * @brief 触发调度
 *
 * @details 保存当前线程上下文，选择最高优先级就绪线程并切换
 */
void schedule(void);

/**
 * @brief 将线程加入就绪队列
 *
 * @param thread 要入队的线程指针
 */
void scheduler_enqueue(KThread_t *thread);

/**
 * @brief 将线程从就绪队列移除
 *
 * @param thread 要出队的线程指针
 */
void scheduler_dequeue(KThread_t *thread);

/**
 * @brief O(1) 选择最高优先级就绪线程
 *
 * @return 最高优先级就绪线程指针，无就绪线程时返回 idle 线程
 */
KThread_t *scheduler_pick_next(void);

/**
 * @brief O(1) 选择最高优先级就绪线程（调用者已持锁）
 *
 * @details 在调用者已持有 cpu_q->lock 的情况下执行位图查找。
 *          供 schedule() 在锁内调用，避免嵌套加锁。
 *          不返回 idle 线程（idle 由调用者回退处理）。
 *
 * @return 最高优先级就绪线程指针，无就绪线程时返回 NULL
 */
KThread_t *scheduler_pick_next_locked(void);

/**
 * @brief 时钟滴答处理
 *
 * @details 处理 RR 时间片递减，时间片耗尽时触发调度。
 *          本函数可能在中断上下文中调用，故不直接调 schedule()，
 *          而是置位 need_resched，由 IRQ 出口或 idle 线程检查后调度。
 */
void scheduler_tick(void);

/**
 * @brief 设置当前 CPU 的 need_resched 标志
 *
 * @details 在中断上下文中检测到需要重新调度时调用。
 *          仅置位标志，不执行实际的上下文切换（避免在中断中切换栈）。
 */
void scheduler_set_need_resched(void);

/**
 * @brief 检查并清除当前 CPU 的 need_resched 标志
 *
 * @return 非 0 表示需要重新调度（标志已清除），0 表示无需调度
 *
 * @details 由 IRQ 出口路径或 idle 线程调用。读取后自动清除标志。
 */
uint32_t scheduler_test_and_clear_need_resched(void);

/**
 * @brief 中断返回前的重调度检查
 *
 * @details 在 IRQ 处理完成、返回到被中断线程之前调用。
 *          若 need_resched 已置位，则在此处（中断已关闭、即将退出）
 *          调用 schedule()，确保上下文切换不发生在 IRQ 嵌套栈帧中。
 *          schedule() 内的 context_switch 会切栈，但切换发生在 IRQ 即将
 *          返回的边界，符合 ARM64 中断返回语义。
 */
void scheduler_irq_exit_check(void);

/**
 * @brief 设置当前 CPU 的运行线程
 *
 * @param thread 要设置为当前的线程指针
 */
void scheduler_load_current(KThread_t *thread);

#endif /* KERNEL_SCHED_SCHEDULER_H */
