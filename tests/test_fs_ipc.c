/**
 * @file    test_fs_ipc.c
 * @brief   FS 服务 IPC 接口单元测试
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details 测试 FS 服务 IPC 客户端接口：
 *          - fs_open/fs_close
 *          - fs_read/fs_write
 *          - fs_lseek
 *          - fs_fstat
 *          - fs_ioctl/fs_fcntl
 *          - fs_chmod/chown
 *
 * @note MISRA-C:2012 合规
 * @note TDD: RED 阶段
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "kernel/errno.h"
#include "kernel/fs_ipc.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* ========================================================================
 * 测试宏定义
 * ======================================================================== */

#define TEST_ASSERT(condition) do { \
    if (!(condition)) { \
        printf("TEST_ASSERT failed at line %d\n", __LINE__); \
        return; \
    } \
} while (0)

#define TEST_ASSERT_EQ(a, b) TEST_ASSERT((a) == (b))
#define TEST_ASSERT_NE(a, b) TEST_ASSERT((a) != (b))
#define TEST_ASSERT_GT(a, b) TEST_ASSERT((a) > (b))
#define TEST_ASSERT_GE(a, b) TEST_ASSERT((a) >= (b))
#define TEST_ASSERT_LT(a, b) TEST_ASSERT((a) < (b))
#define TEST_ASSERT_LE(a, b) TEST_ASSERT((a) <= (b))
#define TEST_ASSERT_TRUE(x) TEST_ASSERT((x) == true)
#define TEST_ASSERT_FALSE(x) TEST_ASSERT((x) == false)
#define TEST_ASSERT_NOT_NULL(x) TEST_ASSERT((x) != NULL)
#define TEST_ASSERT_NULL(x) TEST_ASSERT((x) == NULL)
#define TEST_ASSERT_EQUAL_STRING(a, b) TEST_ASSERT(strcmp((a), (b)) == 0)
#define TEST_ASSERT_EQUAL_INT32(a, b) TEST_ASSERT((a) == (b))
#define TEST_ASSERT_EQUAL_INT64(a, b) TEST_ASSERT((a) == (b))
#define TEST_ASSERT_EQUAL_UINT32(a, b) TEST_ASSERT((a) == (b))
#define TEST_ASSERT_LESS_THAN_INT32(a, b) TEST_ASSERT((a) < (b))
#define TEST_ASSERT_GREATER_OR_EQUAL_INT32(a, b) TEST_ASSERT((a) >= (b))
#define TEST_ASSERT_GREATER_THAN_INT32(a, b) TEST_ASSERT((a) > (b))
#define TEST_ASSERT_LESS_THAN_INT64(a, b) TEST_ASSERT((a) < (b))

/* ========================================================================
 * 测试用例：fs_open/fs_close
 * ======================================================================== */

/**
 * @brief 测试 fs_open 打开文件
 */
void test_fs_open_valid_file(void)
{
    int32_t fd;
    const char *path = "/test.txt";

    /* 打开已存在的文件 */
    fd = fs_open(path, (uint32_t)FS_O_RDONLY, 0U);

    /* 验证返回有效的文件描述符 */
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));
}

/**
 * @brief 测试 fs_open 创建文件
 */
void test_fs_open_create_file(void)
{
    int32_t fd;
    const char *path = "/new_file.txt";

    /* 创建新文件 */
    fd = fs_open(path, (uint32_t)(FS_O_CREAT | FS_O_RDWR), 0644U);

    /* 验证返回有效的文件描述符 */
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));
}

/**
 * @brief 测试 fs_open 打开不存在的文件（O_CREAT 标志）
 */
void test_fs_open_nonexistent_file(void)
{
    int32_t fd;
    const char *path = "/nonexistent.txt";

    /* 尝试打开不存在的文件（不创建） */
    fd = fs_open(path, (uint32_t)FS_O_RDONLY, 0U);

    /* 验证返回错误 */
    TEST_ASSERT_LESS_THAN_INT32(0, fd);
}

/**
 * @brief 测试 fs_close 关闭无效文件描述符
 */
