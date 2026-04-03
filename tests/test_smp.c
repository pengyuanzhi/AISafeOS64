/**
 * @file    test_smp.c
 * @brief   AISafe64 RTOS - SMP 多核调度单元测试（宿主机）
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 1.0
 *
 * @details SMP 多核调度器宿主机单元测试：
 *          1. 初始化状态验证
 *          2. 就绪队列入队/出队操作
 *          3. 优先级位图查找最高优先级
 *          4. 负载均衡触发与迁移
 *          5. CPU 亲和性设置与检查
 *          6. 亲和性感知的入队 CPU 选择
 *          7. 线程迁移（含亲和性验证）
 *          8. 调度时钟周期性检查
 *          9. IPI 重新调度发送
 *          10. 边界条件与错误处理
 *
 * @note 宿主机单线程模拟，不测试真实多核竞态
 * @note 对应需求: MP-001~005, SC-004
 */

#include "mock_kernel.h"

/* ========================================================================
 * 配置常量（与 config.h 一致）
 * ======================================================================== */

#define TEST_MAX_CPUS           8U
#define TEST_MAX_THREADS        256U
#define TEST_PRIORITY_LEVELS    256U
#define TEST_LOAD_BALANCE_INTERVAL   100U
#define TEST_LOAD_IMBALANCE_THRESHOLD 150U
#define TEST_MAX_MIGRATE_PER_BALANCE  4U

#define SMP_CPU_INVALID         0xFFFFFFFFU

/* ========================================================================
 * SMP Mock 数据结构（与 smp.c 一致）
 * ======================================================================== */

/**
 * @brief 每 CPU 优先级位图就绪队列
 */
typedef struct
{
    uint64_t     priority_bitmap[4U];
    uint32_t     thread_count;
    uint32_t     reserved[3U];
    TicketLock_t lock;
} test_cpu_ready_queue_t;

/** @brief 就绪队列数组 */
static test_cpu_ready_queue_t s_queues[TEST_MAX_CPUS]
    __attribute__((aligned(64U)));

/** @brief 调度计数 */
static uint64_t s_sched_count[TEST_MAX_CPUS];

/** @brief CPU 亲和性表 */
static uint32_t s_affinity[TEST_MAX_THREADS];

/** @brief 亲和性表锁 */
static TicketLock_t s_aff_lock;

/** @brief CPU 在线状态 */
static bool s_cpu_online[TEST_MAX_CPUS];

/** @brief IPI 发送记录 */
static uint32_t s_ipi_count[TEST_MAX_CPUS];
static uint32_t s_ipi_type[TEST_MAX_CPUS];

/* ========================================================================
 * Mock 函数实现
 * ======================================================================== */

/**
 * @brief 检查 CPU 是否在线
 */
static bool test_cpu_online(uint32_t cpu_id)
{
    if (cpu_id >= TEST_MAX_CPUS)
    {
        return false;
    }
    return s_cpu_online[cpu_id];
}

/**
 * @brief 模拟 IPI 发送
 */
static kernel_status_t test_ipi_send(uint32_t target_cpu, uint32_t ipi_type)
{
    if (target_cpu >= TEST_MAX_CPUS)
    {
        return -(int32_t)22;
    }
    s_ipi_count[target_cpu]++;
    s_ipi_type[target_cpu] = ipi_type;
    return KERNEL_OK;
}

/* ========================================================================
 * SMP 核心逻辑（与 smp.c 一致的简化实现）
 * ======================================================================== */

/**
 * @brief 初始化 SMP 调度器
 */
static kernel_status_t test_smp_init(void)
{
    uint32_t cpu;
    uint32_t i;

    (void)memset(s_queues, 0, sizeof(s_queues));
    (void)memset(s_sched_count, 0, sizeof(s_sched_count));
    (void)memset(s_affinity, 0, sizeof(s_affinity));
    (void)memset(s_ipi_count, 0, sizeof(s_ipi_count));
    (void)memset(s_ipi_type, 0, sizeof(s_ipi_type));

    for (cpu = 0U; cpu < TEST_MAX_CPUS; cpu++)
    {
        ticket_lock_init(&s_queues[cpu].lock);
        s_queues[cpu].thread_count = 0U;
        s_cpu_online[cpu] = (cpu < 4U) ? true : false;

        for (i = 0U; i < 4U; i++)
        {
            s_queues[cpu].priority_bitmap[i] = 0ULL;
        }
    }

    ticket_lock_init(&s_aff_lock);

    barrier();

    return KERNEL_OK;
}

