/**
 * @file    ext4_atomic.c
 * @brief   EXT4 原子操作实现
 * @author  AISafe64 Team
 * @date    2026-05-11
 * @version 1.0
 *
 * @details EXT4 原子操作实现：
 *          - 文件级原子操作（创建、删除、重命名）
 *          - 目录级原子操作（创建、删除、链接）
 *          - 原子事务管理
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "ext4_atomic.h"
#include "ext4_journal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ========================================================================
 * 内部变量
 * ======================================================================== */

/** @brief 原子操作模块初始化标志 */
static bool s_initialized = false;

/** @brief 事务序列号 */
static uint32_t s_txn_sequence = 1U;

/** @brief 模拟 Inode 分配器 */
static uint32_t s_next_inode = 10U;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 验证文件名
 *
 * @param name  文件名
 * @return true 有效，false 无效
 */
static bool validate_name(const char *name)
{
    if (name == NULL)
    {
        return false;
    }

    uint32_t len = (uint32_t)strlen(name);

    /* 检查长度 */
    if (len == 0U || len > 255U)
    {
        return false;
    }

    /* 检查特殊字符 */
    for (uint32_t i = 0U; i < len; i++)
    {
        char ch = name[i];
        if (ch == '/' || ch == '\0')
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief 模拟分配 Inode
 *
 * @return Inode 编号
 */
static uint32_t alloc_inode(void)
{
    return s_next_inode++;
}

/**
 * @brief 模拟释放 Inode
 *
 * @param inode  Inode 编号
 */
static void free_inode(uint32_t inode)
{
    /* 简化实现：仅标记为已释放 */
    (void)inode;
}

/**
 * @brief 模拟添加目录项
 *
 * @param parent_ino  父目录 Inode
 * @param name        文件名
 * @param inode       Inode 编号
 * @return 0 成功，<0 失败
 */
static int32_t dir_add_entry(uint32_t parent_ino, const char *name, uint32_t inode)
{
    (void)parent_ino;
    (void)name;
    (void)inode;

    /* 简化实现：总是成功 */
    return 0;
}

/**
 * @brief 模拟删除目录项
 *
 * @param parent_ino  父目录 Inode
 * @param name        文件名
 * @return 0 成功，<0 失败
 */
static int32_t dir_remove_entry(uint32_t parent_ino, const char *name)
{
    (void)parent_ino;
    (void)name;

    /* 简化实现：总是成功 */
    return 0;
}

/**
 * @brief 写入元数据到 Journal
 *
 * @param txn       事务结构体
 * @param op        操作记录
 * @return 0 成功，<0 失败
 */
static int32_t write_metadata_to_journal(ext4_atomic_txn_t *txn,
                                          const ext4_atomic_op_t *op)
{
    ext4_journal_metadata_t metadata;

    /* 填充 Journal 元数据记录 */
    (void)memset(&metadata, 0, sizeof(ext4_journal_metadata_t));

    /* 映射原子操作类型到 Journal 元数据类型 */
    switch (op->type)
    {
        case EXT4_ATOMIC_OP_INODE_ALLOC:
        case EXT4_ATOMIC_OP_INODE_FREE:
        case EXT4_ATOMIC_OP_INODE_UPDATE:
            metadata.type = EXT4_JMETADATA_INODE;
            break;

        case EXT4_ATOMIC_OP_BLOCK_ALLOC:
        case EXT4_ATOMIC_OP_BLOCK_FREE:
            metadata.type = EXT4_JMETADATA_BLOCK;
            break;

        case EXT4_ATOMIC_OP_DIR_ADD:
        case EXT4_ATOMIC_OP_DIR_REMOVE:
            metadata.type = EXT4_JMETADATA_DIR;
            break;

        default:
            return -22; /* EINVAL */
    }

    metadata.sequence = txn->sequence;
    metadata.block = op->block;
    metadata.inode = op->inode;
    metadata.size = op->size;
    metadata.flags = op->flags;
    metadata.timestamp = 0U;

    /* 写入 Journal */
    int32_t ret = ext4_journal_write_metadata(&metadata);
    if (ret != 0)
    {
        return ret;
    }

    return 0;
}

/**
 * @brief 写入 SYNC 记录
 *
 * @param txn  事务结构体
 * @return 0 成功，<0 失败
 */
static int32_t write_sync_record(ext4_atomic_txn_t *txn)
{
    ext4_journal_metadata_t metadata;

    /* 填充 SYNC 记录 */
    (void)memset(&metadata, 0, sizeof(ext4_journal_metadata_t));

    metadata.type = EXT4_JMETADATA_SYNC;
    metadata.sequence = txn->sequence;
    metadata.inode = 0U;
    metadata.block = 0U;
    metadata.size = 0U;
    metadata.flags = 0U;
    metadata.timestamp = 0U;

    /* 写入 Journal */
    int32_t ret = ext4_journal_write_metadata(&metadata);
    if (ret != 0)
    {
        return ret;
    }

    return 0;
}

/* ========================================================================
 * 接口函数实现
 * ======================================================================== */

/**
 * @brief 初始化原子操作模块
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_atomic_init(void)
{
    if (s_initialized)
    {
        return 0;
    }

    s_txn_sequence = 1U;
    s_next_inode = 10U;
    s_initialized = true;

    return 0;
}

/**
 * @brief 销毁原子操作模块
 */
void ext4_atomic_destroy(void)
{
    s_initialized = false;
    s_txn_sequence = 1U;
}

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
                                 uint32_t mode, uint32_t uid, uint32_t gid)
{
    ext4_atomic_txn_t txn;
    ext4_atomic_op_t op;
    uint32_t inode;
    int32_t ret;

    /* 参数验证 */
    if (parent_ino == 0U)
    {
        return -22; /* EINVAL */
    }

    if (!validate_name(name))
    {
        return -22; /* EINVAL */
    }

    /* 开始事务 */
    ret = ext4_atomic_txn_begin(&txn);
    if (ret != 0)
    {
        return ret;
    }

    /* 操作 1: 分配 Inode */
    (void)memset(&op, 0, sizeof(ext4_atomic_op_t));
    op.type = EXT4_ATOMIC_OP_INODE_ALLOC;
    op.inode = 0U;
    op.size = mode;

    ret = write_metadata_to_journal(&txn, &op);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        return ret;
    }

    inode = alloc_inode();
    txn.op_count++;

    /* 操作 2: 添加目录项 */
    (void)memset(&op, 0, sizeof(ext4_atomic_op_t));
    op.type = EXT4_ATOMIC_OP_DIR_ADD;
    op.parent_ino = parent_ino;
    op.inode = inode;
    (void)strncpy(op.name, name, sizeof(op.name) - 1U);
    op.name[sizeof(op.name) - 1U] = '\0';

    ret = write_metadata_to_journal(&txn, &op);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        free_inode(inode);
        return ret;
    }

    ret = dir_add_entry(parent_ino, name, inode);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        free_inode(inode);
        return ret;
    }

    txn.op_count++;

    /* 写入 SYNC 记录 */
    ret = write_sync_record(&txn);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        free_inode(inode);
        (void)dir_remove_entry(parent_ino, name);
        return ret;
    }

    /* 提交事务 */
    ret = ext4_atomic_txn_commit(&txn);
    if (ret != 0)
    {
        return ret;
    }

    (void)uid;
    (void)gid;

    return (int32_t)inode;
}

