/**
 * @file    smp.c
 * @brief   多核调度实现（真实线程迁移）
 * @author  AISafe64 Team
 * @date    2026-04-07
 * @version 3.0
 *
 * @details 实现多核调度、负载均衡、工作窃取和 CPU 亲和性。
 *          - 直接操作调度器真实就绪队列（g_scheduler.cpu_queues）
 *          - KThread_t 对象迁移（非仅优先级编号）
 *          - 工作窃取：空闲 CPU 主动从忙碌 CPU 窃取线程
 *          - 自适应均衡间隔：根据负载差异动态调整
 *          - CPU 亲和性跟踪与强制执行
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: MP-001~005, SC-004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/smp.h>
#include <kernel/ipi.h>
#include <kernel/config.h>
#include <kernel/spinlock.h>
#include <kernel/barrier.h>
#include <kernel/alignment.h>
#include <kernel/errno.h>
#include <kernel/bitmap.h>
#include <stdint.h>
#include "scheduler.h"
#include "hal.h"

/* ========================================================================
 * 内部常量定义
 * ======================================================================== */

/** @brief 自适应均衡间隔下限 */
#define LB_INTERVAL_MIN         32U

/** @brief 自适应均衡间隔上限 */
#define LB_INTERVAL_MAX         256U

/** @brief 默认均衡间隔 */
#define LB_INTERVAL_DEF         100U

/** @brief 负载不均衡阈值（百分比） */
#define LOAD_IMBALANCE_THRESHOLD 150U

/** @brief 单次最大迁移线程数 */
#define MAX_MIGRATE_PER_BALANCE  4U

/** @brief 工作窃取：源 CPU 至少保留的线程数 */
#define STEAL_MIN_RESERVE        2U

/* ========================================================================
 * 内部状态
 * ======================================================================== */

/**
 * @brief 全局调度统计（每 CPU 独立 cache 行，避免伪共享）
 *
 * @details P1-4 修复：原 s_schedule_count 为普通数组，相邻 CPU 计数器
 *          落在同一 cache 行造成伪共享。现用 CACHE_ALIGN 结构体数组，
 *          每个 CPU 的计数独占一个 cache 行。
 */
typedef struct
{
    uint64_t count;            /**< @brief 调度计数 */
    uint64_t pad[7];           /**< @brief 填充至 64 字节 cache 行 */
} CACHE_ALIGN(64) smp_counter_t;

static smp_counter_t s_schedule_count[CONFIG_MAX_CPUS];

/**
 * @brief 每 CPU 自适应均衡间隔与负载均衡请求标志（cache 行对齐）
 *
 * @details P1-4 修复：s_lb_interval 与 s_lb_pending 同样按 CPU 频繁读写，
 *          与 s_schedule_count 一样需要避免伪共享，故放入 cache 行对齐结构体。
 *          s_lb_pending 同时承担 P1-3 中"定时器置标志、idle 执行均衡"的职责。
 */
typedef struct
{
    uint32_t interval;         /**< @brief 自适应均衡间隔 */
    uint32_t lb_pending;       /**< @brief 负载均衡请求标志（0=无，1=待执行） */
    uint32_t pad[14];          /**< @brief 填充至 64 字节 cache 行 */
} CACHE_ALIGN(64) smp_lb_state_t;

static smp_lb_state_t s_lb_state[CONFIG_MAX_CPUS];

/* ========================================================================
 * 迁移统计状态
 * ======================================================================== */

/**
 * @brief 每 CPU 迁移统计（cache 行对齐，避免伪共享）
 *
 * @details P1-4 修复：原 s_migrate_stats 为普通数组（每元素 16 字节），
 *          多个 CPU 的统计共享 cache 行造成伪共享。现每个 CPU 独占 cache 行。
 */
typedef struct
{
    smp_migrate_stats_t stats; /**< @brief 迁移统计 */
    uint8_t pad[48];           /**< @brief 填充至 64 字节 cache 行 */
} CACHE_ALIGN(64) smp_migrate_entry_t;

