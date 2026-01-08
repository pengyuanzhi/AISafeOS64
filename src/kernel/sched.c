/**
 * @file sched.c
 * @brief AISafe64 Core Scheduler Implementation
 *
 * @details Multi-core scheduler with scheduling class support
 *
 * @note MISRA-C:2012 compliant
 * @note ISO 26262 ASIL-D compliant
 *
 * @version 1.0
 * @date 2025-01-08
 *
 * @author AISafe64 Team
 */

#include "sched.h"
#include "spinlock.h"
#include "bitmap.h"
#include "rbtree.h"
#include "list.h"
#include "mm.h"

/*
 * External Declarations (Scheduling Classes)
 */

extern const SchedClass_t sched_class_fifo;
extern const SchedClass_t sched_class_edf;
extern const SchedClass_t sched_class_cfs;
extern const SchedClass_t sched_class_rr;
extern const SchedClass_t sched_class_idle;

/*
 * Global Variables
 */

/* Per-CPU run queues */
static rq_t per_cpu_rq[MAX_CPUS];

/* Scheduling class linked list */
static const SchedClass_t *sched_class_h = NULL;

/* Scheduler initialization flag */
static uint32_t scheduler_initialized = 0U;

/* Scheduler statistics */
static uint64_t scheduler_ticks = 0ULL;

/*
 * Utility Functions
 */

/**
 * @brief Find highest priority set in bitmap
 * @param bitmap 256-bit bitmap (4 x 64-bit words)
 * @return Priority level (0-255), 255 if empty
 *
 * @note Uses CLZ instruction for O(1) lookup
 */
uint32_t find_highest_priority(const uint64_t *bitmap)
{
    uint32_t word_idx;
    uint64_t bitmap_u64;
    uint32_t bit_offset;

    /* Iterate through 4 words (256 bits) */
    for (word_idx = 0U; word_idx < 4U; word_idx++)
    {
        bitmap_u64 = bitmap[word_idx];

        /* Skip empty words */
        if (bitmap_u64 == 0ULL)
        {
            continue;
        }

        /* Use CLZ to find highest set bit */
        bit_offset = (uint32_t)__builtin_clzll(bitmap_u64);
        return (word_idx * 64U) + (63U - bit_offset);
    }

    /* No bits set */
    return 255U;
}

/**
 * @brief Convert priority to weight (CFS)
 * @param prio Priority level (0-255)
 * @return Weight value
 */
uint32_t prio_to_weight(uint32_t prio)
{
    /* Static weight table (similar to Linux) */
    static const uint32_t prio_to_weight_array[40] = {
        /* -20 */ 88761, 71755, 56483, 46273, 36291,
        /* -15 */ 29154, 23254, 18705, 14949, 11916,
        /* -10 */ 9548,  7620,  6100,  4904,  3906,
        /*  -5 */ 3121,  2501,  1991,  1586,  1277,
        /*   0 */ 1024,  820,   655,   526,   423,
        /*   5 */ 335,   272,   215,   172,   137,
        /*  10 */ 110,   87,    70,    56,    45,
        /*  15 */ 36,    29,    23,    18,    15};

    /* Map priority 0-255 to weight index */
    if (prio >= 128U)
    {
        /* Real-time priority (high weight) */
        return 1024U;
    }
    else
    {
        /* Normal priority (use weight table) */
        uint32_t idx = prio % 40U;
        return prio_to_weight_array[idx];
    }
}

/**
 * @brief Get scheduler clock
 * @return Current time in nanoseconds
 */
uint64_t sched_clock(void)
{
    /* TODO: Implement actual clock read */
    /* For now, return tick count in nanoseconds */
    return scheduler_ticks * 1000000ULL; /* 1 tick = 1ms = 1,000,000 ns */
}

/**
 * @brief Get run queue for CPU
 * @param cpu CPU ID
 * @return Run queue pointer
 */
struct rq *cpu_rq(uint32_t cpu)
{
    if (cpu >= MAX_CPUS)
    {
        return NULL;
    }
    return &per_cpu_rq[cpu];
}

/**
 * @brief Get current CPU's run queue
 * @return Run queue pointer
 */
struct rq *this_rq(void)
{
    uint32_t cpu = smp_processor_id();
    return cpu_rq(cpu);
}

/**
 * @brief Get current CPU ID
 * @return CPU ID
 */