/**
 * @brief 原子删除文件
 *
 * @param parent_ino  父目录 Inode
 * @param name        文件名
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_atomic_delete_file(uint32_t parent_ino, const char *name)
{
    ext4_atomic_txn_t txn;
    ext4_atomic_op_t op;
    int32_t ret;

    /* 参数验证 */
    if (parent_ino == 0U)
    {
        return -22; /* EINVAL */
    }

    if (!validate_name(name))
    {
        return -22; /* EINVAL */
    }

    /* 开始事务 */
    ret = ext4_atomic_txn_begin(&txn);
    if (ret != 0)
    {
        return ret;
    }

    /* 操作 1: 删除目录项 */
    (void)memset(&op, 0, sizeof(ext4_atomic_op_t));
    op.type = EXT4_ATOMIC_OP_DIR_REMOVE;
    op.parent_ino = parent_ino;
    (void)strncpy(op.name, name, sizeof(op.name) - 1U);
    op.name[sizeof(op.name) - 1U] = '\0';

    ret = write_metadata_to_journal(&txn, &op);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        return ret;
    }

    ret = dir_remove_entry(parent_ino, name);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        return ret;
    }

    txn.op_count++;

    /* 操作 2: 释放 Inode */
    (void)memset(&op, 0, sizeof(ext4_atomic_op_t));
    op.type = EXT4_ATOMIC_OP_INODE_FREE;
    op.inode = 0U; /* 简化：实际应从目录项获取 */

    ret = write_metadata_to_journal(&txn, &op);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        return ret;
    }

    free_inode(0U);
    txn.op_count++;

    /* 写入 SYNC 记录 */
    ret = write_sync_record(&txn);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        return ret;
    }

    /* 提交事务 */
    ret = ext4_atomic_txn_commit(&txn);
    if (ret != 0)
    {
        return ret;
    }

    return 0;
}

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
                                 uint32_t new_parent_ino, const char *new_name)
{
    ext4_atomic_txn_t txn;
    ext4_atomic_op_t op;
    int32_t ret;

    /* 参数验证 */
    if (old_parent_ino == 0U)
    {
        return -22; /* EINVAL */
    }

    if (!validate_name(old_name))
    {
        return -22; /* EINVAL */
    }

    if (new_parent_ino == 0U)
    {
        return -22; /* EINVAL */
    }

    if (!validate_name(new_name))
    {
        return -22; /* EINVAL */
    }

    /* 开始事务 */
    ret = ext4_atomic_txn_begin(&txn);
    if (ret != 0)
    {
        return ret;
    }

    /* 操作 1: 在新目录添加目录项 */
    (void)memset(&op, 0, sizeof(ext4_atomic_op_t));
    op.type = EXT4_ATOMIC_OP_DIR_ADD;
    op.parent_ino = new_parent_ino;
    op.inode = 0U; /* 简化：实际应从旧目录项获取 */
    (void)strncpy(op.name, new_name, sizeof(op.name) - 1U);
    op.name[sizeof(op.name) - 1U] = '\0';

    ret = write_metadata_to_journal(&txn, &op);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        return ret;
    }

    ret = dir_add_entry(new_parent_ino, new_name, 0U);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        return ret;
    }

    txn.op_count++;

    /* 操作 2: 从旧目录删除目录项 */
    (void)memset(&op, 0, sizeof(ext4_atomic_op_t));
    op.type = EXT4_ATOMIC_OP_DIR_REMOVE;
    op.parent_ino = old_parent_ino;
    (void)strncpy(op.name, old_name, sizeof(op.name) - 1U);
    op.name[sizeof(op.name) - 1U] = '\0';

    ret = write_metadata_to_journal(&txn, &op);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        (void)dir_remove_entry(new_parent_ino, new_name);
        return ret;
    }

    ret = dir_remove_entry(old_parent_ino, old_name);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        (void)dir_remove_entry(new_parent_ino, new_name);
        return ret;
    }

    txn.op_count++;

    /* 写入 SYNC 记录 */
    ret = write_sync_record(&txn);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        (void)dir_add_entry(old_parent_ino, old_name, 0U);
        (void)dir_remove_entry(new_parent_ino, new_name);
        return ret;
    }

    /* 提交事务 */
    ret = ext4_atomic_txn_commit(&txn);
    if (ret != 0)
    {
        return ret;
    }

    return 0;
}

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
                                uint32_t mode, uint32_t uid, uint32_t gid)
{
    ext4_atomic_txn_t txn;
    ext4_atomic_op_t op;
    uint32_t inode;
    int32_t ret;

    /* 参数验证 */
    if (parent_ino == 0U)
    {
        return -22; /* EINVAL */
    }

    if (!validate_name(name))
    {
        return -22; /* EINVAL */
    }

    /* 开始事务 */
    ret = ext4_atomic_txn_begin(&txn);
    if (ret != 0)
    {
        return ret;
    }

    /* 操作 1: 分配 Inode */
    (void)memset(&op, 0, sizeof(ext4_atomic_op_t));
    op.type = EXT4_ATOMIC_OP_INODE_ALLOC;
    op.inode = 0U;
    op.size = mode;

    ret = write_metadata_to_journal(&txn, &op);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        return ret;
    }

    inode = alloc_inode();
    txn.op_count++;

    /* 操作 2: 添加目录项 */
    (void)memset(&op, 0, sizeof(ext4_atomic_op_t));
    op.type = EXT4_ATOMIC_OP_DIR_ADD;
    op.parent_ino = parent_ino;
    op.inode = inode;
    (void)strncpy(op.name, name, sizeof(op.name) - 1U);
    op.name[sizeof(op.name) - 1U] = '\0';

    ret = write_metadata_to_journal(&txn, &op);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        free_inode(inode);
        return ret;
    }

    ret = dir_add_entry(parent_ino, name, inode);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        free_inode(inode);
        return ret;
    }

    txn.op_count++;

    /* 写入 SYNC 记录 */
    ret = write_sync_record(&txn);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        free_inode(inode);
        (void)dir_remove_entry(parent_ino, name);
        return ret;
    }

    /* 提交事务 */
    ret = ext4_atomic_txn_commit(&txn);
    if (ret != 0)
    {
        return ret;
    }

    (void)uid;
    (void)gid;

    return (int32_t)inode;
}

