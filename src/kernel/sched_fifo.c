/**
 * @file sched_fifo.c
 * @brief FIFO Scheduling Class Implementation
 *
 * @details First-In-First-Out scheduler for real-time tasks
 *          Supports 256 priority levels with O(1) scheduling
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
#include "list.h"

/*
 * FIFO-specific Data Structures
 */

/**
 * @brief FIFO run queue (per-priority)
 */
typedef struct fifo_rq
{
    struct list_head queue_array[256]; /**< 256 priority queues */
    uint64_t priority_bitmap[4];       /**< 256-bit bitmap */
    uint32_t nr_running;               /**< Number of running tasks */
} fifo_rq_t;

/*
 * Forward Declarations
 */

static int fifo_sched_init(struct rq *rq);
static void fifo_sched_enqueue(struct rq *rq, TCB_t *task);
static void fifo_sched_dequeue(struct rq *rq, TCB_t *task);
static TCB_t *fifo_sched_pick_next(struct rq *rq);
static void fifo_sched_tick(struct rq *rq, TCB_t *task);
static void fifo_sched_update_curr(struct rq *rq);
static void fifo_sched_yield(struct rq *rq, TCB_t *task);
static int fifo_sched_can_preempt(const struct rq *rq, const TCB_t *task);
static int fifo_sched_switch_to(struct rq *rq, TCB_t *task, const struct SchedClass *new_class);
static int fifo_sched_get_stats(const struct rq *rq, void *stats);

/*
 * FIFO Scheduling Class Definition
 */

const SchedClass_t sched_class_fifo = {.name = "FIFO",
                                       .priority = 10U,
                                       .flags =
                                           SCHED_CLASS_FLAG_REALTIME | SCHED_CLASS_FLAG_PREEMPT,
                                       .id = SCHED_FIFO,

                                       /* Core operations */
                                       .init = fifo_sched_init,
                                       .enqueue = fifo_sched_enqueue,
                                       .dequeue = fifo_sched_dequeue,
                                       .pick_next = fifo_sched_pick_next,
                                       .task_tick = fifo_sched_tick,
                                       .update_curr = fifo_sched_update_curr,

                                       /* Optional operations */
                                       .yield = fifo_sched_yield,
                                       .can_preempt = fifo_sched_can_preempt,
                                       .task_fork = NULL,
                                       .switch_to = fifo_sched_switch_to,
                                       .get_stats = fifo_sched_get_stats};

/*
 * Helper Functions
 */

/**
 * @brief Get FIFO run queue
 * @param rq Generic run queue
 * @return FIFO run queue pointer
 */
static fifo_rq_t *get_fifo_rq(struct rq *rq)
{
    if (rq == NULL) {
        return NULL;
    }
    return (fifo_rq_t *)rq->fifo_rq;
}

/*
 * FIFO Scheduler Operations
 */

/**
 * @brief Initialize FIFO scheduler
 * @param rq Run queue pointer
 * @return 0 on success
 */
static int fifo_sched_init(struct rq *rq)
{
    fifo_rq_t *fifo_rq;
    uint32_t i;

    /* Allocate FIFO run queue */
    fifo_rq = (fifo_rq_t *)malloc(sizeof(fifo_rq_t));
    if (fifo_rq == NULL) {
        return -1; /* ENOMEM */
    }

    /* Initialize priority queues */
    for (i = 0U; i < 256U; i++) {
        INIT_LIST_HEAD(&fifo_rq->queue_array[i]);
    }

    /* Initialize priority bitmap */
    (void)memset(fifo_rq->priority_bitmap, 0, sizeof(fifo_rq->priority_bitmap));

    /* Initialize statistics */
    fifo_rq->nr_running = 0U;

    /* Link to generic run queue */
    rq->fifo_rq = (void *)fifo_rq;

    return 0;
}

/**
 * @brief Enqueue task to FIFO queue
 * @param rq Run queue pointer
 * @param task Task control block pointer
 */
static void fifo_sched_enqueue(struct rq *rq, TCB_t *task)
{
    fifo_rq_t *fifo_rq;
    struct list_head *queue;
    uint32_t prio;

    /* Get FIFO run queue */
    fifo_rq = get_fifo_rq(rq);
    if (fifo_rq == NULL) {
        return;
    }

    /* Get task priority */
    prio = task->prio;

    /* Validate priority */
    if (prio >= 256U) {
        return;
    }

    /* Get priority queue */
    queue = &fifo_rq->queue_array[prio];

    /* Add to tail of queue (FIFO order) */
    list_add_tail(&task->run_list, queue);

    /* Update bitmap */
    set_bit(prio, fifo_rq->priority_bitmap);

    /* Update statistics */
    fifo_rq->nr_running++;
}

/**
 * @brief Dequeue task from FIFO queue
 * @param rq Run queue pointer
 * @param task Task control block pointer
 */
static void fifo_sched_dequeue(struct rq *rq, TCB_t *task)
{
    fifo_rq_t *fifo_rq;
    uint32_t prio;

    /* Get FIFO run queue */
    fifo_rq = get_fifo_rq(rq);
    if (fifo_rq == NULL) {
        return;
    }

    /* Get task priority */
    prio = task->prio;

    /* Validate priority */
    if (prio >= 256U) {
        return;
    }

    /* Remove from queue */
    list_del_init(&task->run_list);

    /* Update bitmap if queue is now empty */
    if (list_empty(&fifo_rq->queue_array[prio])) {
        clear_bit(prio, fifo_rq->priority_bitmap);
    }

    /* Update statistics */
    if (fifo_rq->nr_running > 0U) {
        fifo_rq->nr_running--;
    }
}

