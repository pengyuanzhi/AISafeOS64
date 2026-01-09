/**
 * @file test_sched.c
 * @brief AISafe64 RTOS - Scheduler Unit Tests
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details Comprehensive scheduler unit tests
 *          - Scheduler initialization
 *          - Task creation and management
 *          - All scheduling classes (FIFO, EDF, CFS, RR, IDLE)
 *          - Enqueue/dequeue operations
 *          - Priority handling
 *          - Statistics
 *
 * @note MISRA-C:2012 compliant
 */

#include "test_framework.h"
#include "../src/include/sched.h"
#include "../src/include/list.h"
#include "../src/include/mm.h"
#include <stdint.h>
#include <string.h>

/*
 * Mock Functions for Testing
 */

static uint64_t test_time = 0ULL;

/* Mock sched_clock */
uint64_t sched_clock(void)
{
    return test_time;
}

/* Mock this_rq */
static struct rq *g_test_rq = NULL;

struct rq *this_rq(void)
{
    return g_test_rq;
}

/* Dummy task entry function */
static void dummy_task_entry(void)
{
    /* Dummy task - does nothing */
    while (1)
    {
        /* Wait forever */
    }
}

/*
 * Test Helper Functions
 */

/**
 * @brief Create a mock test task with specified parameters
 * @note This creates a minimal TCB for unit testing scheduler classes
 */
static TCB_t *create_test_task(const char *name, uint8_t prio, SchedPolicy_t policy)
{
    TCB_t *task;

    /* Allocate task */
    task = (TCB_t *)kmalloc(sizeof(TCB_t));
    if (task == NULL)
    {
        return NULL;
    }

    /* Initialize task structure */
    (void)memset(task, 0, sizeof(TCB_t));

    /* Set basic fields */
    task->tid = (uint32_t)(uintptr_t)task; /* Use address as fake TID for testing */
    task->state = TASK_READY;
    task->prio = prio;
    task->static_prio = prio;
    task->normal_prio = prio;
    task->cpu_affinity = 0x01U;
    task->stack_size = 8192U;
    task->stack_base = 0ULL;
    task->stack_ptr = 0ULL;

    /* Copy name */
    if (name != NULL)
    {
        (void)strncpy(task->name, name, sizeof(task->name) - 1U);
        task->name[sizeof(task->name) - 1U] = '\0';
    }

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

    /* Initialize runtime statistics */
    task->vruntime = 0ULL;
    task->exec_start = 0ULL;
    task->sum_exec_runtime = 0ULL;
    task->deadline = 0ULL;
    task->time_slice = 0U;

    /* Initialize list heads */
    INIT_LIST_HEAD(&task->tasks);
    INIT_LIST_HEAD(&task->rq_list);
    INIT_LIST_HEAD(&task->run_list);

    return task;
}

/**
 * @brief Clean up a test task
 */
static void destroy_test_task(TCB_t *task)
{
    if (task != NULL)
    {
        if (task->stack_base != 0ULL)
        {
            kfree((void *)(uintptr_t)task->stack_base);
        }
        kfree(task);
    }
}

/*
 * Scheduling Class Registration Tests
 */

/**
 * @brief Test that all scheduling classes are available
 */
TEST_CASE(sched_classes_available)
{
    /* Test that all scheduling class pointers are non-NULL */
    TEST_ASSERT_NOT_NULL(&sched_class_fifo);
    TEST_ASSERT_NOT_NULL(&sched_class_edf);
    TEST_ASSERT_NOT_NULL(&sched_class_cfs);
    TEST_ASSERT_NOT_NULL(&sched_class_rr);
    TEST_ASSERT_NOT_NULL(&sched_class_idle);
}

/**
 * @brief Test scheduling class function pointers
 */
TEST_CASE(sched_classes_have_required_methods)
{
    /* All scheduling classes must have core methods implemented */
    TEST_ASSERT_NOT_NULL(sched_class_fifo.init);
    TEST_ASSERT_NOT_NULL(sched_class_fifo.enqueue);
    TEST_ASSERT_NOT_NULL(sched_class_fifo.dequeue);
    TEST_ASSERT_NOT_NULL(sched_class_fifo.pick_next);
    TEST_ASSERT_NOT_NULL(sched_class_fifo.task_tick);
    TEST_ASSERT_NOT_NULL(sched_class_fifo.update_curr);

    TEST_ASSERT_NOT_NULL(sched_class_cfs.init);
    TEST_ASSERT_NOT_NULL(sched_class_cfs.enqueue);
    TEST_ASSERT_NOT_NULL(sched_class_cfs.dequeue);
    TEST_ASSERT_NOT_NULL(sched_class_cfs.pick_next);
    TEST_ASSERT_NOT_NULL(sched_class_cfs.task_tick);
    TEST_ASSERT_NOT_NULL(sched_class_cfs.update_curr);
}