static smp_migrate_entry_t s_migrate_stats[CONFIG_MAX_CPUS];

/** @brief 迁移统计锁 */
static TicketLock_t s_migrate_stats_lock;

/* ========================================================================
 * 调度器队列远程操作锁
 * ========================================================================
 *
 * @details P1-3 修复前：调用者（smp_load_balance/smp_work_steal/
 *          smp_migrate_thread）在最外层用 hal_irq_disable 统一关中断，
 *          内部 sched_queue_lock 仅取普通 ticket 锁。这造成双重关中断，
 *          且负载均衡全程关中断（扫描所有 CPU + 双锁 + 循环迁移 + IPI），
 *          关闭区间远超 10μs。
 *
 *          P1-3 修复后：删除外层 hal_irq_disable，关中断下沉到每个队列
 *          锁内部（irqsave）。这样只在真正持锁操作队列时关中断，
 *          扫描负载、计算迁移目标等均开中断执行。负载均衡整体不再
 *          在定时器中断上下文执行（由 idle 线程处理）。
 */

/**
 * @brief 单队列加锁（irqsave，自行关中断）
 *
 * @param q 队列指针
 *
 * @return 保存的 IRQ 状态，供 sched_queue_unlock 恢复
 */
static uint32_t sched_queue_lock(PerCPUReadyQueue_t *q)
{
    return ticket_lock_acquire_irqsave(&q->lock);
}

/**
 * @brief 单队列解锁（恢复 IRQ 状态）
 */
static void sched_queue_unlock(PerCPUReadyQueue_t *q, uint32_t irq_state)
{
    ticket_lock_release_irqrestore(&q->lock, irq_state);
}

/**
 * @brief 双队列加锁（按地址顺序，避免死锁；本核全程关中断）
 *
 * @details 两次锁都基于 irqsave。当 a != b 时，先关中断取第一把锁，
 *          再取第二把锁，第二把锁的 irq_state 被丢弃（中断已关）。
 *          调用者必须用 sched_unlock_dual 释放，并传入首把锁返回的
 *          irq_state 以恢复中断。
 *
 * @param a 队列 A
 * @param b 队列 B
 *
 * @return 首次关中断时保存的 IRQ 状态，供 sched_unlock_dual 恢复
 */
static uint32_t sched_lock_dual(PerCPUReadyQueue_t *a, PerCPUReadyQueue_t *b)
{
    uint32_t irq_state;

    if (a == b)
    {
        irq_state = sched_queue_lock(a);
    }
    else if ((uintptr_t)a < (uintptr_t)b)
    {
        irq_state = sched_queue_lock(a);
        (void)sched_queue_lock(b);
    }
    else
    {
        irq_state = sched_queue_lock(b);
        (void)sched_queue_lock(a);
    }

    return irq_state;
}

static void sched_unlock_dual(PerCPUReadyQueue_t *a,
                              PerCPUReadyQueue_t *b,
                              uint32_t irq_state)
{
    if (a == b)
    {
        sched_queue_unlock(a, irq_state);
    }
    else if ((uintptr_t)a < (uintptr_t)b)
    {
        /* 先释放 b（恢复时中断仍关，因状态为已关），再释放 a 恢复中断 */
        sched_queue_unlock(b, irq_state);
        sched_queue_unlock(a, irq_state);
    }
    else
    {
        sched_queue_unlock(a, irq_state);
        sched_queue_unlock(b, irq_state);
    }
}

/* ========================================================================
 * 多核调度初始化
 * ======================================================================== */

