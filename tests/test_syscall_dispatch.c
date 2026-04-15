/**
 * @file    test_syscall_dispatch.c
 * @brief   AISafeOS64 musl 系统调用分发器测试
 * @version 1.0
 *
 * @note MISRA-C:2012 合规
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <sys/uio.h>

#include "musl_safety.h"
#include "syscall_numbers.h"

/* 测试版本：使用 syscall_entry_test.h 桩 */
#define AISAFE_TEST_MODE 1
#include "syscall_entry_test.h"
#include "syscall_arch.h"

/* 测试计数器 */
static int g_test_passed = 0;
static int g_test_failed = 0;

/* ========================================================================
 * 测试工具函数
 * ======================================================================== */

static void test_start(const char *name)
{
    printf("[SYSCALL_DISPATCH] %s ... ", name);
}

static void test_pass(void)
{
    printf("✓ PASSED\n");
    g_test_passed++;
}

static void test_fail(const char *reason)
{
    printf("✗ FAILED (%s)\n", reason);
    g_test_failed++;
}

/* ========================================================================
 * __sysinfo 函数指针测试
 * ======================================================================== */

static void test_sysinfo_pointer_valid(void)
{
    test_start("sysinfo_pointer_valid");
    if (__sysinfo != 0)
    {
        test_pass();
    }
    else
    {
        test_fail("expected __sysinfo != 0");
    }
}

/* ========================================================================
 * 参数验证测试
 * ======================================================================== */

static void test_dispatch_invalid_syscall(void)
{
    int result;

    test_start("dispatch_invalid_syscall");
    result = aisafe_syscall_dispatch(-1, 0, 0, 0, 0, 0, 0);
    /* -1 非法 syscall 号，应该返回 -EINVAL */
    if (result == -EINVAL)
    {
        test_pass();
    }
    else
    {
        printf("(got %d, expected %d)", result, -EINVAL);
        test_fail("expected -EINVAL for invalid syscall number");
    }
}

static void test_dispatch_valid_syscall_getpid(void)
{
    long result;

    test_start("dispatch_valid_syscall_getpid");
    result = aisafe_syscall_dispatch(__NR_getpid, 0, 0, 0, 0, 0, 0);
    /* 应该返回 PID > 0 */
    if (result > 0)
    {
        test_pass();
    }
    else
    {
        test_fail("expected PID > 0");
    }
}

static void test_dispatch_valid_syscall_write_stdout(void)
{
    char buffer[100] = "Hello, AISafeOS64!\\n";
    long result;

    test_start("dispatch_valid_syscall_write_stdout");
    result = aisafe_syscall_dispatch(__NR_write, 1, (long)buffer, 20, 0, 0, 0);
    if (result == 20)
    {
        test_pass();
    }
    else
    {
        test_fail("expected 20 bytes written");
    }
}

static void test_dispatch_invalid_write_fd(void)
{
    long result;

    test_start("dispatch_invalid_write_fd");
    result = aisafe_syscall_dispatch(__NR_write, 999, (long)"test", 4, 0, 0, 0);
    if (result == -EBADF)
    {
        test_pass();
    }
    else
    {
        test_fail("expected -EBADF for invalid fd");
    }
}

static void test_dispatch_invalid_pointer(void)
{
    long result;

    test_start("dispatch_invalid_pointer");
    result = aisafe_syscall_dispatch(__NR_write, 1, (long)NULL, 4, 0, 0, 0);
    /* NULL 指针参数，应该返回 -EFAULT */
    if (result == -EFAULT)
    {
        test_pass();
    }
    else
    {
        printf("(got %ld, expected %d)", result, -EFAULT);
        test_fail("expected -EFAULT for NULL pointer");
    }
}

static void test_dispatch_valid_mmap(void)
{
    long result;

    test_start("dispatch_valid_mmap");
    /* mmap(addr, length, prot, flags, fd, offset) */
    result = aisafe_syscall_dispatch(__NR_mmap, 0, 4096, 3, 0x22, -1, 0);
    /* 应该返回成功（> 0）或特定错误 */
    if (result > 0 || result == -ENOMEM || result == -EINVAL)
    {
        test_pass();
    }
    else
    {
        test_fail("unexpected result");
    }
}

/* ========================================================================
 * ENOSYS 桩函数测试
 * ======================================================================== */

static void test_dispatch_enosys_fork(void)
{
    long result;

    test_start("dispatch_enosys_fork");
    result = aisafe_syscall_dispatch(__NR_fork, 0, 0, 0, 0, 0, 0);
    if (result == -ENOSYS)
    {
        test_pass();
    }
    else
    {
        test_fail("expected -ENOSYS for fork");
    }
}

