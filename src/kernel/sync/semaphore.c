/**
 * @file semaphore.c
 * @brief AISafe64 RTOS - 信号量实现
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 信号量实现
 *          - 支持二值信号量（max_count=1）
 *          - 支持计数信号量
 *          - 原子操作保证线程安全
 *
 * @note MISRA-C:2012合规
 */

#include "sync.h"
#include "types.h"
#include "sched.h"
#include "irq.h"
#include <limits.h>
#include <stddef.h>

/**
 * @brief 信号量初始化
 * @param sem 信号量指针
 * @param initial_count 初始计数
 * @param max_count 最大计数
 * @return 成功返回0，失败返回负错误码
 *
 * @details 初始化信号量
 *          - 二值信号量：max_count=1
 *          - 计数信号量：max_count>1
 */
int semaphore_init(semaphore_t *sem, uint32_t initial_count, uint32_t max_count)
{
    if (sem == NULL)
    {
        return -EINVAL;
    }

    /* 参数验证 */
    if (max_count == 0U)
    {
        return -EINVAL;
    }

    if (initial_count > max_count)
    {
        return -EINVAL;
    }

    /* 初始化信号量 */
    sem->count = initial_count;
    sem->max_count = max_count;

    /* 初始化等待队列 */
    INIT_LIST_HEAD(&sem->wait_queue);

    /* 初始化自旋锁 */
    spinlock_init(&sem->lock);

    return 0;
}

/**
 * @brief 信号量等待（P操作）
 * @param sem 信号量指针
 * @return 成功返回0，失败返回负错误码
 *
 * @details 如果计数为0，阻塞当前任务（无限等待）
 *          - 成功：计数减1
 *          - 失败：阻塞等待
 */
int semaphore_wait(semaphore_t *sem)
{
    /* 调用带超时版本，UINT64_MAX表示无限等待 */
    return semaphore_wait_timeout(sem, UINT64_MAX);
}

/**
 * @brief 信号量尝试等待
 * @param sem 信号量指针
 * @return 成功返回0，失败返回负错误码
 *
 * @details 非阻塞模式
 *          - 如果计数>0，计数减1并返回成功
 *          - 如果计数=0，立即返回失败
 */
int semaphore_trywait(semaphore_t *sem)
{
    if (sem == NULL)
    {
        return -EINVAL;
    }

    /* 快速路径：重试循环避免 TOCTOU(Time-of-Check vs. Time-of-Use) race */
    for (;;)
    {
        uint32_t current_count = sem->count;

        /* 检查是否有可用资源 */
        if (current_count == 0U)
        {
            /* 计数为0，立即返回失败 */
            return -EAGAIN;
        }

        /* 尝试原子减1 */
        uint32_t expected = current_count;
        uint32_t desired = current_count - 1U;

        if (atomic_compare_exchange_strong((volatile uint32_t *)&sem->count, &expected, desired))
        {
            /* 成功获取信号量 */
            return 0;
        }

        /* CAS失败，重试（可能其他CPU刚释放）*/
        cpu_relax();
    }
}

/**
 * @brief 信号量等待（P操作，带超时)
 * @param sem 信号量指针
 * @param timeout_ms 超时时间（毫秒），0表示非阻塞，UINT64_MAX表示无限等待
 * @return 成功返回0，失败返回负错误码
 *
 * @details 如果计数为0，阻塞当前任务
 *          - 支持0超时（非阻塞）
 *          - 支持有限超时
 *          - 支持无限等待
 *
 * @note 使用约束：
 *       - timeout_ms = 0: 可以在任何上下文调用（非阻塞）
 *       - timeout_ms > 0: 必须在任务上下文中调用（不能在中断中调用）
 *       - 一个任务不能同时等待多个信号量（会返回 -EBUSY）
 *       - 典型场景：中断释放信号量（semaphore_post），任务等待信号量
 *
 * @lock_sem: 必须持有 sem->lock 保护 wait_queue
 */