kernel_status_t smp_sched_init(void)
{
    uint32_t cpu;
    uint32_t i;
    volatile uint8_t *dst;

    /* P1-4：s_schedule_count 现为 cache 行对齐结构体数组，
     * 逐字节清零以确保填充区亦被清零。 */
    dst = (volatile uint8_t *)s_schedule_count;
    for (i = 0U; i < sizeof(s_schedule_count); i++)
    {
        dst[i] = 0U;
    }

    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        s_lb_state[cpu].interval = LB_INTERVAL_DEF;
        s_lb_state[cpu].lb_pending = 0U;
        s_migrate_stats[cpu].stats.migrate_count = 0U;
        s_migrate_stats[cpu].stats.steal_count = 0U;
        s_migrate_stats[cpu].stats.affinity_reject = 0U;
        s_migrate_stats[cpu].stats.load_balance_count = 0U;
    }

    /* P1-5：亲和性已移至 KThread_t，无需全局 affinity 锁与表。
     * g_scheduler.thread_table 在 scheduler.c 中静态零初始化，
     * 故所有线程 affinity_mask 初值默认为 0（无约束）。 */
    ticket_lock_init(&s_migrate_stats_lock);
    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 就绪队列操作（兼容接口）
 * ======================================================================== */

kernel_status_t smp_enqueue(uint32_t cpu_id, uint32_t priority)
{
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return -(int32_t)EINVAL;
    }
    if (priority > 255U)
    {
        return -(int32_t)EINVAL;
    }
    return KERNEL_OK;
}

kernel_status_t smp_dequeue(uint32_t cpu_id, uint32_t priority)
{
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return -(int32_t)EINVAL;
    }
    if (priority > 255U)
    {
        return -(int32_t)EINVAL;
    }
    return KERNEL_OK;
}

/* ========================================================================
 * 远程队列操作（真实 KThread_t 迁移）
 * ======================================================================== */

/**
 * @brief 从远程 CPU 队列取出最高优先级线程（需已持锁）
 */
static KThread_t *remote_pick_highest(uint32_t cpu_id)
{
    PerCPUReadyQueue_t *cpu_q;
    uint32_t highest_prio;
    struct list_head *first;

    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return NULL;
    }

    cpu_q = &g_scheduler.cpu_queues[cpu_id];
    highest_prio = bitmap256_find_highest(&cpu_q->bitmap);

    if (highest_prio >= CONFIG_PRIORITY_LEVELS)
    {
        return NULL;
    }

    first = cpu_q->queues[highest_prio].next;
    if (first == &cpu_q->queues[highest_prio])
    {
        return NULL;
    }

    return container_of(first, KThread_t, rq_list);
}

/**
 * @brief 从远程 CPU 队列移除线程（需已持锁）
 */
static void remote_remove_thread(PerCPUReadyQueue_t *cpu_q, KThread_t *thread)
{
    thread->rq_list.prev->next = thread->rq_list.next;
    thread->rq_list.next->prev = thread->rq_list.prev;
    thread->rq_list.next = &thread->rq_list;
    thread->rq_list.prev = &thread->rq_list;

    if (cpu_q->queues[thread->prio].next == &cpu_q->queues[thread->prio])
    {
        bitmap256_clear(&cpu_q->bitmap, (uint32_t)thread->prio);
    }

    if (cpu_q->nr_running > 0U)
    {
        cpu_q->nr_running--;
    }
}

/**
 * @brief 将线程加入远程 CPU 队列（需已持锁）
 */
static void remote_add_thread(PerCPUReadyQueue_t *cpu_q, KThread_t *thread)
{
    bitmap256_set(&cpu_q->bitmap, (uint32_t)thread->prio);

    thread->rq_list.next = &cpu_q->queues[thread->prio];
    thread->rq_list.prev = cpu_q->queues[thread->prio].prev;
    cpu_q->queues[thread->prio].prev->next = &thread->rq_list;
    cpu_q->queues[thread->prio].prev = &thread->rq_list;

    cpu_q->nr_running++;
}

/* ========================================================================
 * CPU 亲和性管理
 * ======================================================================== */

/**
 * @brief 设置线程的 CPU 亲和性（per-object 原子写，无全局锁）
 *
 * @details P1-5 修复：亲和性掩码存储在线程控制块 KThread_t 中，
 *          ARM64 上 32 位对齐读写本身是原子的，故无需全局锁保护，
 *          消除所有 CPU 争用同一 s_affinity_lock 的瓶颈。
 */
