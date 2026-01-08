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
 *
 * @note MISRA-C:2012合规
 */

#include "sched.h"
#include "printk.h"
#include "mm.h"
#include <string.h>

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
void scheduler_start(void) {
    struct rq *rq;
    TCB_t *first;

    /* 获取当前CPU的运行队列 */
    rq = this_rq();
    if (rq == NULL) {
        printk("[FATAL] Run queue not initialized!\n");
        goto kernel_panic;
    }

    /* 检查是否有空闲任务 */
    if (rq->idle == NULL) {
        printk("[FATAL] Idle task not created!\n");
        goto kernel_panic;
    }

    /* 检查是否有其他任务 */
    first = pick_next_task(rq);
    if (first == NULL) {
        printk("[FATAL] No tasks to run!\n");
        goto kernel_panic;
    }

    /* 设置当前任务 */
    rq->curr = first;

    printk("[SCHED] Starting scheduler with task %u\n", first->tid);

    /* 跳转到第一个任务（永不返回） */
    /* TODO: 实现首次上下文切换 */
    /* 临时解决方案：直接跳转到空闲任务 */
    if (first->sched_class != NULL &&
        first->sched_class->id == 4U) {  /* IDLE class */
        /* 空闲任务 */
        while (1) {
            __asm__ volatile("wfi");
        }
    }

kernel_panic:
    printk("[FATAL] Scheduler failed to start!\n");
    while (1) {
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
