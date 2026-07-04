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
#include <kernel/smp.h>
#include <kernel/mmu.h>
#include <kernel/mm/slab.h>
#include <stdint.h>
#include <string.h>
#include "hal.h"

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

/* HAL 接口 */
extern void hal_uart_puts(uint64_t base, const char *str);

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

/**
 * @brief idle 线程入口函数
 *
 * @details 所有 CPU 的 idle 线程共用此入口。
 *          进入 WFE 低功耗等待循环。
 */
static void idle_task_entry(void *arg)
{
    (void)arg;

    for (;;)
    {
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
            cpu_q->queues[j].next = &cpu_q->queues[j];
            cpu_q->queues[j].prev = &cpu_q->queues[j];
        }

        cpu_q->lock = 0U;
        cpu_q->nr_running = 0U;
        cpu_q->current_thread = NULL;
        cpu_q->idle_thread = NULL;
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
        thread->rq_list.next = &thread->rq_list;
        thread->rq_list.prev = &thread->rq_list;
        thread->sleep_node.next = &thread->sleep_node;
        thread->sleep_node.prev = &thread->sleep_node;
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

    if (thread == NULL)
    {
        return;
    }

    cpu_id = hal_get_cpu_id();
    cpu_q = &g_scheduler.cpu_queues[cpu_id];

    /* 设置位图对应位 */
    bitmap256_set(&cpu_q->bitmap, (uint32_t)thread->prio);

    /* 加入对应优先级链表尾部 */
    thread->rq_list.next = &cpu_q->queues[thread->prio];
    thread->rq_list.prev = cpu_q->queues[thread->prio].prev;
    cpu_q->queues[thread->prio].prev->next = &thread->rq_list;
    cpu_q->queues[thread->prio].prev = &thread->rq_list;

    /* 递增运行计数 */
    cpu_q->nr_running++;
    barrier();
}

/* ========================================================================
 * 出队操作
 * ======================================================================== */

void scheduler_dequeue(KThread_t *thread)
{
    uint32_t cpu_id;
    PerCPUReadyQueue_t *cpu_q;

    if (thread == NULL)
    {
        return;
    }

    cpu_id = hal_get_cpu_id();
    cpu_q = &g_scheduler.cpu_queues[cpu_id];

    /* 从链表中移除 */
    thread->rq_list.prev->next = thread->rq_list.next;
    thread->rq_list.next->prev = thread->rq_list.prev;
    thread->rq_list.next = &thread->rq_list;
    thread->rq_list.prev = &thread->rq_list;

    /* 如果该优先级链表为空，清除位图对应位 */
    if (cpu_q->queues[thread->prio].next == &cpu_q->queues[thread->prio])
    {
        bitmap256_clear(&cpu_q->bitmap, (uint32_t)thread->prio);
    }

    /* 递减运行计数 */
    if (cpu_q->nr_running > 0U)
    {
        cpu_q->nr_running--;
    }

    barrier();
}

/* ========================================================================
 * O(1) 选择最高优先级线程
 * ======================================================================== */

KThread_t *scheduler_pick_next(void)
{
    uint32_t cpu_id;
    uint32_t highest_prio;
    PerCPUReadyQueue_t *cpu_q;
    struct list_head *first;
    KThread_t *next;

    cpu_id = hal_get_cpu_id();
    cpu_q = &g_scheduler.cpu_queues[cpu_id];

    /* O(1) 查找最高优先级 */
    highest_prio = bitmap256_find_highest(&cpu_q->bitmap);

    if (highest_prio < CONFIG_PRIORITY_LEVELS)
    {
        /* 从对应优先级链表头部取出第一个线程 */
        first = cpu_q->queues[highest_prio].next;
        next = container_of(first, KThread_t, rq_list);
        return next;
    }

    /* 没有就绪线程，返回 idle 线程 */
    if (cpu_q->idle_thread != NULL)
    {
        return cpu_q->idle_thread;
    }

    return NULL;
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
 * 触发调度
 * ======================================================================== */

void schedule(void)
{
    KThread_t *prev;
    KThread_t *next;
    uint32_t cpu_id;

    cpu_id = hal_get_cpu_id();
    prev = g_scheduler.cpu_queues[cpu_id].current_thread;
    next = scheduler_pick_next();

    if (next == NULL)
    {
        return;
    }

    /* 从就绪队列中移除下一个线程（防止同时在队列和运行状态） */
    if (next->state == KTHREAD_STATE_READY)
    {
        scheduler_dequeue(next);
    }

    /* 如果下一个线程与当前线程相同，无需切换 */
    if (prev == next)
    {
        return;
    }

    /* 将当前线程从运行状态改为就绪状态并重新入队 */
    if ((prev != NULL) && (prev->state == KTHREAD_STATE_RUNNING))
    {
        prev->state = KTHREAD_STATE_READY;
        scheduler_enqueue(prev);
    }

    /* 设置下一个线程为当前线程 */
    scheduler_load_current(next);

    /* 保存当前 ELR/SPSR 到 prev context (context[13]/[14]) */
    prev->context[13U] = hal_read_elr();
    prev->context[14U] = hal_read_spsr();

    /* P0-2: 用户态地址空间隔离 - 切换 TTBR0
     * TODO: schedule() 里的常规切换需要独立的汇编处理
     * （当前仅 scheduler_start 的首次切换工作） */
    if (next->is_user != 0U)
    {
        /* 常规切换暂不处理（单驱动线程不会触发 schedule） */
    }
    else
    {
        mmu_switch_to_kernel();
    }

    /* 恢复 next 的 ELR/SPSR */
    hal_write_elr(next->context[13U]);
    hal_write_spsr(next->context[14U]);
    hal_isb();

    /* 执行上下文切换 */
    context_switch(prev->context, next->context);
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

    /* 仅对 RR 策略的线程处理时间片 */
    if (current->policy == KTHREAD_POLICY_RR)
    {
        if (current->time_slice > 0U)
        {
            current->time_slice--;
            if (current->time_slice == 0U)
            {
                /* 时间片耗尽，重载并触发调度 */
                current->time_slice = current->time_slice_reload;
                schedule();
                return;
            }
        }
    }

    /* 优先级抢占检查：如果就绪队列中有更高优先级线程，立即抢占 */
    next = scheduler_pick_next();
    if ((next != NULL) && (next != current) && (next->prio > current->prio))
    {
        schedule();
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

    /* 用户态线程：清除 SCTLR.WXN（bit 18），允许可写页执行。
     * WXN=1 时 AP=RW 的页不可执行 → EL0 Permission fault。
     * 共享 PGD 方案：不切换 TTBR0。 */
    if (first_thread->is_user != 0U)
    {
        uint64_t sctlr;
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
        sctlr &= ~(1ULL << 18U);
        __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
        __asm__ volatile("isb");
    }

    hal_uart_puts(0x09000000UL, "[k] Start sched\n");

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

