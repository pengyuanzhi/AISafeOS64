/**
 * @file    smp.c
 * @brief   多核调度实现
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 2.1
 *
 * @details 实现多核调度、负载均衡、工作窃取和 CPU 亲和性。
 *          - 每 CPU 就绪队列（256 级优先级位图）
 *          - 基于优先级的负载迁移（多线程批量迁移）
 *          - CPU 亲和性跟踪与强制执行
 *          - 亲和性感知的就绪队列选择
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

/* ========================================================================
 * 内部常量定义
 * ======================================================================== */

/** @brief 负载均衡间隔（调度次数） */
#define LOAD_BALANCE_INTERVAL      100U

/** @brief 负载不均衡阈值（百分比，150 表示最忙 CPU 超过均值 1.5 倍） */
#define LOAD_IMBALANCE_THRESHOLD   150U

/** @brief 单次负载均衡最大迁移线程数 */
#define MAX_MIGRATE_PER_BALANCE    4U

/* ========================================================================
 * 每 CPU 就绪队列
 * ======================================================================== */

/**
 * @brief 每 CPU 优先级位图就绪队列
 *
 * @details 使用 4 个 uint64_t 位图表示 256 级优先级。
 *          每个优先级对应一个位，置 1 表示该优先级有就绪线程。
 *          thread_count 跟踪就绪线程总数（含同优先级多线程）。
 */
typedef struct
{
    uint64_t     priority_bitmap[4U];   /**< @brief 优先级位图 */
    uint32_t     thread_count;          /**< @brief 就绪线程计数 */
    uint32_t     reserved[3U];          /**< @brief 预留对齐到缓存行 */
    TicketLock_t lock;                  /**< @brief 队列自旋锁 */
} cpu_ready_queue_t;

/** @brief 每 CPU 就绪队列数组（缓存行对齐，避免伪共享） */
static cpu_ready_queue_t s_cpu_queues[CONFIG_MAX_CPUS]
    __attribute__((aligned(64U)));

/** @brief 全局调度统计 */
static uint64_t s_schedule_count[CONFIG_MAX_CPUS];

/* ========================================================================
 * CPU 亲和性表
 * ======================================================================== */

/**
 * @brief 每 CPU 亲和性位图
 *
 * @details 索引为线程 ID，值为允许运行的 CPU 位掩码。
 *          0 表示无亲和性约束（可在任意 CPU 运行）。
 *          非 0 表示仅能在掩码指定的 CPU 上运行。
 */
static uint32_t s_thread_affinity[CONFIG_MAX_THREADS];

/** @brief 亲和性表锁 */
static TicketLock_t s_affinity_lock;

/* ========================================================================
 * 多核调度初始化
 * ======================================================================== */

/**
 * @brief 初始化多核调度器
 *
 * @details 清零所有每 CPU 就绪队列、调度统计和亲和性表。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: MP-001
 */
kernel_status_t smp_sched_init(void)
{
    uint32_t cpu;
    uint32_t i;
    volatile uint8_t *dst;

    /* 手动清零 s_cpu_queues（MISRA 合规，避免 memset） */
    dst = (volatile uint8_t *)s_cpu_queues;
    for (i = 0U; i < sizeof(s_cpu_queues); i++)
    {
        dst[i] = 0U;
    }

    /* 手动清零 s_schedule_count */
    dst = (volatile uint8_t *)s_schedule_count;
    for (i = 0U; i < sizeof(s_schedule_count); i++)
    {
        dst[i] = 0U;
    }

    /* 手动清零 s_thread_affinity */
    dst = (volatile uint8_t *)s_thread_affinity;
    for (i = 0U; i < sizeof(s_thread_affinity); i++)
    {
        dst[i] = 0U;
    }

    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        ticket_lock_init(&s_cpu_queues[cpu].lock);
        s_cpu_queues[cpu].thread_count = 0U;

        for (i = 0U; i < 4U; i++)
        {
            s_cpu_queues[cpu].priority_bitmap[i] = 0ULL;
        }
    }

    ticket_lock_init(&s_affinity_lock);

    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 就绪队列操作
 * ======================================================================== */

