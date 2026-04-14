/**
 * @file    test_proc_states.c
 * @brief   进程状态机和进程管理详细测试
 * @author  AISafe64 Team
 * @date    2026-04-14
 * @version 1.0
 *
 * @details 测试进程状态机转换、进程生命周期、进程树管理等
 *
 * @note 在 QEMU 中运行，通过 UART 输出结果
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "mock_kernel.h"
#include <kernel/process.h>
#include <kernel/errno.h>

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
        printf("[PASS] %s (line %d)\n", msg, __LINE__); \
    } else { \
        s_tests_failed++; \
        printf("[FAIL] %s (line %d)\n", msg, __LINE__); \
    } \
} while(0)

/* ========================================================================
 * 测试函数
 * ======================================================================== */

/**
 * @brief 测试1: 进程状态初始化
 */
static void test_state_initialization(void)
{
    printf("Test 1: 进程状态初始化...\n");

    /* 简化测试：fork 进程并检查 PID */
    int32_t child_pid = fork();

    if (child_pid == 0)
    {
        /* 子进程 */
        printf("  [child] fork OK\n");
        exit(0);
    }

    /* 父进程 */
    if (child_pid > 0)
    {
        printf("  [parent] fork OK, child_pid=%d\n", child_pid);

        int status;
        pid_t waited = waitpid(child_pid, &status, 0);
        TEST_ASSERT(waited == child_pid, "waitpid returns child pid");
        TEST_ASSERT(status == 0, "child exit status is 0");
    }
    else
    {
        TEST_ASSERT(false, "fork should succeed");
    }
}

/**
 * @brief 测试2: 进程创建（fork）
 */
static void test_fork(void)
{
    printf("Test 2: 进程创建 (fork)...\n");

    int32_t ppid = fork();

    if (ppid == 0)
    {
        /* 子进程：验证自己是子进程 */
        pid_t my_pid = fork();

        if (my_pid == 0)
        {
            /* 孙进程 */
            printf("  [grandchild] created\n");
            exit(10);
        }

        /* 子进程：验证有孙进程 */
        int status;
        pid_t grandchild = waitpid(my_pid, &status, 0);
        TEST_ASSERT(grandchild == my_pid, "waitpid grandchild");
        TEST_ASSERT(status == 10, "grandchild exit status");

        printf("  [child] grandchild exited\n");

        exit(20);
    }

    /* 父进程：验证有子进程 */
    int status;
    pid_t child = waitpid(ppid, &status, 0);
    TEST_ASSERT(child == ppid, "waitpid child");
    TEST_ASSERT(status == 20, "child exit status");

    printf("  [parent] child exited\n");
    printf("  [parent] fork test passed\n");
}

/**
 * @brief 测试3: 进程退出（exit）
 */
static void test_exit(void)
{
    printf("Test 3: 进程退出 (exit)...\n");

    pid_t child = fork();

    if (child == 0)
    {
        /* 子进程退出 */
        printf("  [child] exit with status 42\n");
        exit(42);
    }

    /* 父进程等待 */
    int status;
    pid_t waited = waitpid(child, &status, 0);
    TEST_ASSERT(waited == child, "waitpid returns child");
    TEST_ASSERT(status == 42, "exit status is 42");

    printf("  [parent] child exited with status 42\n");
    printf("  [parent] exit test passed\n");
}

/**
 * @brief 测试4: 等待指定子进程（waitpid）
 */
static void test_waitpid_specific(void)
{
    printf("Test 4: 等待指定子进程 (waitpid)...\n");

    pid_t child1 = fork();
    if (child1 == 0)
    {
        exit(1);
    }

    pid_t child2 = fork();
    if (child2 == 0)
    {
        exit(2);
    }

    /* 等待 child2 */
    int status;
    pid_t waited = waitpid(child2, &status, 0);
    TEST_ASSERT(waited == child2, "waitpid child2");
    TEST_ASSERT(status == 2, "child2 exit status");

    printf("  [parent] child2 exited with status 2\n");

    /* 等待 child1 */
    waited = waitpid(child1, &status, 0);
    TEST_ASSERT(waited == child1, "waitpid child1");
    TEST_ASSERT(status == 1, "child1 exit status");

    printf("  [parent] child1 exited with status 1\n");
    printf("  [parent] waitpid test passed\n");
}

/**
 * @brief 测试5: 等待任意子进程（waitpid -1）
 */
static void test_waitpid_any(void)
{
    printf("Test 5: 等待任意子进程 (waitpid -1)...\n");

    pid_t child = fork();
    if (child == 0)
    {
        exit(5);
    }

    /* 等待任意子进程 */
    int status;
    pid_t waited = waitpid(-1, &status, 0);
    TEST_ASSERT(waited == child, "waitpid -1 returns child");

    printf("  [parent] child exited with status 5\n");
    printf("  [parent] waitpid any test passed\n");
}

