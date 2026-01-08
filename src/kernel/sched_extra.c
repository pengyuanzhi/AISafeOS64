/**
 * @file sched_extra.c
 * @brief AISafe64 RTOS - 调度器补充实现
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 补充调度器核心功能
 *          - scheduler_start()
 *          - 任务管理补充函数
 *          - 辅助函数
 *          - 睡眠任务管理
 *
 * @note MISRA-C:2012合规
 */

#include "sched.h"
#include "printk.h"
#include "mm.h"
#include "time.h"
#include <string.h>

/**
 * @brief 睡眠任务节点
 */
typedef struct sleep_task
{
    TCB_t *task;                  /**< 任务TCB指针 */
    uint64_t wakeup_ticks;         /**< 唤醒时间（滴答数） */
    struct sleep_task *next;       /**< 下一个睡眠任务 */
} sleep_task_t;

/**
 * @brief 睡眠任务链表头
 * @details 按唤醒时间排序（升序）
 */
static sleep_task_t *g_sleep_queue = NULL;

/**
 * @brief 任务ID计数器
 */
static uint32_t g_tid_counter = 1U;

/**
 * @brief 获取下一个任务ID
 * @return 任务ID
 */
uint32_t get_next_tid(void) {
    uint32_t tid = __sync_fetch_and_add(&g_tid_counter, 1U);
    return tid;
}

/**
 * @brief 外部汇编函数
 */
extern void context_switch_asm(TCB_t *prev, TCB_t *next);
extern void arch_setup_task_context(TCB_t *task, void (*entry)(void),
                                    void *arg, uint64_t stack_top);
extern void cpu_switch_to_first_task(TCB_t *task);

/**
 * @brief 上下文切换（C包装）
 */
void context_switch(TCB_t *prev, TCB_t *next) {
    if (prev == next) {
        return;
    }

    /* 调用汇编实现的上下文切换 */
    context_switch_asm(prev, next);
}

/**
 * @brief 启动调度器
 * @note 不返回
 */
void scheduler_start(void)
{
    struct rq *rq;
    TCB_t *first;

    /* 获取当前CPU的运行队列 */
    rq = this_rq();
    if (rq == NULL)
    {
        printk("[FATAL] Run queue not initialized!\n");
        goto kernel_panic;
    }

    /* 检查是否有空闲任务 */
    if (rq->idle == NULL)
    {
        printk("[FATAL] Idle task not created!\n");
        goto kernel_panic;
    }

    /* 检查是否有其他任务 */
    first = pick_next_task(rq);
    if (first == NULL)
    {
        printk("[FATAL] No tasks to run!\n");
        goto kernel_panic;
    }

    /* 设置当前任务 */
    rq->curr = first;

    /* 设置任务状态为运行中 */
    first->state = TASK_RUNNING;

    printk("[SCHED] Starting scheduler with task %u (prio=%u)\n",
           first->tid, first->prio);

    /* 首次上下文切换 */
    /* 这是启动调度器后的第一次切换，不需要保存当前上下文 */
    cpu_switch_to_first_task(first);

    /* 永不返回 */

kernel_panic:
    printk("[FATAL] Scheduler failed to start!\n");
    while (1)
    {
        __asm__ volatile("wfi");
    }
}

/**
 * @brief 唤醒阻塞的任务
 * @param task 任务TCB指针
 * @return 成功返回0，失败返回负错误码
 */
int wake_up_task(TCB_t *task) {
    struct rq *rq;
    unsigned long flags;

    if (task == NULL) {
        return -1;  /* EINVAL */
    }

    rq = task->rq;
    if (rq == NULL) {
        return -1;
    }

    /* 获取锁 */
    spin_lock_irqsave(&rq->lock, flags);

    /* 检查任务状态 */
    if (task->state == TASK_BLOCKED || task->state == TASK_SLEEPING) {
        /* 设置为就绪态 */
        task->state = TASK_READY;

        /* 加入运行队列 */
        enqueue_task(task);

        /* 设置调度标志 */
        rq->need_resched = 1U;
    }

    /* 释放锁 */
    spin_unlock_irqrestore(&rq->lock, flags);

    return 0;
}

