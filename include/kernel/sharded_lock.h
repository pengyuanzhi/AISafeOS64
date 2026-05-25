/**
 * @file    sharded_lock.h
 * @brief   分片锁（Sharded Lock）数据结构
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 本文件定义了分片锁数据结构：
 *          - 分片锁结构
 *          - 分片锁初始化/销毁
 *          - 分片锁获取/释放
 *          - 分片选择策略
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.5 - SMP 优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_SHARDED_LOCK_H
#define KERNEL_SHARDED_LOCK_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/spinlock.h>
#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * 分片锁配置常量
 * ======================================================================== */

/** @brief 默认分片数量（必须是 2 的幂次方） */
#define SHARD_LOCK_DEFAULT_SHARDS  4U

/** @brief 最大分片数量 */
#define SHARD_LOCK_MAX_SHARDS        32U

/** @brief 分片锁位图掩码 */
#define SHARD_LOCK_SHARD_MASK        (SHARD_LOCK_MAX_SHARDS - 1U)

/* ========================================================================
 * 分片锁结构
 * ======================================================================== */

/**
 * @brief 分片锁
 *
 * @details 分片锁将全局锁分为多个分片（Shard），
 *          每个分片独立管理一部分数据，减少锁竞争。
 *          分片选择策略：
 *          - 简单哈希分片（数据指针哈希）
 *          - 每个分片一个 TicketLock
 */
typedef struct
{
    uint32_t        num_shards;      /**< @brief 分片数量 */
    TicketLock_t    *shards;         /**< @brief 分片锁数组 */
} ShardedLock_t;

/* ========================================================================
 * 分片锁操作 API
 * ======================================================================== */

/**
 * @brief 初始化分片锁
 *
 * @details 初始化分片锁，分配分片锁数组。
 *
 * @param slock      分片锁指针
 * @param num_shards 分片数量
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOMEM 内存不足
 */
kernel_status_t sharded_lock_init(ShardedLock_t *slock, uint32_t num_shards);

/**
 * @brief 销毁分片锁
 *
 * @details 释放分片锁数组和结构体。
 *
 * @param slock 分片锁指针
 */
void sharded_lock_destroy(ShardedLock_t *slock);

/**
 * @brief 根据键值选择分片
 *
 * @details 使用简单哈希算法选择分片。
 *          分片索引 = (key >> N) & (num_shards - 1)
 *          其中 N 根据系统对齐选择（通常 3-6 位）
 *
 * @param slock      分片锁指针
 * @param key        键值（数据指针）
 *
 * @return 分片索引 [0, num_shards)
 */
uint32_t sharded_lock_select_shard(const ShardedLock_t *slock, uintptr_t key);

#endif /* KERNEL_SHARDED_LOCK_H */
