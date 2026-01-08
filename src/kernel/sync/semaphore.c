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
 * @note 后续集成任务管理器实现阻塞等待
 */

#include "sync.h"
#include "types.h"
#include "sched.h"
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
int semaphore_init(semaphore_t *sem, int32_t initial_count, uint32_t max_count)
{
    if (sem == NULL)
    {
        return -ERROR_INVALID_PARAM;
    }

    /* 参数验证 */
    if (initial_count < 0)
    {
        return -ERROR_INVALID_PARAM;
    }

    if (max_count == 0U)
    {
        return -ERROR_INVALID_PARAM;
    }

    if ((uint32_t)initial_count > max_count)
    {
        return -ERROR_INVALID_PARAM;
    }

    /* 初始化信号量 */
    sem->count = initial_count;
    sem->max_count = max_count;

    /* 初始化等待队列 */
    INIT_LIST_HEAD(&sem->wait_queue);

    /* 内存屏障 */
    MEMORY_BARRIER();

    return ERROR_SUCCESS;
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
        return -ERROR_INVALID_PARAM;
    }

    int32_t current_count = sem->count;

    /* 检查是否有可用资源 */
    if (current_count <= 0)
    {
        /* 计数为0，立即返回失败 */
        return -ERROR_WOULD_BLOCK;
    }

    /* 尝试原子减1（使用标准原子操作） */
    uint32_t expected = (uint32_t)current_count;
    uint32_t desired = (uint32_t)(current_count - 1);

    if (atomic_compare_exchange_strong((volatile uint32_t *)&sem->count, &expected, desired))
    {
        /* 成功获取信号量 */
        return ERROR_SUCCESS;
    }

    /* CAS失败，返回错误 */
    return -ERROR_WOULD_BLOCK;
}

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
 * @note 使用约束：
 *       - 必须在任务上下文中调用（不能在中断中调用）
 *       - semaphore_post 可以在中断中调用
 *       - 典型场景：中断释放信号量，任务等待信号量
 */
int semaphore_wait_timeout(semaphore_t *sem, uint64_t timeout_ms)
{
    if (sem == NULL)
    {
        return -ERROR_INVALID_PARAM;
    }

    /* 非阻塞模式：timeout_ms = 0 */
    if (timeout_ms == 0ULL)
    {
        return semaphore_trywait(sem);
    }

    /* 首先尝试快速路径：检查是否有可用资源 */
    for (;;)
    {
        int32_t current_count = sem->count;

        /* 检查是否有可用资源 */
        if (current_count > 0)
        {
            /* 尝试原子减1 */
            uint32_t expected = (uint32_t)current_count;
            uint32_t desired = (uint32_t)(current_count - 1);

            if (atomic_compare_exchange_strong((volatile uint32_t *)&sem->count, &expected,
                                               desired))
            {
                /* 成功获取信号量 */
                return ERROR_SUCCESS;
            }

            /* CAS失败，重试 */
            continue;
        }

        /* 计数为0，需要阻塞等待 */
        break;
    }

    /* 获取当前任务 */
    TCB_t *current = get_current_task();

    /* 运行时检查：确保不在中断上下文中调用 */
    if (current == NULL)
    {
        /* 在中断上下文中调用 semaphore_wait 是编程错误 */
        /* 根据使用约束：只能在中断中释放，不能在中断中获取 */
        return -ERROR_INVALID_STATE;
    }

    /* 任务上下文：使用调度器阻塞 */
    semaphore_wait_node_t wait_node;
    wait_node.task = current;
    wait_node.timeout = (timeout_ms == UINT64_MAX) ? 0 : (sched_clock() + timeout_ms * 1000000ULL);

    /* 将当前任务添加到等待队列 */
    list_add_tail(&wait_node.wait_list, &sem->wait_queue);

    /* 阻塞当前任务 */
    current->state = TASK_BLOCKED;
    schedule(); /* 触发调度，切换到其他任务 */

    /* 任务被唤醒后，检查是否成功获取信号量 */
    if (current->state == TASK_BLOCKED)
    {
        /* 超时或被中断，从等待队列中移除 */
        list_del(&wait_node.wait_list);
        return -ERROR_TIMEOUT;
    }

    /* 成功获取信号量（semaphore_post中已设置state为TASK_READY） */
    return ERROR_SUCCESS;
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
 *       - 中断安全：使用原子操作保证线程安全
 */
int semaphore_post(semaphore_t *sem)
{
    if (sem == NULL)
    {
        return -ERROR_INVALID_PARAM;
    }

    for (;;)
    {
        int32_t current_count = sem->count;

        /* 检查是否超过最大计数 */
        if ((uint32_t)current_count >= sem->max_count)
        {
            /* 计数已达到上限 */
            return -ERROR_OVERFLOW;
        }

        /* 尝试原子加1（使用标准原子操作） */
        uint32_t expected = (uint32_t)current_count;
        uint32_t desired = (uint32_t)(current_count + 1);

        if (atomic_compare_exchange_strong((volatile uint32_t *)&sem->count, &expected, desired))
        {
            /* 成功释放信号量 */

            /* 内存屏障 */
            MEMORY_BARRIER();

            /* 检查是否有任务在等待 */
            if (!list_empty(&sem->wait_queue))
            {
                /* 获取等待队列中的第一个任务 */
                struct list_head *entry = sem->wait_queue.next;
                semaphore_wait_node_t *wait_node =
                    list_entry(entry, semaphore_wait_node_t, wait_list);
                TCB_t *task = wait_node->task;

                /* 从等待队列中移除 */
                list_del(entry);

                /* 唤醒任务 */
                if (task != NULL)
                {
                    task->state = TASK_READY;
                    enqueue_task(task);
                }
            }

            return ERROR_SUCCESS;
        }

        /* CAS失败，立即重试（不使用 WFE 保证实时性） */
    }
}

/**
 * @brief 获取信号量计数
 * @param sem 信号量指针
 * @return 当前计数
 */
int32_t semaphore_getcount(semaphore_t *sem)
{
    if (sem == NULL)
    {
        return -1;
    }

    /* 内存屏障确保读取最新值 */
    MEMORY_BARRIER();

    return sem->count;
}
