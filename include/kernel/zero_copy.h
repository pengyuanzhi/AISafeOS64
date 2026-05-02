/**
 * @file    zero_copy.h
 * @brief   零拷贝消息传递接口
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 提供零拷贝消息传递接口：
 *          - 共享内存区域管理
 *          - 零拷贝消息发送
 *          - 零拷贝消息接收
 *          - 引用计数管理
 *
 * @note MISRA C:2012 合规
 * @note 对应优化点：零拷贝消息传递
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_ZERO_COPY_H
#define KERNEL_ZERO_COPY_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/alignment.h>
#include <kernel/spinlock.h>
#include <kernel/kobject.h>
#include <stdint.h>

/* ========================================================================
 * 零拷贝配置常量
 * ======================================================================== */

/** @brief 最大消息头数量 */
#define ZERO_COPY_MAX_MSGS    1024U

/** @brief 消息缓冲区大小 */
#define ZERO_COPY_BUF_SIZE    4096U  /* 4KB */

/** @brief 最大引用计数 */
#define ZERO_COPY_MAX_REFS    65536U

/** @brief 共享内存区域数量 */
#define ZERO_COPY_SHM_REGIONS 4U

/* ========================================================================
 * 消息状态
 * ======================================================================== */

/**
 * @brief 消息状态
 */
typedef enum
{
    ZERO_COPY_MSG_FREE = 0U,       /**< @brief 空闲 */
    ZERO_COPY_MSG_IN_USE           /**< @brief 使用中 */
} zero_copy_msg_state_t;

/* ========================================================================
 * 消息头
 * ======================================================================== */

/**
 * @brief 消息头
 *
 * @details 消息头存储在共享内存中，
 *          包含消息大小、引用计数、状态等信息。
 */
typedef struct CACHE_ALIGN(64)
{
    zero_copy_msg_state_t state;    /**< @brief 消息状态 */
    kobj_id_t              src;     /**< @brief 源线程 ID */
    kobj_id_t              dst;     /**< @brief 目标线程 ID */
    uint32_t               size;    /**< @brief 消息大小 */
    uint32_t               ref_cnt; /**< @brief 引用计数 */
    uint32_t               msg_id;  /**< @brief 消息 ID */
    uint32_t               flags;   /**< @brief 消息标志 */
} zero_copy_msg_header_t;

/* ========================================================================
 * 消息缓冲区
 * ======================================================================== */

/**
 * @brief 消息缓冲区
 *
 * @details 消息数据存储在缓冲区中。
 */
typedef struct CACHE_ALIGN(64)
{
    zero_copy_msg_header_t header;  /**< @brief 消息头 */
    uint8_t                data[ZERO_COPY_BUF_SIZE]; /**< @brief 消息数据 */
} zero_copy_msg_t;

/* ========================================================================
 * 共享内存区域
 * ======================================================================== */

/**
 * @brief 共享内存区域
 *
 * @details 管理多个消息缓冲区。
 */
typedef struct CACHE_ALIGN(64)
{
    zero_copy_msg_t        msgs[ZERO_COPY_MAX_MSGS];   /**< @brief 消息数组 */
    uint32_t               free_count;                 /**< @brief 空闲消息数量 */
    uint32_t               used_count;                  /**< @brief 已使用消息数量 */
    uint32_t               next_msg_id;                /**< @brief 下一个消息 ID */
    spinlock_t             lock;                       /**< @brief 区域锁 */
    uint32_t               region_id;                  /**< @brief 区域 ID */
} zero_copy_shm_region_t;

/* ========================================================================
 * 零拷贝管理器
 * ======================================================================== */

/**
 * @brief 零拷贝管理器
 *
 * @brief 管理所有共享内存区域和消息分配。
 */
typedef struct CACHE_ALIGN(64)
{
    zero_copy_shm_region_t  regions[ZERO_COPY_SHM_REGIONS];  /**< @brief 共享内存区域数组 */
    uint32_t                total_free;                        /**< @brief 总空闲消息数量 */
    uint64_t                total_sends;                       /**< @brief 总发送次数 */
    uint64_t                total_receives;                    /**< @brief 总接收次数 */
    uint64_t                total_copies;                      /**< @brief 总拷贝次数 */
} zero_copy_manager_t;

/* ========================================================================
 * 零拷贝消息传递 API
 * ======================================================================== */

/**
 * @brief 初始化零拷贝管理器
 *
 * @param manager 零拷贝管理器实例
 * @param region_count 共享内存区域数量
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t zero_copy_init(zero_copy_manager_t *manager, uint32_t region_count);

/**
 * @brief 发送零拷贝消息
 *
 * @details 零拷贝发送消息，不进行数据拷贝。
 *
 * @param manager 零拷贝管理器实例
 * @param dst_id  目标线程 ID
 * @param data    消息数据指针
 * @param size    消息大小
 * @param msg_id  输出参数，消息 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOSPC 共享内存已满
 */
kernel_status_t zero_copy_send(zero_copy_manager_t *manager,
                               kobj_id_t dst_id,
                               const void *data,
                               uint32_t size,
                               uint32_t *msg_id);

/**
 * @brief 接收零拷贝消息
 *
 * @details 零拷贝接收消息，直接访问共享内存中的数据。
 *
 * @param manager 零拷贝管理器实例
 * @param src_id  源线程 ID
 * @param data    输出参数，消息数据指针
 * @param size    输出参数，消息大小
 * @param msg_id  输出参数，消息 ID
 * @param flags   输出参数，消息标志
 *
 * @return KERNEL_OK 成功
 * @return -EAGAIN 没有消息
 */
kernel_status_t zero_copy_receive(zero_copy_manager_t *manager,
                                  kobj_id_t src_id,
                                  const void **data,
                                  uint32_t *size,
                                  uint32_t *msg_id,
                                  uint32_t *flags);

/**
 * @brief 释放消息
 *
 * @details 释放之前分配的消息，减少引用计数。
 *
 * @param manager 零拷贝管理器实例
 * @param msg_id  消息 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t zero_copy_release(zero_copy_manager_t *manager, uint32_t msg_id);

/**
 * @brief 增加引用计数
 *
 * @details 增加消息的引用计数，防止被过早释放。
 *
 * @param manager 零拷贝管理器实例
 * @param msg_id  消息 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t zero_copy_ref(zero_copy_manager_t *manager, uint32_t msg_id);

/**
 * @brief 获取统计信息
 *
 * @param manager       零拷贝管理器实例
 * @param total_sends   输出：总发送次数
 * @param total_receives 输出：总接收次数
 * @param total_copies  输出：总拷贝次数
 * @param total_free    输出：总空闲消息数量
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t zero_copy_get_stats(zero_copy_manager_t *manager,
                                     uint64_t *total_sends,
                                     uint64_t *total_receives,
                                     uint64_t *total_copies,
                                     uint32_t *total_free);

#endif /* KERNEL_ZERO_COPY_H */
