/**
 * @file    test_timer.c
 * @brief   AISafe64 RTOS - ARM 通用定时器与软件定时器单元测试（宿主机）
 * @author  AISafe64 Team
 * @date    2026-04-03
 * @version 1.0
 *
 * @details ARM 通用定时器宿主机自包含测试
 *          测试与内核 timer.c 一致的逻辑：
 *          - 初始化（滴答清零、队列为空）
 *          - 系统滴答递增
 *          - 单次软件定时器（触发后变 EXPIRED）
 *          - 周期软件定时器（触发后重入队）
 *          - 停止定时器
 *          - 时间转换宏（MS_TO_TICKS / TICKS_TO_MS）
 *          - 睡眠队列唤醒
 *          - NULL 参数安全
 *          - 多定时器同时过期
 *          - 压力测试
 *
 * @note 对应需求: TM-001~004（定时器）、TF-001
 */

#include "mock_kernel.h"

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

#undef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* ========================================================================
 * 配置常量（与 kernel/config.h 一致）
 * ======================================================================== */

#define CONFIG_TICK_RATE_HZ  1000U

/* ========================================================================
 * TimerState_t 与 SoftwareTimer_t 定义（与 kernel/timer.h 一致）
 * ======================================================================== */

typedef enum
{
    TIMER_STATE_IDLE     = 0U,
    TIMER_STATE_ACTIVE  = 1U,
    TIMER_STATE_EXPIRED = 2U,
    TIMER_STATE_CALLBACK = 3U
} TimerState_t;

typedef void (*TimerCallback_t)(void *arg);

typedef struct
{
    uint64_t expire_tick;
    uint64_t interval;
    TimerCallback_t callback;
    void *callback_arg;
    TimerState_t state;
    struct list_head node;
} SoftwareTimer_t;

/* ========================================================================
 * KThread_t 简化定义（用于睡眠队列测试）
 * ======================================================================== */

typedef enum
{
    KTHREAD_STATE_DEAD      = 0U,
    KTHREAD_STATE_READY     = 1U,
    KTHREAD_STATE_RUNNING   = 2U,
    KTHREAD_STATE_BLOCKED   = 3U,
    KTHREAD_STATE_SLEEPING  = 4U,
    KTHREAD_STATE_SUSPENDED = 5U
} KThreadState_t;

typedef struct
{
    uint32_t tid;
    KThreadState_t state;
    struct list_head sleep_node;
    uint64_t wakeup_tick;
} MockThread_t;

/* ========================================================================
 * 时间转换宏（与 kernel/timer.h 一致）
 * ======================================================================== */

#define MS_TO_TICKS(ms)   ((uint64_t)(ms) * (uint64_t)CONFIG_TICK_RATE_HZ / 1000U)
#define TICKS_TO_MS(ticks) ((uint64_t)(ticks) * 1000U / (uint64_t)CONFIG_TICK_RATE_HZ)
#define US_TO_TICKS(us)   ((uint64_t)(us) * (uint64_t)CONFIG_TICK_RATE_HZ / 1000000U)

/* ========================================================================
 * 定时器系统模拟实现（与 timer.c 逻辑一致）
 * ======================================================================== */

/** @brief 系统滴答计数器 */
static uint64_t s_system_ticks = 0ULL;

/** @brief 软件定时器队列 */
static struct list_head s_timer_queue;

/** @brief 睡眠等待队列 */
static struct list_head s_sleep_queue;

/** @brief 回调记录（测试辅助） */
static uint32_t s_callback_count = 0U;
static void *s_callback_last_arg = NULL;
static uint32_t s_periodic_callback_count = 0U;

/**
 * @brief 测试回调函数
 */
static void test_callback(void *arg)
{
    s_callback_count++;
    s_callback_last_arg = arg;
}

/**
 * @brief 周期定时器回调
 */
static void periodic_callback(void *arg)
{
    (void)arg;
    s_periodic_callback_count++;
}

/**
 * @brief 初始化定时器系统
 * @details 与 timer.c 的 timer_init 一致
 */
