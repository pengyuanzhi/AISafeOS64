/**
 * @file sched_cfs.c
 * @brief CFS Scheduling Class Implementation
 *
 * @details Completely Fair Scheduler using red-black tree
 *          Based on virtual runtime (vruntime)
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
 * CFS Constants
 */

#define NICE_0_LOAD 1024U             /**< Load weight for nice 0 */
#define CFS_GRANULARITY_NS 1000000ULL /**< 1ms granularity */

/*
 * CFS-specific Data Structures
 */

/**
 * @brief CFS run queue
 */
typedef struct cfs_rq
{
    struct rb_root tasks_timeline; /**< Red-black tree sorted by vruntime */
    uint64_t min_vruntime;         /**< Minimum vruntime in queue */
    uint32_t nr_running;           /**< Number of running tasks */
    uint64_t exec_clock;           /**< Execution clock */
} cfs_rq_t;

/*
 * Forward Declarations
 */

static int cfs_sched_init(struct rq *rq);
static void cfs_sched_enqueue(struct rq *rq, TCB_t *task);
static void cfs_sched_dequeue(struct rq *rq, TCB_t *task);
static TCB_t *cfs_sched_pick_next(struct rq *rq);
static void cfs_sched_tick(struct rq *rq, TCB_t *task);
static void cfs_sched_update_curr(struct rq *rq);
static void cfs_sched_yield(struct rq *rq, TCB_t *task);
static int cfs_sched_can_preempt(const struct rq *rq, const TCB_t *task);
static int cfs_sched_switch_to(struct rq *rq, TCB_t *task, const struct SchedClass *new_class);
static int cfs_sched_get_stats(const struct rq *rq, void *stats);

/*
 * CFS Scheduling Class Definition
 */

const SchedClass_t sched_class_cfs = {.name = "CFS",
                                      .priority = 30U,
                                      .flags = SCHED_CLASS_FLAG_FAIR | SCHED_CLASS_FLAG_PREEMPT,
                                      .id = SCHED_CFS,

                                      /* Core operations */
                                      .init = cfs_sched_init,
                                      .enqueue = cfs_sched_enqueue,
                                      .dequeue = cfs_sched_dequeue,
                                      .pick_next = cfs_sched_pick_next,
                                      .task_tick = cfs_sched_tick,
                                      .update_curr = cfs_sched_update_curr,

                                      /* Optional operations */
                                      .yield = cfs_sched_yield,
                                      .can_preempt = cfs_sched_can_preempt,
                                      .task_fork = NULL,
                                      .switch_to = cfs_sched_switch_to,
                                      .get_stats = cfs_sched_get_stats};

/*
 * Helper Functions
 */

/**
 * @brief Get CFS run queue
 * @param rq Generic run queue
 * @return CFS run queue pointer
 */
static cfs_rq_t *get_cfs_rq(struct rq *rq)
{
    if (rq == NULL) {
        return NULL;
    }
    return (cfs_rq_t *)rq->cfs_rq;
}

/*
 * CFS Scheduler Operations
 */

/**
 * @brief Initialize CFS scheduler
 * @param rq Run queue pointer
 * @return 0 on success
 */
static int cfs_sched_init(struct rq *rq)
{
    cfs_rq_t *cfs_rq;

    /* Allocate CFS run queue */
    cfs_rq = (cfs_rq_t *)malloc(sizeof(cfs_rq_t));
    if (cfs_rq == NULL) {
        return -1; /* ENOMEM */
    }

    /* Initialize red-black tree */
    cfs_rq->tasks_timeline = RB_ROOT;

    /* Initialize statistics */
    cfs_rq->nr_running = 0U;
    cfs_rq->min_vruntime = 0ULL;
    cfs_rq->exec_clock = 0ULL;

    /* Link to generic run queue */
    rq->cfs_rq = (void *)cfs_rq;

    return 0;
}

/**
 * @brief Enqueue task to CFS queue
 * @param rq Run queue pointer
 * @param task Task control block pointer
 */
static void cfs_sched_enqueue(struct rq *rq, TCB_t *task)
{
    cfs_rq_t *cfs_rq;
    struct rb_node **link;
    struct rb_node *parent;
    TCB_t *entry;

    /* Get CFS run queue */
    cfs_rq = get_cfs_rq(rq);
    if (cfs_rq == NULL) {
        return;
    }

    /* Initialize vruntime if needed */
    if (task->vruntime == 0ULL) {
        task->vruntime = cfs_rq->min_vruntime;
    }

    /* Find insertion position in red-black tree */
    link = &cfs_rq->tasks_timeline.rb_node;
    parent = NULL;

    while (*link != NULL) {
        parent = *link;
        entry = rb_entry(parent, TCB_t, run_node);

        /* Compare vruntime */
        if (task->vruntime < entry->vruntime) {
            link = &(*link)->rb_left;
        } else {
            link = &(*link)->rb_right;
        }
    }

    /* Insert into tree */
    rb_link_node(&task->run_node, parent, link);
    rb_insert_color(&task->run_node, &cfs_rq->tasks_timeline);

    /* Update minimum vruntime */
    if (task->vruntime < cfs_rq->min_vruntime) {
        cfs_rq->min_vruntime = task->vruntime;
    }

    /* Update statistics */
    cfs_rq->nr_running++;
}

/**
 * @brief Dequeue task from CFS queue
 * @param rq Run queue pointer
 * @param task Task control block pointer
 */
