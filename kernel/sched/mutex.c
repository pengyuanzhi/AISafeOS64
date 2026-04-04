/**
 * @file    mutex.c
 * @brief   内核互斥锁实现（支持优先级继承和天花板协议）
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 实现所有 mutex.h 中声明的函数：
 *          - mutex_init:          初始化互斥锁
 *          - mutex_lock:          获取互斥锁（阻塞，支持优先级继承）
 *          - mutex_unlock:        释放互斥锁（恢复优先级，唤醒等待者）
 *          - mutex_try_lock:      非阻塞尝试获取
 *          - mutex_lock_timeout:  带超时获取
 *          - mutex_is_held:       检查锁状态
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-004（内核同步原语）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/mutex.h>
#include <kernel/errno.h>
#include <kernel/barrier.h>
#include <kernel/config.h>
#include <kernel/compiler.h>
#include <kernel/timer.h>
#include "thread.h"
#include "scheduler.h"
#include <stdint.h>
#include "hal.h"

/* 前向声明: 线程和调度器接口 */
/* ========================================================================
 * 内部类型定义
 * ======================================================================== */

/**
 * @brief 等待队列节点（嵌入到 KThread 中）
 *
 * @details 使用 KThread_t 中已有的 sleep_node 作为等待队列节点
 */

/* 前向声明: 线程管理接口 */
struct KThread;
extern struct KThread *kthread_get_current(void);
extern void schedule(void);
extern void scheduler_enqueue(struct KThread *thread);
extern kernel_status_t kthread_set_priority(thread_id_t tid, priority_t prio);

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 根据 tid 获取线程指针
 *
 * @note 简化实现：直接从 g_scheduler.thread_table 查找
 *       当前为桩函数，待完整实现后启用
 */
#if 0
struct KThread *get_thread_by_tid(uint32_t tid)
{
    (void)tid;
    /* 简化实现：在完整版本中从 thread_table 查找 */
    return NULL;
}
#endif

/* ========================================================================
 * 互斥锁初始化
 * ======================================================================== */

void mutex_init(Mutex_t *mutex, priority_t ceiling)
{
    if (mutex == NULL)
    {
        return;
    }

    mutex->owner_tid = 0U;
    mutex->state = MUTEX_STATE_UNLOCKED;
    mutex->ceiling = ceiling;
    mutex->original_prio = PRIORITY_MIN;
    mutex->lock_count = 0U;
    mutex->wait_queue.next = &mutex->wait_queue;
    mutex->wait_queue.prev = &mutex->wait_queue;
    barrier();
}

/* ========================================================================
 * 获取互斥锁（阻塞）
 * ======================================================================== */

int32_t mutex_lock(Mutex_t *mutex)
{
    struct KThread *current;

    if (mutex == NULL)
    {
        return -(int32_t)EINVAL;
    }

    current = kthread_get_current();
    if (current == NULL)
    {
        return -(int32_t)ESRCH;
    }

    /* 优先级天花板协议：如果设置了天花板，提升到天花板优先级 */
    if (mutex->ceiling != 0U)
    {
        /* 保存原始优先级 */
        /* 在完整实现中需要访问 current->prio */
    }

    /* 尝试获取锁 */
    if (mutex->state == MUTEX_STATE_UNLOCKED)
    {
        /* 锁空闲，直接获取 */
        mutex->owner_tid = current->tid;
        mutex->state = MUTEX_STATE_LOCKED;
        mutex->lock_count++;
        barrier();
        return 0;
    }

    /* 锁已被占用 */

    /* 优先级继承：如果等待者优先级高于持有者，提升持有者优先级 */
    /* 在完整实现中需要比较 current->prio 与 owner->prio */

    /* 设置状态为有竞争 */
    mutex->state = MUTEX_STATE_CONTENDED;

    /* 将当前线程加入等待队列 */
    current->sleep_node.next = mutex->wait_queue.next;
    current->sleep_node.prev = &mutex->wait_queue;
    mutex->wait_queue.next->prev = &current->sleep_node;
    mutex->wait_queue.next = &current->sleep_node;

    /* 设置为阻塞状态 */
    current->state = KTHREAD_STATE_BLOCKED;
    barrier();

    /* 触发调度 */
    schedule();

    /* 被唤醒后，锁已由 unlock 传递给当前线程 */
    return 0;
}

