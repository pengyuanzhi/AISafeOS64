/**
 * @file    scheduler.c
 * @brief   256 级优先级位图调度器实现
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 实现所有 scheduler.h 中声明的函数：
 *          - scheduler_init:      初始化所有 CPU 的就绪队列，创建 idle 线程
 *          - scheduler_start:     启动调度（永不返回）
 *          - schedule:            保存/切换上下文，触发调度
 *          - scheduler_enqueue:   入队（位图置位 + 添加到优先级链表尾部）
 *          - scheduler_dequeue:   出队（链表移除 + 清位图位）
 *          - scheduler_pick_next: O(1) 选择最高优先级线程
 *          - scheduler_tick:      时钟滴答处理（RR 时间片）
 *          - scheduler_load_current: 设置当前线程
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SC-001(O(1) 优先级位图调度), SC-002(上下文切换)
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "scheduler.h"
#include "thread.h"

#include <kernel/barrier.h>
#include <kernel/compiler.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <kernel/klog.h>
#include <kernel/smp.h>
#include <kernel/mmu.h>
#include <kernel/mm/slab.h>
#include <stdint.h>
#include <string.h>
#include "hal.h"
#include "edf.h"

/* 前向声明: ARM64 上下文切换接口（定义在 context.S） */
extern void context_switch(uint64_t *prev_ctx, uint64_t *next_ctx);
extern void arch_setup_thread_context(uint64_t *ctx, uint64_t entry,
                                      uint64_t arg, uint64_t stack_top);
extern void cpu_switch_to_first_task(uint64_t *ctx);

/* 汇编版 mmu_switch_to_user（通过 TTBR1 高地址执行 TTBR0 切换，
 * 避免切换后低地址内核代码取指失败） */
extern void mmu_switch_to_user_asm(uint64_t user_pgd, uint64_t *ctx);

/* 前向声明: 链接脚本定义的堆起止符号 */
extern char __heap_start[];
extern char __heap_end[];

/* 前向声明: 栈分配函数 */
static vaddr_t stack_alloc(uint32_t size);

/* ========================================================================
 * 内部就绪队列操作（调用者必须已持有 cpu_q->lock）
 *
 * @details scheduler_enqueue/scheduler_dequeue 与 schedule() 共享同一套
 *          入队/出队逻辑，避免重复实现导致的逻辑漂移。这些函数不获取
 *          队列锁，锁由调用者负责。
 * ======================================================================== */

/**
 * @brief 将线程从就绪队列移除（需已持锁）
 *
 * @details 摘除线程节点、清空优先级位图（若链表空）、递减运行计数。
 *          节点被重新初始化为自指（list_del_init 语义）。
 *
 * @param cpu_q 当前 CPU 的就绪队列
 * @param thread 待移除的线程
 */
static void __sched_dequeue_locked(PerCPUReadyQueue_t *cpu_q, KThread_t *thread)
{
    list_del_init(&thread->rq_list);

    if (list_empty(&cpu_q->queues[thread->prio]) != 0)
    {
        bitmap256_clear(&cpu_q->bitmap, (uint32_t)thread->prio);
    }

    if (cpu_q->nr_running > 0U)
    {
        cpu_q->nr_running--;
    }
}

/**
 * @brief 将线程加入就绪队列尾部（需已持锁）
 *
 * @details 置位优先级位图、追加到对应优先级链表尾部、递增运行计数。
 *
 * @param cpu_q 当前 CPU 的就绪队列
 * @param thread 待加入的线程
 */
static void __sched_enqueue_locked(PerCPUReadyQueue_t *cpu_q, KThread_t *thread)
{
    bitmap256_set(&cpu_q->bitmap, (uint32_t)thread->prio);
    list_add_tail(&thread->rq_list, &cpu_q->queues[thread->prio]);
    cpu_q->nr_running++;
}

/* ========================================================================
 * 线程栈 Slab 分配器实现
 * ======================================================================== */

/**
 * @brief 初始化线程栈 Slab 缓存
 *
 * @details 为不同大小的线程栈创建 Slab 缓存
 *
 * @return KERNEL_OK 成功
 * @return -ENOMEM 内存不足
 */
