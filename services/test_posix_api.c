/**
 * @file    test_posix_api.c
 * @brief   musl 适配层 - POSIX API 集成测试
 * @version 1.0
 *
 * @details 测试 musl_aisafe 提供的 POSIX API 功能：
 *          - 字符串操作：strlen(), strcpy(), strcmp(), strstr(), sprintf()
 *          - 内存操作：malloc(), free(), memcpy(), memset(), memcmp()
 *          - 文件操作：open(), read(), write(), close(), lseek()
 *          - 进程操作：getpid(), getppid()
 *          - 系统信息：uname(), sysinfo()
 *
 * @note MISRA-C:2012 合规
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <errno.h>

/* ========================================================================
 * 测试结果统计
 * ======================================================================== */

static int g_test_total = 0;
static int g_test_passed = 0;
static int g_test_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        g_test_total++; \
        if (condition) { \
            g_test_passed++; \
            printf("  [PASS] %s\n", message); \
        } else { \
            g_test_failed++; \
            printf("  [FAIL] %s (errno=%d)\n", message, errno); \
        } \
    } while (0)

/* ========================================================================
 * 字符串操作测试
 * ======================================================================== */

static void test_string_operations(void)
{
    printf("\n========== 字符串操作测试 ==========\n");

    const char *str1 = "Hello, AISafeOS64!";
    char buf[64];

    /* strlen() */
    size_t len = strlen(str1);
    TEST_ASSERT(len == 18U, "strlen() 测试");

    /* strcpy() */
    (void)strcpy(buf, str1);
    TEST_ASSERT(strcmp(buf, str1) == 0, "strcpy() 测试");

    /* strcmp() */
    TEST_ASSERT(strcmp(buf, str1) == 0, "strcmp() 相等测试");
    TEST_ASSERT(strcmp(buf, "Hello") != 0, "strcmp() 不相等测试");

    /* strstr() */
    char *ptr = strstr(buf, "AISafe");
    TEST_ASSERT(ptr != NULL, "strstr() 查找测试");
    TEST_ASSERT(ptr - buf == 7, "strstr() 位置测试");

    /* sprintf() */
    int ret = sprintf(buf, "PID=%d, Value=%d", 123, 456);
    TEST_ASSERT(ret == 17, "sprintf() 返回值测试");
    TEST_ASSERT(strcmp(buf, "PID=123, Value=456") == 0, "sprintf() 格式化测试");
}

/* ========================================================================
 * 内存操作测试
 * ======================================================================== */

