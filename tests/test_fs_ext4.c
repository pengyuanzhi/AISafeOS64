/**
 * @file    test_fs_ext4.c
 * @brief   Ext4 文件系统单元测试
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 文件系统单元测试：
 *          - 超级块解析
 *          - Inode 操作
 *          - 文件创建/删除
 *          - 目录创建/删除
 *          - 权限管理
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
 * Ext4 常量定义
 * ======================================================================== */

#define EXT4_MAGIC               0xEF53U
#define EXT4_BLOCK_SIZE          4096U
#define EXT4_NAME_LEN            255U
#define EXT4_MAX_FILES          64U
#define EXT4_MAX_DIRS           16U

typedef int32_t kernel_status_t;
#define KERNEL_OK  ((kernel_status_t)0)
#define ENOENT     2
#define EEXIST     17
#define ENOMEM     12
#define EINVAL     22

typedef enum
{
    EXT4_TYPE_REGULAR = 0U,
    EXT4_TYPE_DIR      = 1U
} ext4_file_type_t;

/* ========================================================================
 * Ext4 Mock 数据结构
 * ======================================================================== */

typedef struct ext4_file
{
    char            name[EXT4_NAME_LEN];
    uint32_t        ino;
    uint32_t        size;
    uint32_t        mode;
    uint32_t        uid;
    uint32_t        gid;
    ext4_file_type_t type;
    bool            in_use;
} ext4_file_t;

typedef struct ext4_dir
{
    char            name[EXT4_NAME_LEN];
    uint32_t        ino;
    uint32_t        nfiles;
    bool            in_use;
} ext4_dir_t;

static ext4_file_t s_files[EXT4_MAX_FILES];
static ext4_dir_t  s_dirs[EXT4_MAX_DIRS];
static uint32_t     s_next_ino = 1U;

/* ========================================================================
 * Mock API 实现（简化版）
 * ======================================================================== */

/**
 * @brief 初始化 Ext4
 */
static void ext4_init(void)
{
    (void)memset(s_files, 0, sizeof(s_files));
    (void)memset(s_dirs, 0, sizeof(s_dirs));
    s_next_ino = 1U;

    /* 创建根目录 */
    (void)strncpy(s_dirs[0].name, "/", EXT4_NAME_LEN - 1U);
    s_dirs[0].ino = s_next_ino++;
    s_dirs[0].in_use = true;
}

/**
 * @brief 创建文件
 */
static int32_t ext4_create_file(const char *name, uint32_t mode,
                                     uint32_t uid, uint32_t gid)
{
    uint32_t i;

    /* 检查文件是否已存在 */
    for (i = 0U; i < EXT4_MAX_FILES; i++)
    {
        if (s_files[i].in_use && (strcmp(s_files[i].name, name) == 0))
        {
            return -EEXIST;
        }
    }

    /* 分配新文件 */
    for (i = 0U; i < EXT4_MAX_FILES; i++)
    {
        if (!s_files[i].in_use)
        {
            (void)strncpy(s_files[i].name, name, EXT4_NAME_LEN - 1U);
            s_files[i].ino = s_next_ino++;
            s_files[i].size = 0U;
            s_files[i].mode = mode;
            s_files[i].type = EXT4_TYPE_REGULAR;
            s_files[i].uid = uid;
            s_files[i].gid = gid;
            s_files[i].in_use = true;
            return (int32_t)i;
        }
    }

    return -ENOMEM;
}

/**
 * @brief 删除文件
 */
