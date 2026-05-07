/**
 * @file    ramfs_hash.c
 * @brief   RAMFS 文件名哈希索引实现
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details RAMFS 文件名哈希索引模块
 *          - djb2 哈希算法
 *          - 开放寻址冲突解决（线性探测）
 *          - O(1) 平均查找时间
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "ramfs_hash.h"
#include <string.h>

/* ========================================================================
 * 哈希函数
 * ======================================================================== */

/**
 * @brief djb2 哈希算法
 *
 * @details 经典 djb2 算法：hash = hash * 33 + c
 *          初始种子 5381，分布均匀
 *
 * @param name 文件名
 *
 * @return 哈希值
 */
uint32_t ramfs_hash_djb2(const char *name)
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

/* ========================================================================
 * 哈希表操作
 * ======================================================================== */

/**
 * @brief 初始化哈希表
 *
 * @details 将所有槽位标记为空，计数归零
 */
int32_t ramfs_hash_init(ramfs_hash_table_t *table)
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

/**
 * @brief 获取哈希表条目数
 */
uint32_t ramfs_hash_count(const ramfs_hash_table_t *table)
{
    if (table == NULL)
    {
        return 0U;
    }

    return table->count;
}

/**
 * @brief 插入文件引用
 *
 * @details 使用开放寻址（线性探测）解决冲突
 *          不允许重复文件名
 */
int32_t ramfs_hash_insert(ramfs_hash_table_t *table, const ramfs_file_ref_t *ref)
{
    uint32_t hash_val;
    uint32_t idx;
    uint32_t probe;
    uint32_t first_deleted;

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
    first_deleted = RAMFS_HASH_SIZE; /* 无效索引标记 */

    /* 线性探测 */
    for (probe = 0U; probe < RAMFS_HASH_SIZE; probe++)
    {
        uint32_t slot = (idx + probe) % RAMFS_HASH_SIZE;

        if (table->entries[slot].state == RAMFS_HASH_SLOT_EMPTY)
        {
            /* 找到空槽或已删除槽 */
            uint32_t target = (first_deleted < RAMFS_HASH_SIZE) ?
                              first_deleted : slot;

            (void)strncpy(table->entries[target].name, ref->name,
                          RAMFS_HASH_NAME_MAX - 1U);
            table->entries[target].name[RAMFS_HASH_NAME_MAX - 1U] = '\0';
            table->entries[target].ino = ref->ino;
            table->entries[target].file_index = ref->file_index;
            table->entries[target].state = RAMFS_HASH_SLOT_USED;
            table->count++;

            return 0;
        }

        if (table->entries[slot].state == RAMFS_HASH_SLOT_USED)
        {
            /* 检查重复 */
            if (strcmp(table->entries[slot].name, ref->name) == 0)
            {
                return -1; /* 重复 */
            }
        }

        if ((table->entries[slot].state == RAMFS_HASH_SLOT_DELETED) &&
            (first_deleted == RAMFS_HASH_SIZE))
        {
            first_deleted = slot;
        }
    }

    /* 探测完毕未找到空槽，使用已删除槽 */
    if (first_deleted < RAMFS_HASH_SIZE)
    {
        (void)strncpy(table->entries[first_deleted].name, ref->name,
                      RAMFS_HASH_NAME_MAX - 1U);
        table->entries[first_deleted].name[RAMFS_HASH_NAME_MAX - 1U] = '\0';
        table->entries[first_deleted].ino = ref->ino;
        table->entries[first_deleted].file_index = ref->file_index;
        table->entries[first_deleted].state = RAMFS_HASH_SLOT_USED;
        table->count++;

        return 0;
    }

    return -1;
}

/**
 * @brief 查找文件引用
 *
 * @details 线性探测直到找到匹配或空槽
 */
ramfs_file_ref_t *ramfs_hash_lookup(ramfs_hash_table_t *table, const char *name)
{
    uint32_t hash_val;
    uint32_t idx;
    uint32_t probe;
    uint32_t slot;

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
        slot = (idx + probe) % RAMFS_HASH_SIZE;

        if (table->entries[slot].state == RAMFS_HASH_SLOT_EMPTY)
        {
            /* 空槽表示查找结束 */
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

/**
 * @brief 删除文件引用
 *
 * @details 标记为 DELETED 而非 EMPTY，以保持探测链完整
 */
int32_t ramfs_hash_remove(ramfs_hash_table_t *table, const char *name)
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

/**
 * @brief 更新文件引用
 *
 * @details 查找已有条目并更新 ino 和 file_index
 */
int32_t ramfs_hash_update(ramfs_hash_table_t *table, const ramfs_file_ref_t *ref)
{
    ramfs_file_ref_t *entry;

    if ((table == NULL) || (ref == NULL))
    {
        return -1;
    }

    entry = ramfs_hash_lookup(table, ref->name);
    if (entry == NULL)
    {
        return -1;
    }

    entry->ino = ref->ino;
    entry->file_index = ref->file_index;

    return 0;
}