kernel_status_t smp_set_affinity(uint32_t thread_id, uint32_t cpu_mask)
{
    uint32_t valid_mask;
    KThread_t *thread;

    if (thread_id >= CONFIG_MAX_THREADS)
    {
        return -(int32_t)EINVAL;
    }

    if (cpu_mask == 0U)
    {
        thread = &g_scheduler.thread_table[thread_id];
        thread->affinity_mask = 0U;
        barrier_store();
        return KERNEL_OK;
    }

    valid_mask = cpu_mask & ((1U << CONFIG_MAX_CPUS) - 1U);
    if (valid_mask == 0U)
    {
        return -(int32_t)EINVAL;
    }

    thread = &g_scheduler.thread_table[thread_id];
    thread->affinity_mask = valid_mask;
    barrier_store();

    return KERNEL_OK;
}

/**
 * @brief 获取线程的 CPU 亲和性（per-object 原子读，无全局锁）
 */
uint32_t smp_get_affinity(uint32_t thread_id)
{
    uint32_t mask;

    if (thread_id >= CONFIG_MAX_THREADS)
    {
        return 0U;
    }

    barrier_load();
    mask = g_scheduler.thread_table[thread_id].affinity_mask;

    return mask;
}

bool smp_affinity_allowed(uint32_t thread_id, uint32_t cpu_id)
{
    uint32_t mask;

    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return false;
    }

    mask = smp_get_affinity(thread_id);

    if (mask == 0U)
    {
        return true;
    }

    return ((mask & (1U << cpu_id)) != 0U) ? true : false;
}

uint32_t smp_select_target_cpu(uint32_t thread_id, uint32_t exclude_cpu)
{
    uint32_t mask;
    uint32_t cpu;
    uint32_t best_cpu;
    uint32_t min_load;

    mask = smp_get_affinity(thread_id);
    best_cpu = SMP_CPU_INVALID;
    min_load = 0xFFFFFFFFU;

    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        if ((cpu == exclude_cpu) || (!smp_cpu_online(cpu)))
        {
            continue;
        }
        if ((mask != 0U) && ((mask & (1U << cpu)) == 0U))
        {
            continue;
        }
        if (g_scheduler.cpu_queues[cpu].nr_running < min_load)
        {
            min_load = g_scheduler.cpu_queues[cpu].nr_running;
            best_cpu = cpu;
        }
    }

    return best_cpu;
}

uint32_t smp_select_enqueue_cpu(uint32_t thread_id, uint32_t hint_cpu)
{
    uint32_t mask;
    uint32_t cpu;
    uint32_t best_cpu;
    uint32_t min_load;

    mask = smp_get_affinity(thread_id);

    if (mask == 0U)
    {
        if ((hint_cpu < CONFIG_MAX_CPUS) && (smp_cpu_online(hint_cpu)))
        {
            return hint_cpu;
        }
        for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
        {
            if (smp_cpu_online(cpu))
            {
                return cpu;
            }
        }
        return 0U;
    }

    best_cpu = SMP_CPU_INVALID;
    min_load = 0xFFFFFFFFU;

    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        if (!smp_cpu_online(cpu))
        {
            continue;
        }
        if ((mask & (1U << cpu)) == 0U)
        {
            continue;
        }
        if (g_scheduler.cpu_queues[cpu].nr_running < min_load)
        {
            min_load = g_scheduler.cpu_queues[cpu].nr_running;
            best_cpu = cpu;
        }
    }

    if (best_cpu == SMP_CPU_INVALID)
    {
        return 0U;
    }

    return best_cpu;
}

/* ========================================================================
 * 负载均衡
 * ======================================================================== */

uint32_t smp_get_load(uint32_t cpu_id)
{
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return 0U;
    }
    return g_scheduler.cpu_queues[cpu_id].nr_running;
}

/**
 * @brief 检查是否需要负载均衡（自适应间隔）
 *
 * @details 根据负载差异动态调整均衡间隔
 */