/**
 * @brief 原子删除目录
 *
 * @param parent_ino  父目录 Inode
 * @param name        目录名
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_atomic_delete_dir(uint32_t parent_ino, const char *name)
{
    ext4_atomic_txn_t txn;
    ext4_atomic_op_t op;
    int32_t ret;

    /* 参数验证 */
    if (parent_ino == 0U)
    {
        return -22; /* EINVAL */
    }

    if (!validate_name(name))
    {
        return -22; /* EINVAL */
    }

    /* 开始事务 */
    ret = ext4_atomic_txn_begin(&txn);
    if (ret != 0)
    {
        return ret;
    }

    /* 操作 1: 删除目录项 */
    (void)memset(&op, 0, sizeof(ext4_atomic_op_t));
    op.type = EXT4_ATOMIC_OP_DIR_REMOVE;
    op.parent_ino = parent_ino;
    (void)strncpy(op.name, name, sizeof(op.name) - 1U);
    op.name[sizeof(op.name) - 1U] = '\0';

    ret = write_metadata_to_journal(&txn, &op);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        return ret;
    }

    ret = dir_remove_entry(parent_ino, name);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        return ret;
    }

    txn.op_count++;

    /* 操作 2: 释放 Inode */
    (void)memset(&op, 0, sizeof(ext4_atomic_op_t));
    op.type = EXT4_ATOMIC_OP_INODE_FREE;
    op.inode = 0U; /* 简化：实际应从目录项获取 */

    ret = write_metadata_to_journal(&txn, &op);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        return ret;
    }

    free_inode(0U);
    txn.op_count++;

    /* 写入 SYNC 记录 */
    ret = write_sync_record(&txn);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        return ret;
    }

    /* 提交事务 */
    ret = ext4_atomic_txn_commit(&txn);
    if (ret != 0)
    {
        return ret;
    }

    return 0;
}

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
                          uint32_t dst_parent_ino, const char *dst_name)
{
    ext4_atomic_txn_t txn;
    ext4_atomic_op_t op;
    int32_t ret;

    /* 参数验证 */
    if (src_parent_ino == 0U)
    {
        return -22; /* EINVAL */
    }

    if (!validate_name(src_name))
    {
        return -22; /* EINVAL */
    }

    if (dst_parent_ino == 0U)
    {
        return -22; /* EINVAL */
    }

    if (!validate_name(dst_name))
    {
        return -22; /* EINVAL */
    }

    /* 开始事务 */
    ret = ext4_atomic_txn_begin(&txn);
    if (ret != 0)
    {
        return ret;
    }

    /* 操作 1: 更新 Inode 链接计数 */
    (void)memset(&op, 0, sizeof(ext4_atomic_op_t));
    op.type = EXT4_ATOMIC_OP_INODE_UPDATE;
    op.inode = 0U; /* 简化：实际应从源目录项获取 */

    ret = write_metadata_to_journal(&txn, &op);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        return ret;
    }

    txn.op_count++;

    /* 操作 2: 在目标目录添加目录项 */
    (void)memset(&op, 0, sizeof(ext4_atomic_op_t));
    op.type = EXT4_ATOMIC_OP_DIR_ADD;
    op.parent_ino = dst_parent_ino;
    op.inode = 0U; /* 简化：实际应从源目录项获取 */
    (void)strncpy(op.name, dst_name, sizeof(op.name) - 1U);
    op.name[sizeof(op.name) - 1U] = '\0';

    ret = write_metadata_to_journal(&txn, &op);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        return ret;
    }

    ret = dir_add_entry(dst_parent_ino, dst_name, 0U);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        return ret;
    }

    txn.op_count++;

    /* 写入 SYNC 记录 */
    ret = write_sync_record(&txn);
    if (ret != 0)
    {
        (void)ext4_atomic_txn_rollback(&txn);
        (void)dir_remove_entry(dst_parent_ino, dst_name);
        return ret;
    }

    /* 提交事务 */
    ret = ext4_atomic_txn_commit(&txn);
    if (ret != 0)
    {
        return ret;
    }

    return 0;
}

