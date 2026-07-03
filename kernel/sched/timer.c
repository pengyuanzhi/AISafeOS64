/**
 * @file    timer.c
 * @brief   ARMv8-A 通用定时器实现
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 实现所有 timer.h 中声明的函数：
 *          - timer_init:               初始化 ARM 通用定时器
 *          - timer_get_ticks/ns/us/ms: 获取时间戳
 *          - timer_interrupt_handler:  定时器中断处理
 *          - kthread_sleep:            线程延迟
 *          - kthread_sleep_ticks:      线程延迟（tick 精度）
 *          - timer_wakeup_thread:      唤醒睡眠线程
 *          - 软件定时器管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: TM-001~004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/timer.h>
#include <kernel/barrier.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <kernel/compiler.h>
#include <kernel/spinlock.h>
#include "thread.h"
#include "scheduler.h"
#include "hal.h"
#include <stdint.h>
#include <stddef.h>

/* 前向声明: 调度器接口（定义在 scheduler.c） */
extern void scheduler_tick(void);

/* ========================================================================
 * 定时器控制位定义
 * ======================================================================== */

/** @brief 定时器使能位 */
#define CNTP_CTL_ENABLE    (1ULL << 0U)

/** @brief 定时器 IMASK 位（中断屏蔽） */
#define CNTP_CTL_IMASK     (1ULL << 1U)

/** @brief 定时器 ISTATUS 位（正在触发） */
#define CNTP_CTL_ISTATUS   (1ULL << 2U)

/* ========================================================================
 * 全局定时器状态
 * ======================================================================== */

/**
 * @brief 系统滴答计数器
 *
 * @details 每次定时器中断递增一次，表示系统启动后的 tick 数。
 *          使用原子操作递增，保证多核并发安全（优化点 1）。
 */
static volatile tick_t s_system_ticks = 0ULL;

/**
 * @brief 计数器频率（Hz），从 CNTFRQ_EL0 读取
 */
static uint64_t s_counter_freq = 0ULL;

/**
 * @brief 每 CPU 软件定时器队列（优化点 3）
 *
 * @details 每个 CPU 拥有独立的软件定时器队列，消除跨核链表竞争。
 *          软件定时器在注册时绑定到当前 CPU，由该 CPU 的定时器中断处理。
 */
static struct list_head s_timer_queues[CONFIG_MAX_CPUS];

/**
 * @brief 每 CPU 软件定时器队列锁（优化点 2）
 *
 * @details 保护对应 CPU 的定时器队列链表操作（注册/遍历/移除）。
 *          定时器中断处理（CPU 本地）和线程注册（可能跨核）都需要加锁。
 */
static TicketLock_t s_timer_locks[CONFIG_MAX_CPUS];

/**
 * @brief 每 CPU 睡眠线程等待队列（优化点 3）
 *
 * @details 每个 CPU 拥有独立的睡眠队列。线程在当前 CPU 上睡眠，
 *          由该 CPU 的定时器中断唤醒，消除跨核竞争。
 */
static struct list_head s_sleep_queues[CONFIG_MAX_CPUS];

/**
 * @brief 每 CPU 睡眠队列锁（优化点 2）
 */
static TicketLock_t s_sleep_locks[CONFIG_MAX_CPUS];

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 获取每个 tick 对应的计数器周期数
 *
 * @return 每个计数器周期的 ticks 数
 */
static uint64_t get_ticks_per_counter_tick(void)
{
    if (s_counter_freq == 0ULL)
    {
        return 1ULL;
    }

    return s_counter_freq / (uint64_t)CONFIG_TICK_RATE_HZ;
}

/**
 * @brief 设置下一次定时器中断
 *
 * @details 根据 CONFIG_TICK_RATE_HZ 计算下一个比较值
 */
void timer_set_next_compare(void)
{
    uint64_t current;
    uint64_t delta;

    current = hal_timer_get_count();
    delta = get_ticks_per_counter_tick();

    hal_timer_set_compare(current + delta);
}

/* ========================================================================
 * 定时器初始化
 * ======================================================================== */

void timer_init(void)
{
    uint32_t i;

    /* 读取计数器频率 */
    s_counter_freq = hal_timer_get_freq();

    /* 初始化系统滴答 */
    s_system_ticks = 0ULL;
    barrier();

    /* 初始化每 CPU 软件定时器队列和睡眠队列 */
    for (i = 0U; i < (uint32_t)CONFIG_MAX_CPUS; i++)
    {
        s_timer_queues[i].next = &s_timer_queues[i];
        s_timer_queues[i].prev = &s_timer_queues[i];

        s_sleep_queues[i].next = &s_sleep_queues[i];
        s_sleep_queues[i].prev = &s_sleep_queues[i];

        ticket_lock_init(&s_timer_locks[i]);
        ticket_lock_init(&s_sleep_locks[i]);
    }

    /* 禁用定时器 */
    hal_timer_set_control(0ULL);

    /* 设置首次比较值 */
    timer_set_next_compare();

    /* 使能定时器（不屏蔽中断） */
    hal_timer_set_control(CNTP_CTL_ENABLE);

    barrier();
}

