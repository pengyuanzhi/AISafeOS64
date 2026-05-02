/**
 * @file    sharded_lock.c
 * @brief   分片锁（Sharded Lock）实现
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 实现分片锁数据结构和操作：
 *          - 分片锁初始化/销毁
 *          - 分片选择策略
 *          - 分片锁获取/释放
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.5 - SMP 优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/sharded_lock.h>
#include <kernel/errno.h>
#include <kernel/mm/slab.h>
#include <kernel/barrier.h>
#include <stddef.h>
#include <string.h>

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 计算分片数量（2 的幂次方）
 *
 * @param num_shards 请求的分片数量
 *
 * @return 调整后的分片数量（最接近且 <= num_shards 的 2 的幂次方）
 */
static uint32_t sharded_lock_adjust_shards(uint32_t num_shards)
{
    uint32_t adjusted_shards = 1U;

    while ((adjusted_shards << 1U) <= num_shards)
    {
        adjusted_shards = adjusted_shards << 1U;
    }

    return adjusted_shards;
}

/* ========================================================================
 * 分片锁初始化和销毁
 * ======================================================================== */

/**
 * @brief 初始化分片锁
 *
 * @details 初始化分片锁，分配分片锁数组。
 *          分片数量调整为 2 的幂次方。
 *
 * @param slock       分片锁指针
 * @param num_shards 请求的分片数量
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOMEM 内存不足
 */
kernel_status_t sharded_lock_init(ShardedLock_t *slock, uint32_t num_shards)
{
    uint32_t i;
    uint32_t adjusted_shards;

    if (slock == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if ((num_shards == 0U) || (num_shards > SHARD_LOCK_MAX_SHARDS))
    {
        return -(int32_t)EINVAL;
    }

    /* 调整分片数量为 2 的幂次方 */
    adjusted_shards = sharded_lock_adjust_shards(num_shards);
    slock->num_shards = adjusted_shards;

    /* 分配分片锁数组 */
    slock->shards = (TicketLock_t *)kmalloc(sizeof(TicketLock_t) * adjusted_shards);
    if (slock->shards == NULL)
    {
        return -(int32_t)ENOMEM;
    }

    /* 初始化每个分片锁 */
    for (i = 0U; i < adjusted_shards; i++)
    {
        ticket_lock_init(&slock->shards[i]);
    }

    return KERNEL_OK;
}

/**
 * @brief 销毁分片锁
 *
 * @details 释放分片锁数组和结构体。
 *
 * @param slock 分片锁指针
 */
void sharded_lock_destroy(ShardedLock_t *slock)
{
    if (slock == NULL)
    {
        return;
    }

    /* 释放分片锁数组 */
    if (slock->shards != NULL)
    {
        kfree(slock->shards);
        slock->shards = NULL;
    }

    slock->num_shards = 0U;
}

/* ========================================================================
 * 分片选择策略
 * ======================================================================== */

/**
 * @brief 简单哈希分片选择
 *
 * @details 使用数据指针的高位作为哈希输入，
 *          简单高效，但可能不是最优的。
 *
 * @param slock 分片锁指针
 * @param key   键值（数据指针）
 *
 * @return 分片索引 [0, num_shards)
 */
static inline uint32_t sharded_lock_select_simple(const ShardedLock_t *slock, const void *key)
{
    uint64_t key_hash;
    uint32_t shard_idx;
    uint32_t shift_bits;

    if (slock == NULL)
    {
        return 0U;
    }

    key_hash = (uint64_t)key;

    /* 计算需要的移位数 */
    shift_bits = 32U;
    while ((1U << shift_bits) < slock->num_shards)
    {
        shift_bits++;
    }

    /* 使用数据指针的高位作为哈希输入 */
    shard_idx = (uint32_t)((key_hash >> (64U - shift_bits)) & (slock->num_shards - 1U));

    return shard_idx;
}

/**
 * @brief 改进的哈希分片选择
 *
 * @details 使用混合哈希算法，提高分片分布的均匀性。
 *          结合高位和低位，减少哈希冲突。
 *
 * @param slock 分片锁指针
 * @param key   键值（数据指针）
 *
 * @return 分片索引 [0, num_shards)
 */
static inline uint32_t sharded_lock_select_improved(const ShardedLock_t *slock, const void *key)
{
    uint64_t key_hash;
    uint32_t shard_idx;
    uint32_t shift_bits;

    if (slock == NULL)
    {
        return 0U;
    }

    key_hash = (uint64_t)key;

    /* 改进的哈希：混合高位和低位 */
    key_hash = key_hash ^ (key_hash >> 32U);

    /* 计算需要的移位数 */
    shift_bits = 32U;
    while ((1U << shift_bits) < slock->num_shards)
    {
        shift_bits++;
    }

    /* 使用混合哈希的高位选择分片 */
    shard_idx = (uint32_t)((key_hash >> (64U - shift_bits)) & (slock->num_shards - 1U));

    return shard_idx;
}

/* ========================================================================
 * 默认分片选择策略
 * ======================================================================== */

/**
 * @brief 根据键值选择分片（默认策略）
 *
 * @details 使用改进的哈希分片选择策略。
 *
 * @param slock 分片锁指针
 * @param key   键值（数据指针）
 *
 * @return 分片索引 [0, num_shards)
 */
uint32_t sharded_lock_select(const ShardedLock_t *slock, const void *key)
{
    return sharded_lock_select_improved(slock, key);
}