int semaphore_wait_timeout(semaphore_t *sem, uint64_t timeout_ms)
{
    if (sem == NULL)
    {
        return -EINVAL;
    }

    /* 非阻塞模式：timeout_ms = 0 */
    if (timeout_ms == 0ULL)
    {
        return semaphore_trywait(sem);
    }

    /* 运行时检查：确保不在中断上下文中调用 */
    if (in_interrupt())
    {
        /* 在中断上下文中调用 semaphore_wait 是编程错误 */
        /* 根据使用约束：只能在中断中释放，不能在中断中获取 */
        return -EPERM;
    }

    /* 获取当前任务（确保在任务上下文） */
    TCB_t *current = get_current_task();

    /* 双重检查：确保 current 非空 */
    if (current == NULL)
    {
        /* 系统错误：不在中断中但也没有当前任务 */
        return -EPERM;
    }

    /* 检查：任务是否已经在等待某个信号量 */
    /* 通过检查 wait_list 是否为空来判断（如果已在队列中，list 不为空）*/
    if (!list_empty(&current->sem_wait_node.wait_list))
    {
        /* 任务正在等待另一个信号量，不允许同时等待多个 */
        return -EBUSY;
    }

    /* 首先尝试快速路径：检查是否有可用资源 */
    for (;;)
    {
        uint32_t current_count = sem->count;

        /* 检查是否有可用资源 */
        if (current_count > 0U)
        {
            /* 尝试原子减1 */
            uint32_t expected = current_count;
            uint32_t desired = current_count - 1U;

            if (atomic_compare_exchange_strong((volatile uint32_t *)&sem->count, &expected,
                                               desired))
            {
                /* 成功获取信号量 */
                return 0;
            }

            /* CAS失败，重试 */
            cpu_relax(); /* 退让策略，减少总线压力 */
            continue;
        }

        /* 计数为0，需要阻塞等待 */
        break;
    }

    /* 任务上下文：使用调度器阻塞 */
    /* 使用 TCB 中嵌入的 wait_node，避免栈上的 lifetime 问题 */

    current->sem_wait_node.task = current;
    current->sem_wait_node.timeout =
        (timeout_ms == UINT64_MAX) ? 0 : (sched_clock() + timeout_ms * 1000000ULL);

    /* 使用自旋锁保护等待队列 */
    spinlock_lock(&sem->lock);

    /* 将当前任务添加到等待队列（锁保护） */
    list_add_tail(&current->sem_wait_node.wait_list, &sem->wait_queue);

    /* 阻塞当前任务（状态转换需要锁保护） */
    current->state = TASK_BLOCKED;

    /* 释放锁，准备调度 */
    spinlock_unlock(&sem->lock);

    /*
     * schedule() 调用契约：
     *
     * 1. 调用前要求：
     *    - 必须在任务上下文中（不在中断中）
     *    - 当前任务状态已设置为 TASK_BLOCKED
     *    - 相关锁已释放
     *
     * 2. 调用效果：
     *    - 保存当前任务上下文
     *    - 选择下一个就绪任务运行
     *    - 可能经过很长时间才返回
     *
     * 3. 返回后保证：
     *    - 重新调度到当前任务
     *    - current 指针仍然有效（使用 TCB 中嵌入的 wait_node）
     *    - 任务状态可能已改变（被 semaphore_post 设置为 TASK_READY）
     *
     * 4. 注意事项：
     *    - schedule() 返回后必须重新获取锁
     *    - 不要假设 schedule() 返回时任务状态
     *    - 检查 current->state 判断唤醒原因
     */
    schedule();

    /* 唤醒后重新获取锁 */
    spinlock_lock(&sem->lock);

    /* 从等待队列中删除（总是执行，避免内存泄漏和UAF）*/
    /* 使用 list_del_init 重新初始化 wait_list，便于下次使用 */
    list_del_init(&current->sem_wait_node.wait_list);

    /* 释放锁 */
    spinlock_unlock(&sem->lock);

    /* 检查任务状态判断是否成功获取信号量 */
    if (current->state == TASK_BLOCKED)
    {
        /* 超时或被中断，任务仍然是阻塞状态 */
        /* 修复状态泄漏：恢复为 READY，避免后续调度器错误 */
        current->state = TASK_READY;
        return -ETIMEDOUT;
    }

    /* 成功获取信号量（semaphore_post中已设置state为TASK_READY） */
    return 0;
}

/**
 * @brief 信号量释放（V操作）
 * @param sem 信号量指针
 * @return 成功返回0，失败返回负错误码
 *
 * @details 释放资源，计数加1
 *          - 如果计数达到上限，返回错误
 *          - 唤醒等待的任务
 *
 * @note 使用约束：
 *       - 可以在任务上下文或中断上下文中调用
 *       - 典型场景：中断处理程序释放信号量，唤醒等待的任务
 *       - 中断安全：使用锁和原子操作保证线程安全
 *
 * @lock_sem: 必须持有 sem->lock 保护 wait_queue
 */
int semaphore_post(semaphore_t *sem)
{
    if (sem == NULL)
    {
        return -EINVAL;
    }

    for (;;)
    {
        uint32_t current_count = sem->count;

        /* 检查是否超过最大计数 */
        if (current_count >= sem->max_count)
        {
            /* 计数已达到上限 */
            return -EOVERFLOW;
        }

        /* 尝试原子加1（使用标准原子操作）*/
        uint32_t expected = current_count;
        uint32_t desired = current_count + 1U;

        if (atomic_compare_exchange_strong((volatile uint32_t *)&sem->count, &expected, desired))
        {
            /* 成功释放信号量 */

            /* 检查是否有任务在等待（需要锁保护）*/
            /* 使用中断安全的自旋锁，因为 semaphore_post 可能在中断中调用 */
            unsigned long flags;
            spin_lock_irqsave(sem->lock, flags);

            if (!list_empty(&sem->wait_queue))
            {
                /* 获取等待队列中的第一个任务 */
                struct list_head *entry = sem->wait_queue.next;
                semaphore_wait_node_t *wait_node =
                    list_entry(entry, semaphore_wait_node_t, wait_list);
                TCB_t *task = wait_node->task;

                /* 从等待队列中移除并重新初始化 */
                /* 使用 list_del_init 确保 wait_list 可被重用，避免 UAF */
                list_del_init(entry);

                /* 唤醒任务（在锁保护下设置状态） */
                if (task != NULL)
                {
                    task->state = TASK_READY;
                    enqueue_task(task);
                }
            }

            spin_unlock_irqrestore(sem->lock, flags);

            return 0;
        }

        /* CAS失败，立即重试 */
        cpu_relax();
    }
}

/**
 * @brief 获取信号量计数
 * @param sem 信号量指针
 * @param count 输出：当前计数
 * @return 成功返回0，失败返回负错误码
 *
 * @details 通过指针参数返回计数，避免返回值与错误码冲突
 */
int semaphore_getcount(semaphore_t *sem, uint32_t *count)
{
    if (sem == NULL || count == NULL)
    {
        return -EINVAL;
    }

    /* 原子读取（使用 acquire 内存序确保读取最新值）*/
    *count = atomic_load_acquire_u32((volatile uint32_t *)&sem->count);
    return 0;
}
