/**
 * @file spinlock.c
 * @brief AISafe64 RTOS - 自旋锁实现
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details Ticket Lock算法实现
 *          - 使用标准原子操作接口
 *          - 公平性保证（FIFO）
 *          - 内存屏障保证
 *
 * @note MISRA-C:2012合规
 * @note 仅适用于多核SMP环境
 */

#include "sync.h"

/**
 * @brief 自旋锁初始化
 * @param lock 自旋锁指针
 *
 * @details 初始化票据为0
 */
void spinlock_init(spinlock_t *lock)
{
    if (lock == NULL) {
        return;
    }

    lock->next_ticket = 0U;
    lock->serving_ticket = 0U;

    /* 内存屏障确保初始化完成 */
    MEMORY_BARRIER();
}

/**
 * @brief 自旋锁加锁
 * @param lock 自旋锁指针
 *
 * @details Ticket Lock算法
 *          - 原子获取下一个票据
 *          - 忙等待直到当前票据等于服务票据
 */
void spinlock_lock(spinlock_t *lock)
{
    if (lock == NULL) {
        return;
    }

    uint32_t ticket;

    /* 原子获取票据（使用标准原子操作） */
    ticket = atomic_fetch_inc(&lock->next_ticket);

    /* 内存屏障确保票据获取完成 */
    MEMORY_BARRIER();

    /* 忙等待直到轮到自己 */
    while (lock->serving_ticket != ticket) {
        /* 降低功耗的等待循环 */
        WFE();
    }
}

/**
 * @brief 自旋锁尝试加锁
 * @param lock 自旋锁指针
 * @return 成功返回true，失败返回false
 *
 * @details 非阻塞模式
 */
bool spinlock_trylock(spinlock_t *lock)
{
    if (lock == NULL) {
        return false;
    }

    uint32_t ticket;

    /* 原子获取票据（使用标准原子操作） */
    ticket = atomic_fetch_inc(&lock->next_ticket);

    /* 内存屏障 */
    MEMORY_BARRIER();

    /* 检查是否立即轮到自己 */
    if (lock->serving_ticket == ticket) {
        return true;
    }

    /* 失败，归还票据 */
    lock->next_ticket = ticket;
    return false;
}

/**
 * @brief 自旋锁解锁
 * @param lock 自旋锁指针
 *
 * @details 释放锁，增加服务票据
 */
void spinlock_unlock(spinlock_t *lock)
{
    if (lock == NULL) {
        return;
    }

    /* 内存屏障确保临界区操作完成 */
    MEMORY_BARRIER();

    /* 增加服务票据（允许下一个等待者获取锁） */
    lock->serving_ticket++;

    /* 发送事件唤醒等待的核心 */
    SEVL();
}