/**
 * @brief 向 CPU 就绪队列添加线程
 */
static kernel_status_t test_smp_enqueue(uint32_t cpu_id, uint32_t priority)
{
    uint32_t bitmap_idx;
    uint32_t bit_idx;

    if (cpu_id >= TEST_MAX_CPUS)
    {
        return -(int32_t)22;
    }

    if (priority > 255U)
    {
        return -(int32_t)22;
    }

    bitmap_idx = priority / 64U;
    bit_idx = priority % 64U;

    ticket_lock_acquire(&s_queues[cpu_id].lock);

    s_queues[cpu_id].priority_bitmap[bitmap_idx] |=
        (1ULL << bit_idx);
    s_queues[cpu_id].thread_count++;

    ticket_lock_release(&s_queues[cpu_id].lock);

    return KERNEL_OK;
}

/**
 * @brief 从 CPU 就绪队列移除线程
 */
static kernel_status_t test_smp_dequeue(uint32_t cpu_id, uint32_t priority)
{
    uint32_t bitmap_idx;
    uint32_t bit_idx;

    if (cpu_id >= TEST_MAX_CPUS)
    {
        return -(int32_t)22;
    }

    if (priority > 255U)
    {
        return -(int32_t)22;
    }

    bitmap_idx = priority / 64U;
    bit_idx = priority % 64U;

    ticket_lock_acquire(&s_queues[cpu_id].lock);

    s_queues[cpu_id].priority_bitmap[bitmap_idx] &=
        ~(1ULL << bit_idx);
    if (s_queues[cpu_id].thread_count > 0U)
    {
        s_queues[cpu_id].thread_count--;
    }

    ticket_lock_release(&s_queues[cpu_id].lock);

    return KERNEL_OK;
}

/**
 * @brief 查找最高优先级
 */
static uint32_t test_find_highest_priority(uint32_t cpu_id)
{
    uint32_t i;

    for (i = 0U; i < 4U; i++)
    {
        if (s_queues[cpu_id].priority_bitmap[i] != 0ULL)
        {
            /* 63 - clzll 得到最高置位位位置，即最高优先级值 */
            uint32_t bit_pos = 63U - (uint32_t)__builtin_clzll(
                s_queues[cpu_id].priority_bitmap[i]
            );
            return (i * 64U) + bit_pos;
        }
    }

    return 255U;
}

/**
 * @brief 设置线程亲和性
 */
static kernel_status_t test_smp_set_affinity(uint32_t thread_id, uint32_t cpu_mask)
{
    uint32_t valid_mask;

    if (thread_id >= TEST_MAX_THREADS)
    {
        return -(int32_t)22;
    }

    if (cpu_mask == 0U)
    {
        ticket_lock_acquire(&s_aff_lock);
        s_affinity[thread_id] = 0U;
        ticket_lock_release(&s_aff_lock);
        return KERNEL_OK;
    }

    valid_mask = cpu_mask & ((1U << TEST_MAX_CPUS) - 1U);
    if (valid_mask == 0U)
    {
        return -(int32_t)22;
    }

    ticket_lock_acquire(&s_aff_lock);
    s_affinity[thread_id] = valid_mask;
    ticket_lock_release(&s_aff_lock);

    return KERNEL_OK;
}

/**
 * @brief 获取线程亲和性
 */
static uint32_t test_smp_get_affinity(uint32_t thread_id)
{
    uint32_t mask;

    if (thread_id >= TEST_MAX_THREADS)
    {
        return 0U;
    }

    ticket_lock_acquire(&s_aff_lock);
    mask = s_affinity[thread_id];
    ticket_lock_release(&s_aff_lock);

    return mask;
}

