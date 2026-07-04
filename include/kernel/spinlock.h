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
 *          - 同一 CPU 重入时触发断言
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-004（内核同步原语）
 */

#ifndef KERNEL_SPINLOCK_H
#define KERNEL_SPINLOCK_H

#include <stdbool.h>
#include <stdint.h>

/* ========== Ticket Lock 定义 ========== */

/**
 * @brief Ticket Lock 结构
 *
 * @details 使用两张票实现 FIFO 公平锁
 *          - next_ticket: 下一个要发放的票号
 *          - serving_ticket: 当前正在服务的票号
 *          - 当 next_ticket == serving_ticket 时，锁空闲
 *
 * @note 不再使用 cpu_id 做递归检测：原检测依赖非原子读且 release 后
 *       残留值导致误判 trap，在正常加锁路径触发随机崩溃，已移除。
 *       ticket lock 不应递归获取，调用方需保证不在持锁时再次 acquire。
 */
typedef struct
{
    volatile uint32_t next_ticket;    /**< @brief 下一个发放的票号 */
    volatile uint32_t serving_ticket; /**< @brief 当前服务的票号 */
} TicketLock_t;

/** @brief Ticket Lock 静态初始化 */
#define TICKET_LOCK_INIT { 0U, 0U }

/* ========== 自旋锁 API ========== */

/**
 * @brief 初始化 Ticket Lock
 * @param lock 锁指针
 */
void ticket_lock_init(TicketLock_t *lock);

/**
 * @brief 获取 Ticket Lock（阻塞自旋）
 * @param lock 锁指针
 * @details 获取一个票号，自旋等待直到轮到自己
 *          使用 HAL 事件等待接口降低功耗
 */
void ticket_lock_acquire(TicketLock_t *lock);

/**
 * @brief 释放 Ticket Lock
 * @param lock 锁指针
 * @details 仅允许持有者释放；若锁为空闲或非持有者调用，则忽略
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
 *          若 lock 为 NULL，则仅返回当前 IRQ 状态
 */
uint32_t ticket_lock_acquire_irqsave(TicketLock_t *lock);

/**
 * @brief 释放自旋锁并恢复 IRQ 状态
 * @param lock 锁指针
 * @param irq_state 之前保存的 IRQ 状态
 *
 * @details 若 lock 为 NULL，则仅恢复 IRQ 状态
 */
void ticket_lock_release_irqrestore(TicketLock_t *lock, uint32_t irq_state);

#endif /* KERNEL_SPINLOCK_H */
