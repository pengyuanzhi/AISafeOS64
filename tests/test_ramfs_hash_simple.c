/**
 * @file    test_ramfs_hash_simple.c
 * @brief   RAMFS 哈希索引简单测试
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details 测试 ramfs 哈希索引的基本功能：
 *          - 初始化
 *          - 插入
 *          - 查找
 *          - 删除
 *          - 性能对比（线性搜索 vs 哈希查找）
 *
 * @note MISRA-C:2012 合规（简化版）
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 模拟 ramfs_hash.h */
#define RAMFS_HASH_SIZE        128U
#define RAMFS_HASH_NAME_MAX    64U
#define RAMFS_HASH_SLOT_EMPTY  0U
#define RAMFS_HASH_SLOT_USED   1U
#define RAMFS_HASH_SLOT_DELETED 2U

typedef struct
{
    char        name[RAMFS_HASH_NAME_MAX];
    uint32_t    ino;
    uint32_t    file_index;
    uint8_t     state;
} ramfs_file_ref_t;

typedef struct
{
    ramfs_file_ref_t entries[RAMFS_HASH_SIZE];
    uint32_t           count;
} ramfs_hash_table_t;

/* djb2 哈希函数 */
static inline uint32_t ramfs_hash_djb2(const char *name)
{
    uint32_t hash = 5381U;
    uint32_t i = 0U;

    if (name == NULL)
    {
        return 0U;
    }

    while (name[i] != '\0')
    {
        hash = ((hash << 5U) + hash) + (uint32_t)(uint8_t)name[i];
        i++;
    }

    return hash;
}

/* 初始化哈希表 */
static int32_t ramfs_hash_init(ramfs_hash_table_t *table)
{
    uint32_t i;

    if (table == NULL)
    {
        return -1;
    }

    (void)memset(table, 0, sizeof(ramfs_hash_table_t));

    for (i = 0U; i < RAMFS_HASH_SIZE; i++)
    {
        table->entries[i].state = RAMFS_HASH_SLOT_EMPTY;
    }

    table->count = 0U;

    return 0;
}

/* 插入 */
static int32_t ramfs_hash_insert(ramfs_hash_table_t *table, const ramfs_file_ref_t *ref)
{
    uint32_t hash_val;
    uint32_t idx;
    uint32_t probe;

    if ((table == NULL) || (ref == NULL))
    {
        return -1;
    }

    if (ref->name[0] == '\0')
    {
        return -1;
    }

    if (table->count >= RAMFS_HASH_SIZE)
    {
        return -1;
    }

    hash_val = ramfs_hash_djb2(ref->name);
    idx = hash_val % RAMFS_HASH_SIZE;

    /* 线性探测 */
    for (probe = 0U; probe < RAMFS_HASH_SIZE; probe++)
    {
        uint32_t slot = (idx + probe) % RAMFS_HASH_SIZE;

        if (table->entries[slot].state == RAMFS_HASH_SLOT_EMPTY)
        {
            (void)strncpy(table->entries[slot].name, ref->name,
                          RAMFS_HASH_NAME_MAX - 1U);
            table->entries[slot].name[RAMFS_HASH_NAME_MAX - 1U] = '\0';
            table->entries[slot].ino = ref->ino;
            table->entries[slot].file_index = ref->file_index;
            table->entries[slot].state = RAMFS_HASH_SLOT_USED;
            table->count++;

            return 0;
        }

        if ((table->entries[slot].state == RAMFS_HASH_SLOT_USED) &&
            (strcmp(table->entries[slot].name, ref->name) == 0))
        {
            return -1; /* 重复 */
        }
    }

    return -1;
}

/* 查找 */
static ramfs_file_ref_t *ramfs_hash_lookup(ramfs_hash_table_t *table, const char *name)
{
    uint32_t hash_val;
    uint32_t idx;
    uint32_t probe;

    if ((table == NULL) || (name == NULL))
    {
        return NULL;
    }

    if (name[0] == '\0')
    {
        return NULL;
    }

    hash_val = ramfs_hash_djb2(name);
    idx = hash_val % RAMFS_HASH_SIZE;

    for (probe = 0U; probe < RAMFS_HASH_SIZE; probe++)
    {
        uint32_t slot = (idx + probe) % RAMFS_HASH_SIZE;

        if (table->entries[slot].state == RAMFS_HASH_SLOT_EMPTY)
        {
            return NULL;
        }

        if ((table->entries[slot].state == RAMFS_HASH_SLOT_USED) &&
            (strcmp(table->entries[slot].name, name) == 0))
        {
            return &table->entries[slot];
        }
    }

    return NULL;
}

