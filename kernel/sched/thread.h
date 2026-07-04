/**
 * @file    thread.h
 * @brief   内核线程控制块定义
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 本文件定义了内核线程控制块（TCB）及相关数据结构：
 *          - 线程状态枚举（KThreadState_t）
 *          - 调度策略枚举（KThreadPolicy_t）
 *          - 线程控制块结构（KThread_t）
 *          - 线程管理 API（创建/退出/挂起/恢复/设置优先级）
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SC-001~008（调度器相关）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_SCHED_THREAD_H
#define KERNEL_SCHED_THREAD_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/list.h>
#include <kernel/alignment.h>
#include <kernel/stack_guard.h>
#include <stdint.h>

/* ========================================================================
 * 上下文保存区大小定义
 * ======================================================================== */

/**
 * @brief ARM64 上下文保存区寄存器数量
 *
 * @details 保存的 callee-saved 寄存器：
 *          - x19 ~ x28 : 10 个通用寄存器
 *          - x29 (FP)  : 帧指针
 *          - x30 (LR)  : 链接寄存器
 *          - SP_el1     : 栈指针
 *          - SPSR_EL1   : 保存的程序状态（EL1h=0x5 / EL0t=0x0）
 *          - ELR_EL1    : 异常返回地址
 *          总计：15 个 uint64_t
 *
 * @note context_switch 仅保存/恢复前 13 项（x19-x28, FP, LR, SP）。
 *       cpu_switch_to_first_task 通过 eret 使用 context[13] SPSR 和
 *       context[14] ELR 返回到正确的异常级别。
 */
#define KTHREAD_CONTEXT_REGS 15U

/**
 * @brief 线程名称最大长度（含终止符）
 */
#define KTHREAD_NAME_MAX 16U

/* ========================================================================
 * 线程状态枚举
 * ======================================================================== */

/**
 * @brief 内核线程状态
 *
 * @details 定义线程在生命周期中的所有可能状态
 */
typedef enum
{
    KTHREAD_STATE_DEAD      = 0U, /**< @brief 已死亡（未使用或已退出） */
    KTHREAD_STATE_READY     = 1U, /**< @brief 就绪（等待 CPU 调度） */
    KTHREAD_STATE_RUNNING   = 2U, /**< @brief 运行中（正在 CPU 上执行） */
    KTHREAD_STATE_BLOCKED   = 3U, /**< @brief 阻塞（等待资源） */
    KTHREAD_STATE_SLEEPING  = 4U, /**< @brief 睡眠（定时唤醒） */
    KTHREAD_STATE_SUSPENDED = 5U  /**< @brief 挂起（需显式恢复） */
} KThreadState_t;

/* ========================================================================
 * 调度策略枚举
 * ======================================================================== */

/**
 * @brief 线程调度策略
 *
 * @details - FIFO : 先进先出，不使用时间片，一直运行直到阻塞或更高优先级就绪
 *          - RR   : 轮转调度，同优先级线程之间按时间片轮转
 */
typedef enum
{
    KTHREAD_POLICY_FIFO = 0U, /**< @brief 先进先出策略 */
    KTHREAD_POLICY_RR   = 1U  /**< @brief 轮转调度策略 */
} KThreadPolicy_t;

/* ========================================================================
 * 前向声明
 * ======================================================================== */

/** @brief 线程入口函数类型 */
typedef void (*kthread_entry_t)(void *arg);

/* ========================================================================
 * 线程控制块
 * ======================================================================== */

/**
 * @brief 内核线程控制块（KThread / TCB）
 *
 * @details 包含线程的所有状态信息：
 *          - 上下文保存区（callee-saved 寄存器）
 *          - 线程标识信息（tid、name）
 *          - 调度信息（priority、policy、time_slice）
 *          - 栈信息（base、size）
 *          - 链表节点（就绪队列、睡眠队列）
 */