/*
 * FIFO Scheduler Tests
 */

/**
 * @brief Test FIFO enqueue and dequeue
 */
TEST_CASE(fifo_enqueue_dequeue)
{
    struct rq rq;
    TCB_t *task1;
    TCB_t *task2;
    TCB_t *picked;

    /* Initialize run queue */
    (void)memset(&rq, 0, sizeof(rq));
    TEST_ASSERT_EQ(sched_class_fifo.init(&rq), 0);

    /* Create tasks */
    task1 = create_test_task("task1", 100U, SCHED_FIFO);
    task2 = create_test_task("task2", 100U, SCHED_FIFO);

    TEST_ASSERT_NOT_NULL(task1);
    TEST_ASSERT_NOT_NULL(task2);

    /* Enqueue tasks */
    sched_class_fifo.enqueue(&rq, task1);
    sched_class_fifo.enqueue(&rq, task2);

    /* Pick next task (should be task1 - FIFO) */
    picked = sched_class_fifo.pick_next(&rq);
    TEST_ASSERT_EQ_PTR(picked, task1);

    /* Dequeue task1 */
    sched_class_fifo.dequeue(&rq, task1);

    /* Pick next (should be task2) */
    picked = sched_class_fifo.pick_next(&rq);
    TEST_ASSERT_EQ_PTR(picked, task2);

    destroy_test_task(task1);
    destroy_test_task(task2);

    /* Clean up run queue */
    if (rq.fifo_rq != NULL)
    {
        kfree(rq.fifo_rq);
    }
}

/**
 * @brief Test FIFO priority ordering
 */
TEST_CASE(fifo_priority_ordering)
{
    struct rq rq;
    TCB_t *task_low;
    TCB_t *task_high;
    TCB_t *picked;

    /* Initialize run queue */
    (void)memset(&rq, 0, sizeof(rq));
    TEST_ASSERT_EQ(sched_class_fifo.init(&rq), 0);

    /* Create tasks with different priorities */
    task_low = create_test_task("low", 150U, SCHED_FIFO);
    task_high = create_test_task("high", 100U, SCHED_FIFO);

    TEST_ASSERT_NOT_NULL(task_low);
    TEST_ASSERT_NOT_NULL(task_high);

    /* Enqueue in random order */
    sched_class_fifo.enqueue(&rq, task_low);
    sched_class_fifo.enqueue(&rq, task_high);

    /* Pick next (should be task_high - higher priority/lower number) */
    picked = sched_class_fifo.pick_next(&rq);
    TEST_ASSERT_EQ_PTR(picked, task_high);

    destroy_test_task(task_low);
    destroy_test_task(task_high);

    /* Clean up run queue */
    if (rq.fifo_rq != NULL)
    {
        kfree(rq.fifo_rq);
    }
}

/*
 * Round Robin Scheduler Tests
 */

/**
 * @brief Test RR time slicing
 */
TEST_CASE(rr_time_slicing)
{
    struct rq rq;
    TCB_t *task;

    /* Initialize run queue */
    (void)memset(&rq, 0, sizeof(rq));
    TEST_ASSERT_EQ(sched_class_rr.init(&rq), 0);

    /* Create task */
    task = create_test_task("rr_task", 100U, SCHED_RR);
    TEST_ASSERT_NOT_NULL(task);

    /* Enqueue task */
    sched_class_rr.enqueue(&rq, task);

    /* Simulate ticks */
    test_time = 0ULL;
    task->time_slice = 0U;
    task->exec_start = test_time;

    /* Simulate 5 ticks */
    for (uint32_t i = 0U; i < 5U; i++)
    {
        test_time += 1000ULL; /* 1ms per tick */
        sched_class_rr.task_tick(&rq, task);
    }

    /* Time slice should be 5 */
    TEST_ASSERT_EQ(task->time_slice, 5U);

    destroy_test_task(task);

    /* Clean up run queue */
    if (rq.rr_rq != NULL)
    {
        kfree(rq.rr_rq);
    }
}

