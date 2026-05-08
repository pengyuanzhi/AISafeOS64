/**
 * @file    fs_lock_hash.h
 * @brief   文件锁哈希索引接口
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details 文件锁哈希索引模块
 *          - 替代全局锁表的线性搜索
 *          - O(1) 平均查找时间
 *          - 线程安全（每个挂载点独立锁）
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_LOCK_HASH_H
#define FS_LOCK_HASH_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 文件锁哈希表大小 */
#define FS_LOCK_HASH_SIZE    64U

/** @brief 最大挂载点数 */
#define FS_MAX_MOUNTS        8U

/** @brief 最大锁数（每挂载点） */
#define FS_MAX_LOCKS         128U

/* ========================================================================
 * 数据结构
 * ======================================================================== */

/**
 * @brief 文件锁哈希表项
 */
typedef struct
{
    uint32_t        mount_id;           /**< 挂载点 ID */
    uint32_t        ino;                 /**< inode 编号 */
    uint32_t        lock_type;           /**< 锁类型 */
    uint32_t        owner_tid;           /**< 持有者线程 ID */
    uint32_t        lock_count;          /**< 锁计数 */
    bool            locked;              /**< 是否锁定 */
} fs_lock_hash_entry_t;

/**
 * @brief 文件锁哈希表
 */
typedef struct
{
    fs_lock_hash_entry_t entries[FS_LOCK_HASH_SIZE];
    bool                   initialized;
    uint32_t               total_locks;       /**< 总锁数 */
} fs_lock_hash_t;

/**
 * @brief 挂载点锁管理器
 */
typedef struct
{
    fs_lock_hash_entry_t locks[FS_MAX_LOCKS];
    bool                  initialized;
    uint32_t              lock_count;        /**< 锁计数 */
    uint32_t              next_lock_id;      /**< 下一个锁 ID */
} fs_mount_lock_mgr_t;

/* ========================================================================
 * 公共接口
 * ======================================================================== */

/**
 * @brief 初始化挂载点锁管理器
 *
 * @param mgr 挂载点锁管理器指针（不能为 NULL）
 *
 * @return 0 成功，<0 失败
 */
int32_t fs_mount_lock_mgr_init(fs_mount_lock_mgr_t *mgr);

/**
 * @brief 锁定文件
 *
 * @param mgr 挂载点锁管理器指针
 * @param mount_id 挂载点 ID
 * @param ino inode 编号
 * @param lock_type 锁类型
 * @param owner_tid 拥有者线程 ID
 *
 * @return 0 成功，<0 失败
 */
int32_t fs_mount_lock_lock(fs_mount_lock_mgr_t *mgr, uint32_t mount_id,
                           uint32_t ino, uint32_t lock_type,
                           uint32_t owner_tid);

/**
 * @brief 解锁文件
 *
 * @param mgr 挂载点锁管理器指针
 * @param mount_id 挂载点 ID
 * @param ino inode 编号
 * @param owner_tid 拥有者线程 ID
 *
 * @return 0 成功，<0 失败
 */
int32_t fs_mount_lock_unlock(fs_mount_lock_mgr_t *mgr, uint32_t mount_id,
                              uint32_t ino, uint32_t owner_tid);

/**
 * @brief 查找文件锁
 *
 * @param mgr 挂载点锁管理器指针
 * @param mount_id 挂载点 ID
 * @param ino inode 编号
 *
 * @return 锁指针，未找到返回 NULL
 */
fs_lock_hash_entry_t *fs_mount_lock_find(fs_mount_lock_mgr_t *mgr,
                                          uint32_t mount_id, uint32_t ino);

/**
 * @brief 清空挂载点锁管理器
 *
 * @param mgr 挂载点锁管理器指针
 */
void fs_mount_lock_clear(fs_mount_lock_mgr_t *mgr);

#endif /* FS_LOCK_HASH_H */