static void test_dispatch_enosys_read(void)
{
    long result;

    test_start("dispatch_enosys_read");
    result = aisafe_syscall_dispatch(__NR_read, 0, (long)"test", 4, 0, 0, 0);
    if (result == -ENOSYS)
    {
        test_pass();
    }
    else
    {
        test_fail("expected -ENOSYS for read");
    }
}

static void test_dispatch_enosys_execve(void)
{
    long result;

    test_start("dispatch_enosys_execve");
    result = aisafe_syscall_dispatch(__NR_execve, 0, 0, 0, 0, 0, 0);
    if (result == -ENOSYS)
    {
        test_pass();
    }
    else
    {
        test_fail("expected -ENOSYS for execve");
    }
}

static void test_dispatch_enosys_brk(void)
{
    long result;

    test_start("dispatch_enosys_brk");
    result = aisafe_syscall_dispatch(__NR_brk, 0x10000, 0, 0, 0, 0, 0);
    if (result == -ENOSYS)
    {
        test_pass();
    }
    else
    {
        test_fail("expected -ENOSYS for brk");
    }
}

/* ========================================================================
 * 文件 I/O 测试
 * ======================================================================== */

static void test_dispatch_write_stderr(void)
{
    char buffer[100] = "stderr test\\n";
    long result;

    test_start("dispatch_write_stderr");
    result = aisafe_syscall_dispatch(__NR_write, 2, (long)buffer, 14, 0, 0, 0);
    if (result == 14)
    {
        test_pass();
    }
    else
    {
        test_fail("expected 14 bytes written");
    }
}

static void test_dispatch_writev_stdout(void)
{
    struct iovec vecs[2] = {
        {(void *)"Hello", 5},
        {(void *)" World", 6}
    };
    long result;

    test_start("dispatch_writev_stdout");
    result = aisafe_syscall_dispatch(__NR_writev, 1, (long)vecs, 2, 0, 0, 0);
    if (result == 11)
    {
        test_pass();
    }
    else
    {
        test_fail("expected 11 bytes written");
    }
}

/* ========================================================================
 * 时间函数测试
 * ======================================================================== */

static void test_dispatch_nanosleep(void)
{
    long result;

    test_start("dispatch_nanosleep");
    result = aisafe_syscall_dispatch(__NR_nanosleep, 0, 0, 0, 0, 0, 0);
    if (result == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("unexpected result");
    }
}

/* ========================================================================
 * 系统调用号定义测试
 * ======================================================================== */

static void test_syscall_numbers_defined(void)
{
    test_start("syscall_numbers_defined");

    if (__NR_read == 63 && __NR_write == 64 &&
        __NR_getpid == 172 && __NR_exit == 93 &&
        __NR_mmap == 222)
    {
        test_pass();
    }
    else
    {
        test_fail("syscall numbers mismatch");
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  musl AISafeOS64 系统调用分发器测试\n");
    printf("========================================\n");
    printf("\n");

    /* __sysinfo 函数指针测试 */
    test_sysinfo_pointer_valid();

    /* 参数验证测试 */
    test_dispatch_invalid_syscall();
    test_dispatch_valid_syscall_getpid();
    test_dispatch_valid_syscall_write_stdout();
    test_dispatch_invalid_write_fd();
    test_dispatch_invalid_pointer();
    test_dispatch_valid_mmap();

    /* ENOSYS 桩函数测试 */
    test_dispatch_enosys_fork();
    test_dispatch_enosys_read();
    test_dispatch_enosys_execve();
    test_dispatch_enosys_brk();

    /* 文件 I/O 测试 */
    test_dispatch_write_stderr();
    test_dispatch_writev_stdout();

    /* 时间函数测试 */
    test_dispatch_nanosleep();

    /* 系统调用号定义测试 */
    test_syscall_numbers_defined();

    /* 测试总结 */
    printf("\n");
    printf("========================================\n");
    printf("  测试结果\n");
    printf("========================================\n");
    printf("Total:    %d\n", g_test_passed + g_test_failed);
    printf("Passed:   %d ✓\n", g_test_passed);
    printf("Failed:   %d ✗\n", g_test_failed);
    printf("========================================\n");
    printf("\n");

    if (g_test_failed == 0)
    {
        printf("✓ 所有测试通过\n");
        return 0;
    }
    else
    {
        printf("✗ 部分测试失败\n");
        return 1;
    }
}
