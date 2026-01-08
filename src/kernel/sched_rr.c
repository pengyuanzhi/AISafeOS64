/**
 * @file sched_rr.c
 * @brief RR Scheduling Class Implementation
 *
 * @details Round Robin scheduler with time slicing
 *          Uses circular queue for O(1) scheduling
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
 * RR Constants
 */

#define RR_TIME_SLICE 10U /**< Time slice in ticks (10ms) */

/*
 * RR-specific Data Structures
 */

/**
 * @brief RR run queue
 */
typedef struct rr_rq
{
    struct list_head queue; /**< Circular queue of tasks */
    uint32_t nr_running;    /**< Number of running tasks */
} rr_rq_t;

/*
 * Forward Declarations
 */

static int rr_sched_init(struct rq *rq);
static void rr_sched_enqueue(struct rq *rq, TCB_t *task);
static void rr_sched_dequeue(struct rq *rq, TCB_t *task);
static TCB_t *rr_sched_pick_next(struct rq *rq);
static void rr_sched_tick(struct rq *rq, TCB_t *task);
static void rr_sched_update_curr(struct rq *rq);
static void rr_sched_yield(struct rq *rq, TCB_t *task);
static int rr_sched_can_preempt(const struct rq *rq, const TCB_t *task);
static int rr_sched_switch_to(struct rq *rq, TCB_t *task, const struct SchedClass *new_class);
static int rr_sched_get_stats(const struct rq *rq, void *stats);

/*
 * RR Scheduling Class Definition
 */

const SchedClass_t sched_class_rr = {.name = "RR",
                                     .priority = 40U,
                                     .flags = SCHED_CLASS_FLAG_FAIR | SCHED_CLASS_FLAG_PREEMPT,
                                     .id = SCHED_RR,

                                     /* Core operations */
                                     .init = rr_sched_init,
                                     .enqueue = rr_sched_enqueue,
                                     .dequeue = rr_sched_dequeue,
                                     .pick_next = rr_sched_pick_next,
                                     .task_tick = rr_sched_tick,
                                     .update_curr = rr_sched_update_curr,

                                     /* Optional operations */
                                     .yield = rr_sched_yield,
                                     .can_preempt = rr_sched_can_preempt,
                                     .task_fork = NULL,
                                     .switch_to = rr_sched_switch_to,
                                     .get_stats = rr_sched_get_stats};

/*
 * Helper Functions
 */

/**
 * @brief Get RR run queue
 * @param rq Generic run queue
 * @return RR run queue pointer
 */
static rr_rq_t *get_rr_rq(struct rq *rq)
{
    if (rq == NULL) {
        return NULL;
    }
    return (rr_rq_t *)rq->rr_rq;
}

/*
 * RR Scheduler Operations
 */

/**
 * @brief Initialize RR scheduler
 * @param rq Run queue pointer
 * @return 0 on success
 */
static int rr_sched_init(struct rq *rq)
{
    rr_rq_t *rr_rq;

    /* Allocate RR run queue */
    rr_rq = (rr_rq_t *)kmalloc((uint64_t)sizeof(rr_rq_t));
    if (rr_rq == NULL) {
        return -1; /* ENOMEM */
    }

    /* Initialize circular queue */
    INIT_LIST_HEAD(&rr_rq->queue);

    /* Initialize statistics */
    rr_rq->nr_running = 0U;

    /* Link to generic run queue */
    rq->rr_rq = (void *)rr_rq;

    return 0;
}

/**
 * @brief Enqueue task to RR queue
 * @param rq Run queue pointer
 * @param task Task control block pointer
 */
static void rr_sched_enqueue(struct rq *rq, TCB_t *task)
{
    rr_rq_t *rr_rq;

    /* Get RR run queue */
    rr_rq = get_rr_rq(rq);
    if (rr_rq == NULL) {
        return;
    }

    /* Reset time slice */
    task->time_slice = 0U;

    /* Add to tail of circular queue */
    list_add_tail(&task->run_list, &rr_rq->queue);

    /* Update statistics */
    rr_rq->nr_running++;
}

/**
 * @brief Dequeue task from RR queue
 * @param rq Run queue pointer
 * @param task Task control block pointer
 */
static void rr_sched_dequeue(struct rq *rq, TCB_t *task)
{
    rr_rq_t *rr_rq;

    /* Get RR run queue */
    rr_rq = get_rr_rq(rq);
    if (rr_rq == NULL) {
        return;
    }

    /* Remove from queue */
    list_del_init(&task->run_list);

    /* Update statistics */
    if (rr_rq->nr_running > 0U) {
        rr_rq->nr_running--;
    }
}