static void cfs_sched_dequeue(struct rq *rq, TCB_t *task)
{
    cfs_rq_t *cfs_rq;
    struct rb_node *node;

    /* Get CFS run queue */
    cfs_rq = get_cfs_rq(rq);
    if (cfs_rq == NULL) {
        return;
    }

    /* Erase from red-black tree */
    rb_erase(&task->run_node, &cfs_rq->tasks_timeline);

    /* Update minimum vruntime */
    node = rb_first(&cfs_rq->tasks_timeline);
    if (node != NULL) {
        TCB_t *first = rb_entry(node, TCB_t, run_node);
        cfs_rq->min_vruntime = first->vruntime;
    }

    /* Update statistics */
    if (cfs_rq->nr_running > 0U) {
        cfs_rq->nr_running--;
    }
}

/**
 * @brief Pick next task to run
 * @param rq Run queue pointer
 * @return Task control block pointer, NULL if no task
 */
static TCB_t *cfs_sched_pick_next(struct rq *rq)
{
    cfs_rq_t *cfs_rq;
    struct rb_node *leftmost;
    TCB_t *task;

    /* Get CFS run queue */
    cfs_rq = get_cfs_rq(rq);
    if (cfs_rq == NULL) {
        return NULL;
    }

    /* Check if any tasks are running */
    if (cfs_rq->nr_running == 0U) {
        return NULL;
    }

    /* Get leftmost node (minimum vruntime) */
    leftmost = rb_first(&cfs_rq->tasks_timeline);
    if (leftmost == NULL) {
        return NULL;
    }

    /* Get task from node */
    task = rb_entry(leftmost, TCB_t, run_node);

    return task;
}

/**
 * @brief Tick handler (no-op, handled by update_curr)
 * @param rq Run queue pointer
 * @param task Current task
 */
static void cfs_sched_tick(struct rq *rq, TCB_t *task)
{
    /* CFS uses update_curr for runtime tracking */
    /* Just update current task here */
    (void)task;
    cfs_sched_update_curr(rq);
}

/**
 * @brief Update current task runtime
 * @param rq Run queue pointer
 */
static void cfs_sched_update_curr(struct rq *rq)
{
    cfs_rq_t *cfs_rq;
    TCB_t *curr;
    uint64_t now;
    uint64_t delta_exec;
    uint64_t delta_fair;
    uint32_t weight;

    /* Get CFS run queue */
    cfs_rq = get_cfs_rq(rq);
    if (cfs_rq == NULL) {
        return;
    }

    /* Get current task */
    curr = rq->curr;
    if (curr == NULL) {
        return;
    }

    /* Get current time */
    now = sched_clock();

    /* Calculate execution time */
    delta_exec = now - curr->exec_start;

    /* Check if any time elapsed */
    if (delta_exec == 0ULL) {
        return;
    }

    /* Update start time */
    curr->exec_start = now;

    /* Update total runtime */
    curr->sum_exec_runtime += delta_exec;

    /* Get weight based on priority */
    weight = prio_to_weight(curr->prio);

    /* Calculate vruntime increment */
    /* vruntime += physical_time * (NICE_0_LOAD / weight) */
    delta_fair = (delta_exec * NICE_0_LOAD) / (uint64_t)weight;

    /* Update vruntime */
    curr->vruntime += delta_fair;

    /* Update execution clock */
    cfs_rq->exec_clock += delta_exec;

    /* Check if task needs requeue */
    if ((curr->vruntime - cfs_rq->min_vruntime) > CFS_GRANULARITY_NS) {
        /* Dequeue and re-enqueue to maintain tree order */
        cfs_sched_dequeue(rq, curr);
        cfs_sched_enqueue(rq, curr);
    }
}

/**
 * @brief Task yield
 * @param rq Run queue pointer
 * @param task Task yielding
 */
static void cfs_sched_yield(struct rq *rq, TCB_t *task)
{
    /* Dequeue and re-enqueue */
    /* This moves task to right of tree */
    cfs_sched_dequeue(rq, task);
    cfs_sched_enqueue(rq, task);
}

/**
 * @brief Check if task can be preempted
 * @param rq Run queue pointer
 * @param task Task to check
 * @return 1 if can preempt, 0 otherwise
 */
static int cfs_sched_can_preempt(const struct rq *rq, const TCB_t *task)
{
    const cfs_rq_t *cfs_rq;
    struct rb_node *leftmost;
    TCB_t *first;

    /* Get CFS run queue */
    cfs_rq = (const cfs_rq_t *)rq->cfs_rq;
    if (cfs_rq == NULL) {
        return 0;
    }

    /* Check if queue is empty */
    if (cfs_rq->nr_running == 0U) {
        return 0;
    }

    /* Get minimum vruntime task */
    leftmost = rb_first(&cfs_rq->tasks_timeline);
    if (leftmost == NULL) {
        return 0;
    }

    first = rb_entry(leftmost, TCB_t, run_node);

    /* Can preempt if current task is not the minimum */
    if (first != task) {
        /* Check vruntime difference */
        if ((first->vruntime + CFS_GRANULARITY_NS) < task->vruntime) {
            return 1;
        }
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
static int cfs_sched_switch_to(struct rq *rq, TCB_t *task, const struct SchedClass *new_class)
{
    /* Dequeue from CFS */
    cfs_sched_dequeue(rq, task);

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
 * @brief Get CFS scheduler statistics
 * @param rq Run queue pointer
 * @param stats Output: statistics
 * @return 0 on success
 */
static int cfs_sched_get_stats(const struct rq *rq, void *stats)
{
    const cfs_rq_t *cfs_rq;
    SchedStats_t *s = (SchedStats_t *)stats;

    if ((rq == NULL) || (stats == NULL)) {
        return -1;
    }

    /* Get CFS run queue */
    cfs_rq = (const cfs_rq_t *)rq->cfs_rq;
    if (cfs_rq == NULL) {
        return -1;
    }

    /* Fill statistics */
    s->nr_running = cfs_rq->nr_running;

    return 0;
}
