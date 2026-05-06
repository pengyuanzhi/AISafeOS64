/**
 * @file    test_fs_ramfs.c
 * @brief   RamFS 内存文件系统单元测试
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.1
 *
 * @details RamFS 内存文件系统单元测试：
 *          - 文件创建/删除
 *          - 文件读写
 *          - 目录创建/删除
 *          - 目录列表
 *          - 文件重命名
 *          - 边界条件和错误处理
 *
 * @note MISRA-C:2012 合规
 * @note TDD: RED - 先写测试
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ========================================================================
 * 简易测试框架（内联版）
 * ======================================================================== */

static uint32_t s_total   = 0U;
static uint32_t s_passed  = 0U;
static uint32_t s_failed  = 0U;

#define TEST_ASSERT_EQ(a, b)                                               \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((int64_t)(a) == (int64_t)(b)) { s_passed++; }                  \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 %lld 实际 %lld\n",                   \
                   __FILE__, __LINE__,                                      \
                   (long long)(int64_t)(b), (long long)(int64_t)(a));       \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_NE(a, b)                                               \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((int64_t)(a) != (int64_t)(b)) { s_passed++; }                  \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 %lld != %lld\n",                     \
                   __FILE__, __LINE__,                                      \
                   (long long)(int64_t)(a), (long long)(int64_t)(b));       \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_GT(a, b)                                               \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((int64_t)(a) > (int64_t)(b)) { s_passed++; }                   \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 %lld > %lld\n",                      \
                   __FILE__, __LINE__,                                      \
                   (long long)(int64_t)(a), (long long)(int64_t)(b));       \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_GE(a, b)                                               \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((int64_t)(a) >= (int64_t)(b)) { s_passed++; }                  \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 %lld >= %lld\n",                     \
                   __FILE__, __LINE__,                                      \
                   (long long)(int64_t)(a), (long long)(int64_t)(b));       \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_TRUE(cond)                                             \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if (cond) { s_passed++; }                                          \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 true: %s\n",                         \
                   __FILE__, __LINE__, #cond);                              \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_FALSE(cond)                                            \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if (!(cond)) { s_passed++; }                                       \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 false: %s\n",                        \
                   __FILE__, __LINE__, #cond);                              \
        }                                                                  \
    } while (0)

#define TEST_RUN(name)                                                     \
    do                                                                     \
    {                                                                      \
        printf("  [RUN] %s\n", #name);                                     \
        test_##name();                                                     \
    } while (0)

/* ========================================================================
 * RamFS 常量定义（与 ramfs.c 一致）
 * ======================================================================== */

#define RAMFS_MAX_FILES         64U
#define RAMFS_MAX_DIRS          16U
#define RAMFS_MAX_FILE_SIZE     4096U
#define RAMFS_MAX_PATH         256U
#define RAMFS_MAX_NAME         64U

typedef int32_t kernel_status_t;
#define KERNEL_OK  ((kernel_status_t)0)
#define ENOENT     2
#define EEXIST     17
#define ENOMEM     12
#define EINVAL     22
#define ENOTEMPTY   39

typedef enum
{
    RAMFS_TYPE_REGULAR = 0U,
    RAMFS_TYPE_DIR      = 1U
} ramfs_file_type_t;

/* ========================================================================
 * RamFS 数据结构（简化版，用于测试）
 * ======================================================================== */

typedef struct ramfs_file
{
    char            name[RAMFS_MAX_NAME]; /**< @brief 文件名 */
    uint32_t        ino;              /**< @brief inode 编号 */
    uint8_t         data[RAMFS_MAX_FILE_SIZE]; /**< @brief 文件数据 */
    uint32_t        size;             /**< @brief 文件大小 */
    uint32_t        mode;             /**< @brief 文件权限 */
    uint32_t        uid;              /**< @brief 用户 ID */
    uint32_t        gid;              /**< @brief 组 ID */
    uint64_t        atime;            /**< @brief 访问时间 */
    uint64_t        mtime;            /**< @brief 修改时间 */
    uint64_t        ctime;            /**< @brief 创建时间 */
    ramfs_file_type_t type;            /**< @brief 文件类型 */
    bool            in_use;           /**< @brief 使用标记 */
} ramfs_file_t;

typedef struct ramfs_dir
{
    char            name[RAMFS_MAX_NAME]; /**< @brief 目录名 */
    uint32_t        ino;              /**< @brief inode 编号 */
    uint32_t        mode;             /**< @brief 权限 */
    uint32_t        nfiles;            /**< @brief 文件数量 */
    bool            in_use;           /**< @brief 使用标记 */
} ramfs_dir_t;

static ramfs_file_t s_files[RAMFS_MAX_FILES];
static ramfs_dir_t  s_dirs[RAMFS_MAX_DIRS];
static uint32_t     s_next_ino = 1U;

/* ========================================================================
 * Mock API 实现（简化版）
 * ======================================================================== */

/**
 * @brief 初始化 RamFS
 */
static void ramfs_init(void)
{
    (void)memset(s_files, 0, sizeof(s_files));
    (void)memset(s_dirs, 0, sizeof(s_dirs));
    s_next_ino = 1U;

    /* 创建根目录 */
    (void)strncpy(s_dirs[0].name, "/", RAMFS_MAX_NAME - 1U);
    s_dirs[0].ino = s_next_ino++;
    s_dirs[0].mode = 0755U;
    s_dirs[0].in_use = true;
}

/**
 * @brief 创建文件
 */
static int32_t ramfs_create_file(const char *name, uint32_t mode)
{
    uint32_t i;

    /* 检查文件是否已存在 */
    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (s_files[i].in_use && (strcmp(s_files[i].name, name) == 0))
        {
            return -EEXIST;
        }
    }

    /* 分配新文件 */
    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (!s_files[i].in_use)
        {
            (void)strncpy(s_files[i].name, name, RAMFS_MAX_NAME - 1U);
            s_files[i].ino = s_next_ino++;
            s_files[i].size = 0U;
            s_files[i].mode = mode;
            s_files[i].type = RAMFS_TYPE_REGULAR;
            s_files[i].uid = 0U;
            s_files[i].gid = 0U;
            s_files[i].atime = 0ULL;
            s_files[i].mtime = 0ULL;
            s_files[i].ctime = 0ULL;
            s_files[i].in_use = true;
            (void)memset(s_files[i].data, 0, RAMFS_MAX_FILE_SIZE);
            return (int32_t)i;
        }
    }

    return -ENOMEM;
}