static int32_t thread_stack_slab_init(void)
{
    int32_t ret;
    size_t pool_sizes[STACK_SIZE_COUNT] = {
        STACK_SIZE_4KB_BYTES * 8,   /* 4KB 栈，每个 Slab 8 个对象 */
        STACK_SIZE_8KB_BYTES * 4,   /* 8KB 栈，每个 Slab 4 个对象 */
        STACK_SIZE_16KB_BYTES * 2    /* 16KB 栈，每个 Slab 2 个对象 */
    };

    for (uint32_t i = 0U; i < STACK_SIZE_COUNT; i++)
    {
        ret = slab_create(&g_scheduler.stack_slab.caches[i], pool_sizes[i]);
        if (ret != KERNEL_OK)
        {
            /* 清理已创建的缓存 */
            for (uint32_t j = 0U; j < i; j++)
            {
                (void)slab_destroy(&g_scheduler.stack_slab.caches[j]);
            }
            return ret;
        }
    }

    g_scheduler.stack_slab.initialized = true;

    return KERNEL_OK;
}

/**
 * @brief 销毁线程栈 Slab 缓存
 *
 * @return KERNEL_OK 成功
 */
static int32_t thread_stack_slab_destroy(void)
{
    if (!g_scheduler.stack_slab.initialized)
    {
        return KERNEL_OK;
    }

    for (uint32_t i = 0U; i < STACK_SIZE_COUNT; i++)
    {
        (void)slab_destroy(&g_scheduler.stack_slab.caches[i]);
    }

    (void)memset(&g_scheduler.stack_slab, 0, sizeof(ThreadStackSlab_t));

    return KERNEL_OK;
}

/**
 * @brief 根据栈大小选择合适的 Slab 缓存
 *
 * @param size 栈大小
 *
 * @return Slab 缓存索引，如果不支持则返回 -1
 */
static int32_t select_stack_cache(uint32_t size)
{
    /* 根据栈大小选择合适的缓存 */
    if (size <= STACK_SIZE_4KB_BYTES)
    {
        return 0; /* 4KB 缓存 */
    }
    else if (size <= STACK_SIZE_8KB_BYTES)
    {
        return 1; /* 8KB 缓存 */
    }
    else if (size <= STACK_SIZE_16KB_BYTES)
    {
        return 2; /* 16KB 缓存 */
    }
    else
    {
        return -1; /* 不支持的大小 */
    }
}

/**
 * @brief 使用 Slab 分配器分配栈空间
 *
 * @param size 请求的栈大小（字节）
 *
 * @return 成功返回栈顶地址，失败返回 0
 */
static vaddr_t stack_alloc_slab(uint32_t size)
{
    int32_t cache_idx;
    void *stack_ptr;
    vaddr_t stack_top;

    if (!g_scheduler.stack_slab.initialized)
    {
        return 0U;
    }

    /* 选择合适的 Slab 缓存 */
    cache_idx = select_stack_cache(size);
    if (cache_idx < 0)
    {
        /* 大小不支持，回退到 bump allocator */
        return stack_alloc(size);
    }

    /* 从 Slab 缓存分配 */
    stack_ptr = slab_alloc(&g_scheduler.stack_slab.caches[cache_idx]);
    if (stack_ptr == NULL)
    {
        return 0U;
    }

    /* 计算 栈顶地址 */
    stack_top = (vaddr_t)((uintptr_t)stack_ptr + size);

    return stack_top;
}

/**
 * @brief 释放栈空间回 Slab 分配器
 *
 * @param stack_base 栈基址
 * @param size 栈大小
 */
static void stack_free_slab(vaddr_t stack_base, uint32_t size)
{
    int32_t cache_idx;

    if (!g_scheduler.stack_slab.initialized)
    {
        return;
    }

    /* 选择合适的 Slab 缓存 */
    cache_idx = select_stack_cache(size);
    if (cache_idx < 0)
    {
        /* 大小不支持，无法释放 */
        return;
    }

    /* 释放回 Slab 缓存 */
    (void)slab_free(&g_scheduler.stack_slab.caches[cache_idx], (void *)(uintptr_t)stack_base);
}

