/**
 * @file    thread.c
 * @brief   内核线程管理实现
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 本文件实现了内核线程的创建、退出、挂起、恢复和优先级设置等功能。
 *
 * @note 使用简单的 bump allocator 从 __heap_start 分配栈空间
 * @note 初始上下文由 arch_setup_thread_context 设置
 *
 * @warning entry 函数不应直接返回，应调用 kthread_exit() 退出
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 *
 * @revision history
 * v1.0 2026-07-05 初始版本
 */

#include "thread.h"
#include "scheduler.h"

#include <kernel/errno.h>
#include <kernel/config.h>
#include <kernel/barrier.h>
#include <kernel/compiler.h>
#include <kernel/smp.h>
#include <stdint.h>
#include <string.h>
#include "hal.h"

/* 前向声明: ARM64 上下文初始化（定义在 context.S） */
extern void arch_setup_thread_context(uint64_t *ctx, uint64_t entry,
                                      uint64_t arg, uint64_t stack_top);

/* 前向声明: 栈分配器（定义在 scheduler.c） */
extern vaddr_t stack_alloc_by_scheduler(uint32_t size);
extern void stack_free_by_scheduler(vaddr_t stack_base, uint32_t size);

/* HAL 接口 */
/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 查找空闲的线程控制块
 *
 * @details 遍历全局线程表，找到第一个状态为 DEAD 的 TCB。
 *          通过 preempt_disable 保护线性扫描，防止多核并发
 *          kthread_create 分配同一 TID 槽位。
 *
 * @return 空闲线程 ID，无空闲时返回 THREAD_ID_INVALID
 */
static thread_id_t alloc_thread_id(void)
{
    thread_id_t tid;

    preempt_disable();
    for (tid = 0U; tid < CONFIG_MAX_THREADS; tid++)
    {
        if (g_scheduler.thread_table[tid].state == KTHREAD_STATE_DEAD)
        {
            /* 原子地标记为占用（防止并发分配同一槽位） */
            g_scheduler.thread_table[tid].state = KTHREAD_STATE_READY;
            preempt_enable();
            return tid;
        }
    }
    preempt_enable();

    return THREAD_ID_INVALID;
}

/**
 * @brief 根据 tid 获取线程控制块指针
 *
 * @param tid 线程 ID
 *
 * @return 线程控制块指针，无效 ID 返回 NULL
 */
static KThread_t *get_thread_by_id(thread_id_t tid)
{
    if (tid >= CONFIG_MAX_THREADS)
    {
        return NULL;
    }

    return &g_scheduler.thread_table[tid];
}

/**
 * @brief 复制线程名称
 *
 * @details 安全地复制线程名称，确保不超过最大长度且以 '\0' 结尾
 *
 * @param dest 目标缓冲区
 * @param src  源字符串
 */
static void copy_thread_name(char *dest, const char *src)
{
    uint32_t i;

    if ((dest == NULL) || (src == NULL))
    {
        return;
    }

    for (i = 0U; i < (KTHREAD_NAME_MAX - 1U); i++)
    {
        dest[i] = src[i];
        if (src[i] == '\0')
        {
            break;
        }
    }

    dest[KTHREAD_NAME_MAX - 1U] = '\0';
}

/* ========================================================================
 * 线程创建
 * ======================================================================== */

