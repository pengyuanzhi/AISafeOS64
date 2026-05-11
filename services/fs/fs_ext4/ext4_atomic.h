/**
 * @file    ext4_atomic.h
 * @brief   EXT4 原子操作接口
 * @author  AISafe64 Team
 * @date    2026-05-11
 * @version 1.0
 *
 * @details EXT4 原子操作接口：
 *          - 文件级原子操作（创建、删除、重命名）
 *          - 目录级原子操作（创建、删除、链接）
 *          - 原子事务管理
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef EXT4_ATOMIC_H
#define EXT4_ATOMIC_H

#include <stdint.h>
#include <stdbool.h>
#include "services/fs/fs_ext4/ext4_journal.h"

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 原子事务最大操作数 */
#define EXT4_ATOMIC_MAX_OPS    16U

/** @brief 原子事务超时（毫秒） */
#define EXT4_ATOMIC_TIMEOUT    5000U

/* ========================================================================
 * 枚举类型
 * ======================================================================== */

/**
 * @brief 原子操作类型
 */
typedef enum
{
    EXT4_ATOMIC_OP_INODE_ALLOC = 0U,    /**< @brief 分配 Inode */
    EXT4_ATOMIC_OP_INODE_FREE,         /**< @brief 释放 Inode */
    EXT4_ATOMIC_OP_DIR_ADD,            /**< @brief 添加目录项 */
    EXT4_ATOMIC_OP_DIR_REMOVE,         /**< @brief 删除目录项 */
    EXT4_ATOMIC_OP_INODE_UPDATE,       /**< @brief 更新 Inode */
    EXT4_ATOMIC_OP_BLOCK_ALLOC,        /**< @brief 分配块 */
    EXT4_ATOMIC_OP_BLOCK_FREE,         /**< @brief 释放块 */
    EXT4_ATOMIC_OP_COUNT               /**< @brief 操作类型数量 */
} ext4_atomic_op_type_t;

/* ========================================================================
 * 数据结构
 * ======================================================================== */

/**
 * @brief 原子操作记录
 */
typedef struct
{
    ext4_atomic_op_type_t type;    /**< @brief 操作类型 */
    uint32_t inode;                /**< @brief Inode 编号 */
    uint32_t block;                /**< @brief 块编号 */
    uint32_t parent_ino;           /**< @brief 父目录 Inode */
    uint32_t size;                 /**< @brief 大小 */
    uint32_t flags;                /**< @brief 标志 */
    char name[256];                /**< @brief 名称 */
    uint8_t data[256];             /**< @brief 数据 */
} ext4_atomic_op_t;

/**
 * @brief 原子事务
 */
typedef struct
{
    uint32_t sequence;            /**< @brief 事务序列号 */
    uint32_t op_count;            /**< @brief 操作计数 */
    ext4_atomic_op_t ops[EXT4_ATOMIC_MAX_OPS]; /**< @brief 操作数组 */
    bool committed;               /**< @brief 已提交标志 */
} ext4_atomic_txn_t;

/* ========================================================================
 * 接口函数声明
 * ======================================================================== */

/**
 * @brief 初始化原子操作模块
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_atomic_init(void);

/**
 * @brief 销毁原子操作模块
 */
void ext4_atomic_destroy(void);

/**
 * @brief 原子创建文件
 *
 * @param parent_ino  父目录 Inode
 * @param name        文件名
 * @param mode        权限模式
 * @param uid         用户 ID
 * @param gid         组 ID
 *
 * @return Inode 编号（>=0 成功），<0 失败
 */
int32_t ext4_atomic_create_file(uint32_t parent_ino, const char *name,
                                 uint32_t mode, uint32_t uid, uint32_t gid);

/**
 * @brief 原子删除文件
 *
 * @param parent_ino  父目录 Inode
 * @param name        文件名
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_atomic_delete_file(uint32_t parent_ino, const char *name);

/**
 * @brief 原子重命名文件
 *
 * @param old_parent_ino  旧父目录 Inode
 * @param old_name        旧文件名
 * @param new_parent_ino  新父目录 Inode
 * @param new_name        新文件名
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_atomic_rename_file(uint32_t old_parent_ino, const char *old_name,
                                 uint32_t new_parent_ino, const char *new_name);

/**
 * @brief 原子创建目录
 *
 * @param parent_ino  父目录 Inode
 * @param name        目录名
 * @param mode        权限模式
 * @param uid         用户 ID
 * @param gid         组 ID
 *
 * @return Inode 编号（>=0 成功），<0 失败
 */
int32_t ext4_atomic_create_dir(uint32_t parent_ino, const char *name,
                                uint32_t mode, uint32_t uid, uint32_t gid);

/**
 * @brief 原子删除目录
 *
 * @param parent_ino  父目录 Inode
 * @param name        目录名
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_atomic_delete_dir(uint32_t parent_ino, const char *name);

/**
 * @brief 原子创建硬链接
 *
 * @param src_parent_ino  源父目录 Inode
 * @param src_name        源文件名
 * @param dst_parent_ino  目标父目录 Inode
 * @param dst_name        目标文件名
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_atomic_link(uint32_t src_parent_ino, const char *src_name,
                          uint32_t dst_parent_ino, const char *dst_name);

/**
 * @brief 开始原子事务
 *
 * @param txn  事务结构体
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_atomic_txn_begin(ext4_atomic_txn_t *txn);

/**
 * @brief 提交原子事务
 *
 * @param txn  事务结构体
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_atomic_txn_commit(ext4_atomic_txn_t *txn);

/**
 * @brief 回滚原子事务
 *
 * @param txn  事务结构体
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_atomic_txn_rollback(ext4_atomic_txn_t *txn);

#endif /* EXT4_ATOMIC_H */