/**
 * @brief 导出的栈分配接口（供 thread.c 调用）
 *
 * @param size 请求的栈大小（字节）
 *
 * @return 成功返回栈顶地址，失败返回 0
 */
vaddr_t stack_alloc_by_scheduler(uint32_t size)
{
    /* 优先使用 Slab 分配器 */
    vaddr_t stack_top = stack_alloc_slab(size);
    if (stack_top != 0U)
    {
        return stack_top;
    }

    /* Slab 分配失败，回退到 bump allocator */
    return stack_alloc(size);
}

/**
 * @brief 导出的栈释放接口（供 thread.c 调用）
 *
 * @param stack_base 栈基址
 * @param size 栈大小
 */
void stack_free_by_scheduler(vaddr_t stack_base, uint32_t size)
{
    /* 优先使用 Slab 分配器释放 */
    stack_free_slab(stack_base, size);
}

/* ========================================================================
 * 全局调度器实例
 * ======================================================================== */

/**
 * @brief 全局调度器实例（静态分配）
 */
Scheduler_t g_scheduler =
{
    .cpu_queues = { { { { 0 } } } },
    .initialized = false,
    .thread_table = { { { 0 } } }
};

/* ========================================================================
 * 栈分配器（简单 bump allocator）
 * ======================================================================== */

/**
 * @brief 内核堆当前分配指针
 *
 * @details 从链接脚本定义的 __heap_start 开始，
 *          向 __heap_end 方向分配。不支持释放回收。
 */
static vaddr_t s_heap_ptr = 0U;

/**
 * @brief 从内核堆分配栈空间
 *
 * @details 使用简单的 bump allocator 从链接脚本定义的堆区域分配栈。
 *          每次分配后指针向前推进，不支持回收。
 *
 * @param size 请求的栈大小（字节）
 *
 * @return 成功返回栈顶地址（已 16 字节对齐），失败返回 0
 */
static vaddr_t stack_alloc(uint32_t size)
{
    uint64_t heap_start_val;
    uint64_t heap_end_val;
    uint64_t new_ptr_val;

    /* 读取链接脚本定义的堆起止地址 */
    heap_start_val = (uint64_t)((uintptr_t)&__heap_start);
    heap_end_val = (uint64_t)((uintptr_t)&__heap_end);

    /* 首次调用时初始化堆指针 */
    if (s_heap_ptr == 0U)
    {
        s_heap_ptr = (vaddr_t)heap_start_val;
        barrier();
    }

    /* 检查堆是否有足够空间 */
    new_ptr_val = (uint64_t)s_heap_ptr + (uint64_t)size;

    /* 对齐到 16 字节边界 */
    new_ptr_val = (new_ptr_val + 15U) & ~((uint64_t)0xFU);

    if (new_ptr_val > heap_end_val)
    {
        return 0U;
    }

    /* 更新分配指针并返回栈顶（对齐后的 new_ptr 作为栈顶） */
    s_heap_ptr = (vaddr_t)new_ptr_val;
    barrier();

    return s_heap_ptr;
}

/**
 * @brief 导出的栈分配接口（供 thread.c 调用）
 *
 * @param size 请求的栈大小（字节）
 *
 * @return 成功返回栈顶地址，失败返回 0
 */
/* ========================================================================
 * idle 线程入口函数
 * ======================================================================== */

/* ========================================================================
 * 抢占控制实现
 * ======================================================================== */

/**
 * @brief 禁止抢占（递增 preempt_count）
 *
 * @details 操作 percpu_t.preempt_count，本核独占无需锁。
 *          preempt_count > 0 时，scheduler_tick/scheduler_irq_exit_check
 *          不触发 schedule()，直到 preempt_count 归零。
 */
void preempt_disable(void)
{
    percpu_t *percpu = smp_get_percpu();
    if (percpu != NULL)
    {
        percpu->preempt_count++;
        barrier();
    }
}

/**
 * @brief 允许抢占（递减 preempt_count，归零时检查重调度）
 *
 * @details preempt_count 归零后，如果有 pending need_resched，触发 schedule()。
 */
