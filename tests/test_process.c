/**
 * @file    test_process.c
 * @brief   进程管理服务单元测试
 * @author  AISafe64 Team
 * @date    2026-04-08
 *
 * @details 宿主机单元测试：进程管理 SVC 分发验证
 *          - SYS_THREAD_CREATE 创建线程
 *          - SYS_THREAD_EXIT 退出线程
 *          - SYS_THREAD_SUSPEND/RESUME 挂起/恢复
 *          - SYS_THREAD_SET_PRIORITY 设置优先级
 *          - SYS_THREAD_GET_ID 获取线程 ID
 *          - SYS_THREAD_YIELD 让出 CPU
 *
 * @note TDD: RED → GREEN → REFACTOR
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mock_kernel.h"

/* Mock 内核类型（mock_kernel.h 中未定义的） */
#ifndef KTHREAD_POLICY_FIFO
#define KTHREAD_POLICY_FIFO 0U
#endif
#ifndef KTHREAD_POLICY_RR
#define KTHREAD_POLICY_RR   1U
#endif
typedef uint32_t KThreadPolicy_t;
#ifndef THREAD_ID_INVALID
#define THREAD_ID_INVALID ((thread_id_t)0xFFFFFFFFU)
#endif
typedef void (*kthread_entry_t)(void *arg);
#ifndef CONFIG_STACK_SIZE_DEFAULT
#define CONFIG_STACK_SIZE_DEFAULT 8192U
#endif

/* ========================================================================
 * 测试计数器
 * ======================================================================== */

static uint32_t s_tests_run = 0U;
static uint32_t s_tests_passed = 0U;
static uint32_t s_tests_failed = 0U;

#define TEST_ASSERT(cond, msg) do { \
    s_tests_run++; \
    if (cond) { \
        s_tests_passed++; \
    } else { \
        s_tests_failed++; \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
    } \
} while(0)

/* ========================================================================
 * Mock 数据结构
 * ======================================================================== */

/** @brief 简化的 syscall_frame_t */
typedef struct
{
    uint64_t x0;
    uint64_t x1;
    uint64_t x2;
    uint64_t x3;
    uint64_t x8;
} test_frame_t;

/* Mock 内核状态 */
static uint32_t s_mock_thread_created = 0U;
static uint32_t s_mock_thread_exited = 0U;
static uint32_t s_mock_thread_suspended = 0U;
static uint32_t s_mock_thread_resumed = 0U;
static uint32_t s_mock_priority_set = 0U;
static uint32_t s_mock_yield_count = 0U;
static uint64_t s_mock_current_tid = 1U;

/* ========================================================================
 * Mock 内核 API
 * ======================================================================== */

static thread_id_t mock_kthread_create(const char *name,
                                        kthread_entry_t entry,
                                        void *arg,
                                        priority_t prio,
                                        KThreadPolicy_t policy,
                                        uint32_t stack_size)
{
    (void)name;
    (void)entry;
    (void)arg;
    (void)prio;
    (void)policy;
    (void)stack_size;
    s_mock_thread_created++;
    /* 返回一个有效的 thread_id */
    return (thread_id_t)(10U + s_mock_thread_created);
}

static void mock_kthread_exit(void)
{
    s_mock_thread_exited++;
}

static kernel_status_t mock_kthread_suspend(thread_id_t tid)
{
    (void)tid;
    s_mock_thread_suspended++;
    return KERNEL_OK;
}

static kernel_status_t mock_kthread_resume(thread_id_t tid)
{
    (void)tid;
    s_mock_thread_resumed++;
    return KERNEL_OK;
}

static kernel_status_t mock_kthread_set_priority(thread_id_t tid, priority_t prio)
{
    (void)tid;
    (void)prio;
    s_mock_priority_set++;
    return KERNEL_OK;
}

static thread_id_t mock_kthread_get_current_tid(void)
{
    return (thread_id_t)s_mock_current_tid;
}

/* ========================================================================
 * Mock SVC 分发器 (模拟 syscall_dispatch.c 的逻辑)
 * ======================================================================== */

#define SYS_THREAD_CREATE       0x0001U
#define SYS_THREAD_EXIT         0x0002U
#define SYS_THREAD_SUSPEND      0x0003U
#define SYS_THREAD_RESUME       0x0004U
#define SYS_THREAD_SET_PRIORITY 0x0005U
#define SYS_THREAD_SET_AFFINITY 0x0006U
#define SYS_THREAD_YIELD        0x0007U
#define SYS_THREAD_GET_ID       0x0008U