thread_id_t kthread_create(const char *name,
                           kthread_entry_t entry,
                           void *arg,
                           priority_t prio,
                           KThreadPolicy_t policy,
                           uint32_t stack_size)
{
    thread_id_t tid;
    KThread_t *thread;
    vaddr_t stack_top;

    /* 参数校验 */
    if (name == NULL)
    {
        return THREAD_ID_INVALID;
    }

    if (entry == NULL)
    {
        return THREAD_ID_INVALID;
    }

    if (stack_size < CONFIG_STACK_SIZE_MIN)
    {
        stack_size = CONFIG_STACK_SIZE_MIN;
    }

    if (stack_size > CONFIG_STACK_SIZE_MAX)
    {
        stack_size = CONFIG_STACK_SIZE_MAX;
    }

    /* 对齐栈大小到 16 字节 */
    stack_size = (stack_size + 15U) & ~15U;

    /* 分配线程 ID */
    tid = alloc_thread_id();
    if (tid == THREAD_ID_INVALID)
    {
        return THREAD_ID_INVALID;
    }

    thread = &g_scheduler.thread_table[tid];

    /* 分配栈空间（由调度器的 bump allocator 提供） */
    stack_top = stack_alloc_by_scheduler(stack_size);
    if (stack_top == 0U)
    {
        return THREAD_ID_INVALID;
    }

    /* 初始化线程控制块（state 已在 alloc_thread_id 中设为 READY） */
    thread->tid = tid;
    thread->entry = entry;
    thread->entry_arg = arg;
    thread->stack_base = stack_top - (vaddr_t)stack_size;
    thread->stack_size = stack_size;

    /* 栈金丝雀保护：在栈底写入魔数值，调度切换时检测溢出 */
    (void)stack_guard_setup(thread->stack_base, (uint64_t)stack_size,
                             &thread->guard);

    thread->prio = prio;
    thread->policy = policy;
    thread->time_slice = (policy == KTHREAD_POLICY_RR)
                         ? (CONFIG_TIME_SLICE_MS * CONFIG_TICK_RATE_HZ / 1000U)
                         : 0U;
    thread->time_slice_reload = thread->time_slice;
    /* P1-5：重置亲和性掩码。线程槽位可被复用（DEAD→新建），
     * 若不清零会继承前一关联线程的亲和性约束。0 表示无约束。 */
    thread->affinity_mask = 0U;
    thread->rq_list.next = &thread->rq_list;
    thread->rq_list.prev = &thread->rq_list;
    thread->edf_node.next = &thread->edf_node;
    thread->edf_node.prev = &thread->edf_node;
    thread->sleep_node.next = &thread->sleep_node;
    thread->sleep_node.prev = &thread->sleep_node;
    /* 默认所属调度类为 NULL：sched_class 框架在 enqueue/dequeue/pick_next/tick
     * 时回退到默认调度类（sched_rr，由 scheduler_init 注册）。
     * 实时或其他策略可显式设置此指针指向对应 sched_class。 */
    thread->sched_class = NULL;
    thread->wakeup_tick = 0ULL;

    copy_thread_name(thread->name, name);

    /* 初始化上下文（ARM64 寄存器） */
    arch_setup_thread_context(thread->context,
                              (uint64_t)((uintptr_t)entry),
                              (uint64_t)((uintptr_t)arg),
                              (uint64_t)stack_top);

    barrier();

    /* 将线程加入就绪队列 */
    scheduler_enqueue(thread);

    return tid;
}

/* ========================================================================
 * 线程退出
 * ======================================================================== */

void kthread_exit(void)
{
    KThread_t *current;
    uint32_t cpu_id;

    cpu_id = hal_get_cpu_id();
    current = g_scheduler.cpu_queues[cpu_id].current_thread;

    if (current == NULL)
    {
        for (;;)
        {
            hal_wfe();
        }
    }

    /* 用户态线程：释放地址空间引用。
     * TODO: 待 kobject 引用计数体系完全接入后，此处应调用
     *       kobj_ref_dec(current->vmspace) 与 kobj_ref_dec(current->cspace)。
     *       当前阶段仅标记，TCB 中已保存 vmspace/cspace 指针供 cleanup 使用，
     *       实际释放由后续模块处理。 */
    if (current->is_user != 0U)
    {
        /* vmspace/cspace 引用计数递减占位（完整实现见后续重构） */
    }

    /* 标记为 DEAD 状态。栈空间不能在此处立即释放：
     * kthread_exit 运行在自身栈上，释放后 context_switch 访问已释放栈会崩溃。
     * 由 idle 线程通过 kthread_cleanup_dead_stacks() 延迟回收。 */
    current->state = KTHREAD_STATE_DEAD;
    barrier();

    /* 触发调度，切换到下一个线程 */
    schedule();

    /* 永不到达 */
    for (;;)
    {
        hal_wfe();
    }
}

/* ========================================================================
 * 线程栈清理
 * ======================================================================== */

/**
 * @brief 清理 DEAD 线程的栈空间
 *
 * @details 遍历所有线程，清理 DEAD 线程的栈空间
 *          释放后清零栈基址和大小字段
 */