void preempt_enable(void)
{
    percpu_t *percpu = smp_get_percpu();
    if (percpu == NULL)
    {
        return;
    }

    if (percpu->preempt_count > 0U)
    {
        percpu->preempt_count--;
        barrier();
    }

    /* preempt_count 归零且有 pending 调度请求 → 立即重调度 */
    if ((percpu->preempt_count == 0U) &&
        (scheduler_test_and_clear_need_resched() != 0U))
    {
        schedule();
    }
}

/**
 * @brief 查询当前是否禁止抢占
 *
 * @return true preempt_count > 0
 */
bool preempt_is_disabled(void)
{
    percpu_t *percpu = smp_get_percpu();
    if (percpu == NULL)
    {
        return false;
    }
    return (percpu->preempt_count > 0U);
}

/* ========================================================================
 * idle 线程
 * ======================================================================== */

/**
 * @brief idle 线程入口函数
 *
 * @details 所有 CPU 的 idle 线程共用此入口。
 *          进入 WFE 低功耗等待循环。
 */
static void idle_task_entry(void *arg)
{
    uint32_t cpu_id;

    (void)arg;
    cpu_id = hal_get_cpu_id();

    for (;;)
    {
        /* idle 线程检查 need_resched：中断中可能请求了重调度，
         * 此处执行实际的上下文切换（idle 是最低优先级，切换安全）。 */
        if (scheduler_test_and_clear_need_resched() != 0U)
        {
            schedule();
            continue;
        }

        /* 在 idle 上下文执行负载均衡与工作窃取。
         * 定时器中断仅置标志，实际迁移在此处完成（开中断、可抢占）。 */
        smp_idle_balance(cpu_id);

        /* 异步刷新内核日志缓冲到控制台（非实时上下文，允许 UART 阻塞） */
        klog_flush();

        hal_wfe();
    }
}

/* ========================================================================
 * 调度器初始化
 * ======================================================================== */

kernel_status_t scheduler_init(void)
{
    uint32_t cpu_id;
    uint32_t i;
    uint32_t j;
    int32_t ret;

    if (g_scheduler.initialized)
    {
        return KERNEL_OK;
    }

    /* 初始化栈保护子系统（金丝雀检测） */
    (void)stack_guard_subsys_init();

    /* 初始化 EDF 实时调度子系统 */
    (void)edf_init();

    /* 初始化线程栈 Slab 缓存 */
    ret = thread_stack_slab_init();
    if (ret != KERNEL_OK)
    {
        return -ENOMEM;
    }

    /* 初始化所有 CPU 的就绪队列 */
    for (cpu_id = 0U; cpu_id < CONFIG_MAX_CPUS; cpu_id++)
    {
        PerCPUReadyQueue_t *cpu_q = &g_scheduler.cpu_queues[cpu_id];

        /* 清零位图 */
        bitmap256_clear_all(&cpu_q->bitmap);

        /* 初始化每优先级就绪链表 */
        for (j = 0U; j < CONFIG_PRIORITY_LEVELS; j++)
        {
            INIT_LIST_HEAD(&cpu_q->queues[j]);
        }

        ticket_lock_init(&cpu_q->lock);
        cpu_q->nr_running = 0U;
        cpu_q->current_thread = NULL;
        cpu_q->idle_thread = NULL;
        cpu_q->need_resched = 0U;
    }

    /* 初始化线程表 */
    for (i = 0U; i < CONFIG_MAX_THREADS; i++)
    {
        KThread_t *thread = &g_scheduler.thread_table[i];
        thread->tid = (thread_id_t)i;
        thread->state = KTHREAD_STATE_DEAD;
        thread->entry = NULL;
        thread->entry_arg = NULL;
        thread->stack_base = 0U;
        thread->stack_size = 0U;
        thread->prio = PRIORITY_MIN;
        thread->policy = KTHREAD_POLICY_FIFO;
        thread->time_slice = 0U;
        thread->time_slice_reload = 0U;
        INIT_LIST_HEAD(&thread->rq_list);
        INIT_LIST_HEAD(&thread->edf_node);
        INIT_LIST_HEAD(&thread->sleep_node);
        thread->wakeup_tick = 0ULL;
        thread->name[0U] = '\0';
    }

    /* 创建每 CPU 的 idle 线程 */
    for (cpu_id = 0U; cpu_id < CONFIG_MAX_CPUS; cpu_id++)
    {
        thread_id_t tid;
        KThread_t *idle_thread;

        /* idle 线程使用最低优先级和 FIFO 策略 */
        tid = kthread_create("idle",
                             idle_task_entry,
                             NULL,
                             PRIORITY_MIN,
                             KTHREAD_POLICY_FIFO,
                             CONFIG_STACK_SIZE_DEFAULT);
        if (tid == THREAD_ID_INVALID)
        {
            return -ENOMEM;
        }

        idle_thread = &g_scheduler.thread_table[tid];
        g_scheduler.cpu_queues[cpu_id].idle_thread = idle_thread;

        /* kthread_create 在 CPU0 运行，idle 线程被 enqueue 到 CPU0 队列。
         * 对于非 CPU0 的 idle 线程，从 CPU0 队列摘除（不放入目标队列）。
         * pick_next 在队列为空时直接用 idle_thread 指针，无需入队。 */
        if (cpu_id != 0U)
        {
            PerCPUReadyQueue_t *cpu0_q = &g_scheduler.cpu_queues[0U];

            /* 无锁操作（此时只有 CPU0 在运行，初始化阶段） */
            list_del_init(&idle_thread->rq_list);
            if (cpu0_q->nr_running > 0U)
            {
                cpu0_q->nr_running--;
            }
            /* 位图不需要清零：pick_next 找不到链表元素时会回退到 idle */
        }
    }

    /* 标记初始化完成 */
    barrier();
    g_scheduler.initialized = true;

    return KERNEL_OK;
}

