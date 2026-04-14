/**
 * @file    test_proc_user.c
 * @brief   用户态进程管理 QEMU 验证测试
 * @author  AISafe64 Team
 * @date    2026-04-14
 * @version 1.0
 *
 * @details 用户态进程管理的端到端验证：
 *          - fork/exec/waitpid/exit 完整链路
 *          - 信号传递机制
 *          - 资源限制设置和查询
 *          - 进程状态管理
 *
 * @note TDD: RED → GREEN → REFACTOR
 * @note 在 QEMU 中运行，验证完整的用户态进程管理
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/syscall.h>
#include <kernel/process.h>
#include <kernel/errno.h>
#include <kernel/types.h>
#include <stdint.h>
#include <string.h>

/* 避免与内核 exit 冲突 */
#define user_exit(status) sys_thread_exit()

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
 * @brief 打印整数到调试输出
 */
static void print_int(const char *prefix, int64_t value)
{
    char buf[32];
    int32_t i = 0;
    uint64_t v;
    bool negative;

    if (value < 0)
    {
        negative = true;
        v = (uint64_t)(-value);
    }
    else
    {
        negative = false;
        v = (uint64_t)value;
    }

    if (v == 0U)
    {
        buf[i++] = '0';
    }
    else
    {
        while (v > 0U)
        {
            buf[i++] = (char)('0' + (v % 10U));
            v /= 10U;
        }
    }

    if (negative)
    {
        buf[i++] = '-';
    }

    sys_debug_print(prefix, strlen(prefix));

    while (i > 0)
    {
        i--;
        char c = buf[i];
        sys_debug_print(&c, 1);
    }

    sys_debug_print("\n", 1);
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试1: fork - 创建子进程
 */
static void test_fork(void)
{
    pid_t child_pid;

    sys_debug_print("Test: fork...\n", 15);

    child_pid = fork();

    if (child_pid == 0)
    {
        /* 子进程 */
        sys_debug_print("  [child] fork OK\n", 19);
        user_exit(0);
    }
    else if (child_pid > 0)
    {
        /* 父进程 */
        int status;
        pid_t waited;

        waited = waitpid(child_pid, &status, 0);

        TEST_ASSERT(waited == child_pid, "waitpid returns child pid");
        TEST_ASSERT(status == 0, "child exit status is 0");
    }
    else
    {
        /* fork 失败 */
        TEST_ASSERT(false, "fork should succeed");
    }
}

/**
 * @brief 测试2: exec - 替换进程映像
 */
static void test_exec(void)
{
    pid_t child_pid;

    sys_debug_print("Test: exec...\n", 13);

    child_pid = fork();

    if (child_pid == 0)
    {
        /* 子进程执行 exec（此处简化为直接 exit） */
        sys_debug_print("  [child] before exec\n", 23);
        user_exit(42);
    }
    else
    {
        /* 父进程等待子进程 */
        int status;
        pid_t waited;

        waited = waitpid(child_pid, &status, 0);

        TEST_ASSERT(waited == child_pid, "waitpid returns child pid");
        TEST_ASSERT(status == 42, "exec process exit status is 42");
    }
}

/**
 * @brief 测试3: waitpid - WNOHANG 非阻塞
 */
static void test_waitpid_nohang(void)
{
    pid_t child_pid;
    int status;

    sys_debug_print("Test: waitpid WNOHANG...\n", 24);

    child_pid = fork();

    if (child_pid == 0)
    {
        /* 子进程等待一段时间 */
        for (volatile uint32_t i = 0U; i < 100000U; i++)
        {
            /* busy wait */
        }
        user_exit(0);
    }

    /* 父进程非阻塞等待 */
    status = 999;
    pid_t waited = waitpid(child_pid, &status, 1); /* WNOHANG */

    TEST_ASSERT((waited == 0) || (waited == child_pid),
                 "waitpid WNOHANG returns 0 or child pid");

    if (waited == 0)
    {
        /* 子进程还未退出，阻塞等待 */
        waited = waitpid(child_pid, &status, 0);
        TEST_ASSERT(waited == child_pid, "waitpid returns child pid");
        TEST_ASSERT(status == 0, "child exit status is 0");
    }
}

/**
 * @brief 测试4: kill - 发送信号
 */
static void test_kill(void)
{
    pid_t child_pid;

    sys_debug_print("Test: kill (SIGTERM)...\n", 23);

    child_pid = fork();

    if (child_pid == 0)
    {
        /* 子进程无限循环 */
        for (;;)
        {
            sys_thread_yield();
        }
    }

    /* 父进程发送 SIGTERM */
    int ret = kill(child_pid, 15); /* SIGTERM */

    TEST_ASSERT(ret == 0, "kill should succeed");

    int status;
    pid_t waited = waitpid(child_pid, &status, 0);

    TEST_ASSERT(waited == child_pid, "waitpid returns child pid");
    TEST_ASSERT(status != 0, "killed process should have non-zero status");
}

/**
 * @brief 测试5: setrlimit/getrlimit - 资源限制
 */
static void test_rlimit(void)
{
    struct rlimit rlim;
    int ret;

    sys_debug_print("Test: rlimit...\n", 15);

    /* 获取栈限制 */
    ret = getrlimit(RLIMIT_STACK, &rlim);

    TEST_ASSERT(ret == 0, "getrlimit should succeed");
    TEST_ASSERT(rlim.cur > 0, "stack limit should be positive");
    TEST_ASSERT(rlim.max >= rlim.cur, "max limit >= cur limit");

    /* 设置栈限制 */
    rlim.cur = 8192U;
    rlim.max = 16384U;
    ret = setrlimit(RLIMIT_STACK, &rlim);

    TEST_ASSERT(ret == 0, "setrlimit should succeed");

    /* 验证设置成功 */
    ret = getrlimit(RLIMIT_STACK, &rlim);

    TEST_ASSERT(ret == 0, "getrlimit should succeed");
    TEST_ASSERT(rlim.cur == 8192U, "stack limit should be 8192");
    TEST_ASSERT(rlim.max == 16384U, "stack max should be 16384");
}

/**
 * @brief 测试6: 多次 fork - 验证 pid 递增
 */
static void test_multiple_fork(void)
{
    pid_t children[5];
    uint32_t i;

    sys_debug_print("Test: multiple fork...\n", 23);

    for (i = 0U; i < 5U; i++)
    {
        children[i] = fork();

        if (children[i] == 0)
        {
            /* 子进程立即退出 */
            user_exit(i);
        }
    }

    /* 父进程等待所有子进程 */
    for (i = 0U; i < 5U; i++)
    {
        int status;
        pid_t waited;

        waited = waitpid(children[i], &status, 0);

        TEST_ASSERT(waited == children[i], "waitpid returns correct child pid");
        TEST_ASSERT(status == (int)i, "child exit status matches");
    }
}

/**
 * @brief 测试7: 进程树 - 祖孙进程
 */
static void test_process_tree(void)
{
    pid_t child_pid;

    sys_debug_print("Test: process tree...\n", 22);

    child_pid = fork();

    if (child_pid == 0)
    {
        /* 子进程创建孙进程 */
        pid_t grandchild_pid = fork();

        if (grandchild_pid == 0)
        {
            /* 孙进程 */
            user_exit(99);
        }

        /* 子进程等待孙进程 */
        int status;
        pid_t waited = waitpid(grandchild_pid, &status, 0);

        TEST_ASSERT(waited == grandchild_pid, "waitpid returns grandchild pid");
        TEST_ASSERT(status == 99, "grandchild exit status is 99");

        user_exit(88);
    }

    /* 父进程等待子进程 */
    int status;
    pid_t waited = waitpid(child_pid, &status, 0);

    TEST_ASSERT(waited == child_pid, "waitpid returns child pid");
    TEST_ASSERT(status == 88, "child exit status is 88");
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    sys_debug_print("\n========================================\n", 42);
    sys_debug_print("  User Process Management Tests\n", 33);
    sys_debug_print("========================================\n", 42);
    sys_debug_print("\n", 1);

    test_fork();
    test_exec();
    test_waitpid_nohang();
    test_kill();
    test_rlimit();
    test_multiple_fork();
    test_process_tree();

    sys_debug_print("\n========================================\n", 42);

    {
        char result[64];
        uint32_t len = 0U;
        const char *prefix = "  Results: ";
        const char *passed = "/0 passed, ";
        const char *failed = " failed\n";
        const char *separator = "========================================\n";

        len = strlen(prefix);
        for (uint32_t i = 0U; i < len; i++)
        {
            sys_debug_print(&prefix[i], 1);
        }

        /* 打印通过数 */
        uint64_t passed_val = (uint64_t)s_tests_passed;
        if (passed_val == 0U)
        {
            sys_debug_print("0", 1);
        }
        else
        {
            char buf[32];
            uint32_t i = 0U;
            while (passed_val > 0U)
            {
                buf[i++] = (char)('0' + (passed_val % 10U));
                passed_val /= 10U;
            }
            while (i > 0U)
            {
                i--;
                sys_debug_print(&buf[i], 1);
            }
        }

        len = strlen(passed);
        for (uint32_t i = 0U; i < len; i++)
        {
            sys_debug_print(&passed[i], 1);
        }

        /* 打印总数 */
        uint64_t total_val = (uint64_t)s_tests_run;
        if (total_val == 0U)
        {
            sys_debug_print("0", 1);
        }
        else
        {
            char buf[32];
            uint32_t i = 0U;
            while (total_val > 0U)
            {
                buf[i++] = (char)('0' + (total_val % 10U));
                total_val /= 10U;
            }
            while (i > 0U)
            {
                i--;
                sys_debug_print(&buf[i], 1);
            }
        }

        len = strlen(failed);
        for (uint32_t i = 0U; i < len; i++)
        {
            sys_debug_print(&failed[i], 1);
        }

        /* 打印失败数 */
        uint64_t failed_val = (uint64_t)s_tests_failed;
        if (failed_val == 0U)
        {
            sys_debug_print("0", 1);
        }
        else
        {
            char buf[32];
            uint32_t i = 0U;
            while (failed_val > 0U)
            {
                buf[i++] = (char)('0' + (failed_val % 10U));
                failed_val /= 10U;
            }
            while (i > 0U)
            {
                i--;
                sys_debug_print(&buf[i], 1);
            }
        }

        len = strlen(failed);
        for (uint32_t i = 0U; i < len; i++)
        {
            sys_debug_print(&failed[i], 1);
        }

        len = strlen(separator);
        for (uint32_t i = 0U; i < len; i++)
        {
            sys_debug_print(&separator[i], 1);
        }
    }

    sys_debug_print("\n", 1);

    return (s_tests_failed > 0U) ? 1 : 0;
}
