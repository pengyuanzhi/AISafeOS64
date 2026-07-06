/**
 * @file    signal.c
 * @brief   POSIX 信号机制实现
 * @author  AISafe64 Team
 * @date    2026-07-05
 * @version 1.0
 *
 * @details 信号机制设计：
 *          - 每个线程有 32 位信号挂起掩码（pending mask）
 *          - 每个信号可注册处理函数（handler）
 *          - signal_kill 向目标线程投递信号（设置 pending 位）
 *          - 信号在线程从内核态返回用户态前检查（do_signal）
 *          - SIGKILL 不可捕获/忽略
 *
 *          信号投递流程：
 *          1. signal_kill 设置目标线程的 pending 位
 *          2. 标记目标线程 need_resched（高优先级信号可抢占）
 *          3. 目标线程从内核返回时检查 pending
 *          4. 如果有未阻塞的信号且注册了 handler，修改返回地址跳转到 handler
 *
 * @note    当前实现为简化版：signal_kill 设置 pending + notification 通知。
 *          完整的信号 handler 跳转（类似 Linux do_signal）需要异常返回路径修改。
 *
 * @note    MISRA-C:2012 合规
 *
 * @revision history
 * v1.0 2026-07-05 初始版本
 */

#include <kernel/process.h>
#include <kernel/config.h>
#include <kernel/errno.h>
#include <kernel/smp.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../sched/thread.h"
#include "../../sched/scheduler.h"

#ifndef SIG_MAX
#define SIG_MAX 32U
#endif

/** @brief 默认信号处理动作 */
typedef enum
{
    SIG_DFL_TERM = 0U,   /**< @brief 默认终止 */
    SIG_DFL_IGN  = 1U,   /**< @brief 默认忽略 */
    SIG_DFL_CORE = 2U    /**< @brief 默认终止+core dump */
} sig_default_action_t;

/** @brief 信号默认动作表 */
static const sig_default_action_t s_default_actions[SIG_MAX] =
{
    SIG_DFL_TERM,  /* SIGHUP */
    SIG_DFL_TERM,  /* SIGINT */
    SIG_DFL_CORE,  /* SIGQUIT */
    SIG_DFL_CORE,  /* SIGILL */
    SIG_DFL_CORE,  /* SIGTRAP */
    SIG_DFL_CORE,  /* SIGABRT */
    SIG_DFL_TERM,  /* SIGEMT */
    SIG_DFL_TERM,  /* SIGFPE */
    SIG_DFL_TERM,  /* SIGKILL (不可捕获) */
    SIG_DFL_TERM,  /* SIGBUS */
    SIG_DFL_TERM,  /* SIGSEGV */
    SIG_DFL_TERM,  /* SIGSYS */
    SIG_DFL_TERM,  /* SIGPIPE */
    SIG_DFL_TERM,  /* SIGALRM */
    SIG_DFL_TERM,  /* SIGTERM */
    SIG_DFL_TERM,  /* SIGUSR1 */
    SIG_DFL_TERM,  /* SIGUSR2 */
    SIG_DFL_IGN,   /* SIGCHLD */
    SIG_DFL_IGN,   /* SIGPWR */
    SIG_DFL_TERM,  /* SIGWINCH */
    SIG_DFL_TERM,  /* SIGURG (实际应忽略) */
    SIG_DFL_TERM,  /* SIGPOLL */
    SIG_DFL_TERM,  /* SIGSTOP (不可捕获) */
    SIG_DFL_TERM,  /* SIGTSTP */
    SIG_DFL_TERM,  /* SIGCONT */
    SIG_DFL_TERM,  /* SIGTTIN */
    SIG_DFL_TERM,  /* SIGTTOU */
    SIG_DFL_TERM,  /* SIGVTALRM */
    SIG_DFL_TERM,  /* SIGPROF */
    SIG_DFL_TERM,  /* SIGXCPU */
    SIG_DFL_TERM,  /* SIGXFSZ */
    SIG_DFL_TERM   /* SIGRTMIN */
};

/* ========================================================================
 * 公共 API
 * ======================================================================== */

/**
 * @brief 向目标线程发送信号
 *
 * @details 设置目标线程的信号挂起位。SIGKILL 直接触发线程终止。
 *
 * @param target_tid 目标线程 ID
 * @param sig 信号编号（1-31）
 * @return KERNEL_OK 成功
 * @return -EINVAL 信号号无效
 * @return -ESRCH 线程不存在
 */