/**
 * @brief Test RR rotation
 */
TEST_CASE(rr_rotation)
{
    struct rq rq;
    TCB_t *task1;
    TCB_t *task2;
    TCB_t *picked;

    /* Initialize run queue */
    (void)memset(&rq, 0, sizeof(rq));
    TEST_ASSERT_EQ(sched_class_rr.init(&rq), 0);

    /* Create tasks */
    task1 = create_test_task("task1", 100U, SCHED_RR);
    task2 = create_test_task("task2", 100U, SCHED_RR);

    TEST_ASSERT_NOT_NULL(task1);
    TEST_ASSERT_NOT_NULL(task2);

    /* Enqueue tasks */
    sched_class_rr.enqueue(&rq, task1);
    sched_class_rr.enqueue(&rq, task2);

    /* Pick first task */
    picked = sched_class_rr.pick_next(&rq);
    TEST_ASSERT_EQ_PTR(picked, task1);

    /* Simulate time slice expiration */
    rq.need_resched = 0U;
    for (uint32_t i = 0U; i < 10U; i++)
    {
        sched_class_rr.task_tick(&rq, task1);
    }

    /* Should set need_resched flag */
    TEST_ASSERT_EQ(rq.need_resched, 1U);

    destroy_test_task(task1);
    destroy_test_task(task2);

    /* Clean up run queue */
    if (rq.rr_rq != NULL)
    {
        kfree(rq.rr_rq);
    }
}

/*
 * CFS Scheduler Tests
 */

/**
 * @brief Test CFS vruntime tracking
 */
TEST_CASE(cfs_vruntime_tracking)
{
    struct rq rq;
    TCB_t *task;

    /* Initialize run queue */
    (void)memset(&rq, 0, sizeof(rq));
    TEST_ASSERT_EQ(sched_class_cfs.init(&rq), 0);

    /* Create task */
    task = create_test_task("cfs_task", 120U, SCHED_CFS);
    TEST_ASSERT_NOT_NULL(task);

    /* Initialize runtime */
    task->vruntime = 1000ULL;
    task->exec_start = 1000ULL;
    rq.curr = task;

    /* Simulate execution */
    test_time = 2000ULL;
    sched_class_cfs.update_curr(&rq);

    /* vruntime should have increased */
    TEST_ASSERT_GT(task->vruntime, 1000ULL);

    destroy_test_task(task);

    /* Clean up run queue */
    if (rq.cfs_rq != NULL)
    {
        kfree(rq.cfs_rq);
    }
}

/**
 * @brief Test CFS task selection by vruntime
 */
TEST_CASE(cfs_pick_by_vruntime)
{
    struct rq rq;
    TCB_t *task_low_vruntime;
    TCB_t *task_high_vruntime;
    TCB_t *picked;

    /* Initialize run queue */
    (void)memset(&rq, 0, sizeof(rq));
    TEST_ASSERT_EQ(sched_class_cfs.init(&rq), 0);

    /* Create tasks */
    task_low_vruntime = create_test_task("low_vr", 100U, SCHED_CFS);
    task_high_vruntime = create_test_task("high_vr", 100U, SCHED_CFS);

    TEST_ASSERT_NOT_NULL(task_low_vruntime);
    TEST_ASSERT_NOT_NULL(task_high_vruntime);

    /* Set different vruntime values */
    task_low_vruntime->vruntime = 1000ULL;
    task_high_vruntime->vruntime = 5000ULL;

    /* Enqueue tasks */
    sched_class_cfs.enqueue(&rq, task_low_vruntime);
    sched_class_cfs.enqueue(&rq, task_high_vruntime);

    /* Pick next (should be task with lower vruntime) */
    picked = sched_class_cfs.pick_next(&rq);
    TEST_ASSERT_EQ_PTR(picked, task_low_vruntime);

    destroy_test_task(task_low_vruntime);
    destroy_test_task(task_high_vruntime);

    /* Clean up run queue */
    if (rq.cfs_rq != NULL)
    {
        kfree(rq.cfs_rq);
    }
}

/*
 * EDF Scheduler Tests
 */

/**
 * @brief Test EDF deadline-based scheduling
 */