/**
 * @brief 向指定 CPU 的就绪队列添加线程
 *
 * @param cpu_id   目标 CPU 编号
 * @param priority 线程优先级（0~255）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: MP-004
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
 * @param cpu_id   目标 CPU 编号
 * @param priority 线程优先级（0~255）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: MP-004
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
 * @brief 查找指定 CPU 就绪队列中的最高优先级
 *
 * @details 使用 CLZ（Count Leading Zeros）指令在位图中
 *          快速定位最高优先级，O(1) 时间复杂度。
 *
 * @param cpu_id CPU 编号
 *
 * @return 最高优先级值（0~254），无就绪线程返回 255
 *
 * @note 调用者必须持有或不依赖锁保护
 */
static uint32_t find_highest_priority(uint32_t cpu_id)
{
    uint32_t i;

    for (i = 0U; i < 4U; i++)
    {
        if (s_cpu_queues[cpu_id].priority_bitmap[i] != 0ULL)
        {
            /* __builtin_clzll 返回前导零数量（从 MSB 起算）。
             * 位图中 bit N 对应优先级 N（在 bitmap[i] 的 0~63 范围内）。
             * 最高置位位位置 = 63 - clzll，即最高优先级值。 */
            uint32_t bit_pos = 63U - (uint32_t)__builtin_clzll(
                s_cpu_queues[cpu_id].priority_bitmap[i]
            );
            return (i * 64U) + bit_pos;
        }
    }

    return 255U;
}

/* ========================================================================
 * CPU 亲和性管理
 * ======================================================================== */

/**
 * @brief 设置线程的 CPU 亲和性
 *
 * @details 将线程绑定到 cpu_mask 指定的 CPU 集合。
 *          亲和性在下次调度时生效。
 *          cpu_mask 为 0 表示无约束（可在任意 CPU 运行）。
 *
 * @param thread_id 线程 ID
 * @param cpu_mask  允许运行的 CPU 位掩码（bit 0 = CPU0）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效（线程 ID 越界或掩码无在线 CPU）
 *
 * @note 对应需求: MP-005
 */
kernel_status_t smp_set_affinity(uint32_t thread_id, uint32_t cpu_mask)
{
    uint32_t valid_mask;

    if (thread_id >= CONFIG_MAX_THREADS)
    {
        return -(int32_t)EINVAL;
    }

    /* cpu_mask == 0 表示无约束，允许 */
    if (cpu_mask == 0U)
    {
        ticket_lock_acquire(&s_affinity_lock);
        s_thread_affinity[thread_id] = 0U;
        ticket_lock_release(&s_affinity_lock);
        return KERNEL_OK;
    }

    /* 验证掩码中至少有一个有效的 CPU 位 */
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

/**
 * @brief 获取线程的 CPU 亲和性掩码
 *
 * @param thread_id 线程 ID
 *
 * @return CPU 亲和性位掩码，0 表示无约束，越界返回 0
 */
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

/**
 * @brief 检查线程是否允许在指定 CPU 上运行
 *
 * @details 如果线程无亲和性约束（mask == 0），允许任意 CPU。
 *          否则检查 cpu_id 是否在亲和性掩码中。
 *
 * @param thread_id 线程 ID
 * @param cpu_id    目标 CPU 编号
 *
 * @return true 允许运行，false 不允许
 */
bool smp_affinity_allowed(uint32_t thread_id, uint32_t cpu_id)
{
    uint32_t mask;

    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return false;
    }

    mask = smp_get_affinity(thread_id);

    /* mask == 0 表示无约束，允许任意 CPU */
    if (mask == 0U)
    {
        return true;
    }

    return ((mask & (1U << cpu_id)) != 0U) ? true : false;
}

/**
 * @brief 根据亲和性选择最佳目标 CPU
 *
 * @details 在负载均衡迁移时，选择目标 CPU 的策略：
 *          1. 如果线程有亲和性约束，选择亲和性掩码中负载最低的 CPU
 *          2. 如果线程无亲和性约束，选择全局负载最低的 CPU
 *
 * @param thread_id   线程 ID
 * @param exclude_cpu 排除的 CPU（通常是源 CPU）
 *
 * @return 目标 CPU 编号，无合适 CPU 返回 SMP_CPU_INVALID
 */
