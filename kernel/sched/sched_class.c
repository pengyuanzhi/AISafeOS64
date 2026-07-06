/**
 * @file    sched_class.c
 * @brief   调度类（sched_class）框架实现
 * @author  AISafe64 Team
 * @date    2026-07-04
 * @version 3.0
 *
 * @details 实现调度类注册与链表遍历：
 *          - sched_class_register：按优先级（数值大的在前）插入链表
 *          - sched_class_pick_next：遍历链表调用 pick_next
 *          - sched_class_enqueue/dequeue：按线程所属调度类分发
 *          - sched_class_tick：调用当前线程所属调度类的 tick
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 *
 * @revision history
 * v1.0 2026-07-05 初始版本
 */

#include "sched_class.h"
#include "scheduler.h"
#include "thread.h"

/* ========================================================================
 * 调度类链表（按 priority 降序排列）
 * ======================================================================== */

/**
 * @brief 调度类链表头
 *
 * @details 链表节点通过 sched_class.next 串联，priority 数值大的在前。
 */
static struct sched_class *s_sched_class_head = NULL;

/**
 * @brief 默认调度类（thread->sched_class 为 NULL 时使用）
 */
static struct sched_class *s_default_sched_class = NULL;

/* ========================================================================
 * 注册与默认调度类
 * ======================================================================== */

void sched_class_register(struct sched_class *cls)
{
    struct sched_class *cur;
    struct sched_class *prev;

    if (cls == NULL)
    {
        return;
    }

    /* 已注册则跳过 */
    if (cls->next != NULL)
    {
        return;
    }

    /* 按优先级降序插入（数值大的在前） */
    prev = NULL;
    cur = s_sched_class_head;
    while ((cur != NULL) && (cur->priority >= cls->priority))
    {
        prev = cur;
        cur = cur->next;
    }

    cls->next = cur;
    if (prev == NULL)
    {
        s_sched_class_head = cls;
    }
    else
    {
        prev->next = cls;
    }
}

void sched_class_set_default(struct sched_class *cls)
{
    s_default_sched_class = cls;
}

struct sched_class *sched_class_default(void)
{
    return s_default_sched_class;
}

/* ========================================================================
 * 框架分发函数
 * ======================================================================== */

void sched_class_enqueue(struct KThread *thread)
{
    struct sched_class *cls;

    if (thread == NULL)
    {
        return;
    }

    cls = thread->sched_class;
    if (cls == NULL)
    {
        cls = s_default_sched_class;
    }

    if ((cls != NULL) && (cls->enqueue != NULL))
    {
        cls->enqueue(thread);
    }
}

void sched_class_dequeue(struct KThread *thread)
{
    struct sched_class *cls;

    if (thread == NULL)
    {
        return;
    }

    cls = thread->sched_class;
    if (cls == NULL)
    {
        cls = s_default_sched_class;
    }

    if ((cls != NULL) && (cls->dequeue != NULL))
    {
        cls->dequeue(thread);
    }
}

struct KThread *sched_class_pick_next(void)
{
    struct sched_class *cur;
    struct KThread *next = NULL;

    /* 按优先级从高到低遍历，返回第一个非 NULL 的结果 */
    cur = s_sched_class_head;
    while (cur != NULL)
    {
        if (cur->pick_next != NULL)
        {
            next = cur->pick_next();
            if (next != NULL)
            {
                break;
            }
        }
        cur = cur->next;
    }

    return next;
}

void sched_class_tick(struct KThread *current)
{
    struct sched_class *cls;

    if (current == NULL)
    {
        return;
    }

    cls = current->sched_class;
    if (cls == NULL)
    {
        cls = s_default_sched_class;
    }

    if ((cls != NULL) && (cls->tick != NULL))
    {
        cls->tick(current);
    }
}