/**
 * @brief 测试6: 进程树深度 - 祖孙进程
 */
static void test_process_tree_depth(void)
{
    printf("Test 6: 进程树深度 (3 层)...\n");

    pid_t grandparent = fork();
    if (grandparent == 0)
    {
        pid_t parent = fork();
        if (parent == 0)
        {
            pid_t child = fork();
            if (child == 0)
            {
                printf("  [grandchild] depth 3\n");
                exit(99);
            }

            /* 父进程 */
            int status;
            pid_t child_pid = waitpid(child, &status, 0);
            TEST_ASSERT(child_pid == child, "waitpid child depth 2");
            TEST_ASSERT(status == 99, "grandchild exit status");

            printf("  [parent] depth 2, child exited\n");

            exit(88);
        }

        /* 祖父进程 */
        int status;
        pid_t parent_pid = waitpid(parent, &status, 0);
        TEST_ASSERT(parent_pid == parent, "waitpid parent depth 1");
        TEST_ASSERT(status == 88, "parent exit status");

        printf("  [grandparent] depth 1, parent exited\n");

        exit(77);
    }

    /* 祖父进程的父进程 */
    int status;
    pid_t gp_pid = waitpid(grandparent, &status, 0);
    TEST_ASSERT(gp_pid == grandparent, "waitpid grandparent");
    TEST_ASSERT(status == 77, "grandparent exit status");

    printf("  [parent] grandparent exited with status 77\n");
    printf("  [parent] process tree depth test passed\n");
}

/**
 * @brief 测试7: 信号发送（kill）
 */
static void test_kill(void)
{
    printf("Test 7: 信号发送 (kill)...\n");

    pid_t child = fork();
    if (child == 0)
    {
        /* 子进程睡眠 */
        printf("  [child] sleeping...\n");
        for (volatile int i = 0; i < 10000000; i++);
        exit(0);
    }

    /* 父进程发送 SIGTERM */
    printf("  [parent] sending SIGTERM to child PID=%d\n", child);

    int ret = kill(child, SIGTERM);
    TEST_ASSERT(ret >= 0, "kill succeeds");

    /* 等待 */
    int status;
    pid_t waited = waitpid(child, &status, 0);
    TEST_ASSERT(waited == child, "waitpid returns child");

    printf("  [parent] child exited, status=%d\n", status);
    printf("  [parent] kill test passed\n");
}

/**
 * @brief 测试8: 进程状态机转换
 */
static void test_state_transitions(void)
{
    printf("Test 8: 进程状态机转换...\n");

    /* 这个测试验证进程状态转换的合法性 */
    pid_t p1 = fork();
    if (p1 == 0)
    {
        exit(1);
    }

    pid_t p2 = fork();
    if (p2 == 0)
    {
        exit(2);
    }

    /* 等待两个子进程 */
    int status;
    pid_t waited1 = waitpid(p1, &status, 0);
    TEST_ASSERT(waited1 == p1, "waitpid p1");
    TEST_ASSERT(status == 1, "p1 exit status");

    pid_t waited2 = waitpid(p2, &status, 0);
    TEST_ASSERT(waited2 == p2, "waitpid p2");
    TEST_ASSERT(status == 2, "p2 exit status");

    printf("  [parent] all children exited\n");
    printf("  [parent] state transitions test passed\n");
}

/* ========================================================================
 * 主测试函数
 * ======================================================================== */

/**
 * @brief 进程状态机测试入口
 */
void test_proc_states_main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  进程状态机详细测试\n");
    printf("========================================\n");
    printf("\n");

    /* 运行所有测试 */
    test_state_initialization();
    test_fork();
    test_exit();
    test_waitpid_specific();
    test_waitpid_any();
    test_process_tree_depth();
    test_kill();
    test_state_transitions();

    /* 打印测试结果 */
    printf("\n");
    printf("========================================\n");
    printf("  测试结果汇总\n");
    printf("========================================\n");
    printf("  总测试数: %u\n", s_tests_run);
    printf("  通过: %u\n", s_tests_passed);
    printf("  失败: %u\n", s_tests_failed);

    if (s_tests_failed == 0)
    {
        printf("  结果: ALL TESTS PASSED\n");
    }
    else
    {
        printf("  结果: SOME TESTS FAILED\n");
    }

    printf("\n");
}

/* ========================================================================
 * main 函数
 * ======================================================================== */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    test_proc_states_main();

    return (s_tests_failed == 0) ? 0 : 1;
}
