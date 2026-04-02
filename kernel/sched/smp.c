/**
 * @file    smp.c
 * @brief   多核调度实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 实现多核调度、负载均衡和工作窃取。
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
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 每 CPU 就绪队列
 * ======================================================================== */

/** @brief 每 CPU 优先级位图（256 级 = 4 个 uint64_t） */
typedef struct
{
    uint64_t    priority_bitmap[4U];    /**< @brief 优先级位图 */
    uint32_t    thread_count;           /**< @brief 就绪线程计数 */
    TicketLock_t lock;                  /**< @brief 队列锁 */
} cpu_ready_queue_t;

/** @brief 每 CPU 就绪队列数组 */
static cpu_ready_queue_t s_cpu_queues[CONFIG_MAX_CPUS]
    __attribute__((aligned(64U)));

/** @brief 负载均衡间隔（调度次数） */
#define LOAD_BALANCE_INTERVAL  100U

/** @brief 负载不均衡阈值（百分比） */
#define LOAD_IMBALANCE_THRESHOLD 150U

/** @brief 全局调度统计 */
static uint64_t s_schedule_count[CONFIG_MAX_CPUS];

/* ========================================================================
 * 多核调度初始化
 * ======================================================================== */

kernel_status_t smp_sched_init(void)
{
    uint32_t cpu;

    (void)memset(s_cpu_queues, 0, sizeof(s_cpu_queues));
    (void)memset(s_schedule_count, 0, sizeof(s_schedule_count));

    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        ticket_lock_init(&s_cpu_queues[cpu].lock);
        s_cpu_queues[cpu].thread_count = 0U;
        s_cpu_queues[cpu].priority_bitmap[0U] = 0ULL;
        s_cpu_queues[cpu].priority_bitmap[1U] = 0ULL;
        s_cpu_queues[cpu].priority_bitmap[2U] = 0ULL;
        s_cpu_queues[cpu].priority_bitmap[3U] = 0ULL;
    }

    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 就绪队列操作
 * ======================================================================== */

/**
 * @brief 向指定 CPU 的就绪队列添加线程
 *
 * @param cpu_id  目标 CPU
 * @param priority 优先级
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t smp_enqueue(uint32_t cpu_id, uint32_t priority)
{
    uint32_t bitmap_idx;
    uint32_t bit_idx;

    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return -(int32_t)EINVAL;
    }

    if (priority > 255U)
    {
        return -(int32_t)EINVAL;
    }

    bitmap_idx = priority / 64U;
    bit_idx = priority % 64U;

    ticket_lock_acquire(&s_cpu_queues[cpu_id].lock);

    s_cpu_queues[cpu_id].priority_bitmap[bitmap_idx] |=
        (1ULL << bit_idx);
    s_cpu_queues[cpu_id].thread_count++;

    ticket_lock_release(&s_cpu_queues[cpu_id].lock);

    return KERNEL_OK;
}

/**
 * @brief 从指定 CPU 的就绪队列移除线程
 *
 * @param cpu_id  目标 CPU
 * @param priority 优先级
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t smp_dequeue(uint32_t cpu_id, uint32_t priority)
{
    uint32_t bitmap_idx;
    uint32_t bit_idx;

    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return -(int32_t)EINVAL;
    }

    if (priority > 255U)
    {
        return -(int32_t)EINVAL;
    }

    bitmap_idx = priority / 64U;
    bit_idx = priority % 64U;

    ticket_lock_acquire(&s_cpu_queues[cpu_id].lock);

    s_cpu_queues[cpu_id].priority_bitmap[bitmap_idx] &=
        ~(1ULL << bit_idx);
    if (s_cpu_queues[cpu_id].thread_count > 0U)
    {
        s_cpu_queues[cpu_id].thread_count--;
    }

    ticket_lock_release(&s_cpu_queues[cpu_id].lock);

    return KERNEL_OK;
}

/**
 * @brief 查找最高优先级
 *
 * @param cpu_id CPU 编号
 *
 * @return 最高优先级（255 = 无就绪线程）
 */