/**
 * @brief 获取当前任务
 * @return 当前任务TCB指针，失败返回NULL
 */
TCB_t *get_current_task(void)
{
    struct rq *rq;

    rq = this_rq();
    if (rq == NULL)
    {
        return NULL;
    }

    return rq->curr;
}

/**
 * @brief 任务唤醒定时器回调
 * @param task 任务TCB指针
 *
 * @details 由软件定时器调用，唤醒睡眠的任务
 */
void task_wakeup_timer(TCB_t *task)
{
    struct rq *rq;
    unsigned long flags;

    if (task == NULL)
    {
        return;
    }

    rq = task->rq;
    if (rq == NULL)
    {
        return;
    }

    /* 获取锁 */
    spin_lock_irqsave(&rq->lock, flags);

    /* 检查任务状态 */
    if (task->state == TASK_SLEEPING)
    {
        /* 设置为就绪态 */
        task->state = TASK_READY;

        /* 加入运行队列 */
        enqueue_task(task);

        /* 设置调度标志 */
        rq->need_resched = 1U;
    }

    /* 释放锁 */
    spin_unlock_irqrestore(&rq->lock, flags);
}

/**
 * @brief 定时器回调包装结构
 */
typedef struct
{
    TCB_t *task;
    swtimer_t timer;
} sleep_timer_wrapper_t;

/**
 * @brief 定时器回调函数
 * @param arg 参数（sleep_timer_wrapper_t指针）
 */
static void sleep_timer_callback(void *arg)
{
    sleep_timer_wrapper_t *wrapper = (sleep_timer_wrapper_t *)arg;

    if (wrapper != NULL)
    {
        /* 唤醒任务 */
        task_wakeup_timer(wrapper->task);

        /* 释放包装器 */
        kfree(wrapper);
    }
}

/**
 * @brief 将任务加入睡眠队列（内部函数）
 * @param task 任务TCB指针
 * @param wakeup_ticks 唤醒时间（滴答数）
 * @return 成功返回0，失败返回负错误码
 */
static int sleep_enqueue(TCB_t *task, uint64_t wakeup_ticks)
{
    sleep_task_t *node;

    if (task == NULL)
    {
        return -ERROR_INVALID_PARAM;
    }

    /* 分配睡眠节点 */
    node = (sleep_task_t *)kmalloc(sizeof(sleep_task_t));
    if (node == NULL)
    {
        return -ERROR_OUT_OF_MEMORY;
    }

    /* 初始化节点 */
    node->task = task;
    node->wakeup_ticks = wakeup_ticks;
    node->next = NULL;

    /* 插入睡眠队列（按唤醒时间排序） */
    if ((g_sleep_queue == NULL) || (wakeup_ticks < g_sleep_queue->wakeup_ticks))
    {
        /* 插入队首 */
        node->next = g_sleep_queue;
        g_sleep_queue = node;
    }
    else
    {
        /* 查找插入位置 */
        sleep_task_t *current = g_sleep_queue;
        while ((current->next != NULL) &&
               (current->next->wakeup_ticks <= wakeup_ticks))
        {
            current = current->next;
        }

        /* 插入节点 */
        node->next = current->next;
        current->next = node;
    }

    return ERROR_SUCCESS;
}

/**
 * @brief 任务睡眠
 * @param ms 睡眠时间（毫秒）
 * @return 成功返回0，失败返回负错误码
 *
 * @details 阻塞当前任务指定时间
 *          - 设置任务状态为TASK_SLEEPING
 *          - 创建定时器在指定时间后唤醒任务
 *          - 触发调度器切换到其他任务
 */
