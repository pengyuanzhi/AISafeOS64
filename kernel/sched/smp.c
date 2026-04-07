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
#include <kernel/errno.h>
#include <kernel/bitmap.h>
#include <stdint.h>
#include "scheduler.h"

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

/** @brief 每 CPU 自适应均衡间隔 */
static uint32_t s_lb_interval[CONFIG_MAX_CPUS];

/** @brief 全局调度统计 */
static uint64_t s_schedule_count[CONFIG_MAX_CPUS];

/** @brief 每 CPU 亲和性位图（索引为线程 ID） */
static uint32_t s_thread_affinity[CONFIG_MAX_THREADS];

/** @brief 亲和性表锁 */
static TicketLock_t s_affinity_lock;

/* ========================================================================
 * 调度器队列远程操作锁
 * ======================================================================== */

static void sched_queue_lock(PerCPUReadyQueue_t *q)
{
    while (!atomic_cas_u32(&q->lock, 0U, 1U))
    {
        cpu_relax();
    }
}

static void sched_queue_unlock(PerCPUReadyQueue_t *q)
{
    barrier_store();
    q->lock = 0U;
}

static void sched_lock_dual(PerCPUReadyQueue_t *a, PerCPUReadyQueue_t *b)
{
    if (a == b)
    {
        sched_queue_lock(a);
    }
    else if ((uintptr_t)a < (uintptr_t)b)
    {
        sched_queue_lock(a);
        sched_queue_lock(b);
    }
    else
    {
        sched_queue_lock(b);
        sched_queue_lock(a);
    }
}

static void sched_unlock_dual(PerCPUReadyQueue_t *a, PerCPUReadyQueue_t *b)
{
    if (a == b)
    {
        sched_queue_unlock(a);
    }
    else if ((uintptr_t)a < (uintptr_t)b)
    {
        sched_queue_unlock(b);
        sched_queue_unlock(a);
    }
    else
    {
        sched_queue_unlock(a);
        sched_queue_unlock(b);
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

    dst = (volatile uint8_t *)s_schedule_count;
    for (i = 0U; i < sizeof(s_schedule_count); i++)
    {
        dst[i] = 0U;
    }

    dst = (volatile uint8_t *)s_thread_affinity;
    for (i = 0U; i < sizeof(s_thread_affinity); i++)
    {
        dst[i] = 0U;
    }

    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        s_lb_interval[cpu] = LB_INTERVAL_DEF;
    }

    ticket_lock_init(&s_affinity_lock);
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

kernel_status_t smp_set_affinity(uint32_t thread_id, uint32_t cpu_mask)
{
    uint32_t valid_mask;

    if (thread_id >= CONFIG_MAX_THREADS)
    {
        return -(int32_t)EINVAL;
    }

    if (cpu_mask == 0U)
    {
        ticket_lock_acquire(&s_affinity_lock);
        s_thread_affinity[thread_id] = 0U;
        ticket_lock_release(&s_affinity_lock);
        return KERNEL_OK;
    }

    valid_mask = cpu_mask & ((1U << CONFIG_MAX_CPUS) - 1U);
    if (valid_mask == 0U)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&s_affinity_lock);
    s_thread_affinity[thread_id] = valid_mask;
    ticket_lock_release(&s_affinity_lock);

    return KERNEL_OK;
}