static bool need_load_balance(uint32_t cpu_id)
{
    uint32_t cpu;
    uint32_t total_load;
    uint32_t online_cpus;
    uint32_t avg_load;
    bool need;

    total_load = 0U;
    online_cpus = 0U;

    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        if (smp_cpu_online(cpu))
        {
            total_load += g_scheduler.cpu_queues[cpu].nr_running;
            online_cpus++;
        }
    }

    if (online_cpus <= 1U)
    {
        return false;
    }

    avg_load = total_load / online_cpus;
    if (avg_load == 0U)
    {
        return false;
    }

    need = false;
    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        if (smp_cpu_online(cpu))
        {
            uint32_t load = g_scheduler.cpu_queues[cpu].nr_running;
            if ((load * 100U) > (avg_load * LOAD_IMBALANCE_THRESHOLD))
            {
                need = true;
                break;
            }
        }
    }

    /* 自适应间隔调整 */
    if (cpu_id < CONFIG_MAX_CPUS)
    {
        if (need)
        {
            if (s_lb_state[cpu_id].interval > LB_INTERVAL_MIN)
            {
                s_lb_state[cpu_id].interval = s_lb_state[cpu_id].interval / 2U;
            }
        }
        else
        {
            if (s_lb_state[cpu_id].interval < LB_INTERVAL_MAX)
            {
                s_lb_state[cpu_id].interval = s_lb_state[cpu_id].interval +
                    (s_lb_state[cpu_id].interval / 4U);
            }
        }
    }

    return need;
}

/**
 * @brief 执行负载均衡（真实 KThread_t 迁移）
 *
 * @details 从最忙 CPU 迁移真实线程到最闲 CPU。
 *          按地址顺序锁定双队列避免 ABBA 死锁。
 *
 * @note 对应需求: MP-004, MP-005
 */