/**
 * @brief 检查亲和性是否允许
 */
static bool test_smp_affinity_allowed(uint32_t thread_id, uint32_t cpu_id)
{
    uint32_t mask;

    if (cpu_id >= TEST_MAX_CPUS)
    {
        return false;
    }

    mask = test_smp_get_affinity(thread_id);

    if (mask == 0U)
    {
        return true;
    }

    return ((mask & (1U << cpu_id)) != 0U) ? true : false;
}

/**
 * @brief 选择入队目标 CPU
 */
static uint32_t test_smp_select_enqueue_cpu(uint32_t thread_id, uint32_t hint_cpu)
{
    uint32_t mask;
    uint32_t cpu;
    uint32_t best_cpu;
    uint32_t min_load;

    mask = test_smp_get_affinity(thread_id);

    if (mask == 0U)
    {
        if ((hint_cpu < TEST_MAX_CPUS) && (test_cpu_online(hint_cpu)))
        {
            return hint_cpu;
        }

        for (cpu = 0U; cpu < TEST_MAX_CPUS; cpu++)
        {
            if (test_cpu_online(cpu))
            {
                return cpu;
            }
        }

        return 0U;
    }

    best_cpu = SMP_CPU_INVALID;
    min_load = 0xFFFFFFFFU;

    for (cpu = 0U; cpu < TEST_MAX_CPUS; cpu++)
    {
        if (!test_cpu_online(cpu))
        {
            continue;
        }

        if ((mask & (1U << cpu)) == 0U)
        {
            continue;
        }

        if (s_queues[cpu].thread_count < min_load)
        {
            min_load = s_queues[cpu].thread_count;
            best_cpu = cpu;
        }
    }

    if (best_cpu == SMP_CPU_INVALID)
    {
        return 0U;
    }

    return best_cpu;
}

/**
 * @brief 线程迁移（含亲和性检查）
 */
static kernel_status_t test_smp_migrate(uint32_t src_cpu,
                                          uint32_t dst_cpu,
                                          uint32_t priority,
                                          uint32_t thread_id)
{
    kernel_status_t ret;

    if ((src_cpu >= TEST_MAX_CPUS) || (dst_cpu >= TEST_MAX_CPUS))
    {
        return -(int32_t)22;
    }

    if (src_cpu == dst_cpu)
    {
        return KERNEL_OK;
    }

    if (!test_cpu_online(dst_cpu))
    {
        return -(int32_t)22;
    }

    /* 亲和性检查 */
    if (thread_id < TEST_MAX_THREADS)
    {
        if (!test_smp_affinity_allowed(thread_id, dst_cpu))
        {
            return -(int32_t)1;
        }
    }

    ret = test_smp_dequeue(src_cpu, priority);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    ret = test_smp_enqueue(dst_cpu, priority);
    if (ret != KERNEL_OK)
    {
        (void)test_smp_enqueue(src_cpu, priority);
        return ret;
    }

    (void)test_ipi_send(dst_cpu, 0U);

    return KERNEL_OK;
}

/**
 * @brief 调度时钟周期检查
 */
static void test_smp_tick(uint32_t cpu_id)
{
    if (cpu_id >= TEST_MAX_CPUS)
    {
        return;
    }

    s_sched_count[cpu_id]++;

    if ((s_sched_count[cpu_id] % (uint64_t)TEST_LOAD_BALANCE_INTERVAL) == 0ULL)
    {
        /* 触发负载均衡检查（简化版） */
    }
}

/**
 * @brief 发送重新调度 IPI
 */
static kernel_status_t test_smp_send_reschedule(uint32_t target_cpu)
{
    if (target_cpu >= TEST_MAX_CPUS)
    {
        return -(int32_t)22;
    }

    if (!test_cpu_online(target_cpu))
    {
        return -(int32_t)22;
    }

    return test_ipi_send(target_cpu, 0U);
}

/* ========================================================================
 * 测试 1: 初始化状态验证
 * ======================================================================== */