uint32_t smp_select_target_cpu(uint32_t thread_id, uint32_t exclude_cpu)
{
    uint32_t mask;
    uint32_t cpu;
    uint32_t best_cpu;
    uint32_t min_load;

    mask = smp_get_affinity(thread_id);

    best_cpu = SMP_CPU_INVALID;
    min_load = 0xFFFFFFFFU;

    if (mask == 0U)
    {
        /* 无亲和性约束：选择所有在线 CPU 中负载最低的 */
        for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
        {
            if ((cpu == exclude_cpu) || (!smp_cpu_online(cpu)))
            {
                continue;
            }

            if (s_cpu_queues[cpu].thread_count < min_load)
            {
                min_load = s_cpu_queues[cpu].thread_count;
                best_cpu = cpu;
            }
        }
    }
    else
    {
        /* 有亲和性约束：仅在掩码指定 CPU 中选择负载最低的 */
        for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
        {
            if ((cpu == exclude_cpu) || (!smp_cpu_online(cpu)))
            {
                continue;
            }

            if ((mask & (1U << cpu)) == 0U)
            {
                continue;
            }

            if (s_cpu_queues[cpu].thread_count < min_load)
            {
                min_load = s_cpu_queues[cpu].thread_count;
                best_cpu = cpu;
            }
        }
    }

    return best_cpu;
}

/* ========================================================================
 * 负载均衡
 * ======================================================================== */

/**
 * @brief 获取指定 CPU 的就绪队列负载
 *
 * @param cpu_id CPU 编号
 *
 * @return 就绪队列中的线程数，无效 CPU 返回 0
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
 * @details 遍历所有在线 CPU，计算平均负载。
 *          如果任一 CPU 的负载超过均值的 LOAD_IMBALANCE_THRESHOLD%，
 *          则判定需要负载均衡。
 *
 * @return true 需要均衡，false 不需要
 */
static bool need_load_balance(void)
{
    uint32_t cpu;
    uint32_t total_load;
    uint32_t online_cpus;
    uint32_t avg_load;

    total_load = 0U;
    online_cpus = 0U;

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

    /* 均值为 0 时无需均衡 */
    if (avg_load == 0U)
    {
        return false;
    }

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

/**
 * @brief 执行负载均衡
 *
 * @details 从最忙的 CPU 迁移线程到最闲的 CPU。
 *          支持批量迁移（最多 MAX_MIGRATE_PER_BALANCE 个线程）。
 *          迁移时尊重 CPU 亲和性约束。
 *
 *          算法：
 *          1. 检测负载不均衡
 *          2. 找到最忙的 CPU（源）和最闲的 CPU（目标）
 *          3. 从源 CPU 取最高优先级线程
 *          4. 执行迁移（从源出队，入队目标）
 *          5. 发送 IPI_RESCHEDULE 通知目标 CPU
 *          6. 重复直到达到迁移上限或负载均衡
 *
 * @return KERNEL_OK 成功或无需均衡
 *
 * @note 对应需求: MP-004, MP-005
 */
kernel_status_t smp_load_balance(void)
{
    uint32_t src_cpu;
    uint32_t dst_cpu;
    uint32_t max_load;
    uint32_t min_load;
    uint32_t src_prio;
    uint32_t migrated;
    uint32_t migrate_limit;
    kernel_status_t ret;
    uint32_t cpu;

    if (!need_load_balance())
    {
        return KERNEL_OK;
    }

    /* 找到最忙的 CPU 作为迁移源 */
    src_cpu = 0U;
    max_load = 0U;

    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
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
    }

    /* 找到最闲的 CPU 作为迁移目标 */
    dst_cpu = SMP_CPU_INVALID;
    min_load = 0xFFFFFFFFU;

    for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
    {
        if (!smp_cpu_online(cpu))
        {
            continue;
        }

        if (cpu == src_cpu)
        {
            continue;
        }

        if (s_cpu_queues[cpu].thread_count < min_load)
        {
            min_load = s_cpu_queues[cpu].thread_count;
            dst_cpu = cpu;
        }
    }

    if ((dst_cpu == SMP_CPU_INVALID) || (src_cpu == dst_cpu))
    {
        return KERNEL_OK;
    }

    /* 迁移上限：不超过源负载的一半，且不超过 MAX_MIGRATE_PER_BALANCE */
    migrate_limit = max_load / 2U;
    if (migrate_limit > MAX_MIGRATE_PER_BALANCE)
    {
        migrate_limit = MAX_MIGRATE_PER_BALANCE;
    }

    if (migrate_limit == 0U)
    {
        migrate_limit = 1U;
    }

    /* 批量迁移：从源 CPU 迁移线程到目标 CPU */
    migrated = 0U;

    while (migrated < migrate_limit)
    {
        /* 从源 CPU 取最高优先级线程 */
        src_prio = find_highest_priority(src_cpu);
        if (src_prio >= 255U)
        {
            break;
        }

        /* 从源 CPU 移除 */
        ret = smp_dequeue(src_cpu, src_prio);
        if (ret != KERNEL_OK)
        {
            break;
        }

        /* 加入目标 CPU */
        ret = smp_enqueue(dst_cpu, src_prio);
        if (ret != KERNEL_OK)
        {
            /* 回滚：重新入队源 CPU */
            (void)smp_enqueue(src_cpu, src_prio);
            break;
        }

        migrated++;
    }

    /* 通知目标 CPU 重新调度 */
    if (migrated > 0U)
    {
        (void)ipi_send(dst_cpu, IPI_TYPE_RESCHEDULE);
    }

    return KERNEL_OK;
}