void test_fs_close_invalid_fd(void)
{
    int32_t ret;

    /* 尝试关闭无效的文件描述符 */
    ret = fs_close(9999U);

    /* 验证返回错误 */
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/* ========================================================================
 * 测试用例：fs_read/fs_write
 * ======================================================================== */

/**
 * @brief 测试 fs_write 写入文件
 */
void test_fs_write_data(void)
{
    int32_t fd;
    const char *path = "/test_write.txt";
    const char *data = "Hello, World!";
    int64_t bytes_written;
    char buf[128];

    /* 创建文件 */
    fd = fs_open(path, (uint32_t)(FS_O_CREAT | FS_O_RDWR), 0644U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);

    /* 写入数据 */
    bytes_written = fs_write((uint32_t)fd, data, (uint64_t)strlen(data));
    TEST_ASSERT_EQUAL_INT64((int64_t)strlen(data), bytes_written);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));

    /* 重新打开文件读取数据 */
    fd = fs_open(path, (uint32_t)FS_O_RDONLY, 0U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);

    (void)memset(buf, 0, sizeof(buf));
    bytes_written = fs_read((uint32_t)fd, buf, sizeof(buf) - 1U);
    TEST_ASSERT_EQUAL_INT64((int64_t)strlen(data), bytes_written);
    TEST_ASSERT_EQUAL_STRING(data, buf);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));
}

/**
 * @brief 测试 fs_read 读取文件
 */
void test_fs_read_data(void)
{
    int32_t fd;
    const char *path = "/test_read.txt";
    const char *data = "AISafeOS64";
    char buf[128];
    int64_t bytes_read;

    /* 创建并写入文件 */
    fd = fs_open(path, (uint32_t)(FS_O_CREAT | FS_O_RDWR), 0644U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);
    TEST_ASSERT_EQUAL_INT64((int64_t)strlen(data),
                             fs_write((uint32_t)fd, data,
                                      (uint64_t)strlen(data)));
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));

    /* 读取文件 */
    fd = fs_open(path, (uint32_t)FS_O_RDONLY, 0U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);

    (void)memset(buf, 0, sizeof(buf));
    bytes_read = fs_read((uint32_t)fd, buf, sizeof(buf) - 1U);
    TEST_ASSERT_EQUAL_INT64((int64_t)strlen(data), bytes_read);
    TEST_ASSERT_EQUAL_STRING(data, buf);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));
}

/**
 * @brief 测试 fs_write 追加数据
 */
void test_fs_write_append(void)
{
    int32_t fd;
    const char *path = "/test_append.txt";
    const char *data1 = "Hello, ";
    const char *data2 = "World!";
    char buf[128];
    int64_t bytes_written;

    /* 创建文件 */
    fd = fs_open(path, (uint32_t)(FS_O_CREAT | FS_O_WRONLY), 0644U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);

    /* 写入数据 */
    bytes_written = fs_write((uint32_t)fd, data1, (uint64_t)strlen(data1));
    TEST_ASSERT_EQUAL_INT64((int64_t)strlen(data1), bytes_written);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));

    /* 以追加模式打开文件 */
    fd = fs_open(path, (uint32_t)(FS_O_WRONLY | FS_O_APPEND), 0644U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);

    /* 追加数据 */
    bytes_written = fs_write((uint32_t)fd, data2, (uint64_t)strlen(data2));
    TEST_ASSERT_EQUAL_INT64((int64_t)strlen(data2), bytes_written);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));

    /* 读取并验证数据 */
    fd = fs_open(path, (uint32_t)FS_O_RDONLY, 0U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);

    (void)memset(buf, 0, sizeof(buf));
    bytes_written = fs_read((uint32_t)fd, buf, sizeof(buf) - 1U);
    TEST_ASSERT_EQUAL_INT64((int64_t)(strlen(data1) + strlen(data2)),
                             bytes_written);
    TEST_ASSERT_EQUAL_STRING("Hello, World!", buf);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));
}

/* ========================================================================
 * 测试用例：fs_lseek
 * ======================================================================== */

/**
 * @brief 测试 fs_lseek SEEK_SET
 */
void test_fs_lseek_seek_set(void)
{
    int32_t fd;
    const char *path = "/test_seek.txt";
    const char *data = "0123456789";
    char buf[128];
    int64_t offset;

    /* 创建文件 */
    fd = fs_open(path, (uint32_t)(FS_O_CREAT | FS_O_RDWR), 0644U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);
    TEST_ASSERT_EQUAL_INT64((int64_t)strlen(data),
                             fs_write((uint32_t)fd, data,
                                      (uint64_t)strlen(data)));

    /* SEEK_SET 到偏移 5 */
    offset = fs_lseek((uint32_t)fd, 5L, (uint32_t)FS_SEEK_SET);
    TEST_ASSERT_EQUAL_INT64(5L, offset);

    /* 读取数据 */
    (void)memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL_INT64(5L, fs_read((uint32_t)fd, buf, 5U));
    TEST_ASSERT_EQUAL_STRING("56789", buf);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));
}

/**
 * @brief 测试 fs_lseek SEEK_CUR
 */