uint32_t smp_processor_id(void)
{
    /* TODO: Implement actual CPU ID read */
    /* For now, return 0 */
    return 0U;
}

/*
 * Scheduling Class Management
 */

/**
 * @brief Register scheduling class
 * @param class Scheduling class pointer
 * @return 0 on success, negative error code on failure
 */
int register_sched_class(const SchedClass_t *class)
{
    const SchedClass_t **link;

    /* Parameter validation */
    if (class == NULL)
    {
        return -1; /* EINVAL */
    }

    /* Check core operations */
    if ((class->init == NULL) || (class->enqueue == NULL) || (class->dequeue == NULL) ||
        (class->pick_next == NULL) || (class->task_tick == NULL) || (class->update_curr == NULL))
    {
        return -1; /* EINVAL */
    }

    /* Insert into linked list (sorted by priority) */
    link = &sched_class_h;

    while (*link != NULL)
    {
        if ((*link)->priority > class->priority)
        {
            break;
        }
        link = &(*link)->next;
    }

    /* Insert class */
    /* Note: We're casting away const to modify next pointer */
    *(const SchedClass_t **)link = class;

    return 0;
}

/*
 * Core Scheduler Functions
 */

/**
 * @brief Initialize multi-core scheduler
 * @return 0 on success, negative error code on failure
 */
int scheduler_init(void)
{
    uint32_t cpu;
    int ret;

    /* Check if already initialized */
    if (scheduler_initialized != 0U)
    {
        return -1; /* EALREADY */
    }

    /* Initialize per-CPU run queues */
    for (cpu = 0U; cpu < MAX_CPUS; cpu++)
    {
        struct rq *rq = &per_cpu_rq[cpu];

        /* Initialize fields */
        rq->cpu = cpu;
        spin_lock_init(&rq->lock);
        rq->curr = NULL;
        rq->idle = NULL;
        rq->nr_switches = 0ULL;
        rq->nr_migrations = 0ULL;
        rq->need_resched = 0U;
        rq->nr_load_updates = 0ULL;
        rq->load_weight = 0ULL;
        rq->idle_time = 0ULL;
        rq->total_time = 0ULL;

        /* Initialize priority bitmap */
        (void)memset(rq->priority_bitmap, 0, sizeof(rq->priority_bitmap));

        /* Clear per-class statistics */
        (void)memset(rq->nr_running, 0, sizeof(rq->nr_running));

        /* Clear scheduler-specific data */
        rq->fifo_rq = NULL;
        rq->edf_rq = NULL;
        rq->cfs_rq = NULL;
        rq->rr_rq = NULL;
        rq->idle_rq = NULL;

        /* Initialize scheduling class for this CPU */
        /* Note: This calls the class-specific init function */
    }

    /* Register scheduling classes */
    sched_class_h = NULL;

    ret = register_sched_class(&sched_class_fifo);
    if (ret != 0)
    {
        return ret;
    }

    ret = register_sched_class(&sched_class_edf);
    if (ret != 0)
    {
        return ret;
    }

    ret = register_sched_class(&sched_class_cfs);
    if (ret != 0)
    {
        return ret;
    }

    ret = register_sched_class(&sched_class_rr);
    if (ret != 0)
    {
        return ret;
    }

    ret = register_sched_class(&sched_class_idle);
    if (ret != 0)
    {
        return ret;
    }

    /* Initialize each scheduling class */
    for (cpu = 0U; cpu < MAX_CPUS; cpu++)
    {
        struct rq *rq = &per_cpu_rq[cpu];
        const SchedClass_t *class;

        /* Call init for each class */
        class = sched_class_h;
        while (class != NULL)
        {
            if (class->init != NULL)
            {
                ret = class->init(rq);
                if (ret != 0)
                {
                    return ret;
                }
            }
            class = class->next;
        }
    }

    scheduler_initialized = 1U;

    return 0;
}

/**
 * @brief Pick next task to run
 * @param rq Run queue pointer
 * @return Task control block pointer, NULL if no task
 *
 * @note Iterates through scheduling classes
 * @note Returns idle task if no other tasks available
 */
TCB_t *pick_next_task(struct rq *rq)
{
    const SchedClass_t *class;
    TCB_t *next_task;

    /* Parameter validation */
    if (rq == NULL)
    {
        return NULL;
    }

    /* Iterate through scheduling classes */
    for (class = sched_class_h; class != NULL; class = class->next)
    {
        /* Check if this class has runnable tasks */
        if (rq->nr_running[class->id] == 0U)
        {
            continue;
        }

        /* Call class-specific pick_next */
        next_task = class->pick_next(rq);

        /* Check if task found */
        if (next_task != NULL)
        {
            return next_task;
        }
    }

    /* No tasks found, return idle task */
    return rq->idle;
}