/* ========================================================================
 * 入队操作
 * ======================================================================== */

void scheduler_enqueue(KThread_t *thread)
{
    uint32_t cpu_id;
    PerCPUReadyQueue_t *cpu_q;
    uint32_t irq_state;

    if (thread == NULL)
    {
        return;
    }

    cpu_id = hal_get_cpu_id();
    cpu_q = &g_scheduler.cpu_queues[cpu_id];

    /* 关中断 + 获取队列锁，保护位图与链表操作 */
    irq_state = ticket_lock_acquire_irqsave(&cpu_q->lock);

    __sched_enqueue_locked(cpu_q, thread);

    ticket_lock_release_irqrestore(&cpu_q->lock, irq_state);
}

/* ========================================================================
 * 出队操作
 * ======================================================================== */

void scheduler_dequeue(KThread_t *thread)
{
    uint32_t cpu_id;
    PerCPUReadyQueue_t *cpu_q;
    uint32_t irq_state;

    if (thread == NULL)
    {
        return;
    }

    cpu_id = hal_get_cpu_id();
    cpu_q = &g_scheduler.cpu_queues[cpu_id];

    /* 关中断 + 获取队列锁，保护链表与位图操作 */
    irq_state = ticket_lock_acquire_irqsave(&cpu_q->lock);

    __sched_dequeue_locked(cpu_q, thread);

    ticket_lock_release_irqrestore(&cpu_q->lock, irq_state);
}

/* ========================================================================
 * O(1) 选择最高优先级线程
 * ======================================================================== */