static void timer_test_init(void)
{
    s_system_ticks = 0ULL;

    s_timer_queue.next = &s_timer_queue;
    s_timer_queue.prev = &s_timer_queue;

    s_sleep_queue.next = &s_sleep_queue;
    s_sleep_queue.prev = &s_sleep_queue;

    s_callback_count = 0U;
    s_callback_last_arg = NULL;
    s_periodic_callback_count = 0U;
}

/**
 * @brief 初始化软件定时器
 * @details 与 timer.c 的 timer_init_soft 一致
 */
static void timer_init_soft(SoftwareTimer_t *timer, TimerCallback_t callback, void *arg)
{
    if (timer == NULL) { return; }

    timer->expire_tick  = 0ULL;
    timer->interval     = 0ULL;
    timer->callback     = callback;
    timer->callback_arg = arg;
    timer->state        = TIMER_STATE_IDLE;
    timer->node.next    = &timer->node;
    timer->node.prev    = &timer->node;
}

/**
 * @brief 启动单次定时器
 * @details 与 timer.c 的 timer_start_oneshot 一致
 */
static void timer_start_oneshot(SoftwareTimer_t *timer, uint32_t ms)
{
    if (timer == NULL) { return; }

    /* 如果已在队列中，先移除 */
    if (timer->node.next != &timer->node)
    {
        timer->node.prev->next = timer->node.next;
        timer->node.next->prev = timer->node.prev;
    }

    timer->expire_tick = s_system_ticks + MS_TO_TICKS(ms);
    timer->interval    = 0ULL;
    timer->state       = TIMER_STATE_ACTIVE;

    /* 插入到定时器队列头部 */
    timer->node.next = s_timer_queue.next;
    timer->node.prev = &s_timer_queue;
    s_timer_queue.next->prev = &timer->node;
    s_timer_queue.next = &timer->node;
}

/**
 * @brief 启动周期定时器
 * @details 与 timer.c 的 timer_start_periodic 一致
 */
static void timer_start_periodic(SoftwareTimer_t *timer, uint32_t ms)
{
    if (timer == NULL) { return; }

    /* 如果已在队列中，先移除 */
    if (timer->node.next != &timer->node)
    {
        timer->node.prev->next = timer->node.next;
        timer->node.next->prev = timer->node.prev;
    }

    timer->expire_tick = s_system_ticks + MS_TO_TICKS(ms);
    timer->interval    = MS_TO_TICKS(ms);
    timer->state       = TIMER_STATE_ACTIVE;

    /* 插入到定时器队列头部 */
    timer->node.next = s_timer_queue.next;
    timer->node.prev = &s_timer_queue;
    s_timer_queue.next->prev = &timer->node;
    s_timer_queue.next = &timer->node;
}

/**
 * @brief 停止软件定时器
 * @details 与 timer.c 的 timer_stop 一致
 */
static void timer_stop(SoftwareTimer_t *timer)
{
    if (timer == NULL) { return; }

    /* 从队列中移除 */
    if (timer->node.next != &timer->node)
    {
        timer->node.prev->next = timer->node.next;
        timer->node.next->prev = timer->node.prev;
        timer->node.next = &timer->node;
        timer->node.prev = &timer->node;
    }

    timer->state = TIMER_STATE_IDLE;
}

/**
 * @brief 定时器中断处理（核心）
 * @details 与 timer.c 的 timer_interrupt_handler 一致
 *          省略硬件比较器操作和 scheduler_tick()
 */