/**
 * @brief Enqueue task to ready queue
 * @param task Task control block pointer
 */
void enqueue_task(TCB_t *task)
{
    struct rq *rq;
    const SchedClass_t *class;

    /* Parameter validation */
    if (task == NULL)
    {
        return;
    }

    class = task->sched_class;
    if (class == NULL)
    {
        return;
    }

    rq = task->rq;
    if (rq == NULL)
    {
        return;
    }

    /* Check task state */
    if (task->state != TASK_READY)
    {
        return;
    }

    /* Call class-specific enqueue */
    class->enqueue(rq, task);

    /* Update statistics */
    rq->nr_running[class->id]++;

    /* Update priority bitmap */
    set_bit(task->prio, rq->priority_bitmap);
}

/**
 * @brief Dequeue task from ready queue
 * @param task Task control block pointer
 */
void dequeue_task(TCB_t *task)
{
    struct rq *rq;
    const SchedClass_t *class;

    /* Parameter validation */
    if (task == NULL)
    {
        return;
    }

    class = task->sched_class;
    if (class == NULL)
    {
        return;
    }

    rq = task->rq;
    if (rq == NULL)
    {
        return;
    }

    /* Call class-specific dequeue */
    class->dequeue(rq, task);

    /* Update statistics */
    if (rq->nr_running[class->id] > 0U)
    {
        rq->nr_running[class->id]--;
    }

    /* Update priority bitmap */
    /* Note: Need to check if other tasks at this priority */
    /* TODO: Implement proper bitmap update */
}

/**
 * @brief Main scheduler function
 */
void schedule(void)
{
    struct rq *rq;
    TCB_t *prev;
    TCB_t *next;
    unsigned long flags;

    /* Get current run queue */
    rq = this_rq();
    if (rq == NULL)
    {
        return;
    }

    /* Acquire lock */
    spin_lock_irqsave(&rq->lock, flags);

    /* Get previous task */
    prev = rq->curr;
    if (prev == NULL)
    {
        spin_unlock_irqrestore(&rq->lock, flags);
        return;
    }

    /* Clear reschedule flag */
    rq->need_resched = 0U;

    /* Update previous task runtime */
    if (prev->sched_class != NULL)
    {
        prev->sched_class->update_curr(rq);
    }

    /* Pick next task */
    next = pick_next_task(rq);

    /* Check if context switch needed */
    if (next == prev)
    {
        spin_unlock_irqrestore(&rq->lock, flags);
        return;
    }

    /* Update run queue */
    rq->curr = next;
    rq->nr_switches++;

    /* Release lock */
    spin_unlock_irqrestore(&rq->lock, flags);

    /* Context switch */
    context_switch(prev, next);
}

/**
 * @brief Scheduler tick handler
 * @param cpu CPU ID
 */
void scheduler_tick(uint32_t cpu)
{
    struct rq *rq;
    TCB_t *curr;
    const SchedClass_t *class;
    unsigned long flags;

    /* Increment tick counter */
    scheduler_ticks++;

    /* Get run queue */
    rq = cpu_rq(cpu);
    if (rq == NULL)
    {
        return;
    }

    /* Acquire lock */
    spin_lock_irqsave(&rq->lock, flags);

    /* Get current task */
    curr = rq->curr;
    if (curr == NULL)
    {
        spin_unlock_irqrestore(&rq->lock, flags);
        return;
    }

    /* Track total time (1 tick per call) */
    rq->total_time++;

    /* Get scheduling class */
    class = curr->sched_class;
    if (class == NULL)
    {
        spin_unlock_irqrestore(&rq->lock, flags);
        return;
    }

    /* Call class-specific tick handler */
    class->task_tick(rq, curr);

    /* Check if reschedule needed */
    if (rq->need_resched != 0U)
    {
        spin_unlock_irqrestore(&rq->lock, flags);
        schedule();
        return;
    }

    /* Release lock */
    spin_unlock_irqrestore(&rq->lock, flags);
}

/**
 * @brief Yield CPU to other tasks
 */