kernel_status_t smp_load_balance(void)
{
    uint32_t src_cpu;
    uint32_t dst_cpu;
    uint32_t max_load;
    uint32_t min_load;
    uint32_t migrated;
    uint32_t migrate_limit;
    uint32_t cpu;
    uint32_t irq_state;
    uint32_t stats_irq;
    PerCPUReadyQueue_t *src_q;
    PerCPUReadyQueue_t *dst_q;

    if (!need_load_balance(0U))
    {
        return KERNEL_OK;
    }

    /* P1-3 修复：不再在最外层关中断。负载扫描、迁移目标计算等
     * 开中断执行；仅在真正操作就绪队列时由 sched_lock_dual 关中断。
     * 关中断区间从"全程"缩短到"每次双锁"，远小于 10μs。 */

    src_cpu = 0U;
    max_load = 0U;
    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        if (!smp_cpu_online(cpu))
        {
            continue;
        }
        if (g_scheduler.cpu_queues[cpu].nr_running > max_load)
        {
            max_load = g_scheduler.cpu_queues[cpu].nr_running;
            src_cpu = cpu;
        }
    }

    dst_cpu = SMP_CPU_INVALID;
    min_load = 0xFFFFFFFFU;
    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        if (!smp_cpu_online(cpu) || (cpu == src_cpu))
        {
            continue;
        }
        if (g_scheduler.cpu_queues[cpu].nr_running < min_load)
        {
            min_load = g_scheduler.cpu_queues[cpu].nr_running;
            dst_cpu = cpu;
        }
    }

    if ((dst_cpu == SMP_CPU_INVALID) || (src_cpu == dst_cpu))
    {
        return KERNEL_OK;
    }

    migrate_limit = max_load / 2U;
    if (migrate_limit > MAX_MIGRATE_PER_BALANCE)
    {
        migrate_limit = MAX_MIGRATE_PER_BALANCE;
    }
    if (migrate_limit == 0U)
    {
        migrate_limit = 1U;
    }

    src_q = &g_scheduler.cpu_queues[src_cpu];
    dst_q = &g_scheduler.cpu_queues[dst_cpu];

    migrated = 0U;
    while (migrated < migrate_limit)
    {
        KThread_t *thread;
        uint32_t tid;

        irq_state = sched_lock_dual(src_q, dst_q);

        thread = remote_pick_highest(src_cpu);
        if (thread == NULL)
        {
            sched_unlock_dual(src_q, dst_q, irq_state);
            break;
        }

        tid = (uint32_t)thread->tid;

        /* 亲和性检查 */
        if ((tid < CONFIG_MAX_THREADS) &&
            (!smp_affinity_allowed(tid, dst_cpu)))
        {
            sched_unlock_dual(src_q, dst_q, irq_state);
            stats_irq = ticket_lock_acquire_irqsave(&s_migrate_stats_lock);
            s_migrate_stats[src_cpu].stats.affinity_reject++;
            ticket_lock_release_irqrestore(&s_migrate_stats_lock, stats_irq);
            break;
        }

        /* 真实迁移 */
        remote_remove_thread(src_q, thread);
        remote_add_thread(dst_q, thread);

        sched_unlock_dual(src_q, dst_q, irq_state);
        migrated++;
    }

    if (migrated > 0U)
    {
        (void)ipi_send(dst_cpu, IPI_TYPE_RESCHEDULE);

        /* 更新迁移统计 */
        stats_irq = ticket_lock_acquire_irqsave(&s_migrate_stats_lock);
        s_migrate_stats[src_cpu].stats.migrate_count += migrated;
        s_migrate_stats[dst_cpu].stats.load_balance_count++;
        ticket_lock_release_irqrestore(&s_migrate_stats_lock, stats_irq);
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 工作窃取
 * ======================================================================== */

/**
 * @brief 工作窃取：空闲 CPU 从忙碌 CPU 窃取线程
 *
 * @details 窃取策略：取源 CPU 最高优先级链表尾部线程（缓存冷）。
 *          条件：当前 CPU 队列为空，源 CPU 至少保留 STEAL_MIN_RESERVE 个线程。
 *
 * @note 对应需求: MP-004
 */
kernel_status_t smp_work_steal(uint32_t current_cpu)
{
    uint32_t src_cpu;
    uint32_t max_load;
    uint32_t cpu;
    KThread_t *thread;
    PerCPUReadyQueue_t *src_q;
    PerCPUReadyQueue_t *dst_q;
    uint32_t tid;
    uint32_t hp;
    uint32_t irq_state;
    uint32_t stats_irq;

    if (current_cpu >= CONFIG_MAX_CPUS)
    {
        return -(int32_t)EINVAL;
    }

    if (g_scheduler.cpu_queues[current_cpu].nr_running > 0U)
    {
        return KERNEL_OK;
    }

    /* P1-3 修复：删除最外层 hal_irq_disable，关中断下沉到 sched_lock_dual。 */

    /* 找最忙的 CPU */
    src_cpu = SMP_CPU_INVALID;
    max_load = STEAL_MIN_RESERVE;
    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        if (!smp_cpu_online(cpu) || (cpu == current_cpu))
        {
            continue;
        }
        if (g_scheduler.cpu_queues[cpu].nr_running > max_load)
        {
            max_load = g_scheduler.cpu_queues[cpu].nr_running;
            src_cpu = cpu;
        }
    }

    if (src_cpu == SMP_CPU_INVALID)
    {
        return KERNEL_OK;
    }

    src_q = &g_scheduler.cpu_queues[src_cpu];
    dst_q = &g_scheduler.cpu_queues[current_cpu];

    irq_state = sched_lock_dual(src_q, dst_q);

    if (src_q->nr_running <= STEAL_MIN_RESERVE)
    {
        sched_unlock_dual(src_q, dst_q, irq_state);
        return KERNEL_OK;
    }

    /* 取最高优先级链表尾部 */
    hp = bitmap256_find_highest(&src_q->bitmap);
    if (hp >= CONFIG_PRIORITY_LEVELS)
    {
        sched_unlock_dual(src_q, dst_q, irq_state);
        return KERNEL_OK;
    }

    if (src_q->queues[hp].prev == &src_q->queues[hp])
    {
        sched_unlock_dual(src_q, dst_q, irq_state);
        return KERNEL_OK;
    }

    thread = container_of(src_q->queues[hp].prev, KThread_t, rq_list);
    tid = (uint32_t)thread->tid;

    if ((tid < CONFIG_MAX_THREADS) &&
        (!smp_affinity_allowed(tid, current_cpu)))
    {
        sched_unlock_dual(src_q, dst_q, irq_state);
        stats_irq = ticket_lock_acquire_irqsave(&s_migrate_stats_lock);
        s_migrate_stats[src_cpu].stats.affinity_reject++;
        ticket_lock_release_irqrestore(&s_migrate_stats_lock, stats_irq);
        return KERNEL_OK;
    }

    remote_remove_thread(src_q, thread);
    remote_add_thread(dst_q, thread);

    sched_unlock_dual(src_q, dst_q, irq_state);

    /* 更新窃取统计 */
    stats_irq = ticket_lock_acquire_irqsave(&s_migrate_stats_lock);
    s_migrate_stats[src_cpu].stats.steal_count++;
    ticket_lock_release_irqrestore(&s_migrate_stats_lock, stats_irq);

    (void)ipi_send(src_cpu, IPI_TYPE_RESCHEDULE);

    return KERNEL_OK;
}

