/**
 * @file    sharded_message_queue.h
 * @brief   消息队列分片锁接口
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 提供消息队列分片锁接口：
 *          - 消息队列分片锁
 *          - 消息入队/出队
 *          - 分片切换
 *          - 统计信息
 *
 * @note MISRA C:2012 合规
 * @note 对应优化点：消息队列分片锁
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_SHARDED_MESSAGE_QUEUE_H
#define KERNEL_SHARDED_MESSAGE_QUEUE_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/sharded_lock.h>
#include <kernel/spinlock.h>
#include <stdint.h>

/* ========================================================================
 * 消息队列分片锁配置常量
 * ======================================================================== */

/** @brief 消息队列分片数量（建议为 CPU 数量的倍数） */
#define SHARDED_MQ_SHARDS      4U

/** @brief 每个分片的最大消息数量 */
#define SHARDED_MQ_MAX_MSGS    128U

/** @brief 消息队列最小阈值（触发切换分片） */
#define SHARDED_MQ_SWITCH_THRESHOLD  64U

/* ========================================================================
 * 消息类型
 * ======================================================================== */

/**
 * @brief 消息类型
 */
typedef enum
{
    SHARDED_MQ_MSG_NORMAL = 0U,   /**< @brief 普通消息 */
    SHARDED_MQ_MSG_REPLY,         /**< @brief 回复消息 */
    SHARDED_MQ_MSG_PULSE,         /**< @brief Pulse 消息 */
    SHARDED_MQ_MSG_COUNT          /**< @brief 消息类型数量 */
} sharded_mq_msg_type_t;

/* ========================================================================
 * 消息结构
 * ======================================================================== */

/**
 * @brief 消息
 *
 * @brief 消息内容（简单实现）
 */
typedef struct
{
    sharded_mq_msg_type_t type;   /**< @brief 消息类型 */
    uint32_t        size;          /**< @brief 消息大小 */
    void           *data;         /**< @brief 消息数据 */
    kobj_id_t       src_thread;   /**< @brief 源线程 ID */
    kobj_id_t       dst_thread;   /**< @brief 目标线程 ID */
    int32_t         status;       /**< @brief 消息状态 */
} sharded_mq_msg_t;

/* ========================================================================
 * 消息队列分片
 * ======================================================================== */

/**
 * @brief 消息队列分片
 *
 * @brief 每个分片维护自己的消息队列。
 */
typedef struct CACHE_ALIGN(64)
{
    sharded_mq_msg_t messages[SHARDED_MQ_MAX_MSGS];  /**< @brief 消息数组 */
    uint32_t          write_idx;                    /**< @brief 写入索引 */
    uint32_t          read_idx;                     /**< @brief 读取索引 */
    uint32_t          msg_count;                    /**< @brief 消息数量 */
    uint32_t          total_enqueues;                /**< @brief 总入队次数 */
    uint32_t          total_dequeues;               /**< @brief 总出队次数 */
    TicketLock_t      lock;                         /**< @brief 分片锁 */
} sharded_mq_shard_t;

/* ========================================================================
 * 消息队列分片锁
 * ======================================================================== */

/**
 * @brief 消息队列分片锁
 *
 * @brief 管理所有分片。
 */
typedef struct CACHE_ALIGN(64)
{
    sharded_mq_shard_t  shards[SHARDED_MQ_SHARDS];   /**< @brief 分片数组 */
    uint32_t            write_shard_idx;              /**< @brief 当前写入分片索引 */
    uint32_t            read_shard_idx;               /**< @brief 当前读取分片索引 */
    uint32_t            switch_count;                 /**< @brief 分片切换次数 */
    uint64_t            total_enqueues;               /**< @brief 总入队次数 */
    uint64_t            total_dequeues;               /**< @total 出队次数 */
    uint32_t            max_msg_count;                /**< @brief 最大消息数量 */
} sharded_mq_t;

/* ========================================================================
 * 消息队列分片锁操作 API
 * ======================================================================== */

/**
 * @brief 初始化消息队列分片锁
 *
 * @param mq 消息队列分片锁实例
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t sharded_mq_init(sharded_mq_t *mq);

/**
 * @brief 销毁消息队列分片锁
 *
 * @param mq 消息队列分片锁实例
 */
void sharded_mq_destroy(sharded_mq_t *mq);

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
kernel_status_t sharded_mq_enqueue(sharded_mq_t *mq, const sharded_mq_msg_t *msg);

/**
 * @brief 出队消息
 *
 * @details 从消息队列中取出一条消息。
 *          自动切换到下一个分片。
 *
 * @param mq    消息队列分片锁实例
 * @param msg   输出参数，消息指针
 *
 * @return KERNEL_OK 成功
 * @return -EAGAIN 队列为空
 */
kernel_status_t sharded_mq_dequeue(sharded_mq_t *mq, sharded_mq_msg_t *msg);

/**
 * @brief 检查消息队列是否为空
 *
 * @param mq    消息队列分片锁实例
 *
 * @return true 表示为空，false 表示不为空
 */
bool sharded_mq_is_empty(sharded_mq_t *mq);

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
                                     uint32_t *switch_count);

#endif /* KERNEL_SHARDED_MESSAGE_QUEUE_H */