#define ENOSYS  38

/**
 * @brief 模拟 SVC 分发器的线程管理部分
 */
static void mock_syscall_dispatch(test_frame_t *frame)
{
    uint32_t syscall_nr = (uint32_t)frame->x8;

    switch (syscall_nr)
    {
        case SYS_THREAD_CREATE:
        {
            /* x0=entry, x1=arg, x2=priority */
            kthread_entry_t entry = (kthread_entry_t)(uintptr_t)frame->x0;
            void *arg = (void *)(uintptr_t)frame->x1;
            priority_t prio = (priority_t)frame->x2;
            thread_id_t tid;

            tid = mock_kthread_create("user", entry, arg, prio,
                                       KTHREAD_POLICY_RR,
                                       CONFIG_STACK_SIZE_DEFAULT);
            if (tid != THREAD_ID_INVALID)
            {
                frame->x0 = (uint64_t)tid;
            }
            else
            {
                frame->x0 = (uint64_t)(-(int64_t)ENOMEM);
            }
            break;
        }

        case SYS_THREAD_EXIT:
        {
            mock_kthread_exit();
            frame->x0 = 0U;
            break;
        }

        case SYS_THREAD_SUSPEND:
        {
            /* 内核 API 返回负错误码，直接传递 */
            thread_id_t tid = (thread_id_t)frame->x0;
            kernel_status_t ret = mock_kthread_suspend(tid);
            frame->x0 = (uint64_t)(int64_t)ret;
            break;
        }

        case SYS_THREAD_RESUME:
        {
            thread_id_t tid = (thread_id_t)frame->x0;
            kernel_status_t ret = mock_kthread_resume(tid);
            frame->x0 = (uint64_t)(int64_t)ret;
            break;
        }

        case SYS_THREAD_SET_PRIORITY:
        {
            thread_id_t tid = (thread_id_t)frame->x0;
            priority_t prio = (priority_t)frame->x1;
            kernel_status_t ret = mock_kthread_set_priority(tid, prio);
            frame->x0 = (uint64_t)(int64_t)ret;
            break;
        }

        case SYS_THREAD_SET_AFFINITY:
        {
            /* TODO: 需要亲和性 API */
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }

        case SYS_THREAD_YIELD:
        {
            s_mock_yield_count++;
            frame->x0 = 0U;
            break;
        }

        case SYS_THREAD_GET_ID:
        {
            frame->x0 = (uint64_t)mock_kthread_get_current_tid();
            break;
        }

        default:
        {
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }
    }
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试1: SYS_THREAD_CREATE — 创建线程
 */
static void test_thread_create(void)
{
    test_frame_t frame;

    printf("Test: SYS_THREAD_CREATE...\n");

    /* 正常创建 */
    memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_THREAD_CREATE;
    frame.x0 = 0x1234U; /* entry */
    frame.x1 = 0U;      /* arg */
    frame.x2 = 100U;    /* priority */

    s_mock_thread_created = 0U;
    mock_syscall_dispatch(&frame);

    TEST_ASSERT(s_mock_thread_created == 1U, "thread_create should be called once");
    TEST_ASSERT(frame.x0 >= 1U, "thread_create should return valid tid");
    printf("  created tid=%lu\n", (unsigned long)frame.x0);
}

/**
 * @brief 测试2: SYS_THREAD_EXIT — 退出线程
 */
static void test_thread_exit(void)
{
    test_frame_t frame;

    printf("Test: SYS_THREAD_EXIT...\n");

    memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_THREAD_EXIT;
    frame.x0 = 0U; /* status */

    s_mock_thread_exited = 0U;
    mock_syscall_dispatch(&frame);

    TEST_ASSERT(s_mock_thread_exited == 1U, "thread_exit should be called");
    TEST_ASSERT(frame.x0 == 0U, "thread_exit should return 0");
}

/**
 * @brief 测试3: SYS_THREAD_GET_ID — 获取线程 ID
 */
static void test_thread_get_id(void)
{
    test_frame_t frame;

    printf("Test: SYS_THREAD_GET_ID...\n");

    memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_THREAD_GET_ID;

    s_mock_current_tid = 42U;
    mock_syscall_dispatch(&frame);

    TEST_ASSERT(frame.x0 == 42U, "get_id should return current tid");
}

/**
 * @brief 测试4: SYS_THREAD_YIELD — 让出 CPU
 */
static void test_thread_yield(void)
{
    test_frame_t frame;

    printf("Test: SYS_THREAD_YIELD...\n");

    memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_THREAD_YIELD;

    s_mock_yield_count = 0U;
    mock_syscall_dispatch(&frame);

    TEST_ASSERT(s_mock_yield_count == 1U, "yield should increment counter");
    TEST_ASSERT(frame.x0 == 0U, "yield should return 0");
}

/**
 * @brief 测试5: SYS_THREAD_SUSPEND/RESUME — 挂起/恢复
 */
static void test_thread_suspend_resume(void)
{
    test_frame_t frame;

    printf("Test: SYS_THREAD_SUSPEND...\n");

    memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_THREAD_SUSPEND;
    frame.x0 = 5U; /* tid */

    s_mock_thread_suspended = 0U;
    mock_syscall_dispatch(&frame);

    TEST_ASSERT(s_mock_thread_suspended == 1U, "suspend should be called");
    TEST_ASSERT(frame.x0 == 0U, "suspend should return 0");

    printf("Test: SYS_THREAD_RESUME...\n");

    memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_THREAD_RESUME;
    frame.x0 = 5U; /* tid */

    s_mock_thread_resumed = 0U;
    mock_syscall_dispatch(&frame);

    TEST_ASSERT(s_mock_thread_resumed == 1U, "resume should be called");
    TEST_ASSERT(frame.x0 == 0U, "resume should return 0");
}

/**
 * @brief 测试6: SYS_THREAD_SET_PRIORITY — 设置优先级
 */
static void test_thread_set_priority(void)
{
    test_frame_t frame;

    printf("Test: SYS_THREAD_SET_PRIORITY...\n");

    memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_THREAD_SET_PRIORITY;
    frame.x0 = 5U;  /* tid */
    frame.x1 = 50U; /* new priority */

    s_mock_priority_set = 0U;
    mock_syscall_dispatch(&frame);

    TEST_ASSERT(s_mock_priority_set == 1U, "set_priority should be called");
    TEST_ASSERT(frame.x0 == 0U, "set_priority should return 0");
}

/**
 * @brief 测试7: 多次创建线程 — 验证 tid 递增
 */
static void test_multiple_create(void)
{
    test_frame_t frame;
    uint64_t prev_tid;
    uint32_t i;

    printf("Test: multiple thread create...\n");

    s_mock_thread_created = 0U;
    prev_tid = 0U;

    for (i = 0U; i < 5U; i++)
    {
        memset(&frame, 0, sizeof(frame));
        frame.x8 = SYS_THREAD_CREATE;
        frame.x0 = 0x1000U + (uint64_t)i;
        frame.x1 = 0U;
        frame.x2 = 100U;

        mock_syscall_dispatch(&frame);

        TEST_ASSERT(frame.x0 > prev_tid, "tid should be increasing");
        prev_tid = frame.x0;
    }

    TEST_ASSERT(s_mock_thread_created == 5U, "should create 5 threads");
}

/**
 * @brief 测试8: 完整生命周期 — create → get_id → yield → exit
 */
static void test_lifecycle(void)
{
    test_frame_t frame;

    printf("Test: full lifecycle...\n");

    /* create */
    memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_THREAD_CREATE;
    frame.x0 = 0x2000U;
    frame.x1 = 0U;
    frame.x2 = 50U;
    mock_syscall_dispatch(&frame);
    TEST_ASSERT(frame.x0 != THREAD_ID_INVALID, "create should succeed");

    /* get_id */
    s_mock_current_tid = frame.x0;
    memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_THREAD_GET_ID;
    mock_syscall_dispatch(&frame);
    TEST_ASSERT(frame.x0 == s_mock_current_tid, "get_id should match created tid");

    /* yield */
    memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_THREAD_YIELD;
    mock_syscall_dispatch(&frame);
    TEST_ASSERT(frame.x0 == 0U, "yield should succeed");

    /* exit */
    memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_THREAD_EXIT;
    mock_syscall_dispatch(&frame);
    TEST_ASSERT(frame.x0 == 0U, "exit should succeed");
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n========================================\n");
    printf("  Process Manager Unit Tests\n");
    printf("========================================\n\n");

    test_thread_create();
    test_thread_exit();
    test_thread_get_id();
    test_thread_yield();
    test_thread_suspend_resume();
    test_thread_set_priority();
    test_multiple_create();
    test_lifecycle();

    printf("\n========================================\n");
    printf("  Results: %u/%u passed, %u failed\n",
           s_tests_passed, s_tests_run, s_tests_failed);
    printf("========================================\n\n");

    return (s_tests_failed > 0U) ? 1 : 0;
}