typedef struct KThread
{
    uint64_t context[KTHREAD_CONTEXT_REGS]; /**< @brief 上下文保存区 */
    thread_id_t tid;       /**< @brief 线程 ID */
    KThreadState_t state;  /**< @brief 线程当前状态 */
    kthread_entry_t entry; /**< @brief 线程入口函数 */
    void *entry_arg;       /**< @brief 入口函数参数 */
    vaddr_t stack_base;    /**< @brief 栈基地址 */
    uint32_t stack_size;   /**< @brief 栈大小（字节） */
    priority_t prio;       /**< @brief 当前优先级 */
    KThreadPolicy_t policy; /**< @brief 调度策略 */
    uint32_t time_slice;   /**< @brief 剩余时间片 */
    uint32_t time_slice_reload; /**< @brief 时间片重载值 */
    struct list_head rq_list;  /**< @brief 就绪队列链表节点 */
    struct list_head sleep_node; /**< @brief 睡眠队列链表节点 */
    tick_t wakeup_tick;    /**< @brief 唤醒时刻（绝对 tick） */
    uint8_t is_user;       /**< @brief 是否为用户态线程（非0=EL0线程） */
    uint8_t _reserved[3];  /**< @brief 填充对齐 */
    vaddr_t user_sp;       /**< @brief 用户态栈指针（EL0 线程使用） */
    uint64_t user_pgd;     /**< @brief 用户态 PGD 物理地址（EL0 线程使用） */
    stack_guard_config_t guard; /**< @brief 栈金丝雀保护配置 */
    char name[KTHREAD_NAME_MAX]; /**< @brief 线程名称 */
} KThread_t;

/* ========================================================================
 * 线程管理 API
 * ======================================================================== */

/**
 * @brief 创建内核线程
 *
 * @param name      线程名称（不得为 NULL，最长 15 字符）
 * @param entry     线程入口函数指针（不得为 NULL）
 * @param arg       入口函数参数（可为 NULL）
 * @param prio      线程优先级（0-255）
 * @param policy    调度策略（FIFO 或 RR）
 * @param stack_size 栈大小（字节，最小 CONFIG_STACK_SIZE_MIN）
 *
 * @return 成功返回线程 ID，失败返回 THREAD_ID_INVALID
 */
thread_id_t kthread_create(const char *name,
                           kthread_entry_t entry,
                           void *arg,
                           priority_t prio,
                           KThreadPolicy_t policy,
                           uint32_t stack_size);

/**
 * @brief 线程退出
 *
 * @details 标记当前线程为 DEAD 状态并触发调度。
 *          此函数不返回。
 *
 * @warning 入口函数不应直接返回，应调用此函数退出
 */
void kthread_exit(void);

/**
 * @brief 挂起指定线程
 *
 * @param tid 要挂起的线程 ID
 *
 * @return 成功返回 KERNEL_OK，失败返回负错误码
 *
 * @retval -ESRCH  线程不存在
 * @retval -EINVAL 线程不在就绪状态
 */
kernel_status_t kthread_suspend(thread_id_t tid);

/**
 * @brief 恢复指定线程
 *
 * @param tid 要恢复的线程 ID
 *
 * @return 成功返回 KERNEL_OK，失败返回负错误码
 *
 * @retval -ESRCH  线程不存在
 * @retval -EINVAL 线程不在挂起状态
 */
kernel_status_t kthread_resume(thread_id_t tid);

/**
 * @brief 设置线程优先级
 *
 * @param tid  要设置的线程 ID
 * @param prio 新的优先级值（0-255）
 *
 * @return 成功返回 KERNEL_OK，失败返回负错误码
 *
 * @retval -ESRCH   线程不存在
 * @retval -EINVAL  优先级值超出范围
 */
kernel_status_t kthread_set_priority(thread_id_t tid, priority_t prio);

/**
 * @brief 获取当前运行线程的指针
 *
 * @return 当前线程的 KThread_t 指针，无当前线程时返回 NULL
 */
KThread_t *kthread_get_current(void);

/**
 * @brief 获取当前运行线程的 ID
 *
 * @details 便捷函数，等价于 kthread_get_current()->tid，
 *          但无需暴露 KThread_t 结构体定义。
 *
 * @return 当前线程 ID，无当前线程时返回 THREAD_ID_INVALID
 */
thread_id_t kthread_get_current_tid(void);

/**
 * @brief 清理 DEAD 线程的栈空间
 *
 * @details 遍历所有线程，清理 DEAD 线程的栈空间
 *          释放后清零栈基址和大小字段
 */
void kthread_cleanup_dead_stacks(void);

#endif /* KERNEL_SCHED_THREAD_H */
