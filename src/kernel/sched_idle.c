/**
 * @file sched_idle.c
 * @brief Idle Scheduling Class Implementation
 *
 * @details Idle task scheduler (lowest priority)
 *          Runs when no other tasks are runnable
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
#include "list.h"
#include "mm.h"

/*
 * Idle-specific Data Structures
 */

/**
 * @brief Idle run queue
 */
typedef struct idle_rq
{
    struct list_head queue; /**< Queue of idle tasks */
    uint32_t nr_running;    /**< Number of idle tasks */
} idle_rq_t;

/*
 * Forward Declarations
 */

static int idle_sched_init(struct rq *rq);
static void idle_sched_enqueue(struct rq *rq, TCB_t *task);
static void idle_sched_dequeue(struct rq *rq, TCB_t *task);
static TCB_t *idle_sched_pick_next(struct rq *rq);
static void idle_sched_tick(struct rq *rq, TCB_t *task);
static void idle_sched_update_curr(struct rq *rq);
static int idle_sched_get_stats(const struct rq *rq, void *stats);

/*
 * Idle Scheduling Class Definition
 */

const SchedClass_t sched_class_idle = {.name = "IDLE",
                                       .priority = 255U,
                                       .flags = SCHED_CLASS_FLAG_IDLE,
                                       .id = SCHED_IDLE,

                                       /* Core operations */
                                       .init = idle_sched_init,
                                       .enqueue = idle_sched_enqueue,
                                       .dequeue = idle_sched_dequeue,
                                       .pick_next = idle_sched_pick_next,
                                       .task_tick = idle_sched_tick,
                                       .update_curr = idle_sched_update_curr,

                                       /* Optional operations */
                                       .yield = NULL,
                                       .can_preempt = NULL,
                                       .task_fork = NULL,
                                       .switch_to = NULL,
                                       .get_stats = idle_sched_get_stats};

/*
 * Helper Functions
 */

/**
 * @brief Get idle run queue
 * @param rq Generic run queue
 * @return Idle run queue pointer
 */
static idle_rq_t *get_idle_rq(struct rq *rq)
{
    if (rq == NULL)
    {
        return NULL;
    }
    return (idle_rq_t *)rq->idle_rq;
}

/*
 * Idle Scheduler Operations
 */

/**
 * @brief Initialize idle scheduler
 * @param rq Run queue pointer
 * @return 0 on success
 */
static int idle_sched_init(struct rq *rq)
{
    idle_rq_t *idle_rq;

    /* Allocate idle run queue */
    idle_rq = (idle_rq_t *)kmalloc((uint64_t)sizeof(idle_rq_t));
    if (idle_rq == NULL)
    {
        return -1; /* ENOMEM */
    }

    /* Initialize queue */
    INIT_LIST_HEAD(&idle_rq->queue);

    /* Initialize statistics */
    idle_rq->nr_running = 0U;

    /* Link to generic run queue */
    rq->idle_rq = (void *)idle_rq;

    return 0;
}

/**
 * @brief Enqueue task to idle queue
 * @param rq Run queue pointer
 * @param task Task control block pointer
 */
static void idle_sched_enqueue(struct rq *rq, TCB_t *task)
{
    idle_rq_t *idle_rq;

    /* Get idle run queue */
    idle_rq = get_idle_rq(rq);
    if (idle_rq == NULL)
    {
        return;
    }

    /* Add to queue */
    list_add_tail(&task->run_list, &idle_rq->queue);

    /* Update statistics */
    idle_rq->nr_running++;
}

/**
 * @brief Dequeue task from idle queue
 * @param rq Run queue pointer
 * @param task Task control block pointer
 */
static void idle_sched_dequeue(struct rq *rq, TCB_t *task)
{
    idle_rq_t *idle_rq;

    /* Get idle run queue */
    idle_rq = get_idle_rq(rq);
    if (idle_rq == NULL)
    {
        return;
    }

    /* Remove from queue */
    list_del_init(&task->run_list);

    /* Update statistics */
    if (idle_rq->nr_running > 0U)
    {
        idle_rq->nr_running--;
    }
}

/**
 * @brief Pick next task to run
 * @param rq Run queue pointer
 * @return Task control block pointer, NULL if no task
 */
static TCB_t *idle_sched_pick_next(struct rq *rq)
{
    idle_rq_t *idle_rq;
    TCB_t *task;

    /* Get idle run queue */
    idle_rq = get_idle_rq(rq);
    if (idle_rq == NULL)
    {
        return NULL;
    }

    /* Check if any tasks are running */
    if (idle_rq->nr_running == 0U)
    {
        return NULL;
    }

    /* Check if queue is empty */
    if (list_empty(&idle_rq->queue))
    {
        return NULL;
    }

    /* Get first task in queue */
    task = list_first_entry(&idle_rq->queue, TCB_t, run_list);

    return task;
}

/**
 * @brief Tick handler (no-op for idle)
 * @param rq Run queue pointer
 * @param task Current task
 *
 * @note Idle tasks typically execute WFI instruction
 */
static void idle_sched_tick(struct rq *rq, TCB_t *task)
{
    /* Idle tasks don't need tick handling */
    /* They typically execute WFI (Wait For Interrupt) */
    (void)rq;
    (void)task;
}

/**
 * @brief Update current task runtime
 * @param rq Run queue pointer
 */
static void idle_sched_update_curr(struct rq *rq)
{
    TCB_t *curr;
    uint64_t now;
    uint64_t delta;

    /* Get current task */
    curr = rq->curr;
    if (curr == NULL)
    {
        return;
    }

    /* Get current time */
    now = sched_clock();

    /* Calculate execution time */
    delta = now - curr->exec_start;

    /* Update start time */
    curr->exec_start = now;

    /* Update total runtime */
    curr->sum_exec_runtime += delta;
}

/**
 * @brief Get idle scheduler statistics
 * @param rq Run queue pointer
 * @param stats Output: statistics
 * @return 0 on success
 */
static int idle_sched_get_stats(const struct rq *rq, void *stats)
{
    const idle_rq_t *idle_rq;
    SchedStats_t *s = (SchedStats_t *)stats;

    if ((rq == NULL) || (stats == NULL))
    {
        return -1;
    }

    /* Get idle run queue */
    idle_rq = (const idle_rq_t *)rq->idle_rq;
    if (idle_rq == NULL)
    {
        return -1;
    }

    /* Fill statistics */
    s->nr_running = idle_rq->nr_running;

    return 0;
}