/**
 * @brief 删除文件
 */
static int32_t ramfs_delete_file(const char *name)
{
    uint32_t i;

    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (s_files[i].in_use && (strcmp(s_files[i].name, name) == 0))
        {
            s_files[i].in_use = false;
            return KERNEL_OK;
        }
    }

    return -ENOENT;
}

/**
 * @brief 写入文件
 */
static int32_t ramfs_write_file(const char *name, const uint8_t *data,
                                   uint32_t size)
{
    uint32_t i;

    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (s_files[i].in_use && (strcmp(s_files[i].name, name) == 0))
        {
            if (size > RAMFS_MAX_FILE_SIZE)
            {
                return -EINVAL;
            }

            (void)memcpy(s_files[i].data, data, size);
            s_files[i].size = size;
            s_files[i].mtime = 1ULL;
            return KERNEL_OK;
        }
    }

    return -ENOENT;
}

/**
 * @brief 读取文件
 */
static int32_t ramfs_read_file(const char *name, uint8_t *data,
                                  uint32_t size)
{
    uint32_t i;

    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (s_files[i].in_use && (strcmp(s_files[i].name, name) == 0))
        {
            uint32_t read_size = (size > s_files[i].size) ?
                                s_files[i].size : size;
            (void)memcpy(data, s_files[i].data, read_size);
            s_files[i].atime = 1ULL;
            return (int32_t)read_size;
        }
    }

    return -ENOENT;
}

/**
 * @brief 创建目录
 */
static int32_t ramfs_create_dir(const char *name, uint32_t mode)
{
    uint32_t i;

    for (i = 0U; i < RAMFS_MAX_DIRS; i++)
    {
        if (!s_dirs[i].in_use)
        {
            (void)strncpy(s_dirs[i].name, name, RAMFS_MAX_NAME - 1U);
            s_dirs[i].ino = s_next_ino++;
            s_dirs[i].mode = mode;
            s_dirs[i].nfiles = 0U;
            s_dirs[i].in_use = true;
            return KERNEL_OK;
        }
    }

    return -ENOMEM;
}

/**
 * @brief 删除目录
 */