int task_sleep(uint64_t ms)
{
    struct rq *rq;
    TCB_t *current;
    unsigned long flags;
    sleep_timer_wrapper_t *wrapper;
    uint64_t wakeup_ticks;
    int ret;

    if (ms == 0UL)
    {
        /* 睡眠时间为0，直接yield */
        yield();
        return ERROR_SUCCESS;
    }

    /* 获取当前任务 */
    rq = this_rq();
    if (rq == NULL)
    {
        return -ERROR_INVALID_STATE;
    }

    /* 获取锁 */
    spin_lock_irqsave(&rq->lock, flags);

    current = rq->curr;
    if (current == NULL)
    {
        spin_unlock_irqrestore(&rq->lock, flags);
        return -ERROR_INVALID_STATE;
    }

    /* 计算唤醒时间 */
    wakeup_ticks = g_jiffies + MSEC_TO_TICKS(ms);

    /* 分配定时器包装器 */
    wrapper = (sleep_timer_wrapper_t *)kmalloc(sizeof(sleep_timer_wrapper_t));
    if (wrapper == NULL)
    {
        spin_unlock_irqrestore(&rq->lock, flags);
        return -ERROR_OUT_OF_MEMORY;
    }

    wrapper->task = current;

    /* 初始化软件定时器 */
    swtimer_init(&wrapper->timer, sleep_timer_callback, wrapper);

    /* 启动定时器 */
    ret = swtimer_start(&wrapper->timer, ms);
    if (ret != ERROR_SUCCESS)
    {
        kfree(wrapper);
        spin_unlock_irqrestore(&rq->lock, flags);
        return ret;
    }

    /* 从运行队列移除 */
    if (current->state == TASK_RUNNING)
    {
        dequeue_task(current);
    }

    /* 设置任务状态为睡眠 */
    current->state = TASK_SLEEPING;

    /* 释放锁 */
    spin_unlock_irqrestore(&rq->lock, flags);

    /* 触发调度 */
    schedule();

    return ERROR_SUCCESS;
}

/**
 * @brief 检查并唤醒睡眠到期的任务（由timer_tick调用）
 */
void check_sleeping_tasks(void)
{
    unsigned long flags;
    struct rq *rq;
    sleep_task_t *current;
    sleep_task_t *prev;
    sleep_task_t *next;

    (void)flags; /* 防止未使用警告 */

    /* 获取当前CPU的运行队列 */
    rq = this_rq();
    if (rq == NULL)
    {
        return;
    }

    /* 遍历睡眠队列 */
    current = g_sleep_queue;
    prev = NULL;

    while (current != NULL)
    {
        /* 检查是否到达唤醒时间 */
        if (current->wakeup_ticks > g_jiffies)
        {
            /* 队列按时间排序，后续任务都未到期 */
            break;
        }

        /* 保存下一个节点 */
        next = current->next;

        /* 唤醒任务 */
        task_wakeup_timer(current->task);

        /* 从队列中移除 */
        if (prev == NULL)
        {
            g_sleep_queue = next;
        }
        else
        {
            prev->next = next;
        }

        /* 释放节点 */
        kfree(current);

        /* 继续处理下一个 */
        current = next;
    }
}

/**
 * @brief 设置任务优先级
 * @param task 任务TCB指针
 * @param prio 新优先级
 * @return 成功返回0，失败返回负错误码
 */
int set_task_priority(TCB_t *task, uint8_t prio) {
    struct rq *rq;
    unsigned long flags;
    bool need_resched;

    if (task == NULL) {
        return -1;  /* EINVAL */
    }

    if (prio >= PRIORITY_LEVELS) {
        return -1;
    }

    rq = task->rq;
    if (rq == NULL) {
        return -1;
    }

    /* 获取锁 */
    spin_lock_irqsave(&rq->lock, flags);

    /* 检查是否需要重新调度 */
    need_resched = false;

    if (task->state == TASK_READY || task->state == TASK_RUNNING) {
        /* 从运行队列移除 */
        dequeue_task(task);

        /* 更新优先级 */
        task->prio = prio;
        task->normal_prio = prio;

        /* 重新加入运行队列 */
        enqueue_task(task);

        /* 如果优先级提高，需要重新调度 */
        if (prio < task->static_prio) {
            need_resched = true;
        }
    } else {
        /* 任务不在运行队列，直接更新优先级 */
        task->prio = prio;
        task->normal_prio = prio;
    }

    /* 释放锁 */
    spin_unlock_irqrestore(&rq->lock, flags);

    /* 触发调度 */
    if (need_resched) {
        schedule();
    }

    return 0;
}

/**
 * @brief 获取调度器统计信息
 * @param cpu CPU ID
 * @param stats 输出：统计信息
 * @return 成功返回0，失败返回负错误码
 */
