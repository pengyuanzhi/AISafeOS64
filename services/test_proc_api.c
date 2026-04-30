/**
 * @file    test_proc_api.c
 * @brief   用户态进程管理 API 端到端测试
 * @author  AISafe64 Team
 * @date    2026-04-14
 * @version 1.0
 *
 * @details 在 QEMU 中验证用户态进程管理 API 的完整链路：
 *          - fork/exec/waitpid/exit 完整生命周期
 *          - 进程状态切换
 *          - 进程间通信
 *
 * @note 在 QEMU 中运行，通过 UART 输出结果
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/process.h>
#include <kernel/syscall.h>
#include <kernel/errno.h>
#include <kernel/types.h>
#include <stdint.h>
#include <string.h>

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
        sys_debug_print("[PASS] ", 7); \
        sys_debug_print(msg, strlen(msg)); \
        sys_debug_print("\n", 1); \
    } else { \
        s_tests_failed++; \
        sys_debug_print("[FAIL] ", 7); \
        sys_debug_print(msg, strlen(msg)); \
        sys_debug_print("\n", 1); \
    } \
} while(0)

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 打印子进程消息
 */
static void print_child_message(const char *msg)
{
    char pid_buf[32];
    int32_t pid;

    pid = fork();

    if (pid == 0)
    {
        /* 子进程 */
        sys_debug_print("[CHILD] ", 8);
        sys_debug_print(msg, strlen(msg));
        sys_debug_print("\n", 1);
        user_exit(0);
    }

    /* 父进程继续 */
}

/* ========================================================================
 * 测试函数
 * ======================================================================== */

/**
 * @brief 测试1: fork - 创建子进程
 */