static void timer_interrupt_handler(void)
{
    /* 递增系统滴答 */
    s_system_ticks++;

    /* 处理软件定时器 */
    while (s_timer_queue.next != &s_timer_queue)
    {
        SoftwareTimer_t *timer = container_of(s_timer_queue.next,
                                               SoftwareTimer_t, node);

        if (timer->expire_tick <= s_system_ticks)
        {
            /* 从队列中移除 */
            timer->node.prev->next = timer->node.next;
            timer->node.next->prev = timer->node.prev;
            timer->node.next = &timer->node;
            timer->node.prev = &timer->node;

            timer->state = TIMER_STATE_CALLBACK;

            /* 执行回调 */
            if (timer->callback != NULL)
            {
                timer->callback(timer->callback_arg);
            }

            /* 如果是周期定时器，重新入队 */
            if (timer->interval > 0ULL)
            {
                timer->expire_tick += timer->interval;
                timer->state = TIMER_STATE_ACTIVE;

                /* 重新插入到队列头部 */
                timer->node.next = s_timer_queue.next;
                timer->node.prev = &s_timer_queue;
                s_timer_queue.next->prev = &timer->node;
                s_timer_queue.next = &timer->node;
            }
            else
            {
                timer->state = TIMER_STATE_EXPIRED;
            }
        }
        else
        {
            break;
        }
    }

    /* 处理睡眠线程唤醒 */
    while (s_sleep_queue.next != &s_sleep_queue)
    {
        MockThread_t *thread = container_of(s_sleep_queue.next,
                                             MockThread_t, sleep_node);

        if (thread->wakeup_tick <= s_system_ticks)
        {
            /* 从睡眠队列移除 */
            thread->sleep_node.prev->next = thread->sleep_node.next;
            thread->sleep_node.next->prev = thread->sleep_node.prev;
            thread->sleep_node.next = &thread->sleep_node;
            thread->sleep_node.prev = &thread->sleep_node;

            /* 唤醒线程 */
            thread->state = KTHREAD_STATE_READY;
        }
        else
        {
            break;
        }
    }
}

/**
 * @brief 模拟线程加入睡眠队列
 */