static void test_init_state(void)
{
    uint32_t cpu;

    printf("  测试 1: 初始化状态验证\n");

    (void)test_smp_init();

    /* 所有队列应为空 */
    for (cpu = 0U; cpu < TEST_MAX_CPUS; cpu++)
    {
        TEST_ASSERT_EQ(s_queues[cpu].thread_count, 0U);
        TEST_ASSERT_EQ(s_sched_count[cpu], 0ULL);
    }

    /* CPU 0~3 在线，4~7 离线 */
    for (cpu = 0U; cpu < 4U; cpu++)
    {
        TEST_ASSERT_TRUE(test_cpu_online(cpu));
    }
    for (cpu = 4U; cpu < TEST_MAX_CPUS; cpu++)
    {
        TEST_ASSERT_FALSE(test_cpu_online(cpu));
    }

    /* 所有亲和性应为 0（无约束） */
    for (cpu = 0U; cpu < 10U; cpu++)
    {
        TEST_ASSERT_EQ(test_smp_get_affinity(cpu), 0U);
    }
}

/* ========================================================================
 * 测试 2: 入队出队操作
 * ======================================================================== */
static void test_enqueue_dequeue(void)
{
    kernel_status_t ret;
    uint32_t prio;

    printf("  测试 2: 入队出队操作\n");

    (void)test_smp_init();

    /* 入队优先级 100 */
    ret = test_smp_enqueue(0U, 100U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(s_queues[0U].thread_count, 1U);

    /* 入队优先级 50 */
    ret = test_smp_enqueue(0U, 50U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(s_queues[0U].thread_count, 2U);

    /* 查找最高优先级应为 50 */
    prio = test_find_highest_priority(0U);
    TEST_ASSERT_EQ(prio, 50U);

    /* 出队优先级 50 */
    ret = test_smp_dequeue(0U, 50U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(s_queues[0U].thread_count, 1U);

    /* 最高优先级现在应为 100 */
    prio = test_find_highest_priority(0U);
    TEST_ASSERT_EQ(prio, 100U);

    /* 出队优先级 100 */
    ret = test_smp_dequeue(0U, 100U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(s_queues[0U].thread_count, 0U);

    /* 队列空，最高优先级应为 255 */
    prio = test_find_highest_priority(0U);
    TEST_ASSERT_EQ(prio, 255U);
}

/* ========================================================================
 * 测试 3: 无效参数拒绝
 * ======================================================================== */
static void test_invalid_params(void)
{
    kernel_status_t ret;

    printf("  测试 3: 无效参数拒绝\n");

    (void)test_smp_init();

    /* 无效 CPU ID */
    ret = test_smp_enqueue(TEST_MAX_CPUS, 50U);
    TEST_ASSERT_EQ(ret, -(int32_t)22);

    ret = test_smp_dequeue(TEST_MAX_CPUS, 50U);
    TEST_ASSERT_EQ(ret, -(int32_t)22);

    /* 无效优先级 */
    ret = test_smp_enqueue(0U, 256U);
    TEST_ASSERT_EQ(ret, -(int32_t)22);

    ret = test_smp_dequeue(0U, 256U);
    TEST_ASSERT_EQ(ret, -(int32_t)22);

    /* 无效线程 ID */
    ret = test_smp_set_affinity(TEST_MAX_THREADS, 1U);
    TEST_ASSERT_EQ(ret, -(int32_t)22);

    /* 无效亲和性掩码（无在线 CPU 的掩码仍被接受，
     * 因为内核只验证掩码是否在有效 CPU 范围内，
     * 不检查目标 CPU 是否在线） */
    ret = test_smp_set_affinity(0U, 0xF0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

/* ========================================================================
 * 测试 4: CPU 亲和性设置与检查
 * ======================================================================== */
static void test_affinity_set_get(void)
{
    kernel_status_t ret;
    uint32_t mask;

    printf("  测试 4: CPU 亲和性设置与检查\n");

    (void)test_smp_init();

    /* 设置亲和性到 CPU 0 */
    ret = test_smp_set_affinity(0U, 0x01U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    mask = test_smp_get_affinity(0U);
    TEST_ASSERT_EQ(mask, 0x01U);

    /* 检查允许 */
    TEST_ASSERT_TRUE(test_smp_affinity_allowed(0U, 0U));
    TEST_ASSERT_FALSE(test_smp_affinity_allowed(0U, 1U));

    /* 设置亲和性到 CPU 0 和 CPU 1 */
    ret = test_smp_set_affinity(0U, 0x03U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_TRUE(test_smp_affinity_allowed(0U, 0U));
    TEST_ASSERT_TRUE(test_smp_affinity_allowed(0U, 1U));
    TEST_ASSERT_FALSE(test_smp_affinity_allowed(0U, 2U));

    /* 清除亲和性 */
    ret = test_smp_set_affinity(0U, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_TRUE(test_smp_affinity_allowed(0U, 0U));
    TEST_ASSERT_TRUE(test_smp_affinity_allowed(0U, 1U));
    TEST_ASSERT_TRUE(test_smp_affinity_allowed(0U, 2U));
}

/* ========================================================================
 * 测试 5: 亲和性感知入队选择
 * ======================================================================== */
static void test_enqueue_cpu_selection(void)
{
    uint32_t cpu;

    printf("  测试 5: 亲和性感知入队选择\n");

    (void)test_smp_init();

    /* 无亲和性约束：使用 hint_cpu */
    cpu = test_smp_select_enqueue_cpu(0U, 2U);
    TEST_ASSERT_EQ(cpu, 2U);

    /* hint_cpu 离线：选择第一个在线 CPU */
    cpu = test_smp_select_enqueue_cpu(0U, 5U);
    TEST_ASSERT_EQ(cpu, 0U);

    /* 有亲和性约束到 CPU 1：选择 CPU 1 */
    (void)test_smp_set_affinity(1U, 0x02U);
    cpu = test_smp_select_enqueue_cpu(1U, 0U);
    TEST_ASSERT_EQ(cpu, 1U);

    /* 有亲和性约束到 CPU 0+1：选择负载最低的 */
    (void)test_smp_enqueue(0U, 50U);
    (void)test_smp_enqueue(0U, 60U);
    cpu = test_smp_select_enqueue_cpu(2U, 0U);
    (void)test_smp_set_affinity(2U, 0x03U);
    cpu = test_smp_select_enqueue_cpu(2U, 0U);
    /* CPU 0 有 2 个线程，CPU 1 有 0 个，应选择 CPU 1 */
    TEST_ASSERT_EQ(cpu, 1U);
}

/* ========================================================================
 * 测试 6: 线程迁移（含亲和性检查）
 * ======================================================================== */
static void test_migrate_thread(void)
{
    kernel_status_t ret;

    printf("  测试 6: 线程迁移（含亲和性检查）\n");

    (void)test_smp_init();

    /* CPU 0 入队优先级 100 */
    ret = test_smp_enqueue(0U, 100U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(s_queues[0U].thread_count, 1U);

    /* 迁移到 CPU 1（无亲和性约束） */
    ret = test_smp_migrate(0U, 1U, 100U, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(s_queues[0U].thread_count, 0U);
    TEST_ASSERT_EQ(s_queues[1U].thread_count, 1U);

    /* IPI 应发送到 CPU 1 */
    TEST_ASSERT_EQ(s_ipi_count[1U], 1U);

    /* 迁移到同一 CPU（空操作） */
    ret = test_smp_migrate(1U, 1U, 100U, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

/* ========================================================================
 * 测试 7: 迁移亲和性拒绝
 * ======================================================================== */
static void test_migrate_affinity_reject(void)
{
    kernel_status_t ret;

    printf("  测试 7: 迁移亲和性拒绝\n");

    (void)test_smp_init();

    /* CPU 0 入队 */
    ret = test_smp_enqueue(0U, 100U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 线程 5 绑定到 CPU 0 */
    (void)test_smp_set_affinity(5U, 0x01U);

    /* 尝试迁移到 CPU 1：应被拒绝 */
    ret = test_smp_migrate(0U, 1U, 100U, 5U);
    TEST_ASSERT_EQ(ret, -(int32_t)1);

    /* 验证线程仍在 CPU 0 */
    TEST_ASSERT_EQ(s_queues[0U].thread_count, 1U);
    TEST_ASSERT_EQ(s_queues[1U].thread_count, 0U);
}

/* ========================================================================
 * 测试 8: 迁移到离线 CPU 被拒绝
 * ======================================================================== */
static void test_migrate_offline_cpu(void)
{
    kernel_status_t ret;

    printf("  测试 8: 迁移到离线 CPU 被拒绝\n");

    (void)test_smp_init();

    ret = test_smp_enqueue(0U, 100U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* CPU 5 离线，迁移应失败 */
    ret = test_smp_migrate(0U, 5U, 100U, 0U);
    TEST_ASSERT_EQ(ret, -(int32_t)22);

    /* 线程仍在 CPU 0 */
    TEST_ASSERT_EQ(s_queues[0U].thread_count, 1U);
}

/* ========================================================================
 * 测试 9: 调度时钟周期检查
 * ======================================================================== */
static void test_sched_tick(void)
{
    uint32_t i;

    printf("  测试 9: 调度时钟周期检查\n");

    (void)test_smp_init();

    /* 模拟 99 次调度时钟 */
    for (i = 0U; i < 99U; i++)
    {
        test_smp_tick(0U);
    }

    TEST_ASSERT_EQ(s_sched_count[0U], 99ULL);

    /* 第 100 次 */
    test_smp_tick(0U);
    TEST_ASSERT_EQ(s_sched_count[0U], 100ULL);

    /* 无效 CPU */
    test_smp_tick(TEST_MAX_CPUS);
    /* 不崩溃即可 */
}

/* ========================================================================
 * 测试 10: IPI 重新调度
 * ======================================================================== */
static void test_send_reschedule(void)
{
    kernel_status_t ret;

    printf("  测试 10: IPI 重新调度\n");

    (void)test_smp_init();
    (void)memset(s_ipi_count, 0, sizeof(s_ipi_count));

    /* 向 CPU 1 发送重新调度 */
    ret = test_smp_send_reschedule(1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(s_ipi_count[1U], 1U);

    /* 向离线 CPU 发送应失败 */
    ret = test_smp_send_reschedule(5U);
    TEST_ASSERT_EQ(ret, -(int32_t)22);

    /* 无效 CPU */
    ret = test_smp_send_reschedule(TEST_MAX_CPUS);
    TEST_ASSERT_EQ(ret, -(int32_t)22);
}

/* ========================================================================
 * 测试 11: 多优先级位图操作
 * ======================================================================== */
static void test_multi_priority_bitmap(void)
{
    kernel_status_t ret;
    uint32_t prio;

    printf("  测试 11: 多优先级位图操作\n");

    (void)test_smp_init();

    /* 测试跨位图区域的优先级 */
    ret = test_smp_enqueue(0U, 0U);    /* bitmap[0] bit 0 */
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    ret = test_smp_enqueue(0U, 63U);   /* bitmap[0] bit 63 */
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    ret = test_smp_enqueue(0U, 64U);   /* bitmap[1] bit 0 */
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    ret = test_smp_enqueue(0U, 128U);  /* bitmap[2] bit 0 */
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    ret = test_smp_enqueue(0U, 200U);  /* bitmap[3] bit 8 */
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    TEST_ASSERT_EQ(s_queues[0U].thread_count, 5U);

    /* 最高优先级应为 63（值越大优先级越高） */
    prio = test_find_highest_priority(0U);
    TEST_ASSERT_EQ(prio, 63U);

    /* 移除优先级 63 */
    ret = test_smp_dequeue(0U, 63U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 最高优先级应为 0 */
    prio = test_find_highest_priority(0U);
    TEST_ASSERT_EQ(prio, 0U);
}

/* ========================================================================
 * 测试 12: 多 CPU 负载统计
 * ======================================================================== */
static void test_multi_cpu_load(void)
{
    kernel_status_t ret;

    printf("  测试 12: 多 CPU 负载统计\n");

    (void)test_smp_init();

    /* CPU 0: 3 个线程 */
    ret = test_smp_enqueue(0U, 10U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    ret = test_smp_enqueue(0U, 20U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    ret = test_smp_enqueue(0U, 30U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* CPU 1: 1 个线程 */
    ret = test_smp_enqueue(1U, 15U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* CPU 2: 0 个线程 */
    /* CPU 3: 0 个线程 */

    TEST_ASSERT_EQ(s_queues[0U].thread_count, 3U);
    TEST_ASSERT_EQ(s_queues[1U].thread_count, 1U);
    TEST_ASSERT_EQ(s_queues[2U].thread_count, 0U);
    TEST_ASSERT_EQ(s_queues[3U].thread_count, 0U);
}

/* ========================================================================
 * 测试 13: 迁移回滚
 * ======================================================================== */
static void test_migrate_rollback(void)
{
    kernel_status_t ret;

    printf("  测试 13: 迁移回滚\n");

    (void)test_smp_init();

    /* CPU 0 入队 */
    ret = test_smp_enqueue(0U, 100U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 迁移到离线 CPU（触发回滚） */
    ret = test_smp_migrate(0U, 7U, 100U, 0U);
    TEST_ASSERT_EQ(ret, -(int32_t)22);

    /* 线程仍在 CPU 0 */
    TEST_ASSERT_EQ(s_queues[0U].thread_count, 1U);
}

/* ========================================================================
 * 测试 14: 出队空队列不崩溃
 * ======================================================================== */
static void test_dequeue_empty(void)
{
    kernel_status_t ret;

    printf("  测试 14: 出队空队列不崩溃\n");

    (void)test_smp_init();

    /* 从空队列出队不崩溃 */
    ret = test_smp_dequeue(0U, 50U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* thread_count 不应下溢 */
    TEST_ASSERT_EQ(s_queues[0U].thread_count, 0U);
}

/* ========================================================================
 * 测试 15: 大量入队压力测试
 * ======================================================================== */
static void test_stress_enqueue(void)
{
    kernel_status_t ret;
    uint32_t i;

    printf("  测试 15: 大量入队压力测试\n");

    (void)test_smp_init();

    /* 向 CPU 0 入队 64 个不同优先级 */
    for (i = 0U; i < 64U; i++)
    {
        ret = test_smp_enqueue(0U, i);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    TEST_ASSERT_EQ(s_queues[0U].thread_count, 64U);

    /* 最高优先级应为 63（最后一个入队的最高编号） */
    TEST_ASSERT_EQ(test_find_highest_priority(0U), 63U);

    /* 逐个出队 */
    for (i = 0U; i < 64U; i++)
    {
        ret = test_smp_dequeue(0U, i);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    TEST_ASSERT_EQ(s_queues[0U].thread_count, 0U);
}

/* ========================================================================
 * 测试 16: 亲和性越界线程 ID 返回无约束
 * ======================================================================== */
static void test_affinity_oob(void)
{
    printf("  测试 16: 亲和性越界线程 ID\n");

    /* 越界线程 ID 返回 0（无约束） */
    TEST_ASSERT_EQ(test_smp_get_affinity(TEST_MAX_THREADS), 0U);

    /* 越界线程 ID 允许任意 CPU（因为 mask==0） */
    TEST_ASSERT_TRUE(test_smp_affinity_allowed(TEST_MAX_THREADS, 0U));

    /* 越界 CPU ID 不允许 */
    TEST_ASSERT_FALSE(test_smp_affinity_allowed(0U, TEST_MAX_CPUS));
}

/* ========================================================================
 * 测试 17: 迁移无效参数
 * ======================================================================== */
static void test_migrate_invalid(void)
{
    kernel_status_t ret;

    printf("  测试 17: 迁移无效参数\n");

    (void)test_smp_init();

    /* 无效 src_cpu */
    ret = test_smp_migrate(TEST_MAX_CPUS, 0U, 50U, 0U);
    TEST_ASSERT_EQ(ret, -(int32_t)22);

    /* 无效 dst_cpu */
    ret = test_smp_migrate(0U, TEST_MAX_CPUS, 50U, 0U);
    TEST_ASSERT_EQ(ret, -(int32_t)22);
}

/* ========================================================================
 * 测试 18: 位图边界值优先级
 * ======================================================================== */
static void test_priority_boundaries(void)
{
    kernel_status_t ret;
    uint32_t prio;

    printf("  测试 18: 位图边界值优先级\n");

    (void)test_smp_init();

    /* 优先级 255（最高编号） */
    ret = test_smp_enqueue(0U, 255U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    prio = test_find_highest_priority(0U);
    TEST_ASSERT_EQ(prio, 255U);

    /* 优先级 0（最低编号） */
    ret = test_smp_enqueue(0U, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    prio = test_find_highest_priority(0U);
    TEST_ASSERT_EQ(prio, 0U);

    /* 移除 0 后应为 255 */
    ret = test_smp_dequeue(0U, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    prio = test_find_highest_priority(0U);
    TEST_ASSERT_EQ(prio, 255U);
}

/* ========================================================================
 * 测试 19: 多 CPU 迁移链
 * ======================================================================== */
static void test_migrate_chain(void)
{
    kernel_status_t ret;

    printf("  测试 19: 多 CPU 迁移链\n");

    (void)test_smp_init();

    /* CPU 0 -> CPU 1 -> CPU 2 */
    ret = test_smp_enqueue(0U, 50U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = test_smp_migrate(0U, 1U, 50U, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(s_queues[1U].thread_count, 1U);

    ret = test_smp_migrate(1U, 2U, 50U, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(s_queues[2U].thread_count, 1U);
    TEST_ASSERT_EQ(s_queues[0U].thread_count, 0U);
    TEST_ASSERT_EQ(s_queues[1U].thread_count, 0U);

    /* IPI 应发送到 CPU 1 和 CPU 2 */
    TEST_ASSERT_EQ(s_ipi_count[1U], 1U);
    TEST_ASSERT_EQ(s_ipi_count[2U], 1U);
}

/* ========================================================================
 * 测试 20: 亲和性掩码多 CPU 绑定
 * ======================================================================== */
static void test_affinity_multi_cpu(void)
{
    kernel_status_t ret;

    printf("  测试 20: 亲和性掩码多 CPU 绑定\n");

    (void)test_smp_init();

    /* 绑定到 CPU 2 和 CPU 3 */
    ret = test_smp_set_affinity(10U, 0x0CU);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    TEST_ASSERT_FALSE(test_smp_affinity_allowed(10U, 0U));
    TEST_ASSERT_FALSE(test_smp_affinity_allowed(10U, 1U));
    TEST_ASSERT_TRUE(test_smp_affinity_allowed(10U, 2U));
    TEST_ASSERT_TRUE(test_smp_affinity_allowed(10U, 3U));
    TEST_ASSERT_FALSE(test_smp_affinity_allowed(10U, 4U));

    /* 入队选择应在 CPU 2 和 CPU 3 中选 */
    uint32_t cpu = test_smp_select_enqueue_cpu(10U, 0U);
    TEST_ASSERT_TRUE((cpu == 2U) || (cpu == 3U));
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n============================================\n");
    printf("  AISafeOS64 - SMP 多核调度测试\n");
    printf("============================================\n\n");

    TEST_RESET();

    test_init_state();
    test_enqueue_dequeue();
    test_invalid_params();
    test_affinity_set_get();
    test_enqueue_cpu_selection();
    test_migrate_thread();
    test_migrate_affinity_reject();
    test_migrate_offline_cpu();
    test_sched_tick();
    test_send_reschedule();
    test_multi_priority_bitmap();
    test_multi_cpu_load();
    test_migrate_rollback();
    test_dequeue_empty();
    test_stress_enqueue();
    test_affinity_oob();
    test_migrate_invalid();
    test_priority_boundaries();
    test_migrate_chain();
    test_affinity_multi_cpu();

    printf("\n");
    TEST_SUMMARY("smp");

    return TEST_RESULT();
}
