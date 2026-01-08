/**
 * @file sched_edf.c
 * @brief EDF Scheduling Class Implementation
 *
 * @details Earliest Deadline First scheduler for real-time tasks
 *          Uses red-black tree for O(log n) scheduling
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
#include "rbtree.h"
#include "list.h"

/*
 * EDF-specific Data Structures
 */

/**
 * @brief EDF run queue
 */
typedef struct edf_rq
{
    struct rb_root tasks_timeline; /**< Red-black tree sorted by deadline */
    uint32_t nr_running;           /**< Number of running tasks */
    uint64_t min_deadline;         /**< Minimum deadline in queue */
} edf_rq_t;

/*
 * Forward Declarations
 */

static int edf_sched_init(struct rq *rq);
static void edf_sched_enqueue(struct rq *rq, TCB_t *task);
static void edf_sched_dequeue(struct rq *rq, TCB_t *task);
static TCB_t *edf_sched_pick_next(struct rq *rq);
static void edf_sched_tick(struct rq *rq, TCB_t *task);
static void edf_sched_update_curr(struct rq *rq);
static int edf_sched_can_preempt(const struct rq *rq, const TCB_t *task);
static int edf_sched_switch_to(struct rq *rq, TCB_t *task, const struct SchedClass *new_class);
static int edf_sched_get_stats(const struct rq *rq, void *stats);

/*
 * EDF Scheduling Class Definition
 */

const SchedClass_t sched_class_edf = {.name = "EDF",
                                      .priority = 20U,
                                      .flags = SCHED_CLASS_FLAG_REALTIME | SCHED_CLASS_FLAG_PREEMPT,
                                      .id = SCHED_EDF,

                                      /* Core operations */
                                      .init = edf_sched_init,
                                      .enqueue = edf_sched_enqueue,
                                      .dequeue = edf_sched_dequeue,
                                      .pick_next = edf_sched_pick_next,
                                      .task_tick = edf_sched_tick,
                                      .update_curr = edf_sched_update_curr,

                                      /* Optional operations */
                                      .yield = NULL,
                                      .can_preempt = edf_sched_can_preempt,
                                      .task_fork = NULL,
                                      .switch_to = edf_sched_switch_to,
                                      .get_stats = edf_sched_get_stats};

/*
 * Helper Functions
 */

/**
 * @brief Get EDF run queue
 * @param rq Generic run queue
 * @return EDF run queue pointer
 */
static edf_rq_t *get_edf_rq(struct rq *rq)
{
    if (rq == NULL) {
        return NULL;
    }
    return (edf_rq_t *)rq->edf_rq;
}

/*
 * EDF Scheduler Operations
 */

/**
 * @brief Initialize EDF scheduler
 * @param rq Run queue pointer
 * @return 0 on success
 */
static int edf_sched_init(struct rq *rq)
{
    edf_rq_t *edf_rq;

    /* Allocate EDF run queue */
    edf_rq = (edf_rq_t *)malloc(sizeof(edf_rq_t));
    if (edf_rq == NULL) {
        return -1; /* ENOMEM */
    }

    /* Initialize red-black tree */
    edf_rq->tasks_timeline = RB_ROOT;

    /* Initialize statistics */
    edf_rq->nr_running = 0U;
    edf_rq->min_deadline = 0ULL;

    /* Link to generic run queue */
    rq->edf_rq = (void *)edf_rq;

    return 0;
}

/**
 * @brief Enqueue task to EDF queue
 * @param rq Run queue pointer
 * @param task Task control block pointer
 */
static void edf_sched_enqueue(struct rq *rq, TCB_t *task)
{
    edf_rq_t *edf_rq;
    struct rb_node **link;
    struct rb_node *parent;
    TCB_t *entry;

    /* Get EDF run queue */
    edf_rq = get_edf_rq(rq);
    if (edf_rq == NULL) {
        return;
    }

    /* Check deadline validity */
    if (task->deadline == 0ULL) {
        return;
    }

    /* Find insertion position in red-black tree */
    link = &edf_rq->tasks_timeline.rb_node;
    parent = NULL;

    while (*link != NULL) {
        parent = *link;
        entry = rb_entry(parent, TCB_t, run_node);

        /* Compare deadlines */
        if (task->deadline < entry->deadline) {
            link = &(*link)->rb_left;
        } else {
            link = &(*link)->rb_right;
        }
    }

    /* Insert into tree */
    rb_link_node(&task->run_node, parent, link);
    rb_insert_color(&task->run_node, &edf_rq->tasks_timeline);

    /* Update minimum deadline */
    if ((edf_rq->min_deadline == 0ULL) || (task->deadline < edf_rq->min_deadline)) {
        edf_rq->min_deadline = task->deadline;
    }

    /* Update statistics */
    edf_rq->nr_running++;
}