void kthread_cleanup_dead_stacks(void)
{
    uint32_t i;

    for (i = 0U; i < CONFIG_MAX_THREADS; i++)
    {
        KThread_t *thread = &g_scheduler.thread_table[i];

        if (thread->state == KTHREAD_STATE_DEAD && thread->stack_base != 0U)
        {
            /* 释放栈空间 */
            stack_free_by_scheduler(thread->stack_base, thread->stack_size);

            /* 清零栈字段 */
            thread->stack_base = 0U;
            thread->stack_size = 0U;
        }
    }
}

/* ========================================================================
 * 线程挂起
 * ======================================================================== */

kernel_status_t kthread_suspend(thread_id_t tid)
{
    KThread_t *thread;

    thread = get_thread_by_id(tid);
    if (thread == NULL)
    {
        return -(int32_t)ESRCH;
    }

    /* 只有就绪状态的线程可以被挂起 */
    if (thread->state == KTHREAD_STATE_READY)
    {
        /* 从就绪队列移除 */
        scheduler_dequeue(thread);
        thread->state = KTHREAD_STATE_SUSPENDED;
        barrier();
        return KERNEL_OK;
    }

    /* 运行状态的线程不能挂起（应由自身调用） */
    return -(int32_t)EINVAL;
}

/* ========================================================================
 * 线程恢复
 * ======================================================================== */

kernel_status_t kthread_resume(thread_id_t tid)
{
    KThread_t *thread;

    thread = get_thread_by_id(tid);
    if (thread == NULL)
    {
        return -(int32_t)ESRCH;
    }

    /* 只有挂起状态的线程可以被恢复 */
    if (thread->state != KTHREAD_STATE_SUSPENDED)
    {
        return -(int32_t)EINVAL;
    }

    /* 恢复为就绪状态并加入就绪队列 */
    thread->state = KTHREAD_STATE_READY;
    scheduler_enqueue(thread);
    barrier();

    /* 主动抢占检查：如果被唤醒线程优先级高于当前运行线程，立即调度 */
    {
        KThread_t *current = kthread_get_current();
        if ((current != NULL) && (thread->prio > current->prio))
        {
            schedule();
        }
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 设置线程优先级
 * ======================================================================== */

kernel_status_t kthread_set_priority(thread_id_t tid, priority_t prio)
{
    KThread_t *thread;

    /* priority_t 为 uint8_t，范围 [0, 255]，始终 < CONFIG_PRIORITY_LEVELS(256) */
    (void)prio;

    thread = get_thread_by_id(tid);
    if (thread == NULL)
    {
        return -(int32_t)ESRCH;
    }

    /* 如果线程在就绪队列中，需要先出队再入队 */
    if (thread->state == KTHREAD_STATE_READY)
    {
        scheduler_dequeue(thread);
        thread->prio = prio;
        scheduler_enqueue(thread);
    }
    else
    {
        thread->prio = prio;
    }

    barrier();

    /* 主动抢占检查：如果提高优先级后的线程优先于当前运行线程，立即调度 */
    {
        KThread_t *current = kthread_get_current();
        if ((current != NULL) && (thread != current) &&
            (thread->state == KTHREAD_STATE_READY) &&
            (thread->prio > current->prio))
        {
            schedule();
        }
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 获取当前线程
 * ======================================================================== */

KThread_t *kthread_get_current(void)
{
    uint32_t cpu_id;

    cpu_id = hal_get_cpu_id();

    if (cpu_id < CONFIG_MAX_CPUS)
    {
        return g_scheduler.cpu_queues[cpu_id].current_thread;
    }

    return NULL;
}

/**
 * @brief 获取当前运行线程的 ID
 *
 * @details 通过 kthread_get_current() 获取当前线程指针，
 *          返回其 tid 字段。用于系统调用 SYS_THREAD_GET_ID。
 *
 * @return 当前线程 ID，无当前线程时返回 THREAD_ID_INVALID
 */
thread_id_t kthread_get_current_tid(void)
{
    KThread_t *current = kthread_get_current();

    if (current != NULL)
    {
        return current->tid;
    }

    return THREAD_ID_INVALID;
}

