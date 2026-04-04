/**
 * @file timer.h
 * @brief ARM 架构定时器接口
 * @author AISafe64 Team
 * @date 2026-03-31
 * @version 2.0
 *
 * @details ARMv8-A 通用定时器接口
 *          - 系统滴答管理
 *          - 软件定时器
 *          - 高精度时间戳
 *          - 线程延迟（sleep/wakeup）
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: TM-001~004
 */

#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/list.h>

/* ========== 前向声明 ========== */

/** @brief 内核线程结构（完整定义在 kernel/sched/thread.h） */
struct KThread;

/* ========== 类型定义 ========== */

/** @brief 系统滴答类型 */
typedef uint64_t tick_t;

/** @brief 软件定时器状态 */
typedef enum
{
    TIMER_STATE_IDLE = 0U,     /**< @brief 空闲（未激活） */
    TIMER_STATE_ACTIVE,       /**< @brief 活跃（等待触发） */
    TIMER_STATE_EXPIRED,      /**< @brief 已过期 */
    TIMER_STATE_CALLBACK      /**< @brief 回调执行中 */
} TimerState_t;

/** @brief 定时器回调函数类型 */
typedef void (*TimerCallback_t)(void *arg);

/**
 * @brief 软件定时器结构
 */
typedef struct SoftwareTimer
{
    tick_t expire_tick;       /**< @brief 过期时刻（绝对 tick） */
    tick_t interval;          /**< @brief 周期间隔（0=单次） */
    TimerCallback_t callback; /**< @brief 回调函数 */
    void *callback_arg;       /**< @brief 回调参数 */
    TimerState_t state;       /**< @brief 定时器状态 */
    struct list_head node;    /**< @brief 定时器队列节点 */
} SoftwareTimer_t;

/* ========== 核心定时器 API ========== */

/**
 * @brief 初始化 ARM 通用定时器
 *
 * @details 配置 CNTPCT_EL0 定时器
 *          - 设置比较器值
 *          - 使能定时器中断
 *          - 启动系统滴答
 */
void timer_init(void);

/**
 * @brief 刷新定时器比较值
 *
 * @details 重新读取物理计数器并设置下一次定时器中断比较值。
 *          应在启用 IRQ 前调用，确保定时器在 IRQ 启用后立即触发。
 */
void timer_set_next_compare(void);

/**
 * @brief 获取当前系统 tick 数
 * @return 当前 tick 值
 */
tick_t timer_get_ticks(void);

/**
 * @brief 获取高精度时间戳（纳秒）
 * @return 当前时间（纳秒）
 */
uint64_t timer_get_ns(void);

/**
 * @brief 获取高精度时间戳（微秒）
 * @return 当前时间（微秒）
 */
uint64_t timer_get_us(void);

/**
 * @brief 获取高精度时间戳（毫秒）
 * @return 当前时间（毫秒）
 */
uint64_t timer_get_ms(void);

/**
 * @brief 定时器中断处理函数
 *
 * @details 由 IRQ handler 调用
 *          - 更新系统 tick
 *          - 检查软件定时器
 *          - 触发调度器 tick
 */
void timer_interrupt_handler(void);

/* ========== 线程延迟 API ========== */

/**
 * @brief 当前线程延迟指定毫秒数
 * @param ms 延迟时间（毫秒）
 * @return 0 成功，负数错误码
 *
 * @note 会触发调度，当前线程进入 SLEEPING 状态
 */
int32_t kthread_sleep(uint32_t ms);

/**
 * @brief 当前线程延迟指定 tick 数
 * @param ticks 延迟 tick 数
 *
 * @note 会触发调度
 */
void kthread_sleep_ticks(tick_t ticks);

/**
 * @brief 唤醒因 sleep 阻塞的线程
 * @param thread 目标线程指针
 */
void timer_wakeup_thread(struct KThread *thread);

/* ========== 软件定时器 API ========== */

/**
 * @brief 初始化软件定时器
 * @param timer 定时器指针
 * @param callback 回调函数
 * @param arg 回调参数
 */
void timer_init_soft(SoftwareTimer_t *timer, TimerCallback_t callback, void *arg);

/**
 * @brief 启动单次软件定时器
 * @param timer 定时器指针
 * @param ms 延迟时间（毫秒）
 */
void timer_start_oneshot(SoftwareTimer_t *timer, uint32_t ms);

/**
 * @brief 启动周期软件定时器
 * @param timer 定时器指针
 * @param ms 周期（毫秒）
 */
void timer_start_periodic(SoftwareTimer_t *timer, uint32_t ms);

/**
 * @brief 停止软件定时器
 * @param timer 定时器指针
 */
void timer_stop(SoftwareTimer_t *timer);

/* ========== tick 与时间转换 ========== */

/** @brief 毫秒转 tick */
#define MS_TO_TICKS(ms) ((tick_t)((uint64_t)(ms) * (uint64_t)CONFIG_TICK_RATE_HZ / 1000U))

/** @brief tick 转毫秒 */
#define TICKS_TO_MS(ticks) ((uint64_t)((ticks) * 1000U / (uint64_t)CONFIG_TICK_RATE_HZ))

/** @brief 微秒转 tick */
#define US_TO_TICKS(us) ((tick_t)((uint64_t)(us) * (uint64_t)CONFIG_TICK_RATE_HZ / 1000000U))

#endif /* KERNEL_TIMER_H */
