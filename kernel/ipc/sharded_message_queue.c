/**
 * @file    sharded_message_queue.c
 * @brief   消息队列分片锁实现
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 实现消息队列分片锁：
 *          - 消息队列分片锁
 *          - 消息入队/出队
 *          - 分片切换
 *          - 统计信息
 *
 * @note MISRA C:2012 合规
 * @note 对应优化点：消息队列分片锁
 *
 * @copyright Copyright (c) 2022 AISafe64 Team
 */

#include <kernel/sharded_message_queue.h>
#include <kernel/printk.h>
#include <kernel/barrier.h>
#include <string.h>

/* ========================================================================
 * 全局消息队列分片锁实例
 * ======================================================================== */

/**
 * @brief 全局消息队列分片锁实例
 */
static sharded_mq_t CACHE_ALIGN(64) s_sharded_mq;

/**
 * @brief 消息队列分片锁初始化标志
 */
static bool s_sharded_mq_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 计算下一个分片索引
 *
 * @param shard_idx 当前分片索引
 *
 * @return 下一组分片索引
 */
static inline uint32_t sharded_mq_next_shard(uint32_t shard_idx)
{
    return (shard_idx + 1U) % SHARDED_MQ_SHARDS;
}

/**
 * @brief 检查消息队列是否为空（本地分片）
 *
 * @param shard  分片指针
 *
 * @return true 表示为空，false 表示不为空
 */
static inline bool sharded_mq_shard_is_empty(sharded_mq_shard_t *shard)
{
    return (shard->msg_count == 0U);
}

/* ========================================================================
 * 消息队列分片锁操作 API 实现
 * ======================================================================== */

/**
 * @brief 初始化消息队列分片锁
 *
 * @param mq 消息队列分片锁实例
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t sharded_mq_init(sharded_mq_t *mq)
{
    uint32_t i;
    uint32_t j;

    if (mq == NULL)
    {
        return -(int32_t)EINVAL;
    }

    (void)memset(mq, 0U, sizeof(sharded_mq_t));

    for (i = 0U; i < SHARDED_MQ_SHARDS; i++)
    {
        (void)memset(&mq->shards[i], 0U, sizeof(sharded_mq_shard_t));

        for (j = 0U; j < SHARDED_MQ_MAX_MSGS; j++)
        {
            mq->shards[i].messages[j].type = SHARDED_MQ_MSG_COUNT;
            mq->shards[i].messages[j].size = 0U;
            mq->shards[i].messages[j].data = NULL;
            mq->shards[i].messages[j].src_thread = 0U;
            mq->shards[i].messages[j].dst_thread = 0U;
            mq->shards[i].messages[j].status = 0;
        }

        mq->shards[i].write_idx = 0U;
        mq->shards[i].read_idx = 0U;
        mq->shards[i].msg_count = 0U;
        mq->shards[i].total_enqueues = 0U;
        mq->shards[i].total_dequeues = 0U;
        ticket_lock_init(&mq->shards[i].lock);
    }

    mq->write_shard_idx = 0U;
    mq->read_shard_idx = 0U;
    mq->switch_count = 0U;
    mq->total_enqueues = 0U;
    mq->total_dequeues = 0U;
    mq->max_msg_count = 0U;

    s_sharded_mq_initialized = true;

    printk("Sharded Message Queue initialized: %u shards, %u max msgs per shard\n",
           SHARDED_MQ_SHARDS, SHARDED_MQ_MAX_MSGS);

    return KERNEL_OK;
}

/**
 * @brief 销毁消息队列分片锁
 *
 * @param mq 消息队列分片锁实例
 */
void sharded_mq_destroy(sharded_mq_t *mq)
{
    if (mq == NULL)
    {
        return;
    }

    (void)memset(mq, 0U, sizeof(sharded_mq_t));
    s_sharded_mq_initialized = false;
}

/**
 * @brief 入队消息
 *
 * @details 向消息队列中添加一条消息。
 *          自动切换到下一个分片。
 *
 * @param mq    消息队列分片锁实例
 * @param msg   消息指针
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t sharded_mq_enqueue(sharded_mq_t *mq, const sharded_mq_msg_t *msg)
{
    uint32_t shard_idx;
    sharded_mq_shard_t *shard;
    uint32_t write_idx;
    uint32_t msg_idx;

    if (mq == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (msg == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_sharded_mq_initialized)
    {
        return -(int32_t)EINVAL;
    }

    if (msg->type >= SHARDED_MQ_MSG_COUNT)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取当前写入分片 */
    shard_idx = mq->write_shard_idx;
    shard = &mq->shards[shard_idx];

    /* 获取写入位置 */
    write_idx = shard->write_idx;

    /* 检查分片是否已满 */
    if (shard->msg_count >= SHARDED_MQ_MAX_MSGS)
    {
        /* 分片已满，切换到下一组分片 */
        shard_idx = sharded_mq_next_shard(shard_idx);
        mq->write_shard_idx = shard_idx;
        mq->switch_count++;

        shard = &mq->shards[shard_idx];
        write_idx = shard->write_idx;
    }

    /* 加锁 */
    ticket_lock_acquire(&shard->lock);

    /* 再次检查分片是否已满（可能已被其他线程切换）*/
    if (shard->msg_count >= SHARDED_MQ_MAX_MSGS)
    {
        ticket_lock_release(&shard->lock);

        /* 切换到下一组分片 */
        shard_idx = sharded_mq_next_shard(shard_idx);
        mq->write_shard_idx = shard_idx;
        mq->switch_count++;

        shard = &mq->shards[shard_idx];
        write_idx = shard->write_idx;
    }

    /* 计算消息索引 */
    msg_idx = write_idx % SHARDED_MQ_MAX_MSGS;

    /* 复制消息内容 */
    (void)memcpy(&shard->messages[msg_idx], msg, sizeof(sharded_mq_msg_t));

    /* 更新写入索引 */
    shard->write_idx = (write_idx + 1U) % SHARDED_MQ_MAX_MSGS;
    shard->msg_count++;
    shard->total_enqueues++;
    mq->total_enqueues++;

    /* 更新最大消息数量 */
    if (shard->msg_count > mq->max_msg_count)
    {
        mq->max_msg_count = shard->msg_count;
    }

    ticket_lock_release(&shard->lock);

    return KERNEL_OK;
}