kernel_status_t signal_kill(thread_id_t target_tid, uint32_t sig)
{
    KThread_t *target;

    if ((sig == 0U) || (sig > SIG_MAX))
    {
        return -(int32_t)EINVAL;
    }

    target = &g_scheduler.thread_table[target_tid];
    if (target->state == KTHREAD_STATE_DEAD)
    {
        return -(int32_t)ESRCH;
    }

    /* SIGKILL: 直接触发终止 */
    if (sig == 9U) /* SIGKILL */
    {
        if (target_tid == kthread_get_current_tid())
        {
            kthread_exit();
        }
        else
        {
            target->state = KTHREAD_STATE_DEAD;
        }
        return KERNEL_OK;
    }

    /* 设置挂起信号位（bit (sig-1)） */
    target->signal_pending |= (1ULL << (sig - 1U));

    /* 唤醒睡眠线程 */
    if (target->state == KTHREAD_STATE_SLEEPING)
    {
        target->state = KTHREAD_STATE_READY;
        scheduler_enqueue(target);
    }

    return KERNEL_OK;
}

/**
 * @brief 注册信号处理函数
 *
 * @details 为指定信号注册用户态处理函数。
 *          handler=NULL 表示恢复默认动作。
 *          SIGKILL/SIGSTOP 不可注册 handler。
 *
 * @param sig 信号编号
 * @param handler 处理函数地址（0 = 默认动作）
 * @return KERNEL_OK 成功
 * @return -EINVAL 信号号无效或不可注册
 */
kernel_status_t signal_action(uint32_t sig, uint64_t handler)
{
    KThread_t *current;

    if ((sig == 0U) || (sig > SIG_MAX))
    {
        return -(int32_t)EINVAL;
    }

    /* SIGKILL/SIGSTOP 不可捕获 */
    if ((sig == 9U) || (sig == 23U))
    {
        return -(int32_t)EINVAL;
    }

    current = kthread_get_current();
    if (current == NULL)
    {
        return -(int32_t)ESRCH;
    }

    if (handler == 0ULL)
    {
        /* 恢复默认动作：清除 handler */
        current->signal_handlers[sig - 1U] = 0ULL;
    }
    else
    {
        current->signal_handlers[sig - 1U] = handler;
    }

    return KERNEL_OK;
}

/**
 * @brief 设置信号屏蔽字
 *
 * @details 阻塞指定信号（不投递，但保持 pending）。
 *
 * @param mask 屏蔽字（bit i = 信号 i+1 被阻塞）
 * @return KERNEL_OK 成功
 */
kernel_status_t signal_procmask(uint64_t mask)
{
    KThread_t *current;

    current = kthread_get_current();
    if (current == NULL)
    {
        return -(int32_t)ESRCH;
    }

    /* SIGKILL/SIGSTOP 不可阻塞 */
    current->signal_mask = mask & ~(1ULL << 8U);   /* 不阻塞 SIGKILL */

    return KERNEL_OK;
}

/**
 * @brief 检查并投递信号（内核返回用户态前调用）
 *
 * @details 检查当前线程是否有未阻塞的挂起信号。
 *          如果有注册 handler 的信号，返回 handler 地址。
 *
 * @param current 当前线程
 * @param out_handler 输出 handler 地址（0 = 无信号需要投递）
 * @return KERNEL_OK 成功
 */
kernel_status_t signal_deliver(KThread_t *current, uint64_t *out_handler)
{
    uint64_t deliverable;
    uint32_t sig;
    uint32_t i;

    if ((current == NULL) || (out_handler == NULL))
    {
        return -(int32_t)EINVAL;
    }

    *out_handler = 0ULL;

    /* 计算可投递的信号（pending & ~mask） */
    deliverable = current->signal_pending & ~current->signal_mask;

    if (deliverable == 0ULL)
    {
        return KERNEL_OK;
    }

    /* 找到最高优先级信号（编号最小的） */
    for (i = 0U; i < SIG_MAX; i++)
    {
        if ((deliverable & (1ULL << i)) != 0ULL)
        {
            sig = i + 1U;
            break;
        }
    }

    if (i >= SIG_MAX)
    {
        return KERNEL_OK;
    }

    /* 清除 pending 位 */
    current->signal_pending &= ~(1ULL << i);

    /* 检查是否有注册 handler */
    if (current->signal_handlers[i] != 0ULL)
    {
        *out_handler = current->signal_handlers[i];
    }
    else
    {
        /* 默认动作 */
        sig_default_action_t action = s_default_actions[i];
        if (action == SIG_DFL_TERM)
        {
            current->state = KTHREAD_STATE_DEAD;
        }
        /* SIG_DFL_IGN: 什么都不做 */
    }

    return KERNEL_OK;
}
