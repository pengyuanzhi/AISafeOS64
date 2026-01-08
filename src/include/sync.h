/**
 * @file sync.h
 * @brief AISafe64 RTOS - 同步原语接口
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 同步原语接口定义
 *          - 自旋锁（Spinlock）
 *          - 互斥锁（Mutex）
 *          - 信号量（Semaphore）
 *
 * @note ARMv8-A原子操作（LDXR/STXR）
 * @note MISRA-C:2012合规
 */

#ifndef SYNC_H
#define SYNC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* 架构特定的内存屏障和原子操作 */
#include "barrier.h"

/* 链表结构 */
#include "list.h"

/* Forward declarations */
struct TCB_t;

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 自旋锁结构
     * @details Ticket Lock算法
     */
    typedef struct
    {
        volatile uint32_t next_ticket;    /**< 下一个票据 */
        volatile uint32_t serving_ticket; /**< 当前服务的票据 */
    } spinlock_t;

    /**
     * @brief 互斥锁结构
     * @details 支持优先级继承
     */
    typedef struct
    {
        volatile uint32_t locked;  /**< 锁状态 */
        void *owner;               /**< 持有者任务 */
        uint32_t owner_priority;   /**< 持有者优先级 */
        uint32_t ceiling_priority; /**< 优先级天花板 */
        uint32_t lock_count;       /**< 递归锁计数 */
    } mutex_t;

    /**
     * @brief 信号量等待节点
     * @details 用于维护信号量等待队列
     */
    typedef struct semaphore_wait_node
    {
        struct list_head wait_list; /**< 链表节点 */
        struct TCB_t *task;         /**< 等待的任务 */
        uint64_t timeout;           /**< 超时时间（可选） */
    } semaphore_wait_node_t;

    /**
     * @brief 信号量结构
     * @details 支持二值信号量和计数信号量
     *
     * @note 锁顺序：先获取 lock，再操作 wait_queue 和 count
     */
    typedef struct
    {
        volatile int32_t count;      /**< 计数 */
        uint32_t max_count;          /**< 最大计数 */
        struct list_head wait_queue; /**< 等待队列 */
        spinlock_t lock;             /**< 保护 wait_queue 和状态转换 */
    } semaphore_t;

    /**
     * @brief 自旋锁初始化
     * @param lock 自旋锁指针
     */
    void spinlock_init(spinlock_t *lock);

    /**
     * @brief 自旋锁加锁
     * @param lock 自旋锁指针
     *
     * @details Ticket Lock算法，忙等待
     */
    void spinlock_lock(spinlock_t *lock);

    /**
     * @brief 自旋锁尝试加锁
     * @param lock 自旋锁指针
     * @return 成功返回true，失败返回false
     */
    bool spinlock_trylock(spinlock_t *lock);

    /**
     * @brief 自旋锁解锁
     * @param lock 自旋锁指针
     */
    void spinlock_unlock(spinlock_t *lock);

    /**
     * @brief 互斥锁初始化
     * @param mutex 互斥锁指针
     * @param ceiling_priority 优先级天花板（0表示不使用）
     */
    void mutex_init(mutex_t *mutex, uint32_t ceiling_priority);

    /**
     * @brief 互斥锁加锁
     * @param mutex 互斥锁指针
     * @return 成功返回0，失败返回负错误码
     *
     * @details 支持递归锁和优先级继承
     */
    int mutex_lock(mutex_t *mutex);

    /**
     * @brief 互斥锁尝试加锁
     * @param mutex 互斥锁指针
     * @return 成功返回0，失败返回负错误码
     */
    int mutex_trylock(mutex_t *mutex);

    /**
     * @brief 互斥锁解锁
     * @param mutex 互斥锁指针
     * @return 成功返回0，失败返回负错误码
     */
    int mutex_unlock(mutex_t *mutex);

    /**
     * @brief 互斥锁持有者检查
     * @param mutex 互斥锁指针
     * @return 当前任务持有返回true
     */
    bool mutex_is_held(mutex_t *mutex);

    /**
     * @brief 信号量初始化
     * @param sem 信号量指针
     * @param initial_count 初始计数
     * @param max_count 最大计数
     * @return 成功返回0，失败返回负错误码
     */
    int semaphore_init(semaphore_t *sem, int32_t initial_count, uint32_t max_count);

    /**
     * @brief 信号量等待（P操作）
     * @param sem 信号量指针
     * @return 成功返回0，失败返回负错误码
     *
     * @details 如果计数为0，阻塞当前任务（无限等待）
     *
     * @note 必须在任务上下文中调用，不能在中断中调用
     */
    int semaphore_wait(semaphore_t *sem);

    /**
     * @brief 信号量等待（P操作，带超时）
     * @param sem 信号量指针
     * @param timeout_ms 超时时间（毫秒），0表示非阻塞，UINT64_MAX表示无限等待
     * @return 成功返回0，失败返回负错误码
     *
     * @details 如果计数为0，阻塞当前任务
     *          - 支持0超时（非阻塞）
     *          - 支持有限超时
     *          - 支持无限等待
     *
     * @note 必须在任务上下文中调用，不能在中断中调用
     */
    int semaphore_wait_timeout(semaphore_t *sem, uint64_t timeout_ms);

    /**
     * @brief 信号量尝试等待
     * @param sem 信号量指针
     * @return 成功返回0，失败返回负错误码
     *
     * @details 非阻塞模式
     */
    int semaphore_trywait(semaphore_t *sem);

    /**
     * @brief 信号量释放（V操作）
     * @param sem 信号量指针
     * @return 成功返回0，失败返回负错误码
     *
     * @details 释放资源，计数加1，唤醒等待的任务
     *
     * @note 可以在任务上下文或中断上下文中调用
     *       典型场景：中断处理程序释放信号量
     */
    int semaphore_post(semaphore_t *sem);

    /**
     * @brief 获取信号量计数
     * @param sem 信号量指针
     * @return 当前计数
     */
    int32_t semaphore_getcount(semaphore_t *sem);