/**
 * @brief 出队消息
 *
 * @details 从消息队列中取出一条消息。
 *          自动切换到下一组分片。
 *
 * @param mq    消息队列分片锁实例
 * @param msg   输出参数，消息指针
 *
 * @return KERNEL_OK 成功
 * @return -EAGAIN 队列为空
 */
kernel_status_t sharded_mq_dequeue(sharded_mq_t *mq, sharded_mq_msg_t *msg)
{
    uint32_t shard_idx;
    sharded_mq_shard_t *shard;
    uint32_t read_idx;
    uint32_t msg_idx;
    bool found;

    if (mq == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (msg == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_sharded_mq_initialized)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取当前读取分片 */
    shard_idx = mq->read_shard_idx;
    shard = &mq->shards[shard_idx];
    found = false;

    /* 查找非空分片 */
    while (!found && shard->msg_count == 0U)
    {
        /* 当前分片为空，切换到下一组分片 */
        shard_idx = sharded_mq_next_shard(shard_idx);
        mq->read_shard_idx = shard_idx;
        mq->switch_count++;

        shard = &mq->shards[shard_idx];

        /* 检查是否回到起始分片 */
        if (shard_idx == mq->write_shard_idx)
        {
            /* 所有分片都为空 */
            return -EAGAIN;
        }
    }

    if (shard->msg_count == 0U)
    {
        /* 仍然为空 */
        return -EAGAIN;
    }

    /* 加锁 */
    ticket_lock_acquire(&shard->lock);

    /* 再次检查分片是否为空（可能已被其他线程切换）*/
    if (shard->msg_count == 0U)
    {
        ticket_lock_release(&shard->lock);

        /* 切换到下一组分片 */
        shard_idx = sharded_mq_next_shard(shard_idx);
        mq->read_shard_idx = shard_idx;
        mq->switch_count++;

        shard = &mq->shards[shard_idx];
    }

    if (shard->msg_count == 0U)
    {
        /* 仍然为空 */
        ticket_lock_release(&shard->lock);
        return -EAGAIN;
    }

    /* 计算读取位置 */
    read_idx = shard->read_idx;
    msg_idx = read_idx % SHARDED_MQ_MAX_MSGS;

    /* 复制消息内容 */
    (void)memcpy(msg, &shard->messages[msg_idx], sizeof(sharded_mq_msg_t));

    /* 清空消息内容 */
    (void)memset(&shard->messages[msg_idx], 0U, sizeof(sharded_mq_msg_t));
    shard->messages[msg_idx].type = SHARDED_MQ_MSG_COUNT;
    shard->messages[msg_idx].size = 0U;
    shard->messages[msg_idx].data = NULL;
    shard->messages[msg_idx].src_thread = 0U;
    shard->messages[msg_idx].dst_thread = 0U;
    shard->messages[msg_idx].status = 0;

    /* 更新读取索引 */
    shard->read_idx = (read_idx + 1U) % SHARDED_MQ_MAX_MSGS;
    shard->msg_count--;
    shard->total_dequeues++;
    mq->total_dequeues++;

    ticket_lock_release(&shard->lock);

    return KERNEL_OK;
}

/**
 * @brief 检查消息队列是否为空
 *
 * @param mq    消息队列分片锁实例
 *
 * @return true 表示为空，false 表示不为空
 */
bool sharded_mq_is_empty(sharded_mq_t *mq)
{
    uint32_t i;

    if (mq == NULL)
    {
        return true;
    }

    if (!s_sharded_mq_initialized)
    {
        return true;
    }

    /* 检查所有分片是否都为空 */
    for (i = 0U; i < SHARDED_MQ_SHARDS; i++)
    {
        if (mq->shards[i].msg_count > 0U)
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief 获取消息队列统计信息
 *
 * @param mq              消息队列分片锁实例
 * @param total_msgs      输出：总消息数量
 * @param total_enqueues  输出：总入队次数
 * @param total_dequeues  输出：总出队次数
 * @param switch_count    输出：分片切换次数
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t sharded_mq_get_stats(sharded_mq_t *mq,
                                     uint32_t *total_msgs,
                                     uint64_t *total_enqueues,
                                     uint64_t *total_dequeues,
                                     uint32_t *switch_count)
{
    uint32_t i;

    if (mq == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if ((total_msgs == NULL) || (total_enqueues == NULL) ||
        (total_dequeues == NULL) || (switch_count == NULL))
    {
        return -(int32_t)EINVAL;
    }

    if (!s_sharded_mq_initialized)
    {
        return -(int32_t)EINVAL;
    }

    /* 计算总消息数量 */
    *total_msgs = 0U;
    for (i = 0U; i < SHARDED_MQ_SHARDS; i++)
    {
        *total_msgs += mq->shards[i].msg_count;
    }

    *total_enqueues = mq->total_enqueues;
    *total_dequeues = mq->total_dequeues;
    *switch_count = mq->switch_count;

    return KERNEL_OK;
}