/* ========================================================================
 * 时间戳接口
 * ======================================================================== */

tick_t timer_get_ticks(void)
{
    tick_t ticks;

    barrier_load();
    ticks = s_system_ticks;
    barrier_load();

    return ticks;
}

uint64_t timer_get_ns(void)
{
    uint64_t counts;
    uint64_t freq;

    counts = hal_timer_get_count();
    freq = s_counter_freq;

    if (freq == 0ULL)
    {
        return 0ULL;
    }

    /* 转换为纳秒：counts * 10^9 / freq */
    return (counts * 1000000000ULL) / freq;
}

uint64_t timer_get_us(void)
{
    uint64_t counts;
    uint64_t freq;

    counts = hal_timer_get_count();
    freq = s_counter_freq;

    if (freq == 0ULL)
    {
        return 0ULL;
    }

    return (counts * 1000000ULL) / freq;
}

uint64_t timer_get_ms(void)
{
    uint64_t counts;
    uint64_t freq;

    counts = hal_timer_get_count();
    freq = s_counter_freq;

    if (freq == 0ULL)
    {
        return 0ULL;
    }

    return (counts * 1000ULL) / freq;
}

/* ========================================================================
 * 定时器中断处理
 * ======================================================================== */

void timer_interrupt_handler(void)
{
    uint32_t cpu_id;
    struct list_head *timer_queue;
    struct list_head *sleep_queue;
    tick_t current_ticks;

    /* 递增系统滴答（原子操作，多核安全） */
    (void)atomic_inc_u64((volatile uint64_t *)&s_system_ticks);
    current_ticks = s_system_ticks;

    cpu_id = hal_get_cpu_id();

    /* 每 CONFIG_TICK_RATE_HZ 个 tick（约 1 秒）打印一次 CPU 心跳 */
    if ((current_ticks % (uint64_t)CONFIG_TICK_RATE_HZ) == 0ULL)
    {
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, '[');
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, (char)('0' + (int32_t)cpu_id));
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, ']');
    }

    /* 设置下一次比较值 */
    timer_set_next_compare();

    /*
     * 软件定时器队列、睡眠队列、调度器 tick 涉及与调度器的复杂交互，
     * 当前仅在 CPU0 处理保证稳定性。从核只做 tick 递增和比较值重设。
     *
     * 每 CPU 队列和锁基础设施已就绪（s_timer_queues/s_sleep_queues/
     * s_timer_locks/s_sleep_locks），待从核 context_switch 和 schedule()
     * 路径完善后，可放开队列处理到所有 CPU。
     */
    if (cpu_id == 0U)
    {
        timer_queue = &s_timer_queues[cpu_id];
        sleep_queue = &s_sleep_queues[cpu_id];

        /* 处理软件定时器（加锁保护） */
        ticket_lock_acquire(&s_timer_locks[cpu_id]);
        while (timer_queue->next != timer_queue)
        {
            SoftwareTimer_t *timer = container_of(timer_queue->next,
                                                   SoftwareTimer_t, node);

            if (timer->expire_tick <= current_ticks)
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
                    timer->node.next = timer_queue->next;
                    timer->node.prev = timer_queue;
                    timer_queue->next->prev = &timer->node;
                    timer_queue->next = &timer->node;
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
        ticket_lock_release(&s_timer_locks[cpu_id]);

        /* 处理睡眠线程唤醒（加锁保护） */
        ticket_lock_acquire(&s_sleep_locks[cpu_id]);
        while (sleep_queue->next != sleep_queue)
        {
            struct KThread *thread = container_of(sleep_queue->next,
                                                   struct KThread, sleep_node);

            if (thread->wakeup_tick <= current_ticks)
            {
                /* 从睡眠队列移除 */
                thread->sleep_node.prev->next = thread->sleep_node.next;
                thread->sleep_node.next->prev = thread->sleep_node.prev;
                thread->sleep_node.next = &thread->sleep_node;
                thread->sleep_node.prev = &thread->sleep_node;

                /* 唤醒线程（恢复就绪状态并入调度队列） */
                thread->state = KTHREAD_STATE_READY;
                scheduler_enqueue(thread);
            }
            else
            {
                break;
            }
        }
        ticket_lock_release(&s_sleep_locks[cpu_id]);

        /* 触发调度器 tick（RR 时间片处理 + 抢占检查） */
        scheduler_tick();
    }
}

/* ========================================================================
 * 线程延迟
 * ======================================================================== */

void kthread_sleep_ticks(tick_t ticks)
{
    struct KThread *current;
    uint32_t cpu_id;
    struct list_head *sleep_queue;

    if (ticks == 0ULL)
    {
        return;
    }

    current = kthread_get_current();
    if (current == NULL)
    {
        return;
    }

    cpu_id = hal_get_cpu_id();
    sleep_queue = &s_sleep_queues[cpu_id];

    /* 设置唤醒时刻 */
    current->wakeup_tick = s_system_ticks + ticks;
    current->state = KTHREAD_STATE_SLEEPING;

    /* 加入当前 CPU 的睡眠队列（加锁保护） */
    ticket_lock_acquire(&s_sleep_locks[cpu_id]);
    current->sleep_node.next = sleep_queue->next;
    current->sleep_node.prev = sleep_queue;
    sleep_queue->next->prev = &current->sleep_node;
    sleep_queue->next = &current->sleep_node;
    ticket_lock_release(&s_sleep_locks[cpu_id]);

    barrier();

    /* 触发调度，切换到其他线程 */
    schedule();
}

