/**
 * @file mutex.h
 * @brief 内核互斥锁接口
 * @author AISafe64 Team
 * @date 2026-03-31
 * @version 2.0
 *
 * @details 内核互斥锁实现
 *          - 支持优先级继承（Priority Inheritance）
 *          - 支持优先级天花板协议（Priority Ceiling）
 *          - 仅在任务上下文使用（禁止中断中使用）
 *          - 支持递归锁定检测
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-004（内核同步原语）
 */

#ifndef KERNEL_MUTEX_H
#define KERNEL_MUTEX_H

#include <stdint.h>
#include <stdbool.h>
#include <kernel/types.h>
#include <kernel/list.h>
#include <kernel/config.h>
#include <stdbool.h>

/* ========== 互斥锁定义 ========== */

/** @brief 互斥锁状态 */
typedef enum
{
    MUTEX_STATE_UNLOCKED = 0U, /**< @brief 未锁定 */
    MUTEX_STATE_LOCKED,       /**< @brief 已锁定 */
    MUTEX_STATE_CONTENDED     /**< @brief 有竞争者等待 */
} MutexState_t;

/**
 * @brief 内核互斥锁结构
 *
 * @details 支持优先级继承的互斥锁
 *          当低优先级线程持锁且高优先级线程等待时，
 *          低优先级线程的优先级临时提升到等待者的优先级
 */
typedef struct
{
    volatile uint32_t owner_tid;     /**< @brief 持有者线程 ID（0=无人持有） */
    MutexState_t state;             /**< @brief 锁状态 */
    priority_t ceiling;             /**< @brief 优先级天花板（0=不使用天花板协议） */
    priority_t original_prio;       /**< @brief 持有者原始优先级（用于恢复） */
    uint32_t lock_count;            /**< @brief 锁定计数（调试用） */
    struct list_head wait_queue;    /**< @brief 等待队列 */
} Mutex_t;

/** @brief 互斥锁静态初始化 */
#define MUTEX_INIT { 0U, MUTEX_STATE_UNLOCKED, 0U, 0U, 0U, { NULL, NULL } }

/** @brief 带优先级天花板的互斥锁静态初始化 */
#define MUTEX_INIT_WITH_CEILING(ceil) { 0U, MUTEX_STATE_UNLOCKED, (ceil), 0U, 0U, { NULL, NULL } }

/* ========== 互斥锁 API ========== */

/**
 * @brief 初始化互斥锁
 * @param mutex 互斥锁指针
 * @param ceiling 优先级天花板（0=不使用天花板协议）
 */
void mutex_init(Mutex_t *mutex, priority_t ceiling);

/**
 * @brief 获取互斥锁（阻塞）
 * @param mutex 互斥锁指针
 * @return 0 成功，负数错误码
 *
 * @details 如果锁已被占用，当前线程进入等待队列并阻塞
 *          支持优先级继承：如果等待者优先级高于持有者，
 *          持有者优先级临时提升
 *
 * @warning 只能在任务上下文调用，禁止在中断中使用
 */
int32_t mutex_lock(Mutex_t *mutex);

/**
 * @brief 释放互斥锁
 * @param mutex 互斥锁指针
 * @return 0 成功，负数错误码
 *
 * @details 释放锁，恢复原始优先级，唤醒等待队列中最高优先级线程
 */
int32_t mutex_unlock(Mutex_t *mutex);

/**
 * @brief 尝试获取互斥锁（非阻塞）
 * @param mutex 互斥锁指针
 * @return 0 成功获取，-EBUSY 锁已被占用
 */
int32_t mutex_try_lock(Mutex_t *mutex);

/**
 * @brief 带超时获取互斥锁
 * @param mutex 互斥锁指针
 * @param timeout_ms 超时时间（毫秒）
 * @return 0 成功，-ETIMEDOUT 超时，-EINVAL 参数无效
 */
int32_t mutex_lock_timeout(Mutex_t *mutex, uint32_t timeout_ms);

/**
 * @brief 检查互斥锁是否被持有
 * @param mutex 互斥锁指针
 * @return true 被持有，false 空闲
 */
bool mutex_is_held(const Mutex_t *mutex);

#endif /* KERNEL_MUTEX_H */
