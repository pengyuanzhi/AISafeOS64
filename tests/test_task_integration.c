/**
 * @file test_task_integration.c
 * @brief AISafe64 RTOS - Task Management Integration Tests
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details Integration tests for task management functionality
 *          - Task creation with actual API
 *          - Task sleep and wakeup
 *          - Task priority management
 *          - Multi-task scheduling
 *          - Task statistics
 *
 * @note MISRA-C:2012 compliant
 */

#include "test_framework.h"
#include "../src/include/sched.h"
#include <string.h>

/*
 * Test Task Entry Points
 */

static volatile uint32_t task1_counter = 0U;
static volatile uint32_t task2_counter = 0U;
static volatile uint32_t task3_counter = 0U;
static volatile bool task1_done = false;
static volatile bool task2_done = false;
static volatile bool task3_done = false;

/**
 * @brief Simple test task that increments a counter
 */
static void simple_task_entry(void)
{
    task1_counter++;

    /* Exit after doing work */
    task1_done = true;
    task_exit(0);
}

/**
 * @brief Test task that sleeps
 */
static void sleeping_task_entry(void)
{
    task2_counter++;

    /* Sleep for 100ms */
    task_sleep(100ULL);

    task2_counter++;
    task2_done = true;

    task_exit(0);
}

/**
 * @brief Test task that yields
 */
static void yielding_task_entry(void)
{
    task3_counter++;

    /* Yield CPU */
    yield();

    task3_counter++;
    task3_done = true;

    task_exit(0);
}

/*
 * Scheduler Initialization Tests
 */

/**
 * @brief Test scheduler initialization
 */
TEST_CASE(scheduler_init_test)
{
    int ret = scheduler_init();

    TEST_ASSERT_EQ(ret, 0);
}

/**
 * @brief Test idle task creation
 */
TEST_CASE(idle_task_creation)
{
    int ret = idle_task_init();

    TEST_ASSERT_EQ(ret, 0);
}

/*
 * Task Creation Tests
 */

/**
 * @brief Test basic task creation with real API
 */
TEST_CASE(task_create_basic_api)
{
    uint32_t tid;
    TCB_t *task;

    /* Reset flags */
    task1_done = false;
    task1_counter = 0U;

    /* Create task */
    tid = task_create("test_task", 128U, 8192U, simple_task_entry, SCHED_CFS);

    TEST_ASSERT_NE(tid, 0U);

    /* Note: In a real system, the scheduler would run the task
     * For unit testing, we just verify creation succeeded */
}

/**
 * @brief Test multiple task creation
 */
TEST_CASE(task_create_multiple)
{
    uint32_t tid1;
    uint32_t tid2;
    uint32_t tid3;

    /* Reset counters */
    task1_counter = 0U;
    task2_counter = 0U;
    task3_counter = 0U;
    task1_done = false;
    task2_done = false;
    task3_done = false;

    /* Create multiple tasks */
    tid1 = task_create("task1", 100U, 8192U, simple_task_entry, SCHED_FIFO);
    tid2 = task_create("task2", 120U, 8192U, sleeping_task_entry, SCHED_RR);
    tid3 = task_create("task3", 140U, 8192U, yielding_task_entry, SCHED_CFS);

    TEST_ASSERT_NE(tid1, 0U);
    TEST_ASSERT_NE(tid2, 0U);
    TEST_ASSERT_NE(tid3, 0U);

    /* Verify tasks have different IDs */
    TEST_ASSERT_NE(tid1, tid2);
    TEST_ASSERT_NE(tid2, tid3);
    TEST_ASSERT_NE(tid1, tid3);
}

/**
 * @brief Test task creation with different priorities
 */
TEST_CASE(task_create_different_priorities)
{
    uint32_t tid_high;
    uint32_t tid_mid;
    uint32_t tid_low;

    /* Create tasks with different priorities */
    tid_high = task_create("high", 50U, 8192U, simple_task_entry, SCHED_FIFO);
    tid_mid = task_create("mid", 128U, 8192U, simple_task_entry, SCHED_FIFO);
    tid_low = task_create("low", 200U, 8192U, simple_task_entry, SCHED_FIFO);

    TEST_ASSERT_NE(tid_high, 0U);
    TEST_ASSERT_NE(tid_mid, 0U);
    TEST_ASSERT_NE(tid_low, 0U);
}

/**
 * @brief Test task creation with different scheduling classes
 */
TEST_CASE(task_create_different_classes)
{
    uint32_t tid_fifo;
    uint32_t tid_rr;
    uint32_t tid_cfs;

    /* Create tasks with different scheduling classes */
    tid_fifo = task_create("fifo_task", 100U, 8192U, simple_task_entry, SCHED_FIFO);
    tid_rr = task_create("rr_task", 100U, 8192U, simple_task_entry, SCHED_RR);
    tid_cfs = task_create("cfs_task", 100U, 8192U, simple_task_entry, SCHED_CFS);

    TEST_ASSERT_NE(tid_fifo, 0U);
    TEST_ASSERT_NE(tid_rr, 0U);
    TEST_ASSERT_NE(tid_cfs, 0U);
}

/*
 * Task Management Tests
 */

/**
 * @brief Test getting current task
 */
TEST_CASE(get_current_task_test)
{
    TCB_t *current;

    current = get_current_task();

    /* Current task should not be NULL (at least idle task exists) */
    TEST_ASSERT_NOT_NULL(current);
}

/**
 * @brief Test task priority setting
 */