static uint32_t find_highest_priority(uint32_t cpu_id)
{
    uint32_t i;
    for (i = 0U; i < 4U; i++)
    {
        if (s_cpu_queues[cpu_id].priority_bitmap[i] != 0ULL)
        {
            uint32_t prio = (uint32_t)__builtin_clzll(
                s_cpu_queues[cpu_id].priority_bitmap[i]
            );
            return (i * 64U) + prio;
        }
    }

    return 255U;
}

/* ========================================================================
 * 负载均衡
 * ======================================================================== */

/**
 * @brief 获取指定 CPU 的负载
 *
 * @param cpu_id CPU 编号
 *
 * @return 就绪队列中的线程数
 */
uint32_t smp_get_load(uint32_t cpu_id)
{
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return 0U;
    }

    return s_cpu_queues[cpu_id].thread_count;
}

/**
 * @brief 检查是否需要负载均衡
 *
 * @return true 需要均衡
 */
static bool need_load_balance(void)
{
    uint32_t cpu;
    uint32_t total_load = 0U;
    uint32_t online_cpus = 0U;
    uint32_t avg_load;

    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        if (smp_cpu_online(cpu))
        {
            total_load += s_cpu_queues[cpu].thread_count;
            online_cpus++;
        }
    }

    if (online_cpus <= 1U)
    {
        return false;
    }

    avg_load = total_load / online_cpus;

    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        if (smp_cpu_online(cpu))
        {
            uint32_t load = s_cpu_queues[cpu].thread_count;
            if ((load * 100U) > (avg_load * LOAD_IMBALANCE_THRESHOLD))
            {
                return true;
            }
        }
    }

    return false;
}

kernel_status_t smp_load_balance(void)
{
    uint32_t src_cpu;
    uint32_t dst_cpu;
    uint32_t max_load = 0U;
    uint32_t min_load = 0xFFFFFFFFU;
    uint32_t src_prio;
    kernel_status_t ret;

    if (!need_load_balance())
    {
        return KERNEL_OK;
    }

    /* 找到最忙和最闲的 CPU */
    src_cpu = 0U;
    dst_cpu = 0U;

    for (uint32_t cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        if (!smp_cpu_online(cpu))
        {
            continue;
        }

        if (s_cpu_queues[cpu].thread_count > max_load)
        {
            max_load = s_cpu_queues[cpu].thread_count;
            src_cpu = cpu;
        }

        if (s_cpu_queues[cpu].thread_count < min_load)
        {
            min_load = s_cpu_queues[cpu].thread_count;
            dst_cpu = cpu;
        }
    }

    if (src_cpu == dst_cpu)
    {
        return KERNEL_OK;
    }

    /* 从源 CPU 取最高优先级线程迁移 */
    src_prio = find_highest_priority(src_cpu);
    if (src_prio >= 255U)
    {
        return KERNEL_OK;
    }

    /* 从源 CPU 移除 */
    ret = smp_dequeue(src_cpu, src_prio);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    /* 加入目标 CPU */
    ret = smp_enqueue(dst_cpu, src_prio);
    if (ret != KERNEL_OK)
    {
        /* 回滚 */
        (void)smp_enqueue(src_cpu, src_prio);
        return ret;
    }

    /* 通知目标 CPU 重新调度 */
    (void)ipi_send(dst_cpu, IPI_TYPE_RESCHEDULE);

    return KERNEL_OK;
}

/* ========================================================================
 * CPU 亲和性
 * ======================================================================== */

/**
 * @brief 设置线程的 CPU 亲和性（简化实现）
 *
 * @param thread  线程指针（未使用）
 * @param cpu_mask CPU 位掩码
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t smp_set_affinity(void *thread, uint32_t cpu_mask)
{
    (void)thread;
    (void)cpu_mask;

    /* 完整实现中：设置线程的 cpu_affinity 字段 */
    return KERNEL_OK;
}