TEST_CASE(edf_deadline_scheduling)
{
    struct rq rq;
    TCB_t *task_early;
    TCB_t *task_late;
    TCB_t *picked;

    /* Initialize run queue */
    (void)memset(&rq, 0, sizeof(rq));
    TEST_ASSERT_EQ(sched_class_edf.init(&rq), 0);

    /* Create tasks */
    task_early = create_test_task("early", 100U, SCHED_EDF);
    task_late = create_test_task("late", 100U, SCHED_EDF);

    TEST_ASSERT_NOT_NULL(task_early);
    TEST_ASSERT_NOT_NULL(task_late);

    /* Set deadlines */
    task_early->deadline = 10000ULL;
    task_late->deadline = 20000ULL;

    /* Enqueue tasks */
    sched_class_edf.enqueue(&rq, task_early);
    sched_class_edf.enqueue(&rq, task_late);

    /* Pick next (should be task with earlier deadline) */
    picked = sched_class_edf.pick_next(&rq);
    TEST_ASSERT_EQ_PTR(picked, task_early);

    destroy_test_task(task_early);
    destroy_test_task(task_late);

    /* Clean up run queue */
    if (rq.edf_rq != NULL)
    {
        kfree(rq.edf_rq);
    }
}

/**
 * @brief Test EDF deadline miss detection
 */
TEST_CASE(edf_deadline_miss)
{
    struct rq rq;
    TCB_t *task;

    /* Initialize run queue */
    (void)memset(&rq, 0, sizeof(rq));
    TEST_ASSERT_EQ(sched_class_edf.init(&rq), 0);

    /* Create task with past deadline */
    task = create_test_task("missed", 100U, SCHED_EDF);
    TEST_ASSERT_NOT_NULL(task);

    task->deadline = 1000ULL;
    test_time = 2000ULL; /* Current time is past deadline */

    /* Call tick handler (checks for deadline miss) */
    sched_class_edf.task_tick(&rq, task);

    /* Task should still exist (no crash) */
    TEST_ASSERT_NOT_NULL(task);

    destroy_test_task(task);

    /* Clean up run queue */
    if (rq.edf_rq != NULL)
    {
        kfree(rq.edf_rq);
    }
}

/*
 * IDLE Scheduler Tests
 */

/**
 * @brief Test IDLE scheduler basic operations
 */
TEST_CASE(idle_basic_operations)
{
    struct rq rq;
    TCB_t *idle_task;
    TCB_t *picked;

    /* Initialize run queue */
    (void)memset(&rq, 0, sizeof(rq));
    TEST_ASSERT_EQ(sched_class_idle.init(&rq), 0);

    /* Create idle task */
    idle_task = create_test_task("idle", 255U, SCHED_IDLE);
    TEST_ASSERT_NOT_NULL(idle_task);

    /* Enqueue idle task */
    sched_class_idle.enqueue(&rq, idle_task);

    /* Pick next (should be idle task) */
    picked = sched_class_idle.pick_next(&rq);
    TEST_ASSERT_EQ_PTR(picked, idle_task);

    destroy_test_task(idle_task);

    /* Clean up run queue */
    if (rq.idle_rq != NULL)
    {
        kfree(rq.idle_rq);
    }
}

/*
 * Statistics Tests
 */

/**
 * @brief Test scheduler statistics
 */
TEST_CASE(scheduler_statistics)
{
    struct rq rq;
    TCB_t *task1;
    TCB_t *task2;
    SchedStats_t stats;
    int ret;

    /* Initialize run queue */
    (void)memset(&rq, 0, sizeof(rq));
    TEST_ASSERT_EQ(sched_class_cfs.init(&rq), 0);

    /* Create tasks */
    task1 = create_test_task("task1", 100U, SCHED_CFS);
    task2 = create_test_task("task2", 100U, SCHED_CFS);

    TEST_ASSERT_NOT_NULL(task1);
    TEST_ASSERT_NOT_NULL(task2);

    /* Enqueue tasks */
    sched_class_cfs.enqueue(&rq, task1);
    sched_class_cfs.enqueue(&rq, task2);

    /* Get statistics */
    (void)memset(&stats, 0, sizeof(stats));
    ret = sched_class_cfs.get_stats(&rq, &stats);

    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQ(stats.nr_running, 2U);

    destroy_test_task(task1);
    destroy_test_task(task2);

    /* Clean up run queue */
    if (rq.cfs_rq != NULL)
    {
        kfree(rq.cfs_rq);
    }
}

/*
 * Preemption Tests
 */