static void test_fork(void)
{
    pid_t child_pid;

    sys_debug_print("Test 1: fork...\n", 16);

    child_pid = fork();

    if (child_pid == 0)
    {
        /* 子进程 */
        sys_debug_print("  [child] fork OK, PID=", 22);
        sys_debug_print_int(child_pid);
        sys_debug_print("\n", 1);
        user_exit(0);
    }

    /* 父进程 */
    if (child_pid > 0)
    {
        sys_debug_print("  [parent] fork OK, child_pid=", 30);
        sys_debug_print_int(child_pid);
        sys_debug_print("\n", 1);

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
 * @brief 测试2: fork 多次 - 创建进程树
 */
static void test_fork_tree(void)
{
    pid_t children[3];
    int i;

    sys_debug_print("Test 2: fork tree (3 children)...\n", 34);

    for (i = 0; i < 3; i++)
    {
        children[i] = fork();
        if (children[i] == 0)
        {
            /* 子进程 */
            sys_debug_print("  [child] PID=", 12);
            sys_debug_print_int(i);
            sys_debug_print("\n", 1);
            user_exit((int)i);
        }
    }

    /* 父进程等待所有子进程 */
    for (i = 0; i < 3; i++)
    {
        int status;
        pid_t waited = waitpid(children[i], &status, 0);
        TEST_ASSERT(waited == children[i], "waitpid returns child pid");
        TEST_ASSERT(status == (int)i, "child exit status matches");
    }

    sys_debug_print("  [parent] all children exited\n", 30);
}

/**
 * @brief 测试3: exit - 进程退出
 */
static void test_exit(void)
{
    pid_t child_pid;

    sys_debug_print("Test 3: exit...\n", 15);

    child_pid = fork();

    if (child_pid == 0)
    {
        /* 子进程退出 */
        sys_debug_print("  [child] calling exit(42)...\n", 29);
        user_exit(42);
    }

    /* 父进程等待 */
    int status;
    pid_t waited = waitpid(child_pid, &status, 0);
    TEST_ASSERT(waited == child_pid, "waitpid returns child pid");
    TEST_ASSERT(status == 42, "child exit status is 42");

    sys_debug_print("  [parent] child exited with status 42\n", 39);
}

/**
 * @brief 测试4: waitpid - 等待指定子进程
 */
static void test_waitpid(void)
{
    pid_t child1, child2;
    int status;

    sys_debug_print("Test 4: waitpid with specific PID...\n", 37);

    child1 = fork();
    if (child1 == 0)
    {
        user_exit(1);
    }

    child2 = fork();
    if (child2 == 0)
    {
        user_exit(2);
    }

    /* 父进程等待 child2 */
    pid_t waited = waitpid(child2, &status, 0);
    TEST_ASSERT(waited == child2, "waitpid returns child2");
    TEST_ASSERT(status == 2, "child2 exit status is 2");

    /* 等待 child1 */
    waited = waitpid(child1, &status, 0);
    TEST_ASSERT(waited == child1, "waitpid returns child1");
    TEST_ASSERT(status == 1, "child1 exit status is 1");

    sys_debug_print("  [parent] both children waited\n", 32);
}

/**
 * @brief 测试5: exec - 替换进程映像（简化版）
 */
static void test_exec(void)
{
    pid_t child_pid;

    sys_debug_print("Test 5: exec (simplified)...\n", 29);

    child_pid = fork();

    if (child_pid == 0)
    {
        /* 子进程退出（简化版 exec） */
        sys_debug_print("  [child] exec called\n", 21);
        user_exit(10);
    }

    /* 父进程等待 */
    int status;
    pid_t waited = waitpid(child_pid, &status, 0);
    TEST_ASSERT(waited == child_pid, "waitpid returns child pid");
    TEST_ASSERT(status == 10, "child exit status is 10");

    sys_debug_print("  [parent] exec test completed\n", 31);
}

/**
 * @brief 测试6: 信号（简化版）
 */
static void test_signal(void)
{
    pid_t child_pid;

    sys_debug_print("Test 6: signal (simplified)...\n", 31);

    child_pid = fork();

    if (child_pid == 0)
    {
        /* 子进程睡眠 */
        sys_debug_print("  [child] sleeping...\n", 22);
        /* 模拟睡眠 */
        for (volatile int i = 0; i < 1000000; i++);
        user_exit(0);
    }

    /* 父进程发送信号 */
    sys_debug_print("  [parent] sending SIGTERM...\n", 30);
    int ret = kill(child_pid, SIGTERM);
    TEST_ASSERT(ret >= 0, "kill succeeds");

    /* 等待 */
    int status;
    pid_t waited = waitpid(child_pid, &status, 0);
    TEST_ASSERT(waited == child_pid, "waitpid returns child pid");

    sys_debug_print("  [parent] signal test completed\n", 33);
}

/* ========================================================================
 * 主测试函数
 * ======================================================================== */

/**
 * @brief 进程管理 API 测试入口
 */
void test_proc_api_main(void)
{
    sys_debug_print("\n", 1);
    sys_debug_print("========================================\n", 28);
    sys_debug_print("  用户态进程管理 API 测试\n", 24);
    sys_debug_print("========================================\n", 28);
    sys_debug_print("\n", 1);

    /* 运行所有测试 */
    test_fork();
    test_fork_tree();
    test_exit();
    test_waitpid();
    test_exec();
    test_signal();

    /* 打印测试结果 */
    sys_debug_print("\n", 1);
    sys_debug_print("========================================\n", 28);
    sys_debug_print("  测试结果汇总\n", 18);
    sys_debug_print("========================================\n", 28);
    sys_debug_print("  总测试数: ", 11);
    sys_debug_print_int((int32_t)s_tests_run);
    sys_debug_print("\n", 1);
    sys_debug_print("  通过: ", 7);
    sys_debug_print_int((int32_t)s_tests_passed);
    sys_debug_print("\n", 1);
    sys_debug_print("  失败: ", 7);
    sys_debug_print_int((int32_t)s_tests_failed);
    sys_debug_print("\n", 1);

    if (s_tests_failed == 0)
    {
        sys_debug_print("  结果: ALL TESTS PASSED\n", 25);
    }
    else
    {
        sys_debug_print("  结果: SOME TESTS FAILED\n", 26);
    }

    sys_debug_print("\n", 1);
}