KThread_t *scheduler_pick_next(void)
{
    uint32_t cpu_id;
    uint32_t highest_prio;
    PerCPUReadyQueue_t *cpu_q;
    KThread_t *next;
    uint32_t irq_state;

    cpu_id = hal_get_cpu_id();
    cpu_q = &g_scheduler.cpu_queues[cpu_id];

    /* 关中断 + 获取队列锁，保证读取位图/链表期间不被并发修改 */
    irq_state = ticket_lock_acquire_irqsave(&cpu_q->lock);

    /* EDF 实时调度：优先选择截止时间最早的线程（仅 CPU0）。
     * edf_pick_next 内部有独立锁，在持锁状态下调用是安全的。 */
    next = NULL;
    if (cpu_id == 0U)
    {
        next = edf_pick_next();
    }

    if (next == NULL)
    {
        /* O(1) 查找最高优先级 */
        highest_prio = bitmap256_find_highest(&cpu_q->bitmap);

        if (highest_prio < CONFIG_PRIORITY_LEVELS)
        {
            /* 从对应优先级链表头部取出第一个线程 */
            next = list_first_entry(&cpu_q->queues[highest_prio],
                                    KThread_t, rq_list);
        }
    }

    ticket_lock_release_irqrestore(&cpu_q->lock, irq_state);

    /* 没有就绪线程，返回 idle 线程（idle 不在就绪队列中，无需持锁访问） */
    if (next == NULL)
    {
        next = cpu_q->idle_thread;
    }

    return next;
}

/* ========================================================================
 * 设置当前线程
 * ======================================================================== */

void scheduler_load_current(KThread_t *thread)
{
    uint32_t cpu_id;
    PerCPUReadyQueue_t *cpu_q;

    if (thread == NULL)
    {
        return;
    }

    cpu_id = hal_get_cpu_id();
    cpu_q = &g_scheduler.cpu_queues[cpu_id];

    cpu_q->current_thread = thread;
    thread->state = KTHREAD_STATE_RUNNING;
    barrier();
}

/* ========================================================================
 * need_resched 标志管理
 * ======================================================================== */

void scheduler_set_need_resched(void)
{
    uint32_t cpu_id;
    cpu_id = hal_get_cpu_id();
    g_scheduler.cpu_queues[cpu_id].need_resched = 1U;
    barrier();
}

uint32_t scheduler_test_and_clear_need_resched(void)
{
    uint32_t cpu_id;
    PerCPUReadyQueue_t *cpu_q;
    uint32_t flag;

    cpu_id = hal_get_cpu_id();
    cpu_q = &g_scheduler.cpu_queues[cpu_id];

    flag = cpu_q->need_resched;
    if (flag != 0U)
    {
        cpu_q->need_resched = 0U;
        barrier();
    }

    return flag;
}

void scheduler_irq_exit_check(void)
{
    /* IRQ 出口路径调用：中断仍关闭（由汇编向量保证）。
     * 若中断中标记了需要重调度，则在此处调用 schedule()。
     *
     * preempt_count > 0 时不触发调度（关调度区域保护）。
     * need_resched 保留，preempt_enable 归零时重新检查。 */
    if (preempt_is_disabled())
    {
        return;
    }

    if (scheduler_test_and_clear_need_resched() != 0U)
    {
        schedule();
    }
}

/* ========================================================================
 * 触发调度
 * ======================================================================== */