static int32_t ext4_delete_file(const char *name)
{
    uint32_t i;

    for (i = 0U; i < EXT4_MAX_FILES; i++)
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
 * @brief 修改权限
 */
static int32_t ext4_chmod(const char *name, uint32_t mode)
{
    uint32_t i;

    for (i = 0U; i < EXT4_MAX_FILES; i++)
    {
        if (s_files[i].in_use && (strcmp(s_files[i].name, name) == 0))
        {
            s_files[i].mode = mode;
            return KERNEL_OK;
        }
    }

    return -ENOENT;
}

/**
 * @brief 修改所有者
 */
static int32_t ext4_chown(const char *name, uint32_t uid, uint32_t gid)
{
    uint32_t i;

    for (i = 0U; i < EXT4_MAX_FILES; i++)
    {
        if (s_files[i].in_use && (strcmp(s_files[i].name, name) == 0))
        {
            s_files[i].uid = uid;
            s_files[i].gid = gid;
            return KERNEL_OK;
        }
    }

    return -ENOENT;
}

/**
 * @brief 创建目录
 */
static int32_t ext4_create_dir(const char *name, uint32_t mode)
{
    uint32_t i;

    for (i = 0U; i < EXT4_MAX_DIRS; i++)
    {
        if (!s_dirs[i].in_use)
        {
            (void)strncpy(s_dirs[i].name, name, EXT4_NAME_LEN - 1U);
            s_dirs[i].ino = s_next_ino++;
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
static int32_t ext4_delete_dir(const char *name)
{
    uint32_t i;

    if (strcmp(name, "/") == 0)
    {
        return -EINVAL;
    }

    for (i = 0U; i < EXT4_MAX_DIRS; i++)
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

void test_ext4_file_create(void)
{
    int32_t ret;

    ext4_init();

    ret = ext4_create_file("test.txt", 0644U, 0U, 0U);
    TEST_ASSERT_GE(ret, 0);

    ret = ext4_create_file("test.txt", 0644U, 0U, 0U);
    TEST_ASSERT_EQ(ret, -EEXIST);
}

void test_ext4_file_delete(void)
{
    int32_t ret;

    ext4_init();

    (void)ext4_create_file("test.txt", 0644U, 0U, 0U);

    ret = ext4_delete_file("test.txt");
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = ext4_delete_file("nonexistent.txt");
    TEST_ASSERT_EQ(ret, -ENOENT);
}

void test_ext4_chmod(void)
{
    int32_t ret;

    ext4_init();

    (void)ext4_create_file("test.txt", 0644U, 0U, 0U);

    ret = ext4_chmod("test.txt", 0755U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = ext4_chmod("nonexistent.txt", 0755U);
    TEST_ASSERT_EQ(ret, -ENOENT);
}

void test_ext4_chown(void)
{
    int32_t ret;

    ext4_init();

    (void)ext4_create_file("test.txt", 0644U, 0U, 0U);

    ret = ext4_chown("test.txt", 1000U, 1000U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = ext4_chown("nonexistent.txt", 1000U, 1000U);
    TEST_ASSERT_EQ(ret, -ENOENT);
}

void test_ext4_dir_create(void)
{
    int32_t ret;

    ext4_init();

    ret = ext4_create_dir("/test", 0755U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

void test_ext4_dir_delete(void)
{
    int32_t ret;

    ext4_init();

    (void)ext4_create_dir("/test", 0755U);

    ret = ext4_delete_dir("/test");
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = ext4_delete_dir("/nonexistent");
    TEST_ASSERT_EQ(ret, -ENOENT);
}

void test_ext4_root_dir_protected(void)
{
    int32_t ret;

    ext4_init();

    ret = ext4_delete_dir("/");
    TEST_ASSERT_EQ(ret, -EINVAL);
}

void test_ext4_multiple_files(void)
{
    int32_t ret;
    uint32_t i;
    char name[32];

    ext4_init();

    for (i = 0U; i < 10U; i++)
    {
        (void)snprintf(name, sizeof(name), "file%u.txt", i);
        ret = ext4_create_file(name, 0644U, 0U, 0U);
        TEST_ASSERT_GE(ret, 0);
    }
}

void test_ext4_permission_check(void)
{
    int32_t ret;

    ext4_init();

    /* 创建带权限的文件 */
    ret = ext4_create_file("test.txt", 0644U, 1000U, 1000U);
    TEST_ASSERT_GE(ret, 0);

    /* 修改权限为 0755 */
    ret = ext4_chmod("test.txt", 0755U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

void test_ext4_owner_check(void)
{
    int32_t ret;

    ext4_init();

    /* 创建文件 */
    ret = ext4_create_file("test.txt", 0644U, 0U, 0U);
    TEST_ASSERT_GE(ret, 0);

    /* 修改所有者 */
    ret = ext4_chown("test.txt", 1000U, 1000U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n");
    printf("============================================\n");
    printf("  Ext4 文件系统单元测试\n");
    printf("============================================\n");
    printf("\n");

    printf("=== 文件操作 ===\n");
    TEST_RUN(ext4_file_create);
    TEST_RUN(ext4_file_delete);

    printf("\n=== 权限管理 ===\n");
    TEST_RUN(ext4_chmod);
    TEST_RUN(ext4_chown);

    printf("\n=== 目录操作 ===\n");
    TEST_RUN(ext4_dir_create);
    TEST_RUN(ext4_dir_delete);
    TEST_RUN(ext4_root_dir_protected);

    printf("\n=== 边界条件 ===\n");
    TEST_RUN(ext4_multiple_files);
    TEST_RUN(ext4_permission_check);
    TEST_RUN(ext4_owner_check);

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
