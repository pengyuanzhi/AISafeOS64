/**
 * @file    test_scheduler.c
 * @brief   AISafe64 RTOS - 256级优先级位图调度器单元测试（宿主机）
 * @author  AISafe64 Team
 * @date    2026-04-02
 * @version 1.0
 *
 * @details 256级优先级位图调度器宿主机自包含测试
 *          测试与内核 scheduler.c 一致的逻辑：
 *          - 初始化（位图清零、链表初始化、线程表初始化）
 *          - 入队（位图置位 + 链表尾部插入）
 *          - 出队（链表移除 + 位图清除）
 *          - O(1) 最高优先级选择（bitmap256_find_highest）
 *          - 时钟滴答（RR 时间片递减）
 *          - 多 CPU 队列独立性
 *          - 压力测试
 *
 * @note 对应需求: SC-001（O(1) 优先级位图调度）、SC-002（上下文切换）、TF-001
 */

#include "mock_kernel.h"

/* ========================================================================
 * 配置常量（与 kernel/config.h 一致）
 * ======================================================================== */

#define CONFIG_MAX_CPUS        8U
#define CONFIG_MAX_THREADS     256U
#define CONFIG_PRIORITY_LEVELS 256U
#define CONFIG_STACK_SIZE_DEFAULT 8192U

/* ========================================================================
 * bitmap256_t 定义与操作（与 kernel/bitmap.h 一致）
 * ======================================================================== */

#define BITMAP256_BITS          256U
#define BITMAP256_BITS_PER_WORD 64U
#define BITMAP256_WORDS         4U

typedef struct
{
    uint64_t bits[BITMAP256_WORDS];
} bitmap256_t;

static inline void bitmap256_clear_all(bitmap256_t *bm)
{
    if (bm == NULL) { return; }
    bm->bits[0U] = 0ULL;
    bm->bits[1U] = 0ULL;
    bm->bits[2U] = 0ULL;
    bm->bits[3U] = 0ULL;
}

static inline void bitmap256_set(bitmap256_t *bm, uint32_t bit)
{
    uint32_t word;
    uint32_t offset;

    if (bm == NULL)      { return; }
    if (bit >= BITMAP256_BITS) { return; }

    word   = bit / BITMAP256_BITS_PER_WORD;
    offset = bit % BITMAP256_BITS_PER_WORD;
    bm->bits[word] |= (1ULL << offset);
}

static inline void bitmap256_clear(bitmap256_t *bm, uint32_t bit)
{
    uint32_t word;
    uint32_t offset;

    if (bm == NULL)      { return; }
    if (bit >= BITMAP256_BITS) { return; }

    word   = bit / BITMAP256_BITS_PER_WORD;
    offset = bit % BITMAP256_BITS_PER_WORD;
    bm->bits[word] &= ~(1ULL << offset);
}

static inline int bitmap256_test(const bitmap256_t *bm, uint32_t bit)
{
    uint32_t word;
    uint32_t offset;

    if (bm == NULL)      { return 0; }
    if (bit >= BITMAP256_BITS) { return 0; }

    word   = bit / BITMAP256_BITS_PER_WORD;
    offset = bit % BITMAP256_BITS_PER_WORD;

    return ((bm->bits[word] & (1ULL << offset)) != 0ULL) ? 1 : 0;
}

static inline uint32_t bitmap256_find_highest(const bitmap256_t *bm)
{
    uint32_t word_idx;

    if (bm == NULL) { return BITMAP256_BITS; }

    for (word_idx = BITMAP256_WORDS; word_idx > 0U; word_idx--)
    {
        uint32_t idx = word_idx - 1U;

        if (bm->bits[idx] != 0ULL)
        {
            int leading_zeros = __builtin_clzll(bm->bits[idx]);
            return (idx * BITMAP256_BITS_PER_WORD) +
                   (uint32_t)(BITMAP256_BITS_PER_WORD - 1U) -
                   (uint32_t)leading_zeros;
        }
    }

    return BITMAP256_BITS;
}

static inline int bitmap256_empty(const bitmap256_t *bm)
{
    if (bm == NULL) { return 1; }
    return ((bm->bits[0U] | bm->bits[1U] | bm->bits[2U] | bm->bits[3U]) == 0ULL)
           ? 1 : 0;
}

/* ========================================================================
 * 侵入式双向链表（与 kernel/list.h 一致）
 * ======================================================================== */