void schedule(void)
{
    KThread_t *prev;
    KThread_t *next;
    uint32_t cpu_id;
    PerCPUReadyQueue_t *cpu_q;
    uint32_t irq_state;

    cpu_id = hal_get_cpu_id();
    cpu_q = &g_scheduler.cpu_queues[cpu_id];
    prev = cpu_q->current_thread;

    /* 持锁覆盖整个调度决策过程（pick_next + dequeue + enqueue +
     * 设置 current_thread），避免并发修改就绪队列。
     * 关中断 + 自旋锁：本函数可能在定时器中断上下文中被调用。 */
    irq_state = ticket_lock_acquire_irqsave(&cpu_q->lock);

    /* ---- 内联 pick_next（不再单独调用 scheduler_pick_next，避免嵌套加锁） ---- */
    next = NULL;
    if (cpu_id == 0U)
    {
        /* EDF 实时调度（仅 CPU0），edf_pick_next 内部有独立锁，持锁调用安全 */
        next = edf_pick_next();
    }
    if (next == NULL)
    {
        /* O(1) 位图查找最高优先级 */
        uint32_t highest = bitmap256_find_highest(&cpu_q->bitmap);
        if (highest < CONFIG_PRIORITY_LEVELS)
        {
            next = list_first_entry(&cpu_q->queues[highest],
                                    KThread_t, rq_list);
        }
    }
    if (next == NULL)
    {
        /* 队列为空，回退到 idle 线程 */
        next = cpu_q->idle_thread;
    }

    if (next == NULL)
    {
        /* 无可运行线程也无可用的 idle 线程 */
        ticket_lock_release_irqrestore(&cpu_q->lock, irq_state);
        return;
    }

    /* ---- dequeue next（已在锁内，复用统一实现） ---- */
    if (next->state == KTHREAD_STATE_READY)
    {
        __sched_dequeue_locked(cpu_q, next);
    }

    /* 如果下一个线程与当前线程相同，无需切换 */
    if (prev == next)
    {
        ticket_lock_release_irqrestore(&cpu_q->lock, irq_state);
        return;
    }

    /* ---- enqueue prev（已在锁内，复用统一实现） ---- */
    if ((prev != NULL) && (prev->state == KTHREAD_STATE_RUNNING))
    {
        prev->state = KTHREAD_STATE_READY;
        __sched_enqueue_locked(cpu_q, prev);
    }

    /* ---- 设置 current_thread（内联 scheduler_load_current） ---- */
    cpu_q->current_thread = next;
    next->state = KTHREAD_STATE_RUNNING;

    /* 临界区到此为止：锁仅保护 pick_next + dequeue + enqueue +
     * current_thread 设置。后续 ELR/SPSR/MMU/guard/context_switch
     * 不操作就绪队列，移到锁外执行以缩短临界区。
     *
     * 注意：这里用 ticket_lock_release（而非 release_irqrestore）保持中断关闭。
     * 从释锁到 context_switch 之间若开启中断，定时器 IRQ 可能重入
     * schedule()（scheduler_irq_exit_check），而此时 prev 上下文尚未保存，
     * 会破坏寄存器。故此窗口内必须禁用中断，待 context_switch 切到 next
     * 后再恢复（见函数末尾）。 */
    ticket_lock_release(&cpu_q->lock);

    /* 保存当前 ELR/SPSR 到 prev context (context[13]/[14]) */
    if (prev != NULL)
    {
        prev->context[13U] = hal_read_elr();
        prev->context[14U] = hal_read_spsr();
    }

    /* 用户态地址空间隔离：仅用户线程需要切换 TTBR0。
     * 内核线程（idle 等）TTBR0 保持空，无需切换。
     * TTBR1（内核高地址）永远不变。 */
    if (next->is_user != 0U)
    {
        /* 用户线程：TTBR0 设为用户 PGD */
        mmu_switch_to_user(next->user_pgd);
    }
    /* 内核线程：不切换 TTBR0（保持空），避免不必要的 TLB 失效 */

    /* 恢复 next 的 ELR/SPSR */
    hal_write_elr(next->context[13U]);
    hal_write_spsr(next->context[14U]);
    hal_isb();

    /* 栈金丝雀检查：检测 prev 线程是否发生栈溢出 */
    if ((prev != NULL) && (prev->guard.enabled) &&
        (!stack_guard_check(&prev->guard)))
    {
        klog_error("[GUARD] Stack overflow detected in thread ");
        klog_error(prev->name);
        klog_putc('\n');
        /* 栈溢出：终止线程 */
        kthread_exit();
    }

    /* 执行上下文切换（切换到 next 的栈与 callee-saved 寄存器） */
    context_switch(prev->context, next->context);

    /* ---- 此处是"被切换回来"的线程恢复执行的位置 ----
     * context_switch 返回意味着本线程（作为 next）被重新调度。
     * 此时中断仍关闭（切走时关掉的），恢复进入 schedule() 前的中断状态。
     * 注意：irq_state 是本线程上次进入 schedule() 时保存的 DAIF。 */
    hal_local_irq_restore(irq_state);
}

/* ========================================================================
 * 时钟滴答处理
 * ======================================================================== */

