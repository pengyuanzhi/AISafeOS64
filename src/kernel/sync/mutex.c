/**
 * @file mutex.c
 * @brief AISafe64 RTOS - 互斥锁实现
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 互斥锁实现
 *          - 支持递归锁
 *          - 支持优先级继承（占位符）
 *          - 支持优先级天花板
 *
 * @note MISRA-C:2012合规
 * @note 后续集成任务管理器实现完整的优先级继承
 */

#include "sync.h"
#include "types.h"

/**
 * @brief 获取当前任务ID（占位符）
 * @return 任务ID（暂时返回0）
 */
static inline void *get_current_task(void)
{
    /* TODO: 集成任务管理器 */
    return (void *)0UL;
}

/**
 * @brief 获取当前任务优先级（占位符）
 * @return 优先级（暂时返回0）
 */
static inline uint32_t get_current_priority(void)
{
    /* TODO: 集成任务管理器 */
    return 0U;
}

/**
 * @brief 设置任务优先级（占位符）
 * @param task 任务指针
 * @param priority 新优先级
 */
static inline void set_task_priority(void *task, uint32_t priority)
{
    /* TODO: 集成任务管理器实现优先级继承 */
    (void)task;
    (void)priority;
}

/**
 * @brief 互斥锁初始化
 * @param mutex 互斥锁指针
 * @param ceiling_priority 优先级天花板（0表示不使用）
 *
 * @details 初始化互斥锁
 *          - 初始状态：未锁定
 *          - 可选优先级天花板协议
 */
void mutex_init(mutex_t *mutex, uint32_t ceiling_priority)
{
    if (mutex == NULL) {
        return;
    }

    mutex->locked = 0U;
    mutex->owner = NULL;
    mutex->owner_priority = 0U;
    mutex->ceiling_priority = ceiling_priority;
    mutex->lock_count = 0U;
}

/**
 * @brief 互斥锁加锁
 * @param mutex 互斥锁指针
 * @return 成功返回0，失败返回负错误码
 *
 * @details 支持递归锁和优先级继承
 *          - 如果锁已被当前任务持有，增加递归计数
 *          - 否则阻塞等待锁
 */
int mutex_lock(mutex_t *mutex)
{
    if (mutex == NULL) {
        return -ERROR_INVALID_PARAM;
    }

    void *current_task = get_current_task();

    /* 检查是否为递归锁（当前任务已持有） */
    if ((mutex->owner == current_task) && (mutex->locked != 0U)) {
        mutex->lock_count++;
        return ERROR_SUCCESS;
    }

    /* 尝试获取锁 */
    while (1) {
        /* 原子尝试加锁（使用标准原子操作） */
        if (atomic_cas_u32(&mutex->locked, 0U, 1U)) {
            /* 成功获取锁 */
            break;
        }

        /* 失败，等待（TODO: 阻塞当前任务） */
        WFE();
    }

    /* 设置锁持有者 */
    mutex->owner = current_task;
    mutex->lock_count = 1U;

    /* 优先级天花板协议 */
    if (mutex->ceiling_priority != 0U) {
        uint32_t current_priority = get_current_priority();
        if (current_priority < mutex->ceiling_priority) {
            /* 提升优先级到天花板 */
            set_task_priority(current_task, mutex->ceiling_priority);
            mutex->owner_priority = current_priority;
        } else {
            mutex->owner_priority = current_priority;
        }
    } else {
        mutex->owner_priority = get_current_priority();
    }

    return ERROR_SUCCESS;
}

/**
 * @brief 互斥锁尝试加锁
 * @param mutex 互斥锁指针
 * @return 成功返回0，失败返回负错误码
 *
 * @details 非阻塞模式
 */
int mutex_trylock(mutex_t *mutex)
{
    if (mutex == NULL) {
        return -ERROR_INVALID_PARAM;
    }

    void *current_task = get_current_task();

    /* 检查是否为递归锁 */
    if ((mutex->owner == current_task) && (mutex->locked != 0U)) {
        mutex->lock_count++;
        return ERROR_SUCCESS;
    }

    /* 尝试获取锁（非阻塞，使用标准原子操作） */
    if (!atomic_cas_u32(&mutex->locked, 0U, 1U)) {
        /* 锁已被其他任务持有 */
        return -ERROR_BUSY;
    }

    /* 成功获取锁 */
    mutex->owner = current_task;
    mutex->lock_count = 1U;

    /* 优先级天花板协议 */
    if (mutex->ceiling_priority != 0U) {
        uint32_t current_priority = get_current_priority();
        if (current_priority < mutex->ceiling_priority) {
            set_task_priority(current_task, mutex->ceiling_priority);
            mutex->owner_priority = current_priority;
        } else {
            mutex->owner_priority = current_priority;
        }
    } else {
        mutex->owner_priority = get_current_priority();
    }

    return ERROR_SUCCESS;
}

/**
 * @brief 互斥锁解锁
 * @param mutex 互斥锁指针
 * @return 成功返回0，失败返回负错误码
 *
 * @details 释放互斥锁
 *          - 递归锁计数减1
 *          - 恢复原始优先级
 */
int mutex_unlock(mutex_t *mutex)
{
    if (mutex == NULL) {
        return -ERROR_INVALID_PARAM;
    }

    void *current_task = get_current_task();

    /* 检查锁是否被当前任务持有 */
    if (mutex->owner != current_task) {
        /* 当前任务未持有锁 */
        return -ERROR_INVALID_STATE;
    }

    /* 递归锁计数减1 */
    mutex->lock_count--;
    if (mutex->lock_count != 0U) {
        /* 递归锁尚未完全释放 */
        return ERROR_SUCCESS;
    }

    /* 恢复原始优先级 */
    if (mutex->ceiling_priority != 0U) {
        if (mutex->owner_priority < mutex->ceiling_priority) {
            set_task_priority(current_task, mutex->owner_priority);
        }
    }

    /* 内存屏障确保临界区操作完成 */
    MEMORY_BARRIER();

    /* 释放锁 */
    mutex->locked = 0U;
    mutex->owner = NULL;

    /* 唤醒等待的任务 */
    SEVL();

    return ERROR_SUCCESS;
}

/**
 * @brief 互斥锁持有者检查
 * @param mutex 互斥锁指针
 * @return 当前任务持有返回true
 */
bool mutex_is_held(mutex_t *mutex)
{
    if (mutex == NULL) {
        return false;
    }

    return (mutex->owner == get_current_task()) && (mutex->locked != 0U);
}