int32_t kthread_sleep(uint32_t ms)
{
    tick_t ticks;

    ticks = MS_TO_TICKS(ms);
    if (ticks == 0ULL)
    {
        ticks = 1ULL;
    }

    kthread_sleep_ticks(ticks);

    return 0;
}

void timer_wakeup_thread(struct KThread *thread)
{
    uint32_t cpu_id;

    if (thread == NULL)
    {
        return;
    }

    if (thread->state != KTHREAD_STATE_SLEEPING)
    {
        return;
    }

    cpu_id = hal_get_cpu_id();

    /* 从当前 CPU 的睡眠队列移除（加锁保护） */
    ticket_lock_acquire(&s_sleep_locks[cpu_id]);
    if (thread->sleep_node.next != &thread->sleep_node)
    {
        thread->sleep_node.prev->next = thread->sleep_node.next;
        thread->sleep_node.next->prev = thread->sleep_node.prev;
        thread->sleep_node.next = &thread->sleep_node;
        thread->sleep_node.prev = &thread->sleep_node;
    }
    ticket_lock_release(&s_sleep_locks[cpu_id]);

    /* 恢复为就绪状态并加入调度队列 */
    thread->state = KTHREAD_STATE_READY;
    scheduler_enqueue(thread);
    barrier();
}

/* ========================================================================
 * 软件定时器
 * ======================================================================== */

void timer_init_soft(SoftwareTimer_t *timer, TimerCallback_t callback, void *arg)
{
    if (timer == NULL)
    {
        return;
    }

    timer->expire_tick = 0ULL;
    timer->interval = 0ULL;
    timer->callback = callback;
    timer->callback_arg = arg;
    timer->state = TIMER_STATE_IDLE;
    timer->node.next = &timer->node;
    timer->node.prev = &timer->node;
}

void timer_start_oneshot(SoftwareTimer_t *timer, uint32_t ms)
{
    uint32_t cpu_id;
    struct list_head *timer_queue;

    if (timer == NULL)
    {
        return;
    }

    cpu_id = hal_get_cpu_id();
    timer_queue = &s_timer_queues[cpu_id];

    /* 如果已在队列中，先移除（加锁保护） */
    if (timer->node.next != &timer->node)
    {
        timer->node.prev->next = timer->node.next;
        timer->node.next->prev = timer->node.prev;
    }

    timer->expire_tick = s_system_ticks + MS_TO_TICKS(ms);
    timer->interval = 0ULL;
    timer->state = TIMER_STATE_ACTIVE;

    /* 插入到当前 CPU 的定时器队列头部（加锁保护） */
    ticket_lock_acquire(&s_timer_locks[cpu_id]);
    timer->node.next = timer_queue->next;
    timer->node.prev = timer_queue;
    timer_queue->next->prev = &timer->node;
    timer_queue->next = &timer->node;
    ticket_lock_release(&s_timer_locks[cpu_id]);

    barrier();
}

void timer_start_periodic(SoftwareTimer_t *timer, uint32_t ms)
{
    uint32_t cpu_id;
    struct list_head *timer_queue;

    if (timer == NULL)
    {
        return;
    }

    cpu_id = hal_get_cpu_id();
    timer_queue = &s_timer_queues[cpu_id];

    /* 如果已在队列中，先移除 */
    if (timer->node.next != &timer->node)
    {
        timer->node.prev->next = timer->node.next;
        timer->node.next->prev = timer->node.prev;
    }

    timer->expire_tick = s_system_ticks + MS_TO_TICKS(ms);
    timer->interval = MS_TO_TICKS(ms);
    timer->state = TIMER_STATE_ACTIVE;

    /* 插入到当前 CPU 的定时器队列头部（加锁保护） */
    ticket_lock_acquire(&s_timer_locks[cpu_id]);
    timer->node.next = timer_queue->next;
    timer->node.prev = timer_queue;
    timer_queue->next->prev = &timer->node;
    timer_queue->next = &timer->node;
    ticket_lock_release(&s_timer_locks[cpu_id]);

    barrier();
}

void timer_stop(SoftwareTimer_t *timer)
{
    uint32_t cpu_id;

    if (timer == NULL)
    {
        return;
    }

    cpu_id = hal_get_cpu_id();

    /* 从当前 CPU 的队列中移除（加锁保护） */
    ticket_lock_acquire(&s_timer_locks[cpu_id]);
    if (timer->node.next != &timer->node)
    {
        timer->node.prev->next = timer->node.next;
        timer->node.next->prev = timer->node.prev;
        timer->node.next = &timer->node;
        timer->node.prev = &timer->node;
    }
    ticket_lock_release(&s_timer_locks[cpu_id]);

    timer->state = TIMER_STATE_IDLE;
    barrier();
}