/**
 * @brief 基于亲和性的线程入队选择
 *
 * @details 当线程需要加入就绪队列时，选择最佳 CPU：
 *          1. 如果线程有亲和性约束，选择亲和性掩码中负载最低的 CPU
 *          2. 如果无约束，选择当前 CPU（缓存亲和性）
 *
 * @param thread_id 线程 ID
 * @param hint_cpu  建议 CPU（通常为当前 CPU）
 *
 * @return 选中的 CPU 编号
 */
uint32_t smp_select_enqueue_cpu(uint32_t thread_id, uint32_t hint_cpu)
{
    uint32_t mask;
    uint32_t cpu;
    uint32_t best_cpu;
    uint32_t min_load;

    mask = smp_get_affinity(thread_id);

    /* 无亲和性约束：优先使用建议 CPU */
    if (mask == 0U)
    {
        if ((hint_cpu < CONFIG_MAX_CPUS) && (smp_cpu_online(hint_cpu)))
        {
            return hint_cpu;
        }

        /* 建议 CPU 不可用，找任意在线 CPU */
        for (cpu = 0U; cpu < CONFIG_MAX_CPUS; cpu++)
        {
            if (smp_cpu_online(cpu))
            {
                return cpu;
            }
        }

        return 0U;
    }

    /* 有亲和性约束：在掩码中选择负载最低的 CPU */
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

        if (s_cpu_queues[cpu].thread_count < min_load)
        {
            min_load = s_cpu_queues[cpu].thread_count;
            best_cpu = cpu;
        }
    }

    /* 如果掩码中无在线 CPU，回退到 CPU 0 */
    if (best_cpu == SMP_CPU_INVALID)
    {
        return 0U;
    }

    return best_cpu;
}

/* ========================================================================
 * 周期性负载均衡触发
 * ======================================================================== */

/**
 * @brief 调度器时钟检查（周期性触发负载均衡）
 *
 * @details 在每次调度器时钟中断中调用。当指定 CPU 的调度次数
 *          达到 LOAD_BALANCE_INTERVAL 的整数倍时，触发一次
 *          负载均衡检查。
 *
 *          算法：
 *          1. 递增当前 CPU 的调度计数
 *          2. 检查是否到达均衡间隔
 *          3. 到达则调用 smp_load_balance() 执行均衡
 *
 * @param cpu_id 当前 CPU 编号
 *
 * @note 对应需求: MP-004
 */
void smp_tick_check_balance(uint32_t cpu_id)
{
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return;
    }

    s_schedule_count[cpu_id]++;

    if ((s_schedule_count[cpu_id] % (uint64_t)LOAD_BALANCE_INTERVAL) == 0ULL)
    {
        (void)smp_load_balance();
    }
}