/**
 * @brief 临界区保护宏（自旋锁）
 */
#define CRITICAL_ENTER(lock) spinlock_lock(lock)

#define CRITICAL_EXIT(lock) spinlock_unlock(lock)

/**
 * @brief RAII风格的自旋锁（需要编译器支持cleanup属性）
 */
#define SCOPE_SPINLOCK(lock)                                                       \
    spinlock_t *__scope_lock __attribute__((cleanup(spinlock_cleanup))) = &(lock); \
    spinlock_lock(__scope_lock)

    /**
     * @brief 自旋锁清理函数（用于RAII）
     */
    static inline void spinlock_cleanup(spinlock_t **lock)
    {
        if (lock != NULL && *lock != NULL)
        {
            spinlock_unlock(*lock);
        }
    }

/**
 * @brief 自旋锁+中断禁用宏
 * @details 用于调度器和中断处理程序
 */

/**
 * @brief 保存中断状态并加锁
 */
#define spin_lock_irqsave(lock, flags)  \
    do                                  \
    {                                   \
        flags = irq_save_and_disable(); \
        spinlock_lock(&(lock));         \
    } while (0)

/**
 * @brief 恢复中断状态并解锁
 */
#define spin_unlock_irqrestore(lock, flags) \
    do                                      \
    {                                       \
        spinlock_unlock(&(lock));           \
        irq_restore(flags);                 \
    } while (0)

/**
 * @brief 禁用中断并加锁
 */
#define spin_lock_irq(lock)     \
    do                          \
    {                           \
        irq_disable_global();   \
        spinlock_lock(&(lock)); \
    } while (0)

/**
 * @brief 解锁并使能中断
 */
#define spin_unlock_irq(lock)     \
    do                            \
    {                             \
        spinlock_unlock(&(lock)); \
        irq_enable_global();      \
    } while (0)

/**
 * @brief 禁用底部中断并加锁
 */
#define spin_lock_bh(lock) spin_lock_irqsave((lock), flags)

/**
 * @brief 解锁并使能底部中断
 */
#define spin_unlock_bh(lock) spin_unlock_irqrestore((lock), flags)

#ifdef __cplusplus
}
#endif

#endif /* SYNC_H */