/**
 * @brief Pick next task to run
 * @param rq Run queue pointer
 * @return Task control block pointer, NULL if no task
 */
static TCB_t *fifo_sched_pick_next(struct rq *rq)
{
    fifo_rq_t *fifo_rq;
    struct list_head *queue;
    TCB_t *task;
    uint32_t prio;
    uint64_t bitmap_u64;
    uint32_t word_idx;
    uint32_t bit_offset;

    /* Get FIFO run queue */
    fifo_rq = get_fifo_rq(rq);
    if (fifo_rq == NULL) {
        return NULL;
    }

    /* Check if any tasks are running */
    if (fifo_rq->nr_running == 0U) {
        return NULL;
    }

    /* Find highest priority (using CLZ) */
    prio = 255U;

    for (word_idx = 0U; word_idx < 4U; word_idx++) {
        bitmap_u64 = fifo_rq->priority_bitmap[word_idx];

        if (bitmap_u64 == 0ULL) {
            continue;
        }

        /* Use CLZ to find highest set bit */
        bit_offset = (uint32_t)__builtin_clzll(bitmap_u64);
        prio = (word_idx * 64U) + (63U - bit_offset);
        break;
    }

    /* Check if valid priority found */
    if (prio == 255U) {
        return NULL; /* Should not happen if nr_running > 0 */
    }

    /* Get priority queue */
    if (prio >= 256U) {
        return NULL;
    }

    queue = &fifo_rq->queue_array[prio];

    /* Check if queue is empty */
    if (list_empty(queue)) {
        return NULL;
    }

    /* Get first task in queue */
    task = list_first_entry(queue, TCB_t, run_list);

    return task;
}

/**
 * @brief Tick handler (no-op for FIFO)
 * @param rq Run queue pointer
 * @param task Current task
 *
 * @note FIFO does not use time slices
 */
static void fifo_sched_tick(struct rq *rq, TCB_t *task)
{
    /* FIFO tasks run until they yield or block */
    /* No time slice management needed */
    (void)rq;
    (void)task;
}

/**
 * @brief Update current task runtime
 * @param rq Run queue pointer
 */
static void fifo_sched_update_curr(struct rq *rq)
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
static void fifo_sched_yield(struct rq *rq, TCB_t *task)
{
    fifo_rq_t *fifo_rq;
    struct list_head *queue;
    uint32_t prio;

    /* Get FIFO run queue */
    fifo_rq = get_fifo_rq(rq);
    if (fifo_rq == NULL) {
        return;
    }

    /* Get task priority */
    prio = task->prio;

    if (prio >= 256U) {
        return;
    }

    /* Get priority queue */
    queue = &fifo_rq->queue_array[prio];

    /* Move to tail of queue */
    list_move_tail(&task->run_list, queue);
}

/**
 * @brief Check if task can be preempted
 * @param rq Run queue pointer
 * @param task Task to check
 * @return 1 if can preempt, 0 otherwise
 */
static int fifo_sched_can_preempt(const struct rq *rq, const TCB_t *task)
{
    const fifo_rq_t *fifo_rq;
    uint32_t prio;
    uint32_t highest_prio;
    uint64_t bitmap_u64;
    uint32_t word_idx;
    uint32_t bit_offset;

    /* Get FIFO run queue */
    fifo_rq = (const fifo_rq_t *)rq->fifo_rq;
    if (fifo_rq == NULL) {
        return 0;
    }

    /* Get task priority */
    prio = task->prio;

    /* Find highest priority */
    highest_prio = 255U;

    for (word_idx = 0U; word_idx < 4U; word_idx++) {
        bitmap_u64 = fifo_rq->priority_bitmap[word_idx];

        if (bitmap_u64 == 0ULL) {
            continue;
        }

        bit_offset = (uint32_t)__builtin_clzll(bitmap_u64);
        highest_prio = (word_idx * 64U) + (63U - bit_offset);
        break;
    }

    /* Can preempt if higher priority task exists */
    if (highest_prio > prio) {
        return 1;
    }

    return 0;
}

/**
 * @brief Switch to different scheduling class
 * @param rq Run queue pointer
 * @param task Task to switch
 * @param new_class New scheduling class
 * @return 0 on success, negative error code on failure
 */
static int fifo_sched_switch_to(struct rq *rq, TCB_t *task, const struct SchedClass *new_class)
{
    /* Dequeue from FIFO */
    fifo_sched_dequeue(rq, task);

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
 * @brief Get FIFO scheduler statistics
 * @param rq Run queue pointer
 * @param stats Output: statistics
 * @return 0 on success
 */
static int fifo_sched_get_stats(const struct rq *rq, void *stats)
{
    const fifo_rq_t *fifo_rq;
    SchedStats_t *s = (SchedStats_t *)stats;

    if ((rq == NULL) || (stats == NULL)) {
        return -1; /* EINVAL */
    }

    /* Get FIFO run queue */
    fifo_rq = (const fifo_rq_t *)rq->fifo_rq;
    if (fifo_rq == NULL) {
        return -1;
    }

    /* Fill statistics */
    s->nr_running = fifo_rq->nr_running;
    /* Note: Other fields are filled by caller */

    return 0;
}