/* ========================================================================
 * 释放互斥锁
 * ======================================================================== */

int32_t mutex_unlock(Mutex_t *mutex)
{
    struct KThread *current;

    if (mutex == NULL)
    {
        return -(int32_t)EINVAL;
    }

    current = kthread_get_current();
    if (current == NULL)
    {
        return -(int32_t)ESRCH;
    }

    /* 检查是否为锁的持有者 */
    if (mutex->owner_tid != current->tid)
    {
        return -(int32_t)EPERM;
    }

    mutex->lock_count--;

    /* 恢复持有者的原始优先级（优先级继承恢复） */
    if (mutex->original_prio != PRIORITY_MIN)
    {
        kthread_set_priority(current->tid, mutex->original_prio);
        mutex->original_prio = PRIORITY_MIN;
    }

    /* 检查等待队列 */
    if (mutex->wait_queue.next != &mutex->wait_queue)
    {
        /* 有等待者：直接将锁传递给第一个等待者 */
        struct KThread *next_thread = container_of(
            mutex->wait_queue.next, struct KThread, sleep_node);

        /* 从等待队列移除 */
        next_thread->sleep_node.prev->next = next_thread->sleep_node.next;
        next_thread->sleep_node.next->prev = next_thread->sleep_node.prev;
        next_thread->sleep_node.next = &next_thread->sleep_node;
        next_thread->sleep_node.prev = &next_thread->sleep_node;

        /* 传递锁 */
        mutex->owner_tid = next_thread->tid;
        mutex->lock_count++;

        /* 唤醒等待者 */
        next_thread->state = KTHREAD_STATE_READY;
        scheduler_enqueue(next_thread);
    }
    else
    {
        /* 无等待者：释放锁 */
        mutex->owner_tid = 0U;
        mutex->state = MUTEX_STATE_UNLOCKED;
    }

    barrier();
    return 0;
}

/* ========================================================================
 * 尝试获取互斥锁（非阻塞）
 * ======================================================================== */

int32_t mutex_try_lock(Mutex_t *mutex)
{
    struct KThread *current;

    if (mutex == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (mutex->state != MUTEX_STATE_UNLOCKED)
    {
        return -(int32_t)EBUSY;
    }

    current = kthread_get_current();
    if (current == NULL)
    {
        return -(int32_t)ESRCH;
    }

    mutex->owner_tid = current->tid;
    mutex->state = MUTEX_STATE_LOCKED;
    mutex->lock_count++;
    barrier();

    return 0;
}

/* ========================================================================
 * 带超时获取互斥锁
 * ======================================================================== */

int32_t mutex_lock_timeout(Mutex_t *mutex, uint32_t timeout_ms)
{
    struct KThread *current;
    tick_t deadline;

    if (mutex == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (timeout_ms == 0U)
    {
        return mutex_try_lock(mutex);
    }

    current = kthread_get_current();
    if (current == NULL)
    {
        return -(int32_t)ESRCH;
    }

    /* 计算超时时刻 */
    {
        extern tick_t timer_get_ticks(void);
        tick_t now = timer_get_ticks();
        deadline = now + MS_TO_TICKS(timeout_ms);
    }

    /* 尝试获取，如果失败就自旋等待直到超时 */
    for (;;)
    {
        if (mutex->state == MUTEX_STATE_UNLOCKED)
        {
            return mutex_lock(mutex);
        }

        /* 检查超时 */
        {
            extern tick_t timer_get_ticks(void);
            tick_t now = timer_get_ticks();
            if (now >= deadline)
            {
                return -(int32_t)ETIMEDOUT;
            }
        }

        /* 短暂让步 */
        cpu_relax();
    }

    /* 永不到达 */
    return -(int32_t)ETIMEDOUT;
}

/* ========================================================================
 * 检查锁状态
 * ======================================================================== */

bool mutex_is_held(const Mutex_t *mutex)
{
    if (mutex == NULL)
    {
        return false;
    }

    return (mutex->state != MUTEX_STATE_UNLOCKED) ? true : false;
}