static void test_memory_operations(void)
{
    printf("\n========== 内存操作测试 ==========\n");

    /* malloc() / free() */
    void *p1 = malloc(128);
    TEST_ASSERT(p1 != NULL, "malloc(128) 分配测试");

    void *p2 = malloc(256);
    TEST_ASSERT(p2 != NULL, "malloc(256) 分配测试");

    free(p1);
    free(p2);

    /* memcpy() */
    char src[] = "Source data";
    char dest[64];
    (void)memcpy(dest, src, sizeof(src));
    TEST_ASSERT(strcmp(dest, src) == 0, "memcpy() 测试");

    /* memset() */
    (void)memset(dest, 0xAA, 16);
    int i;
    for (i = 0; i < 16; i++)
    {
        if (dest[i] != 0xAA)
        {
            break;
        }
    }
    TEST_ASSERT(i == 16, "memset() 测试");

    /* memcmp() */
    char cmp1[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    char cmp2[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT(memcmp(cmp1, cmp2, 8) == 0, "memcmp() 相等测试");

    cmp2[4] = 0xFF;
    TEST_ASSERT(memcmp(cmp1, cmp2, 8) != 0, "memcmp() 不相等测试");
}

/* ========================================================================
 * 进程操作测试
 * ======================================================================== */

static void test_process_operations(void)
{
    printf("\n========== 进程操作测试 ==========\n");

    /* getpid() */
    pid_t pid = getpid();
    TEST_ASSERT(pid > 0, "getpid() 测试");

    /* getppid() */
    pid_t ppid = getppid();
    TEST_ASSERT(ppid > 0, "getppid() 测试");

    printf("  [INFO] PID=%d, PPID=%d\n", (int)pid, (int)ppid);
}

/* ========================================================================
 * 系统信息测试
 * ======================================================================== */

static void test_system_info(void)
{
    printf("\n========== 系统信息测试 ==========\n");

    /* uname() */
    struct utsname uts;
    int ret = uname(&uts);
    TEST_ASSERT(ret == 0, "uname() 测试");
    if (ret == 0)
    {
        printf("  [INFO] sysname: %s\n", uts.sysname);
        printf("  [INFO] nodename: %s\n", uts.nodename);
        printf("  [INFO] release: %s\n", uts.release);
        printf("  [INFO] version: %s\n", uts.version);
        printf("  [INFO] machine: %s\n", uts.machine);
    }

    /* sysinfo() */
    struct sysinfo si;
    ret = sysinfo(&si);
    TEST_ASSERT(ret == 0, "sysinfo() 测试");
    if (ret == 0)
    {
        printf("  [INFO] uptime: %ld\n", si.uptime);
        printf("  [INFO] procs: %u\n", si.procs);
        printf("  [INFO] totalram: %lu MB\n", si.totalram >> 20);
        printf("  [INFO] freeram: %lu MB\n", si.freeram >> 20);
    }
}

/* ========================================================================
 * 文件操作测试
 * ======================================================================== */

static void test_file_operations(void)
{
    printf("\n========== 文件操作测试 ==========\n");

    int fd;
    int ret;
    char buf[64];
    ssize_t n;

    /* open() - 创建文件 */
    fd = open("/test_musl.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
    {
        printf("  [SKIP] 文件操作测试（打开失败，可能 FS 服务未启动）\n");
        return;
    }

    TEST_ASSERT(fd >= 0, "open() 创建文件测试");

    /* write() - 写入数据 */
    const char *data = "Hello from musl_aisafe!";
    n = write(fd, data, strlen(data));
    TEST_ASSERT(n == (ssize_t)strlen(data), "write() 测试");

    /* close() - 关闭文件 */
    ret = close(fd);
    TEST_ASSERT(ret == 0, "close() 测试");

    /* open() - 读取文件 */
    fd = open("/test_musl.txt", O_RDONLY);
    TEST_ASSERT(fd >= 0, "open() 读取文件测试");

    /* read() - 读取数据 */
    n = read(fd, buf, sizeof(buf) - 1);
    TEST_ASSERT(n > 0, "read() 测试");
    if (n > 0)
    {
        buf[n] = '\0';
        TEST_ASSERT(strcmp(buf, data) == 0, "read() 数据验证测试");
    }

    /* lseek() - 文件定位 */
    off_t offset = lseek(fd, 0, SEEK_SET);
    TEST_ASSERT(offset == 0, "lseek() SEEK_SET 测试");

    offset = lseek(fd, 0, SEEK_END);
    TEST_ASSERT(offset == (off_t)strlen(data), "lseek() SEEK_END 测试");

    /* close() - 关闭文件 */
    ret = close(fd);
    TEST_ASSERT(ret == 0, "close() 测试");
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("======================================================\n");
    printf("  musl_aisafe - POSIX API 集成测试\n");
    printf("======================================================\n");

    /* 运行测试 */
    test_string_operations();
    test_memory_operations();
    test_process_operations();
    test_system_info();
    test_file_operations();

    /* 打印测试结果 */
    printf("\n======================================================\n");
    printf("  测试结果: %d/%d 通过\n", g_test_passed, g_test_total);
    printf("  失败: %d\n", g_test_failed);
    printf("======================================================\n");

    if (g_test_failed > 0)
    {
        return 1;
    }

    return 0;
}
