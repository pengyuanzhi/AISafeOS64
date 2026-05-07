/**
 * @file    ramfs_hash.h
 * @brief   RAMFS 文件名哈希索引接口
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details RAMFS 文件名哈希索引模块
 *          - 使用 djb2 哈希算法
 *          - 开放寻址冲突解决
 *          - O(1) 平均查找时间
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef RAMFS_HASH_H
#define RAMFS_HASH_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 哈希表大小（2的幂，开放寻址效率 ~80% 装载因子） */
#define RAMFS_HASH_SIZE        128U

/** @brief 文件名最大长度 */
#define RAMFS_HASH_NAME_MAX    64U

/** @brief 空槽位标记 */
#define RAMFS_HASH_SLOT_EMPTY  0U

/** @brief 已使用槽位标记 */
#define RAMFS_HASH_SLOT_USED   1U

/** @brief 已删除槽位标记（用于开放寻址探测跳过） */
#define RAMFS_HASH_SLOT_DELETED 2U

/* ========================================================================
 * 数据结构
 * ======================================================================== */

/**
 * @brief 文件引用条目（哈希表中的存储单元）
 *
 * @details 存储文件名与文件表索引的映射关系
 */
typedef struct ramfs_file_ref
{
    char        name[RAMFS_HASH_NAME_MAX];  /**< @brief 文件名 */
    uint32_t    ino;                         /**< @brief inode 编号 */
    uint32_t    file_index;                  /**< @brief 文件表索引 */
    uint8_t     state;                       /**< @brief 槽位状态 */
} ramfs_file_ref_t;

/**
 * @brief 哈希表
 *
 * @details 每个挂载点独立维护一个哈希表
 */
typedef struct ramfs_hash_table
{
    ramfs_file_ref_t   entries[RAMFS_HASH_SIZE];  /**< @brief 哈希条目数组 */
    uint32_t           count;                      /**< @brief 当前条目数 */
} ramfs_hash_table_t;

/* ========================================================================
 * 公共接口
 * ======================================================================== */

/**
 * @brief 初始化哈希表
 *
 * @param table 哈希表指针（不能为 NULL）
 *
 * @return 0 成功，<0 失败
 */
int32_t ramfs_hash_init(ramfs_hash_table_t *table);

/**
 * @brief 计算文件名哈希值（djb2 算法）
 *
 * @param name 文件名（不能为 NULL）
 *
 * @return 哈希值
 */
uint32_t ramfs_hash_djb2(const char *name);

/**
 * @brief 插入文件引用到哈希表
 *
 * @param table 哈希表指针（不能为 NULL）
 * @param ref   文件引用（不能为 NULL，name 不能为 NULL）
 *
 * @return 0 成功，<0 失败（表满或重复）
 */
int32_t ramfs_hash_insert(ramfs_hash_table_t *table, const ramfs_file_ref_t *ref);

/**
 * @brief 在哈希表中查找文件
 *
 * @param table 哈希表指针（不能为 NULL）
 * @param name  文件名（不能为 NULL）
 *
 * @return 找到返回条目指针，未找到返回 NULL
 */
ramfs_file_ref_t *ramfs_hash_lookup(ramfs_hash_table_t *table, const char *name);

/**
 * @brief 从哈希表中删除文件
 *
 * @param table 哈希表指针（不能为 NULL）
 * @param name  文件名（不能为 NULL）
 *
 * @return 0 成功，<0 失败
 */
int32_t ramfs_hash_remove(ramfs_hash_table_t *table, const char *name);

/**
 * @brief 更新哈希表中的条目
 *
 * @param table 哈希表指针（不能为 NULL）
 * @param ref   文件引用（不能为 NULL，必须已存在）
 *
 * @return 0 成功，<0 失败
 */
int32_t ramfs_hash_update(ramfs_hash_table_t *table, const ramfs_file_ref_t *ref);

/**
 * @brief 获取哈希表中的条目数
 *
 * @param table 哈希表指针（不能为 NULL）
 *
 * @return 条目数
 */
uint32_t ramfs_hash_count(const ramfs_hash_table_t *table);

#endif /* RAMFS_HASH_H */