uint32_t smp_get_affinity(uint32_t thread_id)
{
    uint32_t mask;

    if (thread_id >= CONFIG_MAX_THREADS)
    {
        return 0U;
    }

    ticket_lock_acquire(&s_affinity_lock);
    mask = s_thread_affinity[thread_id];
    ticket_lock_release(&s_affinity_lock);

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
            if (s_lb_interval[cpu_id] > LB_INTERVAL_MIN)
            {
                s_lb_interval[cpu_id] = s_lb_interval[cpu_id] / 2U;
            }
        }
        else
        {
            if (s_lb_interval[cpu_id] < LB_INTERVAL_MAX)
            {
                s_lb_interval[cpu_id] = s_lb_interval[cpu_id] +
                    (s_lb_interval[cpu_id] / 4U);
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
    PerCPUReadyQueue_t *src_q;
    PerCPUReadyQueue_t *dst_q;

    if (!need_load_balance(0U))
    {
        return KERNEL_OK;
    }

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

        sched_lock_dual(src_q, dst_q);

        thread = remote_pick_highest(src_cpu);
        if (thread == NULL)
        {
            sched_unlock_dual(src_q, dst_q);
            break;
        }

        tid = (uint32_t)thread->tid;

        /* 亲和性检查 */
        if ((tid < CONFIG_MAX_THREADS) &&
            (!smp_affinity_allowed(tid, dst_cpu)))
        {
            sched_unlock_dual(src_q, dst_q);
            break;
        }

        /* 真实迁移 */
        remote_remove_thread(src_q, thread);
        remote_add_thread(dst_q, thread);

        sched_unlock_dual(src_q, dst_q);
        migrated++;
    }

    if (migrated > 0U)
    {
        (void)ipi_send(dst_cpu, IPI_TYPE_RESCHEDULE);
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

    if (current_cpu >= CONFIG_MAX_CPUS)
    {
        return -(int32_t)EINVAL;
    }

    if (g_scheduler.cpu_queues[current_cpu].nr_running > 0U)
    {
        return KERNEL_OK;
    }

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

    sched_lock_dual(src_q, dst_q);

    if (src_q->nr_running <= STEAL_MIN_RESERVE)
    {
        sched_unlock_dual(src_q, dst_q);
        return KERNEL_OK;
    }

    /* 取最高优先级链表尾部 */
    hp = bitmap256_find_highest(&src_q->bitmap);
    if (hp >= CONFIG_PRIORITY_LEVELS)
    {
        sched_unlock_dual(src_q, dst_q);
        return KERNEL_OK;
    }

    if (src_q->queues[hp].prev == &src_q->queues[hp])
    {
        sched_unlock_dual(src_q, dst_q);
        return KERNEL_OK;
    }

    thread = container_of(src_q->queues[hp].prev, KThread_t, rq_list);
    tid = (uint32_t)thread->tid;

    if ((tid < CONFIG_MAX_THREADS) &&
        (!smp_affinity_allowed(tid, current_cpu)))
    {
        sched_unlock_dual(src_q, dst_q);
        return KERNEL_OK;
    }

    remote_remove_thread(src_q, thread);
    remote_add_thread(dst_q, thread);

    sched_unlock_dual(src_q, dst_q);

    (void)ipi_send(src_cpu, IPI_TYPE_RESCHEDULE);

    return KERNEL_OK;
}

/* ========================================================================
 * 周期性负载均衡触发（自适应间隔 + 工作窃取）
 * ======================================================================== */

void smp_tick_check_balance(uint32_t cpu_id)
{
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return;
    }

    s_schedule_count[cpu_id]++;

    if ((s_schedule_count[cpu_id] % (uint64_t)s_lb_interval[cpu_id]) == 0ULL)
    {
        (void)smp_load_balance();
    }

    /* 空闲时尝试工作窃取 */
    if (g_scheduler.cpu_queues[cpu_id].nr_running == 0U)
    {
        (void)smp_work_steal(cpu_id);
    }
}

void smp_sched_tick(uint32_t cpu_id)
{
    smp_tick_check_balance(cpu_id);
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

    sched_lock_dual(src_q, dst_q);

    thread = &g_scheduler.thread_table[thread_id];
    if (thread->state != KTHREAD_STATE_READY)
    {
        sched_unlock_dual(src_q, dst_q);
        return -(int32_t)EINVAL;
    }

    remote_remove_thread(src_q, thread);
    remote_add_thread(dst_q, thread);

    sched_unlock_dual(src_q, dst_q);

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