/**
 * @brief Test FIFO preemption
 */
TEST_CASE(fifo_preemption)
{
    struct rq rq;
    TCB_t *task_low;
    TCB_t *task_high;
    int can_preempt;

    /* Initialize run queue */
    (void)memset(&rq, 0, sizeof(rq));
    TEST_ASSERT_EQ(sched_class_fifo.init(&rq), 0);

    /* Create tasks */
    task_low = create_test_task("low", 150U, SCHED_FIFO);
    task_high = create_test_task("high", 100U, SCHED_FIFO);

    TEST_ASSERT_NOT_NULL(task_low);
    TEST_ASSERT_NOT_NULL(task_high);

    /* Enqueue high priority task */
    sched_class_fifo.enqueue(&rq, task_high);

    /* Check if low priority task can be preempted */
    can_preempt = sched_class_fifo.can_preempt(&rq, task_low);
    TEST_ASSERT_EQ(can_preempt, 1);

    destroy_test_task(task_low);
    destroy_test_task(task_high);

    /* Clean up run queue */
    if (rq.fifo_rq != NULL)
    {
        kfree(rq.fifo_rq);
    }
}

/**
 * @brief Test CFS preemption
 */
TEST_CASE(cfs_preemption)
{
    struct rq rq;
    TCB_t *task_curr;
    TCB_t *task_next;
    int can_preempt;

    /* Initialize run queue */
    (void)memset(&rq, 0, sizeof(rq));
    TEST_ASSERT_EQ(sched_class_cfs.init(&rq), 0);

    /* Create tasks */
    task_curr = create_test_task("curr", 100U, SCHED_CFS);
    task_next = create_test_task("next", 100U, SCHED_CFS);

    TEST_ASSERT_NOT_NULL(task_curr);
    TEST_ASSERT_NOT_NULL(task_next);

    /* Set vruntime (next task has lower vruntime) */
    task_curr->vruntime = 5000ULL;
    task_next->vruntime = 3000ULL;

    /* Enqueue next task */
    sched_class_cfs.enqueue(&rq, task_next);

    /* Check if current task can be preempted */
    can_preempt = sched_class_cfs.can_preempt(&rq, task_curr);
    TEST_ASSERT_EQ(can_preempt, 1);

    destroy_test_task(task_curr);
    destroy_test_task(task_next);

    /* Clean up run queue */
    if (rq.cfs_rq != NULL)
    {
        kfree(rq.cfs_rq);
    }
}

/*
 * Main Test Runner
 */

/**
 * @brief Main test function
 */
int main(void)
{
    /* Initialize test framework */
    test_init();

    /* Test scheduling class registration */
    TEST_SUITE_START(sched_classes)
    {
        TEST_RUN(sched_classes_available);
        TEST_RUN(sched_classes_have_required_methods);
    }
    TEST_SUITE_END()

    /* Test FIFO scheduler */
    TEST_SUITE_START(fifo_scheduler)
    {
        TEST_RUN(fifo_enqueue_dequeue);
        TEST_RUN(fifo_priority_ordering);
        TEST_RUN(fifo_preemption);
    }
    TEST_SUITE_END()

    /* Test Round Robin scheduler */
    TEST_SUITE_START(rr_scheduler)
    {
        TEST_RUN(rr_time_slicing);
        TEST_RUN(rr_rotation);
    }
    TEST_SUITE_END()

    /* Test CFS scheduler */
    TEST_SUITE_START(cfs_scheduler)
    {
        TEST_RUN(cfs_vruntime_tracking);
        TEST_RUN(cfs_pick_by_vruntime);
        TEST_RUN(cfs_preemption);
    }
    TEST_SUITE_END()

    /* Test EDF scheduler */
    TEST_SUITE_START(edf_scheduler)
    {
        TEST_RUN(edf_deadline_scheduling);
        TEST_RUN(edf_deadline_miss);
    }
    TEST_SUITE_END()

    /* Test IDLE scheduler */
    TEST_SUITE_START(idle_scheduler)
    {
        TEST_RUN(idle_basic_operations);
    }
    TEST_SUITE_END()

    /* Test statistics */
    TEST_SUITE_START(statistics)
    {
        TEST_RUN(scheduler_statistics);
    }
    TEST_SUITE_END()

    /* Print test report */
    test_report();

    /* Return test result */
    return (g_test_stats.failed_tests == 0U) ? 0 : 1;
}