void scheduler_tick(void)
{
    KThread_t *current;
    KThread_t *next;
    uint32_t cpu_id;

    cpu_id = hal_get_cpu_id();
    current = g_scheduler.cpu_queues[cpu_id].current_thread;

    if (current == NULL)
    {
        return;
    }

    /* EDF 实时调度：周期作业释放 + 截止时间检查（仅 CPU0） */
    if (cpu_id == 0U)
    {
        edf_tick();
    }

    /* 仅对 RR 策略的线程处理时间片 */
    if (current->policy == KTHREAD_POLICY_RR)
    {
        if (current->time_slice > 0U)
        {
            current->time_slice--;
            if (current->time_slice == 0U)
            {
                /* 时间片耗尽，重载并请求重调度。
                 * 不在中断中直接调 schedule()，仅置位 need_resched，
                 * 由 IRQ 出口（scheduler_irq_exit_check）或 idle 线程处理。 */
                current->time_slice = current->time_slice_reload;
                scheduler_set_need_resched();
                return;
            }
        }
    }

    /* 优先级抢占检查：如果就绪队列中有更高优先级线程，请求重调度。
     * 同样仅置位 need_resched，不在中断上下文执行 context_switch。 */
    next = scheduler_pick_next();
    if ((next != NULL) && (next != current) && (next->prio > current->prio))
    {
        scheduler_set_need_resched();
    }

    /* SMP 负载均衡检查 */
    smp_tick_check_balance(cpu_id);
}

/* ========================================================================
 * 启动调度（永不返回）
 * ======================================================================== */

/**
 * @brief 启动调度器并切换到第一个任务
 *
 * @details 此函数选择最高优先级就绪线程，将其设置为当前线程，
 *          并通过 cpu_switch_to_first_task 切换到该线程执行。
 *          如果没有就绪线程，切换到 idle 线程。
 *          此函数不返回。
 */
void NORETURN scheduler_start(void)
{
    KThread_t *first_thread;
    uint32_t cpu_id;

    cpu_id = hal_get_cpu_id();

    /* 选择最高优先级就绪线程 */
    first_thread = scheduler_pick_next();
    if (first_thread == NULL)
    {
        /* 没有任何线程，挂起 */
        for (;;)
        {
            hal_wfe();
        }
    }

    /* 从就绪队列移除（防止同时在队列和运行状态） */
    if (first_thread->state == KTHREAD_STATE_READY)
    {
        scheduler_dequeue(first_thread);
    }

    /* 设置为当前运行线程 */
    scheduler_load_current(first_thread);

    klog_info("[k] Start sched\n");

    /* 切换到第一个任务（永不返回） */
    cpu_switch_to_first_task(first_thread->context);

    /* 永不到达 */
    for (;;)
    {
        hal_wfe();
    }
}

/* ========================================================================
 * 从核启动调度（永不返回）
 * ======================================================================== */

/**
 * @brief 从核启动调度器
 *
 * @details 从核完成初始化后调用此函数进入调度循环。
 *          与 scheduler_start() 类似，但从核的 current_thread 为 NULL，
 *          不需要保存旧上下文。
 *
 * @warning 此函数不返回
 */
void NORETURN scheduler_start_secondary(void)
{
    KThread_t *first_thread;
    uint32_t cpu_id;

    cpu_id = hal_get_cpu_id();

    /* 选择最高优先级就绪线程 */
    first_thread = scheduler_pick_next();

    if (first_thread == NULL)
    {
        /* 没有就绪线程，进入 idle 循环 */
        PerCPUReadyQueue_t *cpu_q = &g_scheduler.cpu_queues[cpu_id];
        if (cpu_q->idle_thread != NULL)
        {
            first_thread = cpu_q->idle_thread;
        }
        else
        {
            for (;;)
            {
                hal_wfe();
            }
        }
    }

    /* 从就绪队列移除 */
    if (first_thread->state == KTHREAD_STATE_READY)
    {
        scheduler_dequeue(first_thread);
    }

    /* 设置为当前运行线程 */
    scheduler_load_current(first_thread);

    /* 从核用户态线程切换暂不处理（仅主核 scheduler_start 支持） */
    /* if (first_thread->is_user != 0U)
       {
           mmu_switch_to_user_asm(first_thread->user_pgd, first_thread->context);
       } */

    /* 切换到第一个任务（永不返回） */
    cpu_switch_to_first_task(first_thread->context);

    /* 永不到达 */
    for (;;)
    {
        hal_wfe();
    }
}