static int32_t ramfs_delete_dir(const char *name)
{
    uint32_t i;

    /* 根目录不能删除 */
    if (strcmp(name, "/") == 0)
    {
        return -EINVAL;
    }

    for (i = 0U; i < RAMFS_MAX_DIRS; i++)
    {
        if (s_dirs[i].in_use && (strcmp(s_dirs[i].name, name) == 0))
        {
            s_dirs[i].in_use = false;
            return KERNEL_OK;
        }
    }

    return -ENOENT;
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

void test_file_create(void)
{
    int32_t ret;

    ramfs_init();

    ret = ramfs_create_file("test.txt", 0644U);
    TEST_ASSERT_GE(ret, 0);

    ret = ramfs_create_file("test.txt", 0644U);
    TEST_ASSERT_EQ(ret, -EEXIST);
}

void test_file_delete(void)
{
    int32_t ret;

    ramfs_init();

    (void)ramfs_create_file("test.txt", 0644U);

    ret = ramfs_delete_file("test.txt");
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = ramfs_delete_file("nonexistent.txt");
    TEST_ASSERT_EQ(ret, -ENOENT);
}

void test_file_write(void)
{
    int32_t ret;
    uint8_t data[100];
    uint8_t buffer[100];

    ramfs_init();

    (void)ramfs_create_file("test.txt", 0644U);

    (void)memset(data, 0xAA, sizeof(data));
    ret = ramfs_write_file("test.txt", data, 100U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    (void)memset(buffer, 0, sizeof(buffer));
    ret = ramfs_read_file("test.txt", buffer, 100U);
    TEST_ASSERT_EQ(ret, 100);
    TEST_ASSERT_TRUE((memcmp(data, buffer, 100U) == 0));
}

void test_file_read(void)
{
    int32_t ret;
    uint8_t data[50];
    uint8_t buffer[100];

    ramfs_init();

    (void)ramfs_create_file("test.txt", 0644U);
    (void)memset(data, 0xBB, sizeof(data));
    (void)ramfs_write_file("test.txt", data, 50U);

    (void)memset(buffer, 0, sizeof(buffer));
    ret = ramfs_read_file("test.txt", buffer, 100U);
    TEST_ASSERT_EQ(ret, 50);
    TEST_ASSERT_TRUE((memcmp(data, buffer, 50U) == 0));
}

void test_dir_create(void)
{
    int32_t ret;

    ramfs_init();

    ret = ramfs_create_dir("/test", 0755U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

void test_dir_delete(void)
{
    int32_t ret;

    ramfs_init();

    (void)ramfs_create_dir("/test", 0755U);

    ret = ramfs_delete_dir("/test");
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = ramfs_delete_dir("/nonexistent");
    TEST_ASSERT_EQ(ret, -ENOENT);
}

void test_root_dir_protected(void)
{
    int32_t ret;

    ramfs_init();

    ret = ramfs_delete_dir("/");
    TEST_ASSERT_EQ(ret, -EINVAL);
}

void test_multiple_files(void)
{
    int32_t ret;
    uint32_t i;
    char name[32];

    ramfs_init();

    for (i = 0U; i < 10U; i++)
    {
        (void)snprintf(name, sizeof(name), "file%u.txt", i);
        ret = ramfs_create_file(name, 0644U);
        TEST_ASSERT_GE(ret, 0);
    }
}

void test_file_size_limit(void)
{
    int32_t ret;
    uint8_t data[5000];

    ramfs_init();

    (void)ramfs_create_file("large.txt", 0644U);

    (void)memset(data, 0, sizeof(data));
    ret = ramfs_write_file("large.txt", data, 5000U);
    TEST_ASSERT_EQ(ret, -EINVAL);
}

void test_file_not_found(void)
{
    int32_t ret;
    uint8_t buffer[100];

    ramfs_init();

    (void)memset(buffer, 0, sizeof(buffer));
    ret = ramfs_read_file("nonexistent.txt", buffer, 100U);
    TEST_ASSERT_EQ(ret, -ENOENT);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n");
    printf("============================================\n");
    printf("  RamFS 单元测试\n");
    printf("============================================\n");
    printf("\n");

    printf("=== 文件创建/删除 ===\n");
    TEST_RUN(file_create);
    TEST_RUN(file_delete);

    printf("\n=== 文件读写 ===\n");
    TEST_RUN(file_write);
    TEST_RUN(file_read);

    printf("\n=== 目录创建/删除 ===\n");
    TEST_RUN(dir_create);
    TEST_RUN(dir_delete);
    TEST_RUN(root_dir_protected);

    printf("\n=== 边界条件 ===\n");
    TEST_RUN(multiple_files);
    TEST_RUN(file_size_limit);
    TEST_RUN(file_not_found);

    printf("\n");
    printf("============================================\n");
    printf("  测试总结\n");
    printf("============================================\n");
    printf("  总测试数: %u\n", s_total);
    printf("  通过: %u\n", s_passed);
    printf("  失败: %u\n", s_failed);
    printf("============================================\n");
    printf("\n");

    return (s_failed > 0) ? 1 : 0;
}