struct list_head
{
    struct list_head *next;
    struct list_head *prev;
};

static inline void init_list_head(struct list_head *head)
{
    if (head == NULL) { return; }
    head->next = head;
    head->prev = head;
}

static inline int list_empty(const struct list_head *head)
{
    if (head == NULL) { return 1; }
    return (head->next == head) ? 1 : 0;
}

/* ========================================================================
 * KThread_t 定义（与 kernel/sched/thread.h 一致）
 * ======================================================================== */

#define KTHREAD_CONTEXT_REGS 15U
#define KTHREAD_NAME_MAX     16U

typedef enum
{
    KTHREAD_STATE_DEAD      = 0U,
    KTHREAD_STATE_READY     = 1U,
    KTHREAD_STATE_RUNNING   = 2U,
    KTHREAD_STATE_BLOCKED   = 3U,
    KTHREAD_STATE_SLEEPING  = 4U,
    KTHREAD_STATE_SUSPENDED = 5U
} KThreadState_t;

typedef enum
{
    KTHREAD_POLICY_FIFO = 0U,
    KTHREAD_POLICY_RR   = 1U
} KThreadPolicy_t;

typedef void (*kthread_entry_t)(void *arg);

typedef struct KThread
{
    uint64_t context[KTHREAD_CONTEXT_REGS];
    uint32_t tid;
    KThreadState_t state;
    kthread_entry_t entry;
    void *entry_arg;
    uint64_t stack_base;
    uint32_t stack_size;
    uint8_t prio;
    KThreadPolicy_t policy;
    uint32_t time_slice;
    uint32_t time_slice_reload;
    struct list_head rq_list;
    struct list_head sleep_node;
    uint64_t wakeup_tick;
    char name[KTHREAD_NAME_MAX];
} KThread_t;

#define THREAD_ID_INVALID ((uint32_t)0xFFFFFFFFU)

/* ========================================================================
 * PerCPUReadyQueue_t 与 Scheduler_t（与 scheduler.h 一致）
 * ======================================================================== */

typedef struct
{
    bitmap256_t bitmap;
    struct list_head queues[CONFIG_PRIORITY_LEVELS];
    uint32_t lock;
    uint32_t nr_running;
    KThread_t *current_thread;
    KThread_t *idle_thread;
} PerCPUReadyQueue_t;

typedef struct
{
    PerCPUReadyQueue_t cpu_queues[CONFIG_MAX_CPUS];
    KThread_t thread_table[CONFIG_MAX_THREADS];
    bool initialized;
} Scheduler_t;

/* ========================================================================
 * 调度器实现（宿主机自包含版本 — 与 scheduler.c 逻辑一致）
 * ======================================================================== */

static Scheduler_t g_test_sched;

/**
 * @brief 初始化调度器
 * @details 与 scheduler.c 的 scheduler_init 逻辑一致
 *          简化版：不创建 idle 线程（测试中手动设置）
 */
static kernel_status_t sched_init(void)
{
    uint32_t cpu_id;
    uint32_t i;
    uint32_t j;

    if (g_test_sched.initialized)
    {
        return KERNEL_OK;
    }

    for (cpu_id = 0U; cpu_id < CONFIG_MAX_CPUS; cpu_id++)
    {
        PerCPUReadyQueue_t *cpu_q = &g_test_sched.cpu_queues[cpu_id];

        bitmap256_clear_all(&cpu_q->bitmap);

        for (j = 0U; j < CONFIG_PRIORITY_LEVELS; j++)
        {
            cpu_q->queues[j].next = &cpu_q->queues[j];
            cpu_q->queues[j].prev = &cpu_q->queues[j];
        }

        cpu_q->lock          = 0U;
        cpu_q->nr_running    = 0U;
        cpu_q->current_thread = NULL;
        cpu_q->idle_thread   = NULL;
    }

    for (i = 0U; i < CONFIG_MAX_THREADS; i++)
    {
        KThread_t *thread = &g_test_sched.thread_table[i];
        thread->tid       = i;
        thread->state     = KTHREAD_STATE_DEAD;
        thread->entry     = NULL;
        thread->entry_arg = NULL;
        thread->stack_base   = 0U;
        thread->stack_size   = 0U;
        thread->prio      = 0U;
        thread->policy    = KTHREAD_POLICY_FIFO;
        thread->time_slice = 0U;
        thread->time_slice_reload = 0U;
        thread->rq_list.next = &thread->rq_list;
        thread->rq_list.prev = &thread->rq_list;
        thread->sleep_node.next = &thread->sleep_node;
        thread->sleep_node.prev = &thread->sleep_node;
        thread->wakeup_tick = 0ULL;
        thread->name[0U] = '\0';
    }

    g_test_sched.initialized = true;

    return KERNEL_OK;
}

