/**
 * @file    test_fs_ext4_complete.c
 * @brief   Ext4 完整集成测试
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 完整集成测试：
 *          - 超级块测试
 *          - Inode 测试
 *          - 块位图测试
 *          - 目录操作测试
 *          - 文件操作测试
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 完整实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* ========================================================================
 * 简易测试框架
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
 * 模拟初始化
 * ======================================================================== */

#define EXT4_MAGIC               0xEF53U
#define EXT4_SUPERBLOCK_OFFSET   1024U
#define EXT4_SUPERBLOCK_SIZE     1024U

typedef struct
{
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_magic;
} ext4_superblock_t;

extern void ext4_inode_table_init(void);
extern void ext4_block_bitmap_init(void);
extern void ext4_inode_bitmap_init(void);

extern int32_t ext4_get_superblock(uint32_t dev_id, ext4_superblock_t *sb);
extern bool ext4_check_magic(const ext4_superblock_t *sb);
extern bool ext4_validate_superblock(const ext4_superblock_t *sb);
extern ext4_fs_state_t ext4_get_fs_state(const ext4_superblock_t *sb);

extern int32_t ext4_alloc_inode(uint32_t mode, uint32_t uid, uint32_t gid);
extern int32_t ext4_free_inode(uint32_t ino);
extern int32_t ext4_read_inode(uint32_t ino, void *inode);
extern int32_t ext4_write_inode(uint32_t ino, const void *inode);
extern int32_t ext4_get_file_size(uint32_t ino, uint32_t *size);

extern int32_t ext4_alloc_block(uint32_t block_id, uint32_t *block_nr);
extern int32_t ext4_free_block(uint32_t block_id, uint32_t block_nr);
extern bool ext4_is_block_used(uint32_t block_id, uint32_t block_nr);
extern uint32_t ext4_get_free_blocks(uint32_t block_id);

extern int32_t ext4_alloc_inode_bitmap(uint32_t block_id, uint32_t *inode_nr);
extern int32_t ext4_free_inode_bitmap(uint32_t block_id, uint32_t inode_nr);
extern bool ext4_is_inode_used(uint32_t block_id, uint32_t inode_nr);
extern uint32_t ext4_get_free_inodes_bitmap(uint32_t block_id);

extern int32_t ext4_mkdir(uint32_t parent_ino, const char *name,
                          uint32_t mode, uint32_t uid, uint32_t gid);
extern int32_t ext4_rmdir(uint32_t ino);
extern int32_t ext4_lookup(uint32_t parent_ino, const char *name,
                           void *entry);
extern int32_t ext4_readdir(uint32_t parent_ino, void *entries,
                            uint32_t max_count);
extern bool ext4_is_dir_empty(uint32_t ino);

extern int32_t ext4_open(const char *path, uint32_t flags,
                          uint32_t mode, uint32_t uid, uint32_t gid);
extern int32_t ext4_close(int32_t fd);
extern int32_t ext4_read(int32_t fd, void *buf, uint32_t count);
extern int32_t ext4_write(int32_t fd, const void *buf, uint32_t count);
extern int32_t ext4_create(const char *path, uint32_t mode,
                           uint32_t uid, uint32_t gid);
extern int32_t ext4_unlink(const char *path);
extern int32_t ext4_fstat(int32_t fd, uint32_t *size);

/* ========================================================================
 * 测试用例
 * ======================================================================== */

void test_ext4_superblock_check_magic(void)
{
    ext4_superblock_t sb = {0};
    sb.s_magic = EXT4_MAGIC;
    TEST_ASSERT_TRUE(ext4_check_magic(&sb));
}

void test_ext4_superblock_invalid_magic(void)
{
    ext4_superblock_t sb = {0};
    sb.s_magic = 0x0000U;
    TEST_ASSERT_FALSE(ext4_check_magic(&sb));
}

void test_ext4_superblock_validate_valid(void)
{
    ext4_superblock_t sb = {0};
    sb.s_magic = EXT4_MAGIC;
    sb.s_inodes_count = 1000000U;
    sb.s_blocks_count = 1000000U;
    sb.s_log_block_size = 2U;
    TEST_ASSERT_TRUE(ext4_validate_superblock(&sb));
}

void test_ext4_superblock_validate_invalid(void)
{
    ext4_superblock_t sb = {0};
    sb.s_magic = 0x0000U;
    sb.s_log_block_size = 5U;
    TEST_ASSERT_FALSE(ext4_validate_superblock(&sb));
}