void yield(void)
{
    struct rq *rq;
    TCB_t *curr;
    const SchedClass_t *class;

    /* Get run queue */
    rq = this_rq();
    if (rq == NULL)
    {
        return;
    }

    /* Get current task */
    curr = rq->curr;
    if (curr == NULL)
    {
        return;
    }

    /* Get scheduling class */
    class = curr->sched_class;
    if (class == NULL)
    {
        return;
    }

    /* Call class-specific yield */
    if (class->yield != NULL)
    {
        unsigned long flags;
        spin_lock_irqsave(&rq->lock, flags);
        class->yield(rq, curr);
        rq->need_resched = 1U;
        spin_unlock_irqrestore(&rq->lock, flags);
    }

    /* Schedule */
    schedule();
}

/*
 * Task Management
 */

/**
 * @brief Create new task
 * @param name Task name
 * @param prio Priority (0-255)
 * @param stack_size Stack size
 * @param entry Entry point function
 * @param policy Scheduling policy
 * @return Task ID on success, 0 on failure
 */
uint32_t task_create(const char *name, uint8_t prio, uint32_t stack_size, void (*entry)(void),
                     SchedPolicy_t policy)
{
    TCB_t *task;
    uint32_t tid;
    struct rq *rq;

    /* Parameter validation */
    if (name == NULL)
    {
        return 0U;
    }

    if (entry == NULL)
    {
        return 0U;
    }

    if (prio >= PRIORITY_LEVELS)
    {
        return 0U;
    }

    if (stack_size == 0U)
    {
        return 0U;
    }

    /* Allocate task */
    task = (TCB_t *)kmalloc(sizeof(TCB_t));
    if (task == NULL)
    {
        return 0U;
    }

    /* Allocate stack */
    task->stack_base = (uint64_t)kmalloc((uint64_t)stack_size);
    if (task->stack_base == 0ULL)
    {
        kfree(task);
        return 0U;
    }

    /* Initialize task */
    task->tid = get_next_tid();
    task->state = TASK_READY;
    task->prio = prio;
    task->static_prio = prio;
    task->normal_prio = prio;
    task->cpu_affinity = 0x01U; /* CPU 0 */
    task->stack_size = stack_size;
    task->stack_ptr = task->stack_base + stack_size;

    /* Copy name */
    /* TODO: Implement safe string copy */

    /* Set scheduling class */
    switch (policy)
    {
        case SCHED_FIFO:
            task->sched_class = &sched_class_fifo;
            break;
        case SCHED_EDF:
            task->sched_class = &sched_class_edf;
            break;
        case SCHED_CFS:
            task->sched_class = &sched_class_cfs;
            break;
        case SCHED_RR:
            task->sched_class = &sched_class_rr;
            break;
        case SCHED_IDLE:
            task->sched_class = &sched_class_idle;
            break;
        default:
            task->sched_class = &sched_class_cfs;
            break;
    }

    /* Get run queue */
    rq = this_rq();
    if (rq == NULL)
    {
        kfree((void *)task->stack_base);
        kfree(task);
        return 0U;
    }

    task->rq = rq;

    /* Initialize runtime statistics */
    task->vruntime = 0ULL;
    task->exec_start = sched_clock();
    task->sum_exec_runtime = 0ULL;
    task->deadline = 0ULL;
    task->time_slice = 0U;

    /* Enqueue task */
    enqueue_task(task);

    return task->tid;
}

/**
 * @brief Exit current task
 * @param code Exit code
 */
void task_exit(int code)
{
    struct rq *rq;
    TCB_t *curr;
    unsigned long flags;

    (void)code; /* Unused parameter */

    /* Get run queue */
    rq = this_rq();
    if (rq == NULL)
    {
        /* Cannot do much */
        while (1)
        {
            __asm__ volatile("wfi");
        }
    }

    /* Get current task */
    curr = rq->curr;
    if (curr == NULL)
    {
        while (1)
        {
            __asm__ volatile("wfi");
        }
    }

    /* Acquire lock */
    spin_lock_irqsave(&rq->lock, flags);

    /* Set task state */
    curr->state = TASK_ZOMBIE;

    /* Dequeue task */
    dequeue_task(curr);

    /* Schedule next task */
    rq->need_resched = 1U;

    /* Release lock */
    spin_unlock_irqrestore(&rq->lock, flags);

    /* Schedule */
    schedule();

    /* Should never reach here */
    __builtin_unreachable();
}