/**
 * @brief 重置调度器（测试辅助）
 */
static void sched_reset(void)
{
    (void)memset(&g_test_sched, 0, sizeof(g_test_sched));
    g_test_sched.initialized = false;
}

/**
 * @brief 初始化单个线程（测试辅助）
 */
static void thread_init(KThread_t *t, uint32_t tid, uint8_t prio,
                        KThreadPolicy_t policy, uint32_t time_slice)
{
    (void)memset(t, 0, sizeof(KThread_t));
    t->tid              = tid;
    t->state            = KTHREAD_STATE_READY;
    t->prio             = prio;
    t->policy           = policy;
    t->time_slice       = time_slice;
    t->time_slice_reload = time_slice;
    init_list_head(&t->rq_list);
    init_list_head(&t->sleep_node);
}

/**
 * @brief 入队操作
 * @details 与 scheduler.c 的 scheduler_enqueue 逻辑一致
 */
static void sched_enqueue(KThread_t *thread)
{
    PerCPUReadyQueue_t *cpu_q;
    uint32_t cpu_id;

    if (thread == NULL) { return; }

    cpu_id = mock_cpu_id;
    cpu_q  = &g_test_sched.cpu_queues[cpu_id];

    bitmap256_set(&cpu_q->bitmap, (uint32_t)thread->prio);

    thread->rq_list.next = &cpu_q->queues[thread->prio];
    thread->rq_list.prev = cpu_q->queues[thread->prio].prev;
    cpu_q->queues[thread->prio].prev->next = &thread->rq_list;
    cpu_q->queues[thread->prio].prev = &thread->rq_list;

    cpu_q->nr_running++;
}

/**
 * @brief 出队操作
 * @details 与 scheduler.c 的 scheduler_dequeue 逻辑一致
 */