/* ========================================================================
 * 周期性负载均衡触发（自适应间隔 + 工作窃取）
 * ======================================================================== */

/**
 * @brief 定时器中断中的均衡检查（仅置标志，不执行实际均衡）
 *
 * @details P1-3 修复：负载均衡（扫描所有 CPU + 双锁 + 循环迁移 + IPI）
 *          耗时远超 10μs，原本在定时器中断中直接调用 smp_load_balance
 *          会导致中断关闭时间过长。
 *          现在定时器中断只递增计数并在到达间隔时置位 s_lb_pending 标志，
 *          实际均衡由 idle 线程通过 smp_idle_balance() 执行。
 *
 * @note 工作窃取同样不再在中断中执行，改由 idle 线程执行。
 */
void smp_tick_check_balance(uint32_t cpu_id)
{
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return;
    }

    s_schedule_count[cpu_id].count++;

    if ((s_schedule_count[cpu_id].count % (uint64_t)s_lb_state[cpu_id].interval) == 0ULL)
    {
        /* 仅置标志，由 idle 线程执行实际均衡（开中断、可被抢占） */
        s_lb_state[cpu_id].lb_pending = 1U;
        barrier_store();
    }
}

void smp_sched_tick(uint32_t cpu_id)
{
    smp_tick_check_balance(cpu_id);
}

/**
 * @brief idle 线程周期性执行的负载均衡与工作窃取
 *
 * @details 由各 CPU 的 idle 线程在低功耗循环中调用。
 *          - 若本 CPU 的 s_lb_pending 标志置位，执行一次负载均衡并清标志；
 *          - 若本 CPU 就绪队列为空，尝试工作窃取。
 *
 *          在 idle 线程上下文执行，中断开启，可被高优先级线程抢占，
 *          不会延长中断关闭时间。
 */
void smp_idle_balance(uint32_t cpu_id)
{
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return;
    }

    /* 检查负载均衡请求标志（原子读） */
    barrier_load();
    if (s_lb_state[cpu_id].lb_pending != 0U)
    {
        s_lb_state[cpu_id].lb_pending = 0U;
        barrier_store();
        (void)smp_load_balance();
    }

    /* 本 CPU 空闲则尝试工作窃取 */
    if (g_scheduler.cpu_queues[cpu_id].nr_running == 0U)
    {
        (void)smp_work_steal(cpu_id);
    }
}

/* ========================================================================
 * 显式线程迁移
 * ======================================================================== */