/**
 * @brief Pick next task to run
 * @param rq Run queue pointer
 * @return Task control block pointer, NULL if no task
 */
static TCB_t *rr_sched_pick_next(struct rq *rq)
{
    rr_rq_t *rr_rq;
    TCB_t *task;

    /* Get RR run queue */
    rr_rq = get_rr_rq(rq);
    if (rr_rq == NULL) {
        return NULL;
    }

    /* Check if any tasks are running */
    if (rr_rq->nr_running == 0U) {
        return NULL;
    }

    /* Check if queue is empty */
    if (list_empty(&rr_rq->queue)) {
        return NULL;
    }

    /* Get first task in queue */
    task = list_first_entry(&rr_rq->queue, TCB_t, run_list);

    return task;
}

/**
 * @brief Tick handler (check time slice)
 * @param rq Run queue pointer
 * @param task Current task
 */
static void rr_sched_tick(struct rq *rq, TCB_t *task)
{
    rr_rq_t *rr_rq;

    /* Get RR run queue */
    rr_rq = get_rr_rq(rq);
    if (rr_rq == NULL) {
        return;
    }

    /* Increment time slice counter */
    task->time_slice++;

    /* Check if time slice expired */
    if (task->time_slice >= RR_TIME_SLICE) {
        /* Reset time slice */
        task->time_slice = 0U;

        /* Move to tail of queue */
        list_move_tail(&task->run_list, &rr_rq->queue);

        /* Set reschedule flag */
        rq->need_resched = 1U;
    }
}

/**
 * @brief Update current task runtime
 * @param rq Run queue pointer
 */
static void rr_sched_update_curr(struct rq *rq)
{
    TCB_t *curr;
    uint64_t now;
    uint64_t delta;

    /* Get current task */
    curr = rq->curr;
    if (curr == NULL) {
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
 * @brief Task yield
 * @param rq Run queue pointer
 * @param task Task yielding
 */
static void rr_sched_yield(struct rq *rq, TCB_t *task)
{
    rr_rq_t *rr_rq;

    /* Get RR run queue */
    rr_rq = get_rr_rq(rq);
    if (rr_rq == NULL) {
        return;
    }

    /* Reset time slice */
    task->time_slice = 0U;

    /* Move to tail of queue */
    list_move_tail(&task->run_list, &rr_rq->queue);
}

/**
 * @brief Check if task can be preempted
 * @param rq Run queue pointer
 * @param task Task to check
 * @return 1 if can preempt, 0 otherwise
 */
static int rr_sched_can_preempt(const struct rq *rq, const TCB_t *task)
{
    const rr_rq_t *rr_rq;

    /* Get RR run queue */
    rr_rq = (const rr_rq_t *)rq->rr_rq;
    if (rr_rq == NULL) {
        return 0;
    }

    /* Check if there are other tasks */
    if (rr_rq->nr_running > 1U) {
        return 1;
    }

    return 0;
}

/**
 * @brief Switch to different scheduling class
 * @param rq Run queue pointer
 * @param task Task to switch
 * @param new_class New scheduling class
 * @return 0 on success
 */
static int rr_sched_switch_to(struct rq *rq, TCB_t *task, const struct SchedClass *new_class)
{
    /* Dequeue from RR */
    rr_sched_dequeue(rq, task);

    /* Change scheduling class */
    task->sched_class = new_class;

    /* Enqueue to new scheduler */
    if (new_class != NULL) {
        if (new_class->enqueue != NULL) {
            new_class->enqueue(rq, task);
        }
    }

    return 0;
}

/**
 * @brief Get RR scheduler statistics
 * @param rq Run queue pointer
 * @param stats Output: statistics
 * @return 0 on success
 */
static int rr_sched_get_stats(const struct rq *rq, void *stats)
{
    const rr_rq_t *rr_rq;
    SchedStats_t *s = (SchedStats_t *)stats;

    if ((rq == NULL) || (stats == NULL)) {
        return -1;
    }

    /* Get RR run queue */
    rr_rq = (const rr_rq_t *)rq->rr_rq;
    if (rr_rq == NULL) {
        return -1;
    }

    /* Fill statistics */
    s->nr_running = rr_rq->nr_running;

    return 0;
}