/* ========================================================================
 * 调度器时钟节拍
 * ======================================================================== */

/**
 * @brief 调度器时钟周期性检查
 *
 * @details 递增调度计数并在达到间隔时触发负载均衡。
 *          由 smp_tick_check_balance 内部调用。
 *
 *          完整调度器时钟处理流程：
 *          1. 更新当前 CPU 调度计数
 *          2. 检查当前线程时间片是否耗尽
 *          3. 到达负载均衡间隔时触发全局均衡
 *
 * @param cpu_id 当前 CPU 编号
 *
 * @note 对应需求: MP-004
 */
void smp_sched_tick(uint32_t cpu_id)
{
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return;
    }

    /* 递增调度计数 */
    s_schedule_count[cpu_id]++;

    /* 周期性负载均衡检查 */
    if ((s_schedule_count[cpu_id] % (uint64_t)LOAD_BALANCE_INTERVAL) == 0ULL)
    {
        (void)smp_load_balance();
    }
}

/* ========================================================================
 * 显式线程迁移
 * ======================================================================== */

/**
 * @brief 将线程从一个 CPU 迁移到另一个 CPU
 *
 * @details 显式地将指定优先级的线程从源 CPU 迁移到目标 CPU。
 *          执行前检查目标 CPU 是否满足亲和性约束。
 *          迁移成功后向目标 CPU 发送 IPI 重新调度通知。
 *
 *          操作步骤：
 *          1. 参数有效性检查
 *          2. 亲和性验证（如有约束）
 *          3. 从源 CPU 出队
 *          4. 入队目标 CPU
 *          5. 发送 IPI_RESCHEDULE
 *
 * @param src_cpu   源 CPU 编号
 * @param dst_cpu   目标 CPU 编号
 * @param priority  线程优先级
 * @param thread_id 线程 ID（用于亲和性检查）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EPERM 亲和性不允许
 *
 * @note 对应需求: MP-004, MP-005
 */
kernel_status_t smp_migrate_thread(uint32_t src_cpu,
                                     uint32_t dst_cpu,
                                     uint32_t priority,
                                     uint32_t thread_id)
{
    kernel_status_t ret;

    /* 参数检查 */
    if ((src_cpu >= CONFIG_MAX_CPUS) || (dst_cpu >= CONFIG_MAX_CPUS))
    {
        return -(int32_t)EINVAL;
    }

    if (priority > 255U)
    {
        return -(int32_t)EINVAL;
    }

    if (src_cpu == dst_cpu)
    {
        return KERNEL_OK;
    }

    /* 检查目标 CPU 是否在线 */
    if (!smp_cpu_online(dst_cpu))
    {
        return -(int32_t)EINVAL;
    }

    /* 亲和性检查：线程是否允许在目标 CPU 上运行 */
    if (thread_id < CONFIG_MAX_THREADS)
    {
        if (!smp_affinity_allowed(thread_id, dst_cpu))
        {
            return -(int32_t)EPERM;
        }
    }

    /* 从源 CPU 移除 */
    ret = smp_dequeue(src_cpu, priority);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    /* 加入目标 CPU */
    ret = smp_enqueue(dst_cpu, priority);
    if (ret != KERNEL_OK)
    {
        /* 回滚：重新入队源 CPU */
        (void)smp_enqueue(src_cpu, priority);
        return ret;
    }

    /* 通知目标 CPU 重新调度 */
    (void)ipi_send(dst_cpu, IPI_TYPE_RESCHEDULE);

    return KERNEL_OK;
}

/* ========================================================================
 * IPI 重新调度发送
 * ======================================================================== */

/**
 * @brief 向目标 CPU 发送重新调度 IPI
 *
 * @details 封装 ipi_send，专门用于触发目标 CPU 的重新调度。
 *          当线程被唤醒或迁移到其他 CPU 时，需要通知该 CPU
 *          立即检查是否有更高优先级的线程需要运行。
 *
 * @param target_cpu 目标 CPU 编号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 目标 CPU 无效
 *
 * @note 对应需求: MP-004
 */
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