static void sleep_enqueue(MockThread_t *thread, uint64_t ticks)
{
    struct list_head *pos;

    thread->wakeup_tick = s_system_ticks + ticks;
    thread->state = KTHREAD_STATE_SLEEPING;

    /* 按 wakeup_tick 升序插入（最早的在前） */
    for (pos = s_sleep_queue.next; pos != &s_sleep_queue; pos = pos->next)
    {
        MockThread_t *existing = container_of(pos, MockThread_t, sleep_node);
        if (thread->wakeup_tick <= existing->wakeup_tick)
        {
            break;
        }
    }

    thread->sleep_node.next = pos;
    thread->sleep_node.prev = pos->prev;
    pos->prev->next = &thread->sleep_node;
    pos->prev = &thread->sleep_node;
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试 1: 初始化后状态正确
 */
static void test_init_basic(void)
{
    timer_test_init();

    TEST_ASSERT_EQ(s_system_ticks, 0ULL);
    TEST_ASSERT_TRUE(list_empty(&s_timer_queue) == 1);
    TEST_ASSERT_TRUE(list_empty(&s_sleep_queue) == 1);
}

/**
 * @brief 测试 2: 滴答递增正确
 */
static void test_tick_increment(void)
{
    uint32_t i;

    timer_test_init();

    for (i = 0U; i < 100U; i++)
    {
        timer_interrupt_handler();
        TEST_ASSERT_EQ(s_system_ticks, (uint64_t)(i + 1U));
    }

    TEST_ASSERT_EQ(s_system_ticks, 100ULL);
}

/**
 * @brief 测试 3: 单次定时器初始化
 */
static void test_soft_timer_init(void)
{
    SoftwareTimer_t timer;

    timer_init_soft(&timer, test_callback, (void *)42U);

    TEST_ASSERT_EQ(timer.state, TIMER_STATE_IDLE);
    TEST_ASSERT_EQ(timer.expire_tick, 0ULL);
    TEST_ASSERT_EQ(timer.interval, 0ULL);
    TEST_ASSERT_EQ(timer.callback, test_callback);
    TEST_ASSERT_TRUE(timer.node.next == &timer.node);
}

/**
 * @brief 测试 4: 单次定时器触发后变 EXPIRED
 */
static void test_oneshot_fire(void)
{
    SoftwareTimer_t timer;

    timer_test_init();
    timer_init_soft(&timer, test_callback, (void *)1U);

    timer_start_oneshot(&timer, 10U);  /* 10ms = 10 ticks (1000Hz) */
    TEST_ASSERT_EQ(timer.state, TIMER_STATE_ACTIVE);
    TEST_ASSERT_EQ(s_callback_count, 0U);

    /* 前 10 次 tick 不触发 */
    uint32_t i;
    for (i = 0U; i < 9U; i++)
    {
        timer_interrupt_handler();
    }
    TEST_ASSERT_EQ(timer.state, TIMER_STATE_ACTIVE);
    TEST_ASSERT_EQ(s_callback_count, 0U);

    /* 第 10 次 tick 触发 */
    timer_interrupt_handler();
    TEST_ASSERT_EQ(timer.state, TIMER_STATE_EXPIRED);
    TEST_ASSERT_EQ(s_callback_count, 1U);
    TEST_ASSERT_EQ(s_callback_last_arg, (void *)1U);
}

/**
 * @brief 测试 5: 周期定时器反复触发
 */
static void test_periodic_fire(void)
{
    SoftwareTimer_t timer;
    uint32_t i;

    timer_test_init();
    timer_init_soft(&timer, periodic_callback, NULL);

    timer_start_periodic(&timer, 5U);  /* 每 5ms 触发一次 */

    /* 模拟 25 次 tick → 应触发 5 次 */
    for (i = 0U; i < 25U; i++)
    {
        timer_interrupt_handler();
    }

    TEST_ASSERT_EQ(s_periodic_callback_count, 5U);
    TEST_ASSERT_EQ(timer.state, TIMER_STATE_ACTIVE);  /* 仍是活跃 */
}

/**
 * @brief 测试 6: 停止定时器后不触发
 */
static void test_stop_timer(void)
{
    SoftwareTimer_t timer;
    uint32_t i;

    timer_test_init();
    timer_init_soft(&timer, test_callback, NULL);

    timer_start_oneshot(&timer, 10U);
    timer_stop(&timer);

    TEST_ASSERT_EQ(timer.state, TIMER_STATE_IDLE);

    /* 即使 tick 到期也不会触发 */
    for (i = 0U; i < 20U; i++)
    {
        timer_interrupt_handler();
    }

    TEST_ASSERT_EQ(s_callback_count, 0U);
}

/**
 * @brief 测试 7: MS_TO_TICKS / TICKS_TO_MS 宏正确性
 */
static void test_time_conversion(void)
{
    /* CONFIG_TICK_RATE_HZ = 1000, 1ms = 1 tick */
    TEST_ASSERT_EQ(MS_TO_TICKS(1U), 1ULL);
    TEST_ASSERT_EQ(MS_TO_TICKS(1000U), 1000ULL);
    TEST_ASSERT_EQ(MS_TO_TICKS(500U), 500ULL);

    TEST_ASSERT_EQ(TICKS_TO_MS(1ULL), 1ULL);
    TEST_ASSERT_EQ(TICKS_TO_MS(1000ULL), 1000ULL);
    TEST_ASSERT_EQ(TICKS_TO_MS(500ULL), 500ULL);

    /* US_TO_TICKS */
    TEST_ASSERT_EQ(US_TO_TICKS(1000U), 1ULL);
    TEST_ASSERT_EQ(US_TO_TICKS(1000000U), 1000ULL);
}

/**
 * @brief 测试 8: 睡眠线程到期唤醒
 */
static void test_sleep_wakeup(void)
{
    MockThread_t thread;

    timer_test_init();

    thread.tid = 1U;
    init_list_head(&thread.sleep_node);

    /* 线程睡眠 10 ticks */
    sleep_enqueue(&thread, 10U);

    TEST_ASSERT_EQ(thread.state, KTHREAD_STATE_SLEEPING);
    TEST_ASSERT_TRUE(list_empty(&s_sleep_queue) == 0);

    /* tick 1~9: 线程仍在睡眠 */
    uint32_t i;
    for (i = 0U; i < 9U; i++)
    {
        timer_interrupt_handler();
    }
    TEST_ASSERT_EQ(thread.state, KTHREAD_STATE_SLEEPING);

    /* tick 10: 线程被唤醒 */
    timer_interrupt_handler();
    TEST_ASSERT_EQ(thread.state, KTHREAD_STATE_READY);
    TEST_ASSERT_TRUE(list_empty(&s_sleep_queue) == 1);
}

/**
 * @brief 测试 9: 多个睡眠线程按顺序唤醒
 */
static void test_sleep_multiple(void)
{
    MockThread_t t1;
    MockThread_t t2;
    MockThread_t t3;

    timer_test_init();

    t1.tid = 1U;  init_list_head(&t1.sleep_node);
    t2.tid = 2U;  init_list_head(&t2.sleep_node);
    t3.tid = 3U;  init_list_head(&t3.sleep_node);

    /* 不同线程睡眠不同时长 */
    sleep_enqueue(&t1, 5U);   /* tick 5 唤醒 */
    sleep_enqueue(&t2, 10U);  /* tick 10 唤醒 */
    sleep_enqueue(&t3, 15U);  /* tick 15 唤醒 */

    uint32_t i;
    for (i = 0U; i < 5U; i++)
    {
        timer_interrupt_handler();
    }
    TEST_ASSERT_EQ(t1.state, KTHREAD_STATE_READY);
    TEST_ASSERT_EQ(t2.state, KTHREAD_STATE_SLEEPING);
    TEST_ASSERT_EQ(t3.state, KTHREAD_STATE_SLEEPING);

    for (i = 0U; i < 5U; i++)
    {
        timer_interrupt_handler();
    }
    TEST_ASSERT_EQ(t2.state, KTHREAD_STATE_READY);
    TEST_ASSERT_EQ(t3.state, KTHREAD_STATE_SLEEPING);

    for (i = 0U; i < 5U; i++)
    {
        timer_interrupt_handler();
    }
    TEST_ASSERT_EQ(t3.state, KTHREAD_STATE_READY);
}

/**
 * @brief 测试 10: NULL 参数安全检查
 */
static void test_null_param(void)
{
    timer_init_soft(NULL, test_callback, NULL);
    /* 不崩溃 */

    timer_start_oneshot(NULL, 10U);
    /* 不崩溃 */

    timer_start_periodic(NULL, 10U);
    /* 不崩溃 */

    timer_stop(NULL);
    /* 不崩溃 */

    TEST_ASSERT_TRUE(true);
}

/**
 * @brief 测试 11: 多个定时器同时过期
 */
static void test_multiple_timers_same_tick(void)
{
    SoftwareTimer_t t1;
    SoftwareTimer_t t2;
    SoftwareTimer_t t3;

    timer_test_init();
    timer_init_soft(&t1, test_callback, (void *)1U);
    timer_init_soft(&t2, test_callback, (void *)2U);
    timer_init_soft(&t3, test_callback, (void *)3U);

    /* 三个定时器都在 5 ticks 后触发 */
    timer_start_oneshot(&t1, 5U);
    timer_start_oneshot(&t2, 5U);
    timer_start_oneshot(&t3, 5U);

    s_callback_count = 0U;

    uint32_t i;
    for (i = 0U; i < 5U; i++)
    {
        timer_interrupt_handler();
    }

    /* 三个定时器都应该已触发 */
    TEST_ASSERT_EQ(s_callback_count, 3U);
    TEST_ASSERT_EQ(t1.state, TIMER_STATE_EXPIRED);
    TEST_ASSERT_EQ(t2.state, TIMER_STATE_EXPIRED);
    TEST_ASSERT_EQ(t3.state, TIMER_STATE_EXPIRED);
}

/**
 * @brief 测试 12: 定时器与睡眠同时工作
 */
static void test_timer_and_sleep_together(void)
{
    SoftwareTimer_t timer;
    MockThread_t thread;

    timer_test_init();
    timer_init_soft(&timer, test_callback, NULL);

    thread.tid = 1U;
    init_list_head(&thread.sleep_node);

    timer_start_oneshot(&timer, 5U);
    sleep_enqueue(&thread, 10U);

    /* tick 5: 定时器触发，线程仍睡眠 */
    uint32_t i;
    for (i = 0U; i < 5U; i++)
    {
        timer_interrupt_handler();
    }
    TEST_ASSERT_EQ(timer.state, TIMER_STATE_EXPIRED);
    TEST_ASSERT_EQ(s_callback_count, 1U);
    TEST_ASSERT_EQ(thread.state, KTHREAD_STATE_SLEEPING);

    /* tick 10: 线程唤醒 */
    for (i = 0U; i < 5U; i++)
    {
        timer_interrupt_handler();
    }
    TEST_ASSERT_EQ(thread.state, KTHREAD_STATE_READY);
}

/**
 * @brief 测试 13: 周期定时器触发后 expire_tick 更新
 */
static void test_periodic_expire_update(void)
{
    SoftwareTimer_t timer;

    timer_test_init();
    timer_init_soft(&timer, periodic_callback, NULL);

    timer_start_periodic(&timer, 10U);  /* 每 10 ticks */

    /* 第一次触发时 expire_tick 应为 10 */
    uint32_t i;
    for (i = 0U; i < 10U; i++)
    {
        timer_interrupt_handler();
    }

    TEST_ASSERT_EQ(s_periodic_callback_count, 1U);
    /* 触发后 expire_tick 更新为 10 + 10 = 20 */
    TEST_ASSERT_EQ(timer.expire_tick, 20ULL);

    /* 再触发 10 次 tick，第二次触发 */
    for (i = 0U; i < 10U; i++)
    {
        timer_interrupt_handler();
    }

    TEST_ASSERT_EQ(s_periodic_callback_count, 2U);
    TEST_ASSERT_EQ(timer.expire_tick, 30ULL);
}

/**
 * @brief 测试 14: 压力测试 — 1000 次滴答
 */
static void test_stress_ticks(void)
{
    SoftwareTimer_t timer;
    uint32_t i;

    timer_test_init();
    timer_init_soft(&timer, periodic_callback, NULL);

    timer_start_periodic(&timer, 1U);  /* 每 tick 触发 */

    for (i = 0U; i < 1000U; i++)
    {
        timer_interrupt_handler();
    }

    /* 1000 ticks, 每 tick 触发一次 = 1000 次回调 */
    TEST_ASSERT_EQ(s_periodic_callback_count, 1000U);
    TEST_ASSERT_EQ(s_system_ticks, 1000ULL);
}

/**
 * @brief 测试 15: 停止已在队列中的定时器可再次启动
 */
static void test_restart_after_stop(void)
{
    SoftwareTimer_t timer;

    timer_test_init();
    timer_init_soft(&timer, test_callback, NULL);

    /* 启动 → 停止 → 重新启动 */
    timer_start_oneshot(&timer, 5U);
    timer_stop(&timer);
    TEST_ASSERT_EQ(timer.state, TIMER_STATE_IDLE);

    timer_start_oneshot(&timer, 3U);
    TEST_ASSERT_EQ(timer.state, TIMER_STATE_ACTIVE);

    /* 3 ticks 后触发 */
    uint32_t i;
    for (i = 0U; i < 3U; i++)
    {
        timer_interrupt_handler();
    }

    TEST_ASSERT_EQ(s_callback_count, 1U);
    TEST_ASSERT_EQ(timer.state, TIMER_STATE_EXPIRED);
}

/**
 * @brief 测试 16: 0ms 定时器（立即触发）
 */
static void test_zero_ms_timer(void)
{
    SoftwareTimer_t timer;

    timer_test_init();
    timer_init_soft(&timer, test_callback, NULL);

    /* MS_TO_TICKS(0) = 0，expire_tick = s_system_ticks + 0 = 0 */
    timer_start_oneshot(&timer, 0U);
    TEST_ASSERT_EQ(timer.state, TIMER_STATE_ACTIVE);

    /* 第 1 个 tick 就会触发（expire_tick=0 <= s_system_ticks=1） */
    timer_interrupt_handler();
    TEST_ASSERT_EQ(timer.state, TIMER_STATE_EXPIRED);
    TEST_ASSERT_EQ(s_callback_count, 1U);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("=== ARM 通用定时器与软件定时器测试 ===\n\n");

    test_init_basic();
    test_tick_increment();
    test_soft_timer_init();
    test_oneshot_fire();
    test_periodic_fire();
    test_stop_timer();
    test_time_conversion();
    test_sleep_wakeup();
    test_sleep_multiple();
    test_null_param();
    test_multiple_timers_same_tick();
    test_timer_and_sleep_together();
    test_periodic_expire_update();
    test_stress_ticks();
    test_restart_after_stop();
    test_zero_ms_timer();

    TEST_SUMMARY("test_timer");

    return TEST_RESULT();
}
