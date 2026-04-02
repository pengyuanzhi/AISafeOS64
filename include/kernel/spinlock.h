/**
 * @file spinlock.h
 * @brief Ticket Lock 公平自旋锁接口
 * @author AISafe64 Team
 * @date 2026-03-31
 * @version 2.0
 *
 * @details Ticket Lock 公平自旋锁
 *          - FIFO 顺序保证公平性
 *          - 适用于内核临界区（短时间持有）
 *          - 禁止在持锁期间睡眠
 *          - 支持嵌套检测
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-004（内核同步原语）
 */

#ifndef KERNEL_SPINLOCK_H
#define KERNEL_SPINLOCK_H

#include <stdint.h>
#include <kernel/barrier.h>
#include <kernel/compiler.h>

/* ========== Ticket Lock 定义 ========== */

/**
 * @brief Ticket Lock 结构
 *
 * @details 使用两张票实现 FIFO 公平锁
 *          - next_ticket: 下一个要发放的票号
 *          - serving_ticket: 当前正在服务的票号
 *          - 当 next_ticket == serving_ticket 时，锁空闲
 */
typedef struct
{
    volatile uint32_t next_ticket;    /**< @brief 下一个发放的票号 */
    volatile uint32_t serving_ticket; /**< @brief 当前服务的票号 */
    uint32_t cpu_id;                  /**< @brief 持有者的 CPU ID（调试用） */
    uint32_t nest_count;              /**< @brief 嵌套计数 */
} TicketLock_t;

/** @brief Ticket Lock 静态初始化 */
#define TICKET_LOCK_INIT { 0U, 0U, 0xFFFFFFFFU, 0U }

/* ========== 自旋锁 API ========== */

/**
 * @brief 初始化 Ticket Lock
 * @param lock 锁指针
 */
void ticket_lock_init(TicketLock_t *lock);

/**
 * @brief 获取 Ticket Lock（阻塞自旋）
 * @param lock 锁指针
 *
 * @details 获取一个票号，自旋等待直到轮到自己
 *          使用 WFE 指令降低功耗
 */
void ticket_lock_acquire(TicketLock_t *lock);

/**
 * @brief 释放 Ticket Lock
 * @param lock 锁指针
 *
 * @details 递增 serving_ticket，唤醒下一个等待者
 */
void ticket_lock_release(TicketLock_t *lock);

/**
 * @brief 尝试获取 Ticket Lock（非阻塞）
 * @param lock 锁指针
 * @return true 成功获取，false 锁已被占用
 */
bool ticket_lock_try_acquire(TicketLock_t *lock);

/**
 * @brief 检查 Ticket Lock 是否被持有
 * @param lock 锁指针
 * @return true 被持有，false 空闲
 */
bool ticket_lock_is_held(const TicketLock_t *lock);

/* ========== 自旋锁保护宏 ========== */

/**
 * @brief 自旋锁保护临界区宏
 * @param lock 锁指针
 * @details 在代码块开始时自动加锁，结束时自动解锁
 *
 * @par 示例
 * @code
 * TICKET_LOCK_GUARD(&my_lock)
 * {
 *     // 临界区代码
 * }
 * @endcode
 */

/* ========== IRQ 安全自旋锁 ========== */

/**
 * @brief 保存 IRQ 状态并获取自旋锁
 * @param lock 锁指针
 * @return 保存的 IRQ 状态
 *
 * @details 在获取锁之前禁用 IRQ，适合在中断上下文使用
 */
uint32_t ticket_lock_acquire_irqsave(TicketLock_t *lock);

/**
 * @brief 释放自旋锁并恢复 IRQ 状态
 * @param lock 锁指针
 * @param irq_state 之前保存的 IRQ 状态
 */
void ticket_lock_release_irqrestore(TicketLock_t *lock, uint32_t irq_state);

#endif /* KERNEL_SPINLOCK_H */