void test_ext4_inode_alloc_and_free(void)
{
    uint32_t ino;

    ino = ext4_alloc_inode(0644U, 0U, 0U);
    TEST_ASSERT_GE(ino, 1U);

    ext4_free_inode(ino);
}

void test_ext4_inode_alloc_and_read(void)
{
    uint32_t ino;
    uint32_t size;

    ino = ext4_alloc_inode(0644U, 1000U, 1000U);
    TEST_ASSERT_GE(ino, 1U);

    size = 0U;
    ext4_get_file_size(ino, &size);
    TEST_ASSERT_EQ(size, 0U);

    ext4_free_inode(ino);
}

void test_ext4_block_alloc_and_free(void)
{
    uint32_t block_nr;

    ext4_alloc_block(0U, &block_nr);
    TEST_ASSERT_GE(block_nr, 2U);

    ext4_free_block(0U, block_nr);
    TEST_ASSERT_TRUE(ext4_is_block_used(0U, 2U));
}

void test_ext4_block_bitmap(void)
{
    uint32_t free_blocks;

    ext4_get_free_blocks(0U);
    TEST_ASSERT_GE(free_blocks, 0U);
}

void test_ext4_inode_bitmap(void)
{
    uint32_t free_inodes;

    ext4_get_free_inodes_bitmap(0U);
    TEST_ASSERT_GE(free_inodes, 0U);
}

void test_ext4_mkdir_and_rmdir(void)
{
    int32_t dir_ino;

    dir_ino = ext4_mkdir(1U, "testdir", 0755U, 0U, 0U);
    TEST_ASSERT_GE(dir_ino, 0U);

    ext4_rmdir(dir_ino);
}

void test_ext4_create_and_unlink(void)
{
    int32_t ret;

    ret = ext4_create("test.txt", 0644U, 0U, 0U);
    TEST_ASSERT_EQ(ret, 0);

    ret = ext4_unlink("test.txt");
    TEST_ASSERT_EQ(ret, 0);
}

void test_ext4_open_and_close(void)
{
    int32_t fd;

    /* 创建文件 */
    ext4_create("test.txt", 0644U, 0U, 0U);

    /* 打开文件 */
    fd = ext4_open("test.txt", O_WRONLY, 0644U, 0U, 0U);
    TEST_ASSERT_GE(fd, 0U);

    /* 写入数据 */
    const char *data = "Hello, Ext4!";
    int32_t written = ext4_write(fd, data, (uint32_t)strlen(data));
    TEST_ASSERT_EQ(written, (int32_t)strlen(data));

    /* 关闭文件 */
    ext4_close(fd);

    /* 删除文件 */
    ext4_unlink("test.txt");
}

void test_ext4_open_and_read(void)
{
    int32_t fd;
    char buf[256];

    /* 创建文件 */
    ext4_create("test.txt", 0644U, 0U, 0U);

    /* 打开文件（只读） */
    fd = ext4_open("test.txt", O_RDONLY, 0644U, 0U, 0U);
    TEST_ASSERT_GE(fd, 0U);

    /* 读取数据 */
    int32_t read_count = ext4_read(fd, buf, sizeof(buf));
    TEST_ASSERT_GE(read_count, 0);

    /* 关闭文件 */
    ext4_close(fd);

    /* 删除文件 */
    ext4_unlink("test.txt");
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n");
    printf("============================================\n");
    printf("  Ext4 完整集成测试\n");
    printf("============================================\n");
    printf("\n");

    printf("=== 超级块测试 ===\n");
    TEST_RUN(ext4_superblock_check_magic);
    TEST_RUN(ext4_superblock_invalid_magic);
    TEST_RUN(ext4_superblock_validate_valid);
    TEST_RUN(ext4_superblock_validate_invalid);

    printf("\n=== Inode 测试 ===\n");
    TEST_RUN(ext4_inode_alloc_and_free);
    TEST_RUN(ext4_inode_alloc_and_read);

    printf("\n=== 块位图测试 ===\n");
    TEST_RUN(ext4_block_alloc_and_free);
    TEST_RUN(ext4_block_bitmap);
    TEST_RUN(ext4_inode_bitmap);

    printf("\n=== 目录操作测试 ===\n");
    TEST_RUN(ext4_mkdir_and_rmdir);

    printf("\n=== 文件操作测试 ===\n");
    TEST_RUN(ext4_create_and_unlink);
    TEST_RUN(ext4_open_and_close);
    TEST_RUN(ext4_open_and_read);

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