static void sched_dequeue(KThread_t *thread)
{
    PerCPUReadyQueue_t *cpu_q;
    uint32_t cpu_id;

    if (thread == NULL) { return; }

    cpu_id = mock_cpu_id;
    cpu_q  = &g_test_sched.cpu_queues[cpu_id];

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
 * @brief container_of 宏
 */
#undef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/**
 * @brief O(1) 选择最高优先级线程
 * @details 与 scheduler.c 的 scheduler_pick_next 逻辑一致
 */
static KThread_t *sched_pick_next(void)
{
    PerCPUReadyQueue_t *cpu_q;
    uint32_t highest_prio;
    struct list_head *first;
    KThread_t *next;
    uint32_t cpu_id;

    cpu_id = mock_cpu_id;
    cpu_q  = &g_test_sched.cpu_queues[cpu_id];

    highest_prio = bitmap256_find_highest(&cpu_q->bitmap);

    if (highest_prio < CONFIG_PRIORITY_LEVELS)
    {
        first = cpu_q->queues[highest_prio].next;
        next  = container_of(first, KThread_t, rq_list);
        return next;
    }

    if (cpu_q->idle_thread != NULL)
    {
        return cpu_q->idle_thread;
    }

    return NULL;
}

/**
 * @brief 设置当前线程
 */
static void sched_load_current(KThread_t *thread)
{
    PerCPUReadyQueue_t *cpu_q;
    uint32_t cpu_id;

    if (thread == NULL) { return; }

    cpu_id = mock_cpu_id;
    cpu_q  = &g_test_sched.cpu_queues[cpu_id];

    cpu_q->current_thread = thread;
    thread->state = KTHREAD_STATE_RUNNING;
}

/**
 * @brief 时钟滴答处理
 * @details 与 scheduler.c 的 scheduler_tick 逻辑一致
 *          宿主机简化版：不调用 context_switch，仅标记需重新调度
 */
static bool sched_tick_needs_resched;

static void sched_tick(void)
{
    KThread_t *current;
    PerCPUReadyQueue_t *cpu_q;
    uint32_t cpu_id;

    cpu_id = mock_cpu_id;
    cpu_q  = &g_test_sched.cpu_queues[cpu_id];
    current = cpu_q->current_thread;

    if (current == NULL) { return; }

    sched_tick_needs_resched = false;

    if (current->policy == KTHREAD_POLICY_RR)
    {
        if (current->time_slice > 0U)
        {
            current->time_slice--;
            if (current->time_slice == 0U)
            {
                current->time_slice = current->time_slice_reload;
                sched_tick_needs_resched = true;
            }
        }
    }
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试 1: 初始化成功，所有 CPU 队列为空
 */
static void test_init_basic(void)
{
    uint32_t cpu_id;
    uint32_t j;

    sched_reset();
    kernel_status_t ret = sched_init();
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_TRUE(g_test_sched.initialized);

    for (cpu_id = 0U; cpu_id < CONFIG_MAX_CPUS; cpu_id++)
    {
        PerCPUReadyQueue_t *cpu_q = &g_test_sched.cpu_queues[cpu_id];
        TEST_ASSERT_TRUE(bitmap256_empty(&cpu_q->bitmap) == 1);
        TEST_ASSERT_EQ(cpu_q->nr_running, 0U);
        TEST_ASSERT_NULL(cpu_q->current_thread);
        TEST_ASSERT_NULL(cpu_q->idle_thread);

        for (j = 0U; j < CONFIG_PRIORITY_LEVELS; j++)
        {
            TEST_ASSERT_TRUE(list_empty(&cpu_q->queues[j]) == 1);
        }
    }
}

/**
 * @brief 测试 2: 入队一个任务，队列非空
 */
static void test_enqueue_one(void)
{
    KThread_t thread;

    sched_reset();
    sched_init();
    mock_cpu_id = 0U;

    thread_init(&thread, 0U, 128U, KTHREAD_POLICY_FIFO, 0U);
    sched_enqueue(&thread);

    TEST_ASSERT_EQ(g_test_sched.cpu_queues[0U].nr_running, 1U);
    TEST_ASSERT_TRUE(bitmap256_test(&g_test_sched.cpu_queues[0U].bitmap, 128U) == 1);
    TEST_ASSERT_TRUE(list_empty(&g_test_sched.cpu_queues[0U].queues[128U]) == 0);
}

/**
 * @brief 测试 3: 高优先级任务排在对应优先级链表
 */
static void test_enqueue_priority(void)
{
    KThread_t t_lo;
    KThread_t t_hi;
    KThread_t *first;

    sched_reset();
    sched_init();
    mock_cpu_id = 0U;

    thread_init(&t_lo, 0U, 50U, KTHREAD_POLICY_FIFO, 0U);
    thread_init(&t_hi, 1U, 200U, KTHREAD_POLICY_FIFO, 0U);

    sched_enqueue(&t_lo);
    sched_enqueue(&t_hi);

    /* 最高优先级应为 200 */
    first = sched_pick_next();
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQ(first->tid, 1U);
    TEST_ASSERT_EQ(first->prio, 200U);
}

/**
 * @brief 测试 4: 出队返回最高优先级任务
 */
static void test_dequeue_highest(void)
{
    KThread_t t1;
    KThread_t t2;
    KThread_t t3;
    KThread_t *picked;

    sched_reset();
    sched_init();
    mock_cpu_id = 0U;

    thread_init(&t1, 0U, 10U, KTHREAD_POLICY_FIFO, 0U);
    thread_init(&t2, 1U, 100U, KTHREAD_POLICY_FIFO, 0U);
    thread_init(&t3, 2U, 50U, KTHREAD_POLICY_FIFO, 0U);

    sched_enqueue(&t1);
    sched_enqueue(&t2);
    sched_enqueue(&t3);

    /* 选择最高优先级 t2(100) */
    picked = sched_pick_next();
    TEST_ASSERT_EQ(picked->tid, 1U);

    sched_dequeue(picked);

    /* 下一个应为 t3(50) */
    picked = sched_pick_next();
    TEST_ASSERT_EQ(picked->tid, 2U);

    sched_dequeue(picked);

    /* 最后是 t1(10) */
    picked = sched_pick_next();
    TEST_ASSERT_EQ(picked->tid, 0U);
}

/**
 * @brief 测试 5: 空队列出队 pick_next 返回 idle 或 NULL
 */
static void test_dequeue_empty(void)
{
    KThread_t *picked;

    sched_reset();
    sched_init();
    mock_cpu_id = 0U;

    /* 无 idle 线程时返回 NULL */
    picked = sched_pick_next();
    TEST_ASSERT_NULL(picked);
}

/**
 * @brief 测试 6: 入队 N 个任务，按优先级出队
 */
static void test_enqueue_dequeue_all(void)
{
    KThread_t threads[8];
    uint32_t expected_order[] = { 7U, 6U, 5U, 4U, 3U, 2U, 1U, 0U };
    uint32_t priorities[]     = { 10U, 20U, 30U, 40U, 50U, 60U, 70U, 80U };
    uint32_t i;
    KThread_t *picked;

    sched_reset();
    sched_init();
    mock_cpu_id = 0U;

    /* 按 tid 顺序入队，优先级为 priorities[tid] */
    for (i = 0U; i < 8U; i++)
    {
        thread_init(&threads[i], i, (uint8_t)priorities[i], KTHREAD_POLICY_FIFO, 0U);
        sched_enqueue(&threads[i]);
    }

    /* 按优先级从高到低出队 */
    for (i = 0U; i < 8U; i++)
    {
        picked = sched_pick_next();
        TEST_ASSERT_NOT_NULL(picked);
        TEST_ASSERT_EQ(picked->tid, expected_order[i]);
        sched_dequeue(picked);
    }

    TEST_ASSERT_EQ(g_test_sched.cpu_queues[0U].nr_running, 0U);
    TEST_ASSERT_TRUE(bitmap256_empty(&g_test_sched.cpu_queues[0U].bitmap) == 1);
}

/**
 * @brief 测试 7: pick_next 返回最高优先级就绪任务
 */
static void test_pick_next_basic(void)
{
    KThread_t t1;
    KThread_t t2;
    KThread_t *picked;

    sched_reset();
    sched_init();
    mock_cpu_id = 0U;

    thread_init(&t1, 0U, 100U, KTHREAD_POLICY_FIFO, 0U);
    thread_init(&t2, 1U, 200U, KTHREAD_POLICY_FIFO, 0U);

    sched_enqueue(&t1);
    sched_enqueue(&t2);

    picked = sched_pick_next();
    TEST_ASSERT_EQ(picked->prio, 200U);
}

/**
 * @brief 测试 8: RR 时间片耗尽触发重新调度
 */
static void test_tick_timeslice(void)
{
    KThread_t thread;

    sched_reset();
    sched_init();
    mock_cpu_id = 0U;

    thread_init(&thread, 0U, 100U, KTHREAD_POLICY_RR, 3U);
    sched_load_current(&thread);

    /* tick 3 次，时间片从 3 减到 0 */
    sched_tick();
    TEST_ASSERT_EQ(thread.time_slice, 2U);
    TEST_ASSERT_FALSE(sched_tick_needs_resched);

    sched_tick();
    TEST_ASSERT_EQ(thread.time_slice, 1U);
    TEST_ASSERT_FALSE(sched_tick_needs_resched);

    sched_tick();
    /* 时间片耗尽后自动重载 */
    TEST_ASSERT_EQ(thread.time_slice, 3U);
    TEST_ASSERT_TRUE(sched_tick_needs_resched);

    /* 重载值不变 */
    TEST_ASSERT_EQ(thread.time_slice_reload, 3U);
}

/**
 * @brief 测试 9: FIFO 线程不受时间片影响
 */
static void test_tick_fifo_no_effect(void)
{
    KThread_t thread;

    sched_reset();
    sched_init();
    mock_cpu_id = 0U;

    thread_init(&thread, 0U, 100U, KTHREAD_POLICY_FIFO, 5U);
    sched_load_current(&thread);

    sched_tick();
    TEST_ASSERT_EQ(thread.time_slice, 5U);
    TEST_ASSERT_FALSE(sched_tick_needs_resched);
}

/**
 * @brief 测试 10: 同优先级 FIFO 顺序
 */
static void test_same_priority_fifo_order(void)
{
    KThread_t t1;
    KThread_t t2;
    KThread_t t3;
    KThread_t *picked;

    sched_reset();
    sched_init();
    mock_cpu_id = 0U;

    thread_init(&t1, 0U, 100U, KTHREAD_POLICY_FIFO, 0U);
    thread_init(&t2, 1U, 100U, KTHREAD_POLICY_FIFO, 0U);
    thread_init(&t3, 2U, 100U, KTHREAD_POLICY_FIFO, 0U);

    /* 入队顺序: t1, t2, t3（加到尾部） */
    sched_enqueue(&t1);
    sched_enqueue(&t2);
    sched_enqueue(&t3);

    /* pick_next 取链表头部，应为 t1 */
    picked = sched_pick_next();
    TEST_ASSERT_EQ(picked->tid, 0U);
    sched_dequeue(picked);

    picked = sched_pick_next();
    TEST_ASSERT_EQ(picked->tid, 1U);
    sched_dequeue(picked);

    picked = sched_pick_next();
    TEST_ASSERT_EQ(picked->tid, 2U);
}

/**
 * @brief 测试 11: 多 CPU 各自独立队列
 */
static void test_multi_cpu_queues(void)
{
    KThread_t t_cpu0;
    KThread_t t_cpu1;
    KThread_t *picked;

    sched_reset();
    sched_init();

    /* CPU 0 入队 */
    mock_cpu_id = 0U;
    thread_init(&t_cpu0, 0U, 100U, KTHREAD_POLICY_FIFO, 0U);
    sched_enqueue(&t_cpu0);

    /* CPU 1 入队 */
    mock_cpu_id = 1U;
    thread_init(&t_cpu1, 1U, 200U, KTHREAD_POLICY_FIFO, 0U);
    sched_enqueue(&t_cpu1);

    /* CPU 0 只能看到 t_cpu0 */
    mock_cpu_id = 0U;
    picked = sched_pick_next();
    TEST_ASSERT_NOT_NULL(picked);
    TEST_ASSERT_EQ(picked->tid, 0U);
    TEST_ASSERT_EQ(g_test_sched.cpu_queues[0U].nr_running, 1U);

    /* CPU 1 只能看到 t_cpu1 */
    mock_cpu_id = 1U;
    picked = sched_pick_next();
    TEST_ASSERT_NOT_NULL(picked);
    TEST_ASSERT_EQ(picked->tid, 1U);
    TEST_ASSERT_EQ(g_test_sched.cpu_queues[1U].nr_running, 1U);
}

/**
 * @brief 测试 12: idle 线程在队列为空时返回
 */
static void test_idle_task(void)
{
    KThread_t idle;
    KThread_t *picked;

    sched_reset();
    sched_init();
    mock_cpu_id = 0U;

    /* 手动设置 idle 线程 */
    thread_init(&idle, 0U, 0U, KTHREAD_POLICY_FIFO, 0U);
    g_test_sched.cpu_queues[0U].idle_thread = &idle;

    /* 空队列时应返回 idle */
    picked = sched_pick_next();
    TEST_ASSERT_NOT_NULL(picked);
    TEST_ASSERT_EQ(picked->tid, 0U);
}

/**
 * @brief 测试 13: bitmap256_find_highest 正确性
 */
static void test_bitmap_find_highest(void)
{
    bitmap256_t bm;

    bitmap256_clear_all(&bm);
    TEST_ASSERT_EQ(bitmap256_find_highest(&bm), 256U);

    bitmap256_set(&bm, 0U);
    TEST_ASSERT_EQ(bitmap256_find_highest(&bm), 0U);

    bitmap256_set(&bm, 255U);
    TEST_ASSERT_EQ(bitmap256_find_highest(&bm), 255U);

    bitmap256_clear_all(&bm);
    bitmap256_set(&bm, 63U);
    TEST_ASSERT_EQ(bitmap256_find_highest(&bm), 63U);

    bitmap256_set(&bm, 100U);
    TEST_ASSERT_EQ(bitmap256_find_highest(&bm), 100U);

    bitmap256_set(&bm, 200U);
    TEST_ASSERT_EQ(bitmap256_find_highest(&bm), 200U);

    /* 清除 200，最高应为 100 */
    bitmap256_clear(&bm, 200U);
    TEST_ASSERT_EQ(bitmap256_find_highest(&bm), 100U);

    /* NULL 安全 */
    TEST_ASSERT_EQ(bitmap256_find_highest(NULL), 256U);
}

/**
 * @brief 测试 14: NULL 参数安全检查
 */
static void test_null_param(void)
{
    KThread_t *picked;

    sched_reset();
    sched_init();
    mock_cpu_id = 0U;

    /* enqueue NULL 不崩溃 */
    sched_enqueue(NULL);

    /* dequeue NULL 不崩溃 */
    sched_dequeue(NULL);

    /* load_current NULL 不崩溃 */
    sched_load_current(NULL);

    /* 空队列 pick_next 返回 NULL（无 idle） */
    picked = sched_pick_next();
    TEST_ASSERT_NULL(picked);

    /* tick 无当前线程不崩溃 */
    sched_tick();
}

/**
 * @brief 测试 15: 入队最大线程数
 */
static void test_enqueue_max(void)
{
    KThread_t threads[16];
    uint32_t i;

    sched_reset();
    sched_init();
    mock_cpu_id = 0U;

    /* 入队 16 个线程 */
    for (i = 0U; i < 16U; i++)
    {
        thread_init(&threads[i], i, (uint8_t)(i * 16U), KTHREAD_POLICY_FIFO, 0U);
        sched_enqueue(&threads[i]);
    }

    TEST_ASSERT_EQ(g_test_sched.cpu_queues[0U].nr_running, 16U);

    /* 全部出队 */
    for (i = 0U; i < 16U; i++)
    {
        KThread_t *picked = sched_pick_next();
        TEST_ASSERT_NOT_NULL(picked);
        sched_dequeue(picked);
    }

    TEST_ASSERT_EQ(g_test_sched.cpu_queues[0U].nr_running, 0U);
}

/**
 * @brief 测试 16: 线程表初始化状态
 */
static void test_thread_table_init(void)
{
    uint32_t i;

    sched_reset();
    sched_init();

    for (i = 0U; i < CONFIG_MAX_THREADS; i++)
    {
        KThread_t *t = &g_test_sched.thread_table[i];
        TEST_ASSERT_EQ(t->tid, i);
        TEST_ASSERT_EQ(t->state, KTHREAD_STATE_DEAD);
    }
}

/**
 * @brief 测试 17: load_current 设置状态
 */
static void test_load_current(void)
{
    KThread_t thread;

    sched_reset();
    sched_init();
    mock_cpu_id = 0U;

    thread_init(&thread, 0U, 100U, KTHREAD_POLICY_FIFO, 0U);
    sched_load_current(&thread);

    TEST_ASSERT_EQ(thread.state, KTHREAD_STATE_RUNNING);
    TEST_ASSERT_EQ(g_test_sched.cpu_queues[0U].current_thread, &thread);
}

/**
 * @brief 测试 18: 循环 1000 次入队出队无泄漏
 */
static void test_stress_enqueue_dequeue(void)
{
    KThread_t threads[8];
    uint32_t i;
    uint32_t iter;

    sched_reset();
    sched_init();
    mock_cpu_id = 0U;

    /* 初始化 8 个线程 */
    for (i = 0U; i < 8U; i++)
    {
        thread_init(&threads[i], i, (uint8_t)(i * 30U + 10U), KTHREAD_POLICY_FIFO, 0U);
    }

    for (iter = 0U; iter < 1000U; iter++)
    {
        /* 入队所有 */
        for (i = 0U; i < 8U; i++)
        {
            sched_enqueue(&threads[i]);
        }
        TEST_ASSERT_EQ(g_test_sched.cpu_queues[0U].nr_running, 8U);

        /* 出队所有 */
        for (i = 0U; i < 8U; i++)
        {
            KThread_t *picked = sched_pick_next();
            TEST_ASSERT_NOT_NULL(picked);
            sched_dequeue(picked);
        }
        TEST_ASSERT_EQ(g_test_sched.cpu_queues[0U].nr_running, 0U);
        TEST_ASSERT_TRUE(bitmap256_empty(&g_test_sched.cpu_queues[0U].bitmap) == 1);
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("=== 256级优先级位图调度器测试 ===\n\n");

    test_init_basic();
    test_enqueue_one();
    test_enqueue_priority();
    test_dequeue_highest();
    test_dequeue_empty();
    test_enqueue_dequeue_all();
    test_pick_next_basic();
    test_tick_timeslice();
    test_tick_fifo_no_effect();
    test_same_priority_fifo_order();
    test_multi_cpu_queues();
    test_idle_task();
    test_bitmap_find_highest();
    test_null_param();
    test_enqueue_max();
    test_thread_table_init();
    test_load_current();
    test_stress_enqueue_dequeue();

    TEST_SUMMARY("test_scheduler");

    return TEST_RESULT();
}