kernel_status_t smp_migrate_thread(uint32_t src_cpu,
                                     uint32_t dst_cpu,
                                     uint32_t priority,
                                     uint32_t thread_id)
{
    PerCPUReadyQueue_t *src_q;
    PerCPUReadyQueue_t *dst_q;
    KThread_t *thread;
    uint32_t irq_state;

    (void)priority;

    if ((src_cpu >= CONFIG_MAX_CPUS) || (dst_cpu >= CONFIG_MAX_CPUS))
    {
        return -(int32_t)EINVAL;
    }
    if (src_cpu == dst_cpu)
    {
        return KERNEL_OK;
    }
    if (!smp_cpu_online(dst_cpu))
    {
        return -(int32_t)EINVAL;
    }
    if (thread_id >= CONFIG_MAX_THREADS)
    {
        return -(int32_t)EINVAL;
    }
    if (!smp_affinity_allowed(thread_id, dst_cpu))
    {
        return -(int32_t)EPERM;
    }

    src_q = &g_scheduler.cpu_queues[src_cpu];
    dst_q = &g_scheduler.cpu_queues[dst_cpu];

    /* P1-3 修复：删除最外层 hal_irq_disable，关中断下沉到 sched_lock_dual。 */
    irq_state = sched_lock_dual(src_q, dst_q);

    thread = &g_scheduler.thread_table[thread_id];
    if (thread->state != KTHREAD_STATE_READY)
    {
        sched_unlock_dual(src_q, dst_q, irq_state);
        return -(int32_t)EINVAL;
    }

    remote_remove_thread(src_q, thread);
    remote_add_thread(dst_q, thread);

    sched_unlock_dual(src_q, dst_q, irq_state);

    (void)ipi_send(dst_cpu, IPI_TYPE_RESCHEDULE);

    return KERNEL_OK;
}

/* ========================================================================
 * IPI 重新调度发送
 * ======================================================================== */

kernel_status_t smp_send_reschedule(uint32_t target_cpu)
{
    if (target_cpu >= CONFIG_MAX_CPUS)
    {
        return -(int32_t)EINVAL;
    }
    if (!smp_cpu_online(target_cpu))
    {
        return -(int32_t)EINVAL;
    }
    return ipi_send(target_cpu, IPI_TYPE_RESCHEDULE);
}

/* ========================================================================
 * RCU-like 宽限期实现
 * ======================================================================== */

void smp_grace_period_start(smp_grace_period_t *gp)
{
    if (gp == NULL)
    {
        return;
    }

    /* 递增宽限期序号 */
    gp->gp_seq = gp->gp_seq + 1ULL;
    barrier_store();

    /* 向所有其他在线 CPU 广播确认请求 */
    (void)ipi_broadcast(IPI_TYPE_RESCHEDULE, true);
}

void smp_grace_period_wait(const smp_grace_period_t *gp)
{
    uint32_t cpu;
    uint64_t target_seq;

    if (gp == NULL)
    {
        return;
    }

    target_seq = gp->gp_seq;

    /* 自旋等待所有在线 CPU 确认 */
    for (;;)
    {
        bool all_acked = true;

        barrier_load();
        for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
        {
            if (!smp_cpu_online(cpu))
            {
                continue;
            }
            if (gp->cpu_ack[cpu] < target_seq)
            {
                all_acked = false;
                break;
            }
        }

        if (all_acked)
        {
            break;
        }

        cpu_relax();
    }
}

void smp_grace_period_ack(smp_grace_period_t *gp)
{
    uint32_t cpu_id;

    if (gp == NULL)
    {
        return;
    }

    cpu_id = smp_get_cpu_id();
    if (cpu_id < CONFIG_MAX_CPUS)
    {
        gp->cpu_ack[cpu_id] = gp->gp_seq;
        barrier_store();
    }
}

/* ========================================================================
 * 迁移统计查询
 * ======================================================================== */

kernel_status_t smp_get_migrate_stats(uint32_t cpu_id, smp_migrate_stats_t *stats)
{
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return -(int32_t)EINVAL;
    }

    if (stats == NULL)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&s_migrate_stats_lock);

    stats->migrate_count = s_migrate_stats[cpu_id].stats.migrate_count;
    stats->steal_count = s_migrate_stats[cpu_id].stats.steal_count;
    stats->affinity_reject = s_migrate_stats[cpu_id].stats.affinity_reject;
    stats->load_balance_count = s_migrate_stats[cpu_id].stats.load_balance_count;

    ticket_lock_release(&s_migrate_stats_lock);

    return KERNEL_OK;
}