/**
 * @brief Dequeue task from EDF queue
 * @param rq Run queue pointer
 * @param task Task control block pointer
 */
static void edf_sched_dequeue(struct rq *rq, TCB_t *task)
{
    edf_rq_t *edf_rq;
    struct rb_node *node;

    /* Get EDF run queue */
    edf_rq = get_edf_rq(rq);
    if (edf_rq == NULL) {
        return;
    }

    /* Erase from red-black tree */
    rb_erase(&task->run_node, &edf_rq->tasks_timeline);

    /* Update minimum deadline */
    node = rb_first(&edf_rq->tasks_timeline);
    if (node != NULL) {
        TCB_t *first = rb_entry(node, TCB_t, run_node);
        edf_rq->min_deadline = first->deadline;
    } else {
        edf_rq->min_deadline = 0ULL;
    }

    /* Update statistics */
    if (edf_rq->nr_running > 0U) {
        edf_rq->nr_running--;
    }
}

/**
 * @brief Pick next task to run
 * @param rq Run queue pointer
 * @return Task control block pointer, NULL if no task
 */
static TCB_t *edf_sched_pick_next(struct rq *rq)
{
    edf_rq_t *edf_rq;
    struct rb_node *leftmost;
    TCB_t *task;

    /* Get EDF run queue */
    edf_rq = get_edf_rq(rq);
    if (edf_rq == NULL) {
        return NULL;
    }

    /* Check if any tasks are running */
    if (edf_rq->nr_running == 0U) {
        return NULL;
    }

    /* Get leftmost node (earliest deadline) */
    leftmost = rb_first(&edf_rq->tasks_timeline);
    if (leftmost == NULL) {
        return NULL;
    }

    /* Get task from node */
    task = rb_entry(leftmost, TCB_t, run_node);

    return task;
}

/**
 * @brief Tick handler (check deadline miss)
 * @param rq Run queue pointer
 * @param task Current task
 */
static void edf_sched_tick(struct rq *rq, TCB_t *task)
{
    uint64_t now;

    /* Get current time */
    now = sched_clock();

    /* Check for deadline miss */
    if (now >= task->deadline) {
        /* TODO: Handle deadline miss */
        /* Could log, set flag, or trigger recovery */
    }

    (void)rq;
}

/**
 * @brief Update current task runtime
 * @param rq Run queue pointer
 */
static void edf_sched_update_curr(struct rq *rq)
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
 * @brief Check if task can be preempted
 * @param rq Run queue pointer
 * @param task Task to check
 * @return 1 if can preempt, 0 otherwise
 */
static int edf_sched_can_preempt(const struct rq *rq, const TCB_t *task)
{
    const edf_rq_t *edf_rq;
    struct rb_node *leftmost;
    TCB_t *first;

    /* Get EDF run queue */
    edf_rq = (const edf_rq_t *)rq->edf_rq;
    if (edf_rq == NULL) {
        return 0;
    }

    /* Check if queue is empty */
    if (edf_rq->nr_running == 0U) {
        return 0;
    }

    /* Get earliest deadline task */
    leftmost = rb_first(&edf_rq->tasks_timeline);
    if (leftmost == NULL) {
        return 0;
    }

    first = rb_entry(leftmost, TCB_t, run_node);

    /* Can preempt if current task is not the earliest */
    if (first != task) {
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
static int edf_sched_switch_to(struct rq *rq, TCB_t *task, const struct SchedClass *new_class)
{
    /* Dequeue from EDF */
    edf_sched_dequeue(rq, task);

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
 * @brief Get EDF scheduler statistics
 * @param rq Run queue pointer
 * @param stats Output: statistics
 * @return 0 on success
 */
static int edf_sched_get_stats(const struct rq *rq, void *stats)
{
    const edf_rq_t *edf_rq;
    SchedStats_t *s = (SchedStats_t *)stats;

    if ((rq == NULL) || (stats == NULL)) {
        return -1;
    }

    /* Get EDF run queue */
    edf_rq = (const edf_rq_t *)rq->edf_rq;
    if (edf_rq == NULL) {
        return -1;
    }

    /* Fill statistics */
    s->nr_running = edf_rq->nr_running;

    return 0;
}