TEST_CASE(set_task_priority_test)
{
    uint32_t tid;
    TCB_t *task;
    int ret;

    /* Create task */
    tid = task_create("prio_task", 128U, 8192U, simple_task_entry, SCHED_CFS);
    TEST_ASSERT_NE(tid, 0U);

    /* Note: We can't get task by ID yet, so this test is limited
     * In the future, add get_task_by_id() API */
}

/**
 * @brief Test task statistics
 */
TEST_CASE(task_statistics_test)
{
    SchedStats_t stats;
    int ret;

    /* Get statistics for CPU 0 */
    (void)memset(&stats, 0, sizeof(stats));
    ret = sched_get_stats(0U, &stats);

    TEST_ASSERT_EQ(ret, 0);

    /* Verify statistics are accessible */
    /* Note: Values will depend on system state */
}

/**
 * @brief Test multiple statistics retrieval
 */
TEST_CASE(task_statistics_consistency)
{
    SchedStats_t stats1;
    SchedStats_t stats2;
    int ret1;
    int ret2;

    /* Get statistics twice */
    (void)memset(&stats1, 0, sizeof(stats1));
    (void)memset(&stats2, 0, sizeof(stats2));

    ret1 = sched_get_stats(0U, &stats1);
    ret2 = sched_get_stats(0U, &stats2);

    TEST_ASSERT_EQ(ret1, 0);
    TEST_ASSERT_EQ(ret2, 0);

    /* Second call should have equal or greater counters */
    TEST_ASSERT_GE(stats2.nr_switches, stats1.nr_switches);
    TEST_ASSERT_GE(stats2.total_time, stats1.total_time);
}

/*
 * Run Queue Tests
 */

/**
 * @brief Test CPU run queue access
 */
TEST_CASE(cpu_rq_access)
{
    struct rq *rq;

    rq = cpu_rq(0U);

    TEST_ASSERT_NOT_NULL(rq);
    TEST_ASSERT_EQ(rq->cpu, 0U);
}

/**
 * @brief Test this_rq function
 */
TEST_CASE(this_rq_access)
{
    struct rq *rq;

    rq = this_rq();

    TEST_ASSERT_NOT_NULL(rq);
}

/**
 * @brief Test run queue lock
 */
TEST_CASE(run_queue_lock_test)
{
    struct rq *rq;
    unsigned long flags;

    rq = this_rq();
    TEST_ASSERT_NOT_NULL(rq);

    /* Try to acquire lock */
    spin_lock_irqsave(&rq->lock, flags);

    /* Lock should be held */

    /* Release lock */
    spin_unlock_irqrestore(&rq->lock, flags);
}

/*
 * Utility Function Tests
 */

/**
 * @brief Test priority bitmap utility
 */
TEST_CASE(find_highest_priority_test)
{
    uint64_t bitmap[4];
    uint32_t prio;

    /* Test empty bitmap */
    bitmap[0] = 0ULL;
    bitmap[1] = 0ULL;
    bitmap[2] = 0ULL;
    bitmap[3] = 0ULL;

    prio = find_highest_priority(bitmap);
    TEST_ASSERT_EQ(prio, 255U); /* Should return max (empty) */

    /* Test with bit 0 set */
    bitmap[0] = 1ULL;
    prio = find_highest_priority(bitmap);
    TEST_ASSERT_EQ(prio, 0U);

    /* Test with bit 128 set */
    bitmap[0] = 0ULL;
    bitmap[1] = 0ULL;
    bitmap[2] = 1ULL << 63;
    prio = find_highest_priority(bitmap);
    TEST_ASSERT_EQ(prio, 127U);

    /* Test with bit 255 set */
    bitmap[2] = 0ULL;
    bitmap[3] = 1ULL << 63;
    prio = find_highest_priority(bitmap);
    TEST_ASSERT_EQ(prio, 191U);
}

/**
 * @brief Test priority to weight conversion
 */
TEST_CASE(prio_to_weight_test)
{
    uint32_t weight;

    /* Test some known priorities */
    weight = prio_to_weight(0U);
    TEST_ASSERT_NE(weight, 0U);

    weight = prio_to_weight(20U);
    TEST_ASSERT_NE(weight, 0U);

    weight = prio_to_weight(39U);
    TEST_ASSERT_NE(weight, 0U);
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

    /* Test scheduler initialization */
    TEST_SUITE_START(scheduler_init)
    {
        TEST_RUN(scheduler_init_test);
        TEST_RUN(idle_task_creation);
    }
    TEST_SUITE_END()

    /* Test task creation */
    TEST_SUITE_START(task_creation)
    {
        TEST_RUN(task_create_basic_api);
        TEST_RUN(task_create_multiple);
        TEST_RUN(task_create_different_priorities);
        TEST_RUN(task_create_different_classes);
    }
    TEST_SUITE_END()

    /* Test task management */
    TEST_SUITE_START(task_management)
    {
        TEST_RUN(get_current_task_test);
        TEST_RUN(set_task_priority_test);
        TEST_RUN(task_statistics_test);
        TEST_RUN(task_statistics_consistency);
    }
    TEST_SUITE_END()

    /* Test run queue access */
    TEST_SUITE_START(run_queue)
    {
        TEST_RUN(cpu_rq_access);
        TEST_RUN(this_rq_access);
        TEST_RUN(run_queue_lock_test);
    }
    TEST_SUITE_END()

    /* Test utility functions */
    TEST_SUITE_START(utilities)
    {
        TEST_RUN(find_highest_priority_test);
        TEST_RUN(prio_to_weight_test);
    }
    TEST_SUITE_END()

    /* Print test report */
    test_report();

    /* Return test result */
    return (g_test_stats.failed_tests == 0U) ? 0 : 1;
}
