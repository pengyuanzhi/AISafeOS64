/**
 * @file    test_fs_fat32.c
 * @brief   FAT32 文件系统单元测试
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 文件系统测试：
 *          - FAT32 BPB 解析
 *          - FAT 表解析和簇管理
 *          - 目录项解析
 *          - 文件查找
 *          - 文件读写
 *
 * @note MISRA-C:2012 合规
 * @note TDD: RED/GREEN 阶段
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fat32_types.h"
#include "fat32_bpb.h"
#include "fat32_fat.h"
#include "fat32_dir.h"
#include "fat32_file.h"
#include "fat32_path.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * 测试宏定义
 * ======================================================================== */

/** @brief 测试失败计数器 */
static int32_t s_test_failures;

#define TEST_ASSERT(condition) do { \
    if (!(condition)) { \
        printf("  FAIL: %s (line %d)\n", #condition, __LINE__); \
        s_test_failures++; \
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
#define TEST_ASSERT_EQUAL_UINT32(a, b) TEST_ASSERT((uint32_t)(a) == (uint32_t)(b))
#define TEST_ASSERT_LESS_THAN_UINT32(a, b) TEST_ASSERT((uint32_t)(a) < (uint32_t)(b))
#define TEST_ASSERT_EQUAL_MEM(a, b, sz) TEST_ASSERT(memcmp((a), (b), (sz)) == 0)

/* ========================================================================
 * 模拟块设备
 * ======================================================================== */

/** @brief 模拟磁盘大小（1MB） */
#define MOCK_DISK_SECTORS       2048U

/** @brief 模拟磁盘数据 */
static uint8_t s_mock_disk[MOCK_DISK_SECTORS * FAT32_SECTOR_SIZE];

/**
 * @brief 模拟块设备读取
 */
static int32_t mock_block_read(uint64_t sector, void *buf, uint32_t count)
{
    uint64_t offset;
    uint64_t total_bytes;

    if (buf == NULL)
    {
        return -1;
    }

    offset = sector * (uint64_t)FAT32_SECTOR_SIZE;
    total_bytes = (uint64_t)count * (uint64_t)FAT32_SECTOR_SIZE;

    if ((offset + total_bytes) > (uint64_t)sizeof(s_mock_disk))
    {
        return -1;
    }

    (void)memcpy(buf, &s_mock_disk[offset], (size_t)total_bytes);

    return 0;
}

/**
 * @brief 模拟块设备写入
 */
static int32_t mock_block_write(uint64_t sector, const void *buf, uint32_t count)
{
    uint64_t offset;
    uint64_t total_bytes;

    if (buf == NULL)
    {
        return -1;
    }

    offset = sector * (uint64_t)FAT32_SECTOR_SIZE;
    total_bytes = (uint64_t)count * (uint64_t)FAT32_SECTOR_SIZE;

    if ((offset + total_bytes) > (uint64_t)sizeof(s_mock_disk))
    {
        return -1;
    }

    (void)memcpy(&s_mock_disk[offset], buf, (size_t)total_bytes);

    return 0;
}

/* ========================================================================
 * 测试辅助函数
 * ======================================================================== */

/**
 * @brief 初始化模拟 FAT32 磁盘映像
 *
 * @details 创建一个最小的 FAT32 文件系统映像：
 *          - BPB 在扇区 0
 *          - FAT 表在扇区 32-63（保留扇区 = 32）
 *          - 数据区从扇区 96 开始（保留 + 2*FAT）
 *          - 每簇 1 扇区，根目录在簇 2
 */
static void mock_disk_init(void)
{
    fat32_bpb_t *bpb;
    uint32_t fat_start;
    uint32_t data_start;
    uint32_t i;

    (void)memset(s_mock_disk, 0, sizeof(s_mock_disk));

    /* 构建 BPB */
    bpb = (fat32_bpb_t *)s_mock_disk;

    /* 跳转指令 */
    bpb->jmp_boot[0] = 0xEBU;
    bpb->jmp_boot[1] = 0x58U;
    bpb->jmp_boot[2] = 0x90U;

    /* OEM 名称 */
    (void)memcpy(bpb->oem_name, "AISAFE64", 8U);

    /* BPB 参数 */
    bpb->bytes_per_sec = 512U;
    bpb->sec_per_clust = 1U;
    bpb->rsvd_sec_cnt  = 32U;
    bpb->num_fats      = 2U;
    bpb->root_ent_cnt  = 0U;
    bpb->total_sec16   = 0U;
    bpb->media_type    = 0xF8U;
    bpb->fat_sz16      = 0U;
    bpb->sec_per_trk   = 32U;
    bpb->num_heads     = 64U;
    bpb->hidd_sec      = 0U;
    bpb->total_sec32   = MOCK_DISK_SECTORS;

    /* FAT32 扩展 BPB */
    bpb->fat_sz32      = 32U;   /* 每个 FAT 占 32 个扇区 */
    bpb->ext_flags     = 0U;
    bpb->fs_ver        = 0U;
    bpb->root_cluster  = 2U;
    bpb->fs_info_sec   = 1U;
    bpb->bk_boot_sec   = 6U;
    bpb->boot_sig      = 0x29U;
    bpb->vol_id        = 0x12345678U;
    (void)memcpy(bpb->vol_label, "NO NAME    ", 11U);
    (void)memcpy(bpb->fs_type, FAT32_FSTYPE_STR, 8U);

    /* 引导扇区签名 */
    s_mock_disk[510] = 0x55U;
    s_mock_disk[511] = 0xAAU;

    /* 计算 FAT 表和数据区位置 */
    fat_start  = (uint32_t)bpb->rsvd_sec_cnt;
    data_start = fat_start + ((uint32_t)bpb->num_fats * (uint32_t)bpb->fat_sz32);

    /* 初始化 FAT 表：
     * - 簇 0, 1 为保留
     * - 簇 2 为根目录（EOC 标记）
     * - 其余为空闲
     */
    /* FAT 表头两个保留簇 */
    {
        uint32_t *fat0 = (uint32_t *)&s_mock_disk[fat_start * FAT32_SECTOR_SIZE];
        uint32_t *fat1 = (uint32_t *)&s_mock_disk[(fat_start + (uint32_t)bpb->fat_sz32)
                                                    * FAT32_SECTOR_SIZE];

        fat0[0] = 0x0FFFFFF8U;   /* 介质类型 */
        fat0[1] = 0x0FFFFFFFU;   /* EOC 标记 */
        fat0[2] = 0x0FFFFFFFU;   /* 根目录 EOC */

        /* 复制到 FAT 副本 */
        (void)memcpy(fat1, fat0, 12U);
    }

    /* 根目录项（空 - 只有结束标记 0x00） */
    /* 数据区起始位置即根目录 */
    /* 不需要额外操作，磁盘已清零 */
    (void)data_start;
    (void)i;
}

/**
 * @brief 在模拟磁盘中创建一个文件目录项
 *
 * @param name      文件名（8.3格式）
 * @param cluster   起始簇号
 * @param size      文件大小
 * @param attr      文件属性
 */
static void mock_disk_create_file(const char *name, uint32_t cluster,
                                   uint32_t size, uint8_t attr)
{
    fat32_bpb_t *bpb = (fat32_bpb_t *)s_mock_disk;
    uint32_t fat_start  = (uint32_t)bpb->rsvd_sec_cnt;
    uint32_t data_start = fat_start + ((uint32_t)bpb->num_fats * (uint32_t)bpb->fat_sz32);
    uint32_t root_sec   = data_start; /* 根目录在数据区起始位置 */
    fat32_dir_entry_t *dir_entries;
    uint32_t i;

    /* 查找第一个空闲目录项 */
    dir_entries = (fat32_dir_entry_t *)&s_mock_disk[(uint64_t)root_sec * FAT32_SECTOR_SIZE];

    for (i = 0U; i < 16U; i++)
    {
        if (dir_entries[i].name[0] == 0x00U)
        {
            /* 找到空闲项 */
            (void)memcpy(dir_entries[i].name, name, 11U);
            dir_entries[i].attr        = attr;
            dir_entries[i].nt_res      = 0U;
            dir_entries[i].crt_time_tenth = 0U;
            dir_entries[i].crt_time    = 0x8000U; /* 16:00:00 */
            dir_entries[i].crt_date    = 0x5921U; /* 2023-01-01 */
            dir_entries[i].lst_acc_date = 0x5921U;
            dir_entries[i].fst_clus_hi = (uint16_t)(cluster >> 16U);
            dir_entries[i].wrt_time    = 0x8000U;
            dir_entries[i].wrt_date    = 0x5921U;
            dir_entries[i].fst_clus_lo = (uint16_t)(cluster & 0xFFFFU);
            dir_entries[i].file_size   = size;
            break;
        }
    }

    /* 更新 FAT 表中的簇链 */
    if (cluster >= 2U)
    {
        uint32_t *fat0 = (uint32_t *)&s_mock_disk[(uint64_t)fat_start * FAT32_SECTOR_SIZE];
        uint32_t *fat1 = (uint32_t *)&s_mock_disk[(uint64_t)(fat_start + bpb->fat_sz32)
                                                     * FAT32_SECTOR_SIZE];
        fat0[cluster] = FAT32_CLUSTER_EOC;
        fat1[cluster] = FAT32_CLUSTER_EOC;
    }
}

/* ========================================================================
 * 测试用例：FAT32 BPB 解析
 * ======================================================================== */

/**
 * @brief 测试 FAT32 BPB 解析 - 有效 BPB
 */
void test_fat32_bpb_parse_valid(void)
{
    fat32_context_t ctx;
    int32_t ret;

    mock_disk_init();
    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.block_read  = mock_block_read;
    ctx.block_write = mock_block_write;

    /* 解析 BPB */
    ret = fat32_bpb_parse(&ctx);
    TEST_ASSERT_EQ(ret, 0);

    /* 验证 BPB 参数 */
    TEST_ASSERT_EQUAL_UINT32(512U,  ctx.bytes_per_sec);
    TEST_ASSERT_EQUAL_UINT32(1U,    ctx.sec_per_clust);
    TEST_ASSERT_EQUAL_UINT32(512U,  ctx.cluster_size);
    TEST_ASSERT_EQUAL_UINT32(32U,   ctx.rsvd_sec_cnt);
    TEST_ASSERT_EQUAL_UINT32(2U,    ctx.num_fats);
    TEST_ASSERT_EQUAL_UINT32(32U,   ctx.fat_sz32);
    TEST_ASSERT_EQUAL_UINT32(2U,    ctx.root_cluster);
    TEST_ASSERT_EQUAL_UINT32(MOCK_DISK_SECTORS, ctx.total_sectors);

    /* 验证计算出的数据区位置 */
    /* data_sec_start = rsvd_sec_cnt + num_fats * fat_sz32 = 32 + 2*32 = 96 */
    TEST_ASSERT_EQUAL_UINT32(96U, ctx.data_sec_start);

    /* total_clusters = (total_sectors - data_sec_start) / sec_per_clust */
    /* = (2048 - 96) / 1 = 1952 */
    TEST_ASSERT_EQUAL_UINT32(1952U, ctx.total_clusters);

    printf("  BPB: sector=%u, cluster=%u, data_start=%u, clusters=%u\n",
           ctx.bytes_per_sec, ctx.cluster_size,
           ctx.data_sec_start, ctx.total_clusters);
}

/**
 * @brief 测试 FAT32 BPB 解析 - 无效参数
 */
void test_fat32_bpb_parse_null(void)
{
    int32_t ret;

    /* 空指针测试 */
    ret = fat32_bpb_parse(NULL);
    TEST_ASSERT_LT(ret, 0);

    printf("  NULL 参数返回 %d (预期 < 0)\n", ret);
}

/**
 * @brief 测试 FAT32 BPB 解析 - 无效扇区大小
 */
void test_fat32_bpb_parse_invalid_sector_size(void)
{
    fat32_context_t ctx;
    fat32_bpb_t *bpb;
    int32_t ret;

    mock_disk_init();

    /* 篡改扇区大小为无效值 */
    bpb = (fat32_bpb_t *)s_mock_disk;
    bpb->bytes_per_sec = 1024U; /* 非标准值 */

    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.block_read  = mock_block_read;
    ctx.block_write = mock_block_write;

    ret = fat32_bpb_parse(&ctx);
    TEST_ASSERT_LT(ret, 0);

    printf("  无效扇区大小返回 %d (预期 < 0)\n", ret);
}

/* ========================================================================
 * 测试用例：FAT 表解析和簇管理
 * ======================================================================== */

/**
 * @brief 测试 FAT 表读取
 */
void test_fat32_fat_read_entry(void)
{
    fat32_context_t ctx;
    uint32_t cluster_val;
    int32_t ret;

    mock_disk_init();
    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.block_read  = mock_block_read;
    ctx.block_write = mock_block_write;

    ret = fat32_bpb_parse(&ctx);
    TEST_ASSERT_EQ(ret, 0);

    /* 读取簇 0（介质类型） */
    cluster_val = 0U;
    ret = fat32_fat_read_entry(&ctx, 0U, &cluster_val);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQUAL_UINT32(0x0FFFFFF8U, cluster_val);

    /* 读取簇 1（EOC 标记） */
    cluster_val = 0U;
    ret = fat32_fat_read_entry(&ctx, 1U, &cluster_val);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQUAL_UINT32(0x0FFFFFFFU, cluster_val);

    /* 读取簇 2（根目录 EOC） */
    cluster_val = 0U;
    ret = fat32_fat_read_entry(&ctx, 2U, &cluster_val);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_TRUE(fat32_is_eoc(cluster_val));

    /* 读取空闲簇 */
    cluster_val = 0xFFU;
    ret = fat32_fat_read_entry(&ctx, 3U, &cluster_val);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQUAL_UINT32(FAT32_CLUSTER_FREE, cluster_val);

    printf("  FAT[0]=0x%08X, FAT[1]=0x%08X, FAT[2]=0x%08X, FAT[3]=0x%08X\n",
           0x0FFFFFF8U, 0x0FFFFFFFU, cluster_val, FAT32_CLUSTER_FREE);
}

/**
 * @brief 测试 FAT 表写入
 */
void test_fat32_fat_write_entry(void)
{
    fat32_context_t ctx;
    uint32_t cluster_val;
    int32_t ret;

    mock_disk_init();
    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.block_read  = mock_block_read;
    ctx.block_write = mock_block_write;

    ret = fat32_bpb_parse(&ctx);
    TEST_ASSERT_EQ(ret, 0);

    /* 写入簇 3 */
    ret = fat32_fat_write_entry(&ctx, 3U, 0x0FFFFFFFU);
    TEST_ASSERT_EQ(ret, 0);

    /* 读回验证 */
    cluster_val = 0U;
    ret = fat32_fat_read_entry(&ctx, 3U, &cluster_val);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQUAL_UINT32(0x0FFFFFFFU, cluster_val);

    printf("  写入 FAT[3]=0x%08X, 读回=0x%08X\n", 0x0FFFFFFFU, cluster_val);
}

/**
 * @brief 测试簇分配和释放
 */
void test_fat32_cluster_alloc_free(void)
{
    fat32_context_t ctx;
    uint32_t cluster1;
    uint32_t cluster2;
    uint32_t cluster_val;
    int32_t ret;

    mock_disk_init();
    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.block_read  = mock_block_read;
    ctx.block_write = mock_block_write;

    ret = fat32_bpb_parse(&ctx);
    TEST_ASSERT_EQ(ret, 0);

    /* 分配第一个空闲簇 */
    cluster1 = 0U;
    ret = fat32_alloc_cluster(&ctx, &cluster1);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_GE(cluster1, 2U);

    printf("  分配簇 1: %u\n", cluster1);

    /* 分配第二个空闲簇 */
    cluster2 = 0U;
    ret = fat32_alloc_cluster(&ctx, &cluster2);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_GE(cluster2, 2U);
    TEST_ASSERT_NE(cluster1, cluster2);

    printf("  分配簇 2: %u\n", cluster2);

    /* 验证分配的簇被标记为 EOC */
    cluster_val = 0U;
    ret = fat32_fat_read_entry(&ctx, cluster1, &cluster_val);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_TRUE(fat32_is_eoc(cluster_val));

    /* 释放第一个簇 */
    ret = fat32_free_cluster(&ctx, cluster1);
    TEST_ASSERT_EQ(ret, 0);

    /* 验证已释放 */
    cluster_val = 0xFFU;
    ret = fat32_fat_read_entry(&ctx, cluster1, &cluster_val);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_TRUE(fat32_is_free(cluster_val));

    printf("  释放簇 %u 成功\n", cluster1);
}

/**
 * @brief 测试簇链解析
 */
void test_fat32_cluster_chain(void)
{
    fat32_context_t ctx;
    uint32_t chain[4];
    uint32_t chain_len;
    int32_t ret;

    mock_disk_init();
    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.block_read  = mock_block_read;
    ctx.block_write = mock_block_write;

    ret = fat32_bpb_parse(&ctx);
    TEST_ASSERT_EQ(ret, 0);

    /* 构建簇链: 3 -> 4 -> 5 -> EOC */
    ret = fat32_fat_write_entry(&ctx, 3U, 4U);
    TEST_ASSERT_EQ(ret, 0);
    ret = fat32_fat_write_entry(&ctx, 4U, 5U);
    TEST_ASSERT_EQ(ret, 0);
    ret = fat32_fat_write_entry(&ctx, 5U, FAT32_CLUSTER_EOC);
    TEST_ASSERT_EQ(ret, 0);

    /* 解析簇链 */
    chain_len = 0U;
    ret = fat32_get_cluster_chain(&ctx, 3U, chain, 4U, &chain_len);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQUAL_UINT32(3U, chain_len);
    TEST_ASSERT_EQUAL_UINT32(3U, chain[0]);
    TEST_ASSERT_EQUAL_UINT32(4U, chain[1]);
    TEST_ASSERT_EQUAL_UINT32(5U, chain[2]);

    printf("  簇链: %u -> %u -> %u (长度=%u)\n",
           chain[0], chain[1], chain[2], chain_len);
}

/* ========================================================================
 * 测试用例：目录项解析
 * ======================================================================== */

/**
 * @brief 测试目录项解析
 */
void test_fat32_dir_entry_parse(void)
{
    fat32_dir_entry_t entry;
    uint32_t cluster;
    uint32_t size;

    /* 构建一个测试目录项 */
    (void)memset(&entry, 0, sizeof(entry));
    (void)memcpy(entry.name, "TESTFILETXT", 11U);
    entry.attr        = FAT32_ATTR_ARCHIVE;
    entry.fst_clus_hi = 0x0000U;
    entry.fst_clus_lo = 0x0005U;
    entry.file_size   = 1024U;

    /* 解析 */
    cluster = fat32_dir_entry_cluster(&entry);
    size    = entry.file_size;

    TEST_ASSERT_EQUAL_UINT32(5U, cluster);
    TEST_ASSERT_EQUAL_UINT32(1024U, size);

    printf("  目录项: cluster=%u, size=%u\n", cluster, size);
}

/**
 * @brief 测试目录项属性判断
 */
void test_fat32_dir_entry_attributes(void)
{
    fat32_dir_entry_t entry;

    /* 测试目录属性 */
    (void)memset(&entry, 0, sizeof(entry));
    entry.attr = FAT32_ATTR_DIRECTORY;
    TEST_ASSERT_TRUE((entry.attr & FAT32_ATTR_DIRECTORY) != 0U);
    TEST_ASSERT_FALSE(fat32_dir_entry_is_lfn(&entry));

    /* 测试 LFN 属性 */
    entry.attr = FAT32_ATTR_LFN;
    TEST_ASSERT_TRUE(fat32_dir_entry_is_lfn(&entry));

    /* 测试空目录项 */
    entry.name[0] = 0x00U;
    TEST_ASSERT_TRUE(fat32_dir_entry_is_empty(&entry));

    /* 测试已删除目录项 */
    entry.name[0] = 0xE5U;
    TEST_ASSERT_TRUE(fat32_dir_entry_is_deleted(&entry));

    /* 测试普通文件属性 */
    entry.name[0] = 'A';
    entry.attr = FAT32_ATTR_ARCHIVE;
    TEST_ASSERT_FALSE(fat32_dir_entry_is_empty(&entry));
    TEST_ASSERT_FALSE(fat32_dir_entry_is_deleted(&entry));
    TEST_ASSERT_FALSE(fat32_dir_entry_is_lfn(&entry));

    printf("  属性判断: 目录、LFN、空项、删除项 均正确\n");
}

/**
 * @brief 测试 8.3 文件名提取
 */
void test_fat32_dir_extract_name(void)
{
    fat32_dir_entry_t entry;
    char name_out[FAT32_NAME_MAX];
    int32_t ret;

    /* 测试标准文件名 */
    (void)memset(&entry, 0, sizeof(entry));
    (void)memcpy(entry.name, "HELLO   TXT", 11U);

    ret = fat32_dir_extract_name(&entry, name_out, sizeof(name_out));
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQUAL_STRING("HELLO.TXT", name_out);

    /* 测试无扩展名的文件名 */
    (void)memcpy(entry.name, "TESTDIR    ", 11U);
    entry.attr = FAT32_ATTR_DIRECTORY;

    ret = fat32_dir_extract_name(&entry, name_out, sizeof(name_out));
    TEST_ASSERT_EQ(ret, 0);
    /* 目录名应该没有扩展名 */
    printf("  目录名: '%s'\n", name_out);

    /* 测试卷标 */
    (void)memcpy(entry.name, "NO NAME    ", 11U);
    entry.attr = FAT32_ATTR_VOLUME_ID;

    printf("  文件名提取: 'HELLO.TXT' 正确\n");
}

/* ========================================================================
 * 测试用例：路径解析
 * ======================================================================== */

/**
 * @brief 测试路径分割
 */
void test_fat32_path_split(void)
{
    char component[FAT32_NAME_MAX];
    const char *rest;
    int32_t ret;

    /* 测试根路径 */
    rest = "/";
    ret = fat32_path_next_component(&rest, component, sizeof(component));
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQUAL_STRING("", component);

    printf("  路径 '/' -> 组件 '' (返回 0)\n");

    /* 测试单级路径 */
    rest = "/test.txt";
    ret = fat32_path_next_component(&rest, component, sizeof(component));
    TEST_ASSERT_EQ(ret, 1);
    TEST_ASSERT_EQUAL_STRING("test.txt", component);

    printf("  路径 '/test.txt' -> 组件 'test.txt'\n");

    /* 测试多级路径 */
    rest = "/dir1/file.txt";
    ret = fat32_path_next_component(&rest, component, sizeof(component));
    TEST_ASSERT_EQ(ret, 1);
    TEST_ASSERT_EQUAL_STRING("dir1", component);

    /* 继续解析 */
    ret = fat32_path_next_component(&rest, component, sizeof(component));
    TEST_ASSERT_EQ(ret, 1);
    TEST_ASSERT_EQUAL_STRING("file.txt", component);

    /* 末尾 */
    ret = fat32_path_next_component(&rest, component, sizeof(component));
    TEST_ASSERT_EQ(ret, 0);

    printf("  路径 '/dir1/file.txt' -> 'dir1' -> 'file.txt' -> 结束\n");
}

/**
 * @brief 测试文件名匹配
 */
void test_fat32_name_match(void)
{
    bool matched;

    /* 精确匹配 */
    matched = fat32_name_match_83("HELLO   TXT", "hello.txt");
    TEST_ASSERT_TRUE(matched);

    /* 大小写不敏感 */
    matched = fat32_name_match_83("HELLO   TXT", "HELLO.TXT");
    TEST_ASSERT_TRUE(matched);

    matched = fat32_name_match_83("HELLO   TXT", "Hello.Txt");
    TEST_ASSERT_TRUE(matched);

    /* 不匹配 */
    matched = fat32_name_match_83("HELLO   TXT", "world.txt");
    TEST_ASSERT_FALSE(matched);

    /* 目录名匹配 */
    matched = fat32_name_match_83("TESTDIR    ", "testdir");
    TEST_ASSERT_TRUE(matched);

    printf("  文件名匹配: 精确/大小写/不匹配 均正确\n");
}

/* ========================================================================
 * 测试用例：文件查找
 * ======================================================================== */

/**
 * @brief 测试文件查找
 */
void test_fat32_file_lookup(void)
{
    fat32_context_t ctx;
    fat32_dir_entry_t entry;
    int32_t ret;

    mock_disk_init();

    /* 在根目录创建测试文件 */
    mock_disk_create_file("TESTFILETXT", 3U, 100U, FAT32_ATTR_ARCHIVE);

    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.block_read  = mock_block_read;
    ctx.block_write = mock_block_write;

    ret = fat32_bpb_parse(&ctx);
    TEST_ASSERT_EQ(ret, 0);

    /* 查找存在的文件 */
    ret = fat32_lookup_file(&ctx, 2U, "testfile.txt", &entry);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQUAL_UINT32(3U, fat32_dir_entry_cluster(&entry));
    TEST_ASSERT_EQUAL_UINT32(100U, entry.file_size);

    printf("  查找 'testfile.txt': cluster=%u, size=%u\n",
           fat32_dir_entry_cluster(&entry), entry.file_size);

    /* 查找不存在的文件 */
    ret = fat32_lookup_file(&ctx, 2U, "nosuch.txt", &entry);
    TEST_ASSERT_LT(ret, 0);

    printf("  查找 'nosuch.txt': 返回 %d (预期 < 0)\n", ret);
}

/* ========================================================================
 * 测试用例：文件读写
 * ======================================================================== */

/**
 * @brief 测试文件读取
 */
void test_fat32_file_read(void)
{
    fat32_instance_t inst;
    const char *test_data = "Hello AISafeOS64!";
    uint32_t data_len;
    int32_t fd;
    int64_t bytes_read;
    char buf[64];
    int32_t ret;

    mock_disk_init();

    /* 在簇 3 写入测试数据 */
    {
        fat32_bpb_t *bpb = (fat32_bpb_t *)s_mock_disk;
        uint32_t fat_start  = (uint32_t)bpb->rsvd_sec_cnt;
        uint32_t data_start = fat_start + ((uint32_t)bpb->num_fats * (uint32_t)bpb->fat_sz32);
        uint32_t file_sec   = data_start + (3U - 2U); /* 簇 3 在扇区 data_start + 1 */

        /* 写入文件内容 */
        data_len = (uint32_t)strlen(test_data);
        (void)memcpy(&s_mock_disk[(uint64_t)file_sec * FAT32_SECTOR_SIZE],
                     test_data, data_len);
    }

    /* 创建目录项 */
    mock_disk_create_file("READTESTTXT", 3U, (uint32_t)strlen(test_data),
                          FAT32_ATTR_ARCHIVE);

    /* 初始化 FAT32 实例 */
    (void)memset(&inst, 0, sizeof(inst));
    inst.context.block_read  = mock_block_read;
    inst.context.block_write = mock_block_write;

    ret = fat32_bpb_parse(&inst.context);
    TEST_ASSERT_EQ(ret, 0);

    /* 打开文件 */
    fd = fat32_open(&inst, "/readtest.txt");
    TEST_ASSERT_GE(fd, 0);

    printf("  打开文件: fd=%d\n", fd);

    /* 读取文件内容 */
    (void)memset(buf, 0, sizeof(buf));
    bytes_read = fat32_read(&inst, (uint32_t)fd, buf, (uint64_t)(data_len));
    TEST_ASSERT_EQUAL_INT32((int32_t)data_len, (int32_t)bytes_read);
    TEST_ASSERT_EQUAL_STRING(test_data, buf);

    printf("  读取 %lld 字节: '%s'\n", (long long)bytes_read, buf);

    /* 关闭文件 */
    ret = fat32_close(&inst, (uint32_t)fd);
    TEST_ASSERT_EQ(ret, 0);

    printf("  关闭文件成功\n");
}

/**
 * @brief 测试文件写入
 */
void test_fat32_file_write(void)
{
    fat32_instance_t inst;
    const char *write_data = "AISafeOS64 Write Test!";
    uint32_t data_len;
    int32_t fd;
    int64_t bytes_written;
    int32_t ret;

    mock_disk_init();

    /* 创建目录项（文件在簇 3） */
    mock_disk_create_file("WRITETSTTXT", 3U, 0U, FAT32_ATTR_ARCHIVE);

    /* 设置 FAT 表中簇 3 为 EOC */
    {
        fat32_bpb_t *bpb = (fat32_bpb_t *)s_mock_disk;
        uint32_t fat_start = (uint32_t)bpb->rsvd_sec_cnt;
        uint32_t *fat0 = (uint32_t *)&s_mock_disk[(uint64_t)fat_start * FAT32_SECTOR_SIZE];
        fat0[3] = FAT32_CLUSTER_EOC;
    }

    /* 初始化 FAT32 实例 */
    (void)memset(&inst, 0, sizeof(inst));
    inst.context.block_read  = mock_block_read;
    inst.context.block_write = mock_block_write;

    ret = fat32_bpb_parse(&inst.context);
    TEST_ASSERT_EQ(ret, 0);

    /* 打开文件 */
    fd = fat32_open(&inst, "/writetst.txt");
    TEST_ASSERT_GE(fd, 0);

    printf("  打开文件: fd=%d\n", fd);

    /* 写入数据 */
    data_len = (uint32_t)strlen(write_data);
    bytes_written = fat32_write(&inst, (uint32_t)fd, write_data, (uint64_t)data_len);
    TEST_ASSERT_EQUAL_INT32((int32_t)data_len, (int32_t)bytes_written);

    printf("  写入 %lld 字节\n", (long long)bytes_written);

    /* 关闭文件 */
    ret = fat32_close(&inst, (uint32_t)fd);
    TEST_ASSERT_EQ(ret, 0);

    /* 重新打开并读回验证 */
    fd = fat32_open(&inst, "/writetst.txt");
    TEST_ASSERT_GE(fd, 0);

    {
        char buf[64];
        int64_t bytes_read;

        (void)memset(buf, 0, sizeof(buf));
        bytes_read = fat32_read(&inst, (uint32_t)fd, buf, (uint64_t)data_len);
        TEST_ASSERT_EQUAL_INT32((int32_t)data_len, (int32_t)bytes_read);
        TEST_ASSERT_EQUAL_STRING(write_data, buf);

        printf("  读回 %lld 字节: '%s'\n", (long long)bytes_read, buf);
    }

    ret = fat32_close(&inst, (uint32_t)fd);
    TEST_ASSERT_EQ(ret, 0);

    printf("  写入-读回验证通过\n");
}

/**
 * @brief 测试多簇文件读取
 */
void test_fat32_file_read_multi_cluster(void)
{
    fat32_instance_t inst;
    int32_t fd;
    int64_t bytes_read;
    char buf[2048];
    int32_t ret;
    uint32_t i;

    mock_disk_init();

    /* 创建目录项，文件大小 = 3 * 512 = 1536 */
    mock_disk_create_file("BIGFILE TXT", 3U, 1536U, FAT32_ATTR_ARCHIVE);

    /* 构建多簇链: 3 -> 4 -> 5 -> EOC（在 create_file 之后，覆盖 EOC） */
    {
        fat32_bpb_t *bpb = (fat32_bpb_t *)s_mock_disk;
        uint32_t fat_start  = (uint32_t)bpb->rsvd_sec_cnt;
        uint32_t data_start = fat_start + ((uint32_t)bpb->num_fats * (uint32_t)bpb->fat_sz32);
        uint32_t *fat0 = (uint32_t *)&s_mock_disk[(uint64_t)fat_start * FAT32_SECTOR_SIZE];

        fat0[3] = 4U;
        fat0[4] = 5U;
        fat0[5] = FAT32_CLUSTER_EOC;

        /* 在三个簇中写入数据 */
        for (i = 3U; i <= 5U; i++)
        {
            uint32_t sec = data_start + (i - 2U);
            (void)memset(&s_mock_disk[(uint64_t)sec * FAT32_SECTOR_SIZE],
                         (int)('A' + i), FAT32_SECTOR_SIZE);
        }

        /* 更新 FAT 副本 */
        (void)memcpy(&s_mock_disk[(uint64_t)(fat_start + bpb->fat_sz32) * FAT32_SECTOR_SIZE],
                     fat0, (size_t)(bpb->fat_sz32 * FAT32_SECTOR_SIZE));
    }

    /* 初始化并打开文件 */
    (void)memset(&inst, 0, sizeof(inst));
    inst.context.block_read  = mock_block_read;
    inst.context.block_write = mock_block_write;

    ret = fat32_bpb_parse(&inst.context);
    TEST_ASSERT_EQ(ret, 0);

    fd = fat32_open(&inst, "/bigfile.txt");
    TEST_ASSERT_GE(fd, 0);

    /* 读取全部数据 */
    (void)memset(buf, 0, sizeof(buf));
    bytes_read = fat32_read(&inst, (uint32_t)fd, buf, 1536U);
    TEST_ASSERT_EQUAL_INT32(1536, (int32_t)bytes_read);

    /* 验证簇 3 数据（'D'） */
    TEST_ASSERT_EQ((int)buf[0], 'D');
    /* 验证簇 4 数据（'E'） */
    TEST_ASSERT_EQ((int)buf[512], 'E');
    /* 验证簇 5 数据（'F'） */
    TEST_ASSERT_EQ((int)buf[1024], 'F');

    printf("  多簇读取: %lld 字节, 簇3='%c', 簇4='%c', 簇5='%c'\n",
           (long long)bytes_read, buf[0], buf[512], buf[1024]);

    ret = fat32_close(&inst, (uint32_t)fd);
    TEST_ASSERT_EQ(ret, 0);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    int32_t total = 0;
    int32_t passed = 0;

    s_test_failures = 0;

    printf("\n=== FAT32 文件系统测试 ===\n\n");

    /* FAT32 BPB 解析测试 */
    total++;
    printf("测试 %d: test_fat32_bpb_parse_valid...\n", total);
    s_test_failures = 0;
    test_fat32_bpb_parse_valid();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    total++;
    printf("测试 %d: test_fat32_bpb_parse_null...\n", total);
    s_test_failures = 0;
    test_fat32_bpb_parse_null();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    total++;
    printf("测试 %d: test_fat32_bpb_parse_invalid_sector_size...\n", total);
    s_test_failures = 0;
    test_fat32_bpb_parse_invalid_sector_size();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    /* FAT 表测试 */
    total++;
    printf("测试 %d: test_fat32_fat_read_entry...\n", total);
    s_test_failures = 0;
    test_fat32_fat_read_entry();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    total++;
    printf("测试 %d: test_fat32_fat_write_entry...\n", total);
    s_test_failures = 0;
    test_fat32_fat_write_entry();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    total++;
    printf("测试 %d: test_fat32_cluster_alloc_free...\n", total);
    s_test_failures = 0;
    test_fat32_cluster_alloc_free();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    total++;
    printf("测试 %d: test_fat32_cluster_chain...\n", total);
    s_test_failures = 0;
    test_fat32_cluster_chain();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    /* 目录项测试 */
    total++;
    printf("测试 %d: test_fat32_dir_entry_parse...\n", total);
    s_test_failures = 0;
    test_fat32_dir_entry_parse();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    total++;
    printf("测试 %d: test_fat32_dir_entry_attributes...\n", total);
    s_test_failures = 0;
    test_fat32_dir_entry_attributes();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    total++;
    printf("测试 %d: test_fat32_dir_extract_name...\n", total);
    s_test_failures = 0;
    test_fat32_dir_extract_name();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    /* 路径解析测试 */
    total++;
    printf("测试 %d: test_fat32_path_split...\n", total);
    s_test_failures = 0;
    test_fat32_path_split();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    total++;
    printf("测试 %d: test_fat32_name_match...\n", total);
    s_test_failures = 0;
    test_fat32_name_match();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    /* 文件查找测试 */
    total++;
    printf("测试 %d: test_fat32_file_lookup...\n", total);
    s_test_failures = 0;
    test_fat32_file_lookup();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    /* 文件读写测试 */
    total++;
    printf("测试 %d: test_fat32_file_read...\n", total);
    s_test_failures = 0;
    test_fat32_file_read();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    total++;
    printf("测试 %d: test_fat32_file_write...\n", total);
    s_test_failures = 0;
    test_fat32_file_write();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    total++;
    printf("测试 %d: test_fat32_file_read_multi_cluster...\n", total);
    s_test_failures = 0;
    test_fat32_file_read_multi_cluster();
    if (s_test_failures == 0) { passed++; printf("  PASSED\n"); }

    printf("\n=== 测试结果: %d/%d 通过 ===\n", passed, total);

    return (passed == total) ? 0 : 1;
}