/* 删除 */
static int32_t ramfs_hash_remove(ramfs_hash_table_t *table, const char *name)
{
    ramfs_file_ref_t *entry;

    if ((table == NULL) || (name == NULL))
    {
        return -1;
    }

    entry = ramfs_hash_lookup(table, name);
    if (entry == NULL)
    {
        return -1;
    }

    entry->state = RAMFS_HASH_SLOT_DELETED;
    table->count--;

    return 0;
}

/* 线性搜索查找（旧实现） */
static ramfs_file_ref_t *find_file_by_path_linear(const char *path)
{
    static ramfs_file_ref_t s_files[64];
    static uint32_t s_next_ino = 1U;
    uint32_t i;

    if (path == NULL)
    {
        return NULL;
    }

    /* 线性搜索 */
    for (i = 0U; i < 64U; i++)
    {
        if (s_files[i].state == RAMFS_HASH_SLOT_USED &&
            strcmp(s_files[i].name, path) == 0)
        {
            return &s_files[i];
        }
    }

    return NULL;
}

/**
 * @brief 性能测试
 */
int32_t main(void)
{
    ramfs_hash_table_t hash;
    ramfs_file_ref_t refs[64];
    ramfs_file_ref_t *ref;
    uint32_t i, j;
    struct timespec start, end;
    double linear_time, hash_time;

    printf("=== RAMFS 哈希索引性能测试 ===\n\n");

    /* 初始化 */
    ramfs_hash_init(&hash);
    printf("✓ 哈希表初始化成功\n");

    /* 插入测试 */
    printf("\n--- 插入测试 ---\n");
    for (i = 0U; i < 64U; i++)
    {
        (void)snprintf(refs[i].name, 64, "file_%d", i);
        refs[i].name[63] = '\0';
        refs[i].ino = i + 1U;
        refs[i].file_index = i;

        if (ramfs_hash_insert(&hash, &refs[i]) == 0)
        {
            printf("  ✓ 插入 %s (ino=%d)\n", refs[i].name, refs[i].ino);
        }
        else
        {
            printf("  ✗ 插入 %s 失败\n", refs[i].name);
        }
    }
    printf("✓ 插入测试完成，共 %d 个文件\n", (uint32_t)hash.count);

    /* 查找测试 */
    printf("\n--- 查找测试 ---\n");
    for (i = 0U; i < 64U; i++)
    {
        ref = ramfs_hash_lookup(&hash, refs[i].name);
        if (ref != NULL && ref->ino == refs[i].ino)
        {
            printf("  ✓ 查找 %s 成功\n", refs[i].name);
        }
        else
        {
            printf("  ✗ 查找 %s 失败\n", refs[i].name);
        }
    }

    /* 查找不存在的文件 */
    printf("\n--- 查找不存在的文件 ---\n");
    for (i = 0U; i < 10U; i++)
    {
        ref = ramfs_hash_lookup(&hash, "nonexistent_file");
        if (ref == NULL)
        {
            printf("  ✓ 查找 nonexistent_file 失败（正确）\n");
        }
        else
        {
            printf("  ✗ 查找 nonexistent_file 成功（错误）\n");
        }
    }

    /* 删除测试 */
    printf("\n--- 删除测试 ---\n");
    for (i = 0U; i < 16U; i++)
    {
        if (ramfs_hash_remove(&hash, refs[i].name) == 0)
        {
            printf("  ✓ 删除 %s 成功\n", refs[i].name);
        }
        else
        {
            printf("  ✗ 删除 %s 失败\n", refs[i].name);
        }
    }
    printf("✓ 删除测试完成，剩余 %d 个文件\n", (uint32_t)hash.count);

    /* 性能对比测试 */
    printf("\n=== 性能对比测试 ===\n");

    /* 线性搜索性能测试 */
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (j = 0U; j < 1000U; j++)
    {
        for (i = 0U; i < 64U; i++)
        {
            (void)find_file_by_path_linear(refs[i].name);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    linear_time = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);

    /* 哈希查找性能测试 */
    ramfs_hash_init(&hash);
    for (i = 0U; i < 64U; i++)
    {
        (void)ramfs_hash_insert(&hash, &refs[i]);
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (j = 0U; j < 1000U; j++)
    {
        for (i = 0U; i < 64U; i++)
        {
            (void)ramfs_hash_lookup(&hash, refs[i].name);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    hash_time = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);

    printf("\n性能测试结果:\n");
    printf("  线性搜索时间: %.3f μs\n", linear_time / 1e6);
    printf("  哈希查找时间: %.3f μs\n", hash_time / 1e6);
    printf("  性能提升:    %.1f%%\n", (1.0 - (double)hash_time / linear_time) * 100.0);

    printf("\n=== 测试完成 ===\n");

    return 0;
}
