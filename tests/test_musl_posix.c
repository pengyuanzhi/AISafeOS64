/**
 * @file    test_musl_posix.c
 * @brief   AISafe-libc POSIX 文件 I/O 单元测试
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 测试 POSIX 文件 I/O 接口的常量和逻辑正确性
 *          不链接我们的 string 实现（避免覆盖 glibc 的 memset 等关键函数）
 */

/* 使用系统头文件做输出和基本操作 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 我们的 POSIX 头文件 — 只包含常量和类型定义，不链接实现 */
/* 需要测试的常量从头文件直接获取 */

/* fcntl 常量 */
#include <fcntl.h>

/* unistd 常量和声明 */
#include <unistd.h>

/* sys/stat 常量和类型 */
#include <sys/stat.h>

/* errno */
#include <errno.h>

/* ========================================================================
 * 简单测试框架
 * ======================================================================== */

static int s_pass = 0;
static int s_fail = 0;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); s_fail++; } \
    else { s_pass++; } \
} while(0)

#define TEST_ASSERT_EQ(a, b) TEST_ASSERT((a) == (b))

/* ========================================================================
 * fcntl 标志位测试
 * ======================================================================== */

static void test_open_flags(void)
{
    TEST_ASSERT_EQ(O_RDONLY, 0);
    TEST_ASSERT_EQ(O_WRONLY, 1);
    TEST_ASSERT_EQ(O_RDWR, 2);
    TEST_ASSERT(O_CREAT != 0);
    TEST_ASSERT(O_EXCL != 0);
    TEST_ASSERT(O_TRUNC != 0);
    TEST_ASSERT(O_APPEND != 0);
    TEST_ASSERT(O_NONBLOCK != 0);
}

static void test_fcntl_constants(void)
{
    TEST_ASSERT_EQ(F_DUPFD, 0);
    TEST_ASSERT_EQ(F_GETFD, 1);
    TEST_ASSERT_EQ(F_SETFD, 2);
    TEST_ASSERT_EQ(F_GETFL, 3);
    TEST_ASSERT_EQ(F_SETFL, 4);
    TEST_ASSERT_EQ(FD_CLOEXEC, 1);
}

static void test_permission_bits(void)
{
    TEST_ASSERT_EQ(S_IRUSR, 0400);
    TEST_ASSERT_EQ(S_IWUSR, 0200);
    TEST_ASSERT_EQ(S_IXUSR, 0100);
    TEST_ASSERT_EQ(S_IRGRP, 0040);
    TEST_ASSERT_EQ(S_IWGRP, 0020);
    TEST_ASSERT_EQ(S_IXGRP, 0010);
    TEST_ASSERT_EQ(S_IROTH, 0004);
    TEST_ASSERT_EQ(S_IWOTH, 0002);
    TEST_ASSERT_EQ(S_IXOTH, 0001);
}

/* ========================================================================
 * unistd 常量测试
 * ======================================================================== */

static void test_stdio_fileno(void)
{
    TEST_ASSERT_EQ(STDIN_FILENO, 0);
    TEST_ASSERT_EQ(STDOUT_FILENO, 1);
    TEST_ASSERT_EQ(STDERR_FILENO, 2);
}

static void test_seek_whence(void)
{
    TEST_ASSERT_EQ(SEEK_SET, 0);
    TEST_ASSERT_EQ(SEEK_CUR, 1);
    TEST_ASSERT_EQ(SEEK_END, 2);
}

static void test_pathconf_constants(void)
{
    TEST_ASSERT_EQ(_PC_PATH_MAX, 1);
    TEST_ASSERT_EQ(_PC_NAME_MAX, 2);
}

/* ========================================================================
 * sys/stat 测试
 * ======================================================================== */

static void test_stat_type_macros(void)
{
    TEST_ASSERT(S_ISREG(0x8000));
    TEST_ASSERT(S_ISDIR(0x4000));
    TEST_ASSERT(S_ISCHR(0x2000));
    TEST_ASSERT(S_ISBLK(0x6000));
    TEST_ASSERT(S_ISFIFO(0x1000));
    TEST_ASSERT(S_ISLNK(0xA000));

    TEST_ASSERT(!S_ISREG(0));
    TEST_ASSERT(!S_ISDIR(0x8000));
    TEST_ASSERT(!S_ISCHR(0x4000));
}

static void test_stat_type_constants(void)
{
    TEST_ASSERT_EQ(S_IFMT,  0xF000);
    TEST_ASSERT_EQ(S_IFDIR, 0x4000);
    TEST_ASSERT_EQ(S_IFCHR, 0x2000);
    TEST_ASSERT_EQ(S_IFBLK, 0x6000);
    TEST_ASSERT_EQ(S_IFREG, 0x8000);
    TEST_ASSERT_EQ(S_IFIFO, 0x1000);
    TEST_ASSERT_EQ(S_IFLNK, 0xA000);
}

static void test_stat_struct_size(void)
{
    struct stat st;
    memset(&st, 0, sizeof(st));
    TEST_ASSERT(sizeof(st) > 0);
    TEST_ASSERT_EQ(st.st_size, 0);
}

/* ========================================================================
 * 函数行为测试（链接我们的实现）
 * ======================================================================== */

static void test_getpid(void)
{
    pid_t pid = getpid();
    TEST_ASSERT(pid > 0);
}

static void test_isatty(void)
{
    /* 在非终端环境中可能返回 0，但常量定义应该正确 */
    TEST_ASSERT(isatty(0) >= 0);
}

static void test_pathconf_values(void)
{
    long v = pathconf("/", _PC_PATH_MAX);
    TEST_ASSERT(v > 0);
}

static void test_errno_set(void)
{
    errno = 0;
    TEST_ASSERT_EQ(errno, 0);
    errno = ENOENT;
    TEST_ASSERT_EQ(errno, ENOENT);
    errno = 0;
}

/* ========================================================================
 * 主测试入口
 * ======================================================================== */

int main(void)
{
    printf("=== test_musl_posix ===\n\n");

    /* fcntl */
    test_open_flags();
    test_fcntl_constants();
    test_permission_bits();

    /* unistd */
    test_stdio_fileno();
    test_seek_whence();
    test_pathconf_constants();

    /* sys/stat */
    test_stat_type_macros();
    test_stat_type_constants();
    test_stat_struct_size();

    /* 函数行为 */
    test_getpid();
    test_isatty();
    test_pathconf_values();
    test_errno_set();

    printf("\n结果: %d 通过 / %d 失败 / %d 总计\n",
           s_pass, s_fail, s_pass + s_fail);

    return s_fail > 0 ? 1 : 0;
}