int sched_get_stats(uint32_t cpu, SchedStats_t *stats) {
    struct rq *rq;

    if (cpu >= MAX_CPUS) {
        return -1;  /* EINVAL */
    }

    if (stats == NULL) {
        return -1;
    }

    rq = cpu_rq(cpu);
    if (rq == NULL) {
        return -1;
    }

    /* 复制统计信息 */
    stats->nr_switches = rq->nr_switches;
    stats->nr_migrations = rq->nr_migrations;
    stats->load_weight = rq->load_weight;
    stats->nr_running = 0U;

    for (uint32_t i = 0U; i < 5U; i++) {
        stats->nr_running += rq->nr_running[i];
    }

    /* 计算平均运行时间 */
    /* TODO: 实现平均运行时间计算 */
    stats->avg_runtime = 0ULL;

    return 0;
}

/**
 * @brief 触发负载均衡
 * @param cpu CPU ID
 */
void load_balance(uint32_t cpu) {
    (void)cpu;

    /* TODO: 实现负载均衡 */
    /* 当前为单核系统，不需要负载均衡 */
}

/**
 * @brief 迁移任务到不同CPU
 * @param task 任务TCB指针
 * @param dest_cpu 目标CPU ID
 * @return 成功返回0，失败返回负错误码
 */
int migrate_task(TCB_t *task, uint32_t dest_cpu) {
    (void)task;
    (void)dest_cpu;

    /* TODO: 实现任务迁移 */
    /* 当前为单核系统，不支持迁移 */
    return -1;  /* ENOTSUP */
}

/**
 * @brief 创建空闲任务
 * @return 任务TCB指针，失败返回NULL
 */
TCB_t *create_idle_task(void) {
    TCB_t *task;
    uint32_t stack_size = 4096U;  /* 4KB栈 */

    /* 分配TCB */
    task = (TCB_t *)kmalloc(sizeof(TCB_t));
    if (task == NULL) {
        return NULL;
    }

    /* 分配栈 */
    task->stack_base = (uint64_t)kmalloc(stack_size);
    if (task->stack_base == 0ULL) {
        kfree(task);
        return NULL;
    }

    /* 初始化任务 */
    task->tid = 0U;  /* 空闲任务ID为0 */
    task->state = TASK_READY;
    task->prio = 255U;  /* 最低优先级 */
    task->static_prio = 255U;
    task->normal_prio = 255U;
    task->cpu_affinity = 0x01U;
    task->stack_size = stack_size;
    task->stack_ptr = task->stack_base + stack_size;

    /* 设置名称 */
    (void)memset(task->name, 0, sizeof(task->name));
    (void)memcpy(task->name, "idle", 5);

    /* 设置为IDLE调度类 */
    task->sched_class = &sched_class_idle;

    /* 设置运行队列 */
    task->rq = this_rq();

    /* 初始化统计信息 */
    task->vruntime = 0ULL;
    task->exec_start = sched_clock();
    task->sum_exec_runtime = 0ULL;
    task->deadline = 0ULL;
    task->time_slice = 0U;

    /* 初始化链表 */
    INIT_LIST_HEAD(&task->tasks);
    INIT_LIST_HEAD(&task->rq_list);
    INIT_LIST_HEAD(&task->run_list);

    return task;
}

/**
 * @brief 初始化空闲任务
 * @return 成功返回0，失败返回负错误码
 */
int idle_task_init(void) {
    struct rq *rq;
    TCB_t *idle;

    /* 获取运行队列 */
    rq = this_rq();
    if (rq == NULL) {
        return -1;
    }

    /* 创建空闲任务 */
    idle = create_idle_task();
    if (idle == NULL) {
        return -1;
    }

    /* 设置为运行队列的空闲任务 */
    rq->idle = idle;
    rq->curr = idle;

    printk("[SCHED] Idle task created\n");

    return 0;
}

/**
 * @brief 空闲任务入口
 * @param arg 参数
 */
void idle_task_entry(void *arg) {
    (void)arg;

    while (1) {
        /* 等待中断 */
        __asm__ volatile("wfi");

        /* TODO: 统计空闲时间 */
        /* TODO: 功耗管理 */
    }
}