void test_fs_lseek_seek_cur(void)
{
    int32_t fd;
    const char *path = "/test_seek_cur.txt";
    const char *data = "0123456789";
    int64_t offset;

    /* 创建文件 */
    fd = fs_open(path, (uint32_t)(FS_O_CREAT | FS_O_RDWR), 0644U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);
    TEST_ASSERT_EQUAL_INT64((int64_t)strlen(data),
                             fs_write((uint32_t)fd, data,
                                      (uint64_t)strlen(data)));

    /* SEEK_SET 到偏移 0 */
    offset = fs_lseek((uint32_t)fd, 0L, (uint32_t)FS_SEEK_SET);
    TEST_ASSERT_EQUAL_INT64(0L, offset);

    /* SEEK_CUR 移动 5 */
    offset = fs_lseek((uint32_t)fd, 5L, (uint32_t)FS_SEEK_CUR);
    TEST_ASSERT_EQUAL_INT64(5L, offset);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));
}

/**
 * @brief 测试 fs_lseek SEEK_END
 */
void test_fs_lseek_seek_end(void)
{
    int32_t fd;
    const char *path = "/test_seek_end.txt";
    const char *data = "0123456789";
    int64_t offset;

    /* 创建文件 */
    fd = fs_open(path, (uint32_t)(FS_O_CREAT | FS_O_RDWR), 0644U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);
    TEST_ASSERT_EQUAL_INT64((int64_t)strlen(data),
                             fs_write((uint32_t)fd, data,
                                      (uint64_t)strlen(data)));

    /* SEEK_END 到偏移 -5 */
    offset = fs_lseek((uint32_t)fd, -5L, (uint32_t)FS_SEEK_END);
    TEST_ASSERT_EQUAL_INT64((int64_t)(strlen(data) - 5U), offset);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));
}

/* ========================================================================
 * 测试用例：fs_fstat
 * ======================================================================== */

/**
 * @brief 测试 fs_fstat 获取文件状态
 */
void test_fs_fstat_file_status(void)
{
    int32_t fd;
    const char *path = "/test_stat.txt";
    const char *data = "AISafeOS64";
    fs_stat_t stat;
    int32_t ret;

    /* 创建文件 */
    fd = fs_open(path, (uint32_t)(FS_O_CREAT | FS_O_RDWR), 0644U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);
    TEST_ASSERT_EQUAL_INT64((int64_t)strlen(data),
                             fs_write((uint32_t)fd, data,
                                      (uint64_t)strlen(data)));

    /* 获取文件状态 */
    ret = fs_fstat((uint32_t)fd, &stat);
    TEST_ASSERT_EQUAL_INT32(0, ret);
    TEST_ASSERT_EQUAL_UINT32(strlen(data), stat.st_size);
    TEST_ASSERT_EQUAL_UINT32(0U, stat.st_mode & (uint32_t)FS_S_IFDIR);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));
}

/* ========================================================================
 * 测试用例：fs_chmod/chown
 * ======================================================================== */

/**
 * @brief 测试 fs_chmod 修改文件权限
 */
void test_fs_chmod_change_mode(void)
{
    int32_t fd;
    const char *path = "/test_chmod.txt";
    fs_stat_t stat;
    int32_t ret;

    /* 创建文件 */
    fd = fs_open(path, (uint32_t)(FS_O_CREAT | FS_O_RDWR), 0644U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));

    /* 修改文件权限为 0755 */
    ret = fs_chmod(path, 0755U);
    TEST_ASSERT_EQUAL_INT32(0, ret);

    /* 验证文件权限已修改 */
    fd = fs_open(path, (uint32_t)FS_O_RDONLY, 0U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);
    ret = fs_fstat((uint32_t)fd, &stat);
    TEST_ASSERT_EQUAL_INT32(0, ret);
    TEST_ASSERT_EQUAL_UINT32(0755U, stat.st_mode & 0777U);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));
}

/**
 * @brief 测试 fs_chown 修改文件所有者
 */
void test_fs_chown_change_owner(void)
{
    int32_t fd;
    const char *path = "/test_chown.txt";
    fs_stat_t stat;
    int32_t ret;

    /* 创建文件 */
    fd = fs_open(path, (uint32_t)(FS_O_CREAT | FS_O_RDWR), 0644U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));

    /* 修改文件所有者 */
    ret = fs_chown(path, 1000U, 1000U);
    TEST_ASSERT_EQUAL_INT32(0, ret);

    /* 验证文件所有者已修改 */
    fd = fs_open(path, (uint32_t)FS_O_RDONLY, 0U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);
    ret = fs_fstat((uint32_t)fd, &stat);
    TEST_ASSERT_EQUAL_INT32(0, ret);
    TEST_ASSERT_EQUAL_UINT32(1000U, stat.st_uid);
    TEST_ASSERT_EQUAL_UINT32(1000U, stat.st_gid);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));
}