/**
 * @brief 开始原子事务
 *
 * @param txn  事务结构体
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_atomic_txn_begin(ext4_atomic_txn_t *txn)
{
    if (txn == NULL)
    {
        return -22; /* EINVAL */
    }

    if (!s_initialized)
    {
        return -19; /* ENODEV */
    }

    /* 初始化事务 */
    (void)memset(txn, 0, sizeof(ext4_atomic_txn_t));
    txn->sequence = s_txn_sequence++;
    txn->op_count = 0U;
    txn->committed = false;

    return 0;
}

/**
 * @brief 提交原子事务
 *
 * @param txn  事务结构体
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_atomic_txn_commit(ext4_atomic_txn_t *txn)
{
    int32_t ret;

    if (txn == NULL)
    {
        return -22; /* EINVAL */
    }

    /* 提交 Journal */
    ret = ext4_journal_commit();
    if (ret != 0)
    {
        return ret;
    }

    txn->committed = true;

    return 0;
}

/**
 * @brief 回滚原子事务
 *
 * @param txn  事务结构体
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_atomic_txn_rollback(ext4_atomic_txn_t *txn)
{
    if (txn == NULL)
    {
        return -22; /* EINVAL */
    }

    /* 简化实现：仅标记为已回滚 */
    /* 实际实现应该撤销所有操作 */
    (void)txn;

    return 0;
}