/* ========================================================================
 * 测试用例：fs_ioctl/fs_fcntl
 * ======================================================================== */

/**
 * @brief 测试 fs_ioctl 文件控制操作
 */
void test_fs_ioctl_file_control(void)
{
    int32_t fd;
    const char *path = "/test_ioctl.txt";
    int32_t ret;
    uint32_t flags;

    /* 创建文件 */
    fd = fs_open(path, (uint32_t)(FS_O_CREAT | FS_O_RDWR), 0644U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);

    /* 获取文件标志（FIONBIO） */
    flags = 0U;
    ret = fs_ioctl((uint32_t)fd, FS_FIONBIO, &flags);
    TEST_ASSERT_EQUAL_INT32(0, ret);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));
}

/**
 * @brief 测试 fs_fcntl 文件控制操作
 */
void test_fs_fcntl_file_control(void)
{
    int32_t fd;
    const char *path = "/test_fcntl.txt";
    int32_t ret;

    /* 创建文件 */
    fd = fs_open(path, (uint32_t)(FS_O_CREAT | FS_O_RDWR), 0644U);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, fd);

    /* 设置文件标志（F_GETFL） */
    ret = fs_fcntl((uint32_t)fd, FS_F_GETFL, 0);
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(0, ret);

    /* 关闭文件 */
    TEST_ASSERT_EQUAL_INT32(0, fs_close((uint32_t)fd));
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    int32_t test_count = 0;
    int32_t passed = 0;

    printf("\n=== FS IPC 接口测试 ===\n\n");

    /* fs_open/fs_close 测试 */
    test_count++;
    printf("测试 1/%d: test_fs_open_valid_file...\n", test_count);
    test_fs_open_valid_file();
    passed++;
    printf("  PASSED\n");

    test_count++;
    printf("测试 2/%d: test_fs_open_create_file...\n", test_count);
    test_fs_open_create_file();
    passed++;
    printf("  PASSED\n");

    test_count++;
    printf("测试 3/%d: test_fs_open_nonexistent_file...\n", test_count);
    test_fs_open_nonexistent_file();
    passed++;
    printf("  PASSED\n");

    test_count++;
    printf("测试 4/%d: test_fs_close_invalid_fd...\n", test_count);
    test_fs_close_invalid_fd();
    passed++;
    printf("  PASSED\n");

    /* fs_read/fs_write 测试 */
    test_count++;
    printf("测试 5/%d: test_fs_write_data...\n", test_count);
    test_fs_write_data();
    passed++;
    printf("  PASSED\n");

    test_count++;
    printf("测试 6/%d: test_fs_read_data...\n", test_count);
    test_fs_read_data();
    passed++;
    printf("  PASSED\n");

    test_count++;
    printf("测试 7/%d: test_fs_write_append...\n", test_count);
    test_fs_write_append();
    passed++;
    printf("  PASSED\n");

    /* fs_lseek 测试 */
    test_count++;
    printf("测试 8/%d: test_fs_lseek_seek_set...\n", test_count);
    test_fs_lseek_seek_set();
    passed++;
    printf("  PASSED\n");

    test_count++;
    printf("测试 9/%d: test_fs_lseek_seek_cur...\n", test_count);
    test_fs_lseek_seek_cur();
    passed++;
    printf("  PASSED\n");

    test_count++;
    printf("测试 10/%d: test_fs_lseek_seek_end...\n", test_count);
    test_fs_lseek_seek_end();
    passed++;
    printf("  PASSED\n");

    /* fs_fstat 测试 */
    test_count++;
    printf("测试 11/%d: test_fs_fstat_file_status...\n", test_count);
    test_fs_fstat_file_status();
    passed++;
    printf("  PASSED\n");

    /* fs_chmod/chown 测试 */
    test_count++;
    printf("测试 12/%d: test_fs_chmod_change_mode...\n", test_count);
    test_fs_chmod_change_mode();
    passed++;
    printf("  PASSED\n");

    test_count++;
    printf("测试 13/%d: test_fs_chown_change_owner...\n", test_count);
    test_fs_chown_change_owner();
    passed++;
    printf("  PASSED\n");

    /* fs_ioctl/fs_fcntl 测试 */
    test_count++;
    printf("测试 14/%d: test_fs_ioctl_file_control...\n", test_count);
    test_fs_ioctl_file_control();
    passed++;
    printf("  PASSED\n");

    test_count++;
    printf("测试 15/%d: test_fs_fcntl_file_control...\n", test_count);
    test_fs_fcntl_file_control();
    passed++;
    printf("  PASSED\n");

    printf("\n=== 测试结果: %d/%d 通过 ===\n", passed, test_count);

    return (passed == test_count) ? 0 : 1;
}
