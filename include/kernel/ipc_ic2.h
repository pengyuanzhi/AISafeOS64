/**
 * @file    ic2.h
 * @brief   IC2（Inter-Context Communication）快速通信通道接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details IC2 是一种高性能双向通信通道，用于：
 *          - Linux 驱动容器与原生驱动之间的快速数据交换
 *          - 双生驱动的控制面/数据面通信
 *          - 基于共享内存的无锁环形缓冲区
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: DR-006~008
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_IPC_IC2_H
#define KERNEL_IPC_IC2_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * IC2 常量
 * ======================================================================== */

/** @brief IC2 通道最大数量 */
#define IC2_MAX_CHANNELS             32U

/** @brief IC2 环形缓冲区默认大小（字节） */
#define IC2_RING_BUF_SIZE            4096U

/** @brief IC2 最大数据包大小 */
#define IC2_MAX_PACKET_SIZE          1024U

/** @brief IC2 通道名最大长度 */
#define IC2_CHANNEL_NAME_MAX         32U

/* ========================================================================
 * IC2 数据包头
 * ======================================================================== */

/**
 * @brief IC2 数据包头
 *
 * @details 描述一个 IC2 数据包的元信息
 */
typedef struct
{
    uint32_t    length;          /**< @brief 有效负载长度 */
    uint32_t    type;            /**< @brief 数据包类型 */
    uint32_t    flags;           /**< @brief 标志 */
    uint32_t    seq;             /**< @brief 序列号 */
} ic2_packet_header_t;

/* ========================================================================
 * IC2 环形缓冲区描述符
 * ======================================================================== */

/**
 * @brief IC2 共享环形缓冲区
 *
 * @details 单生产者单消费者（SPSC）无锁环形缓冲区
 */
typedef struct
{
    volatile uint32_t head;           /**< @brief 写入位置 */
    volatile uint32_t tail;           /**< @brief 读取位置 */
    uint32_t          capacity;       /**< @brief 缓冲区容量 */
    uint32_t          reserved;       /**< @brief 保留对齐 */
    uint8_t           data[];         /**< @brief 弹性数组数据区 */
} ic2_ringbuf_t;

/* ========================================================================
 * IC2 通道描述符
 * ======================================================================== */

/**
 * @brief IC2 通道状态
 */
typedef enum
{
    IC2_STATE_CLOSED = 0U,      /**< @brief 已关闭 */
    IC2_STATE_OPEN,             /**< @brief 已打开 */
    IC2_STATE_ERROR             /**< @brief 错误状态 */
} ic2_channel_state_t;

/**
 * @brief IC2 通道描述符
 *
 * @details 一个 IC2 通道包含两个方向（A→B 和 B→A）的环形缓冲区
 */
typedef struct
{
    uint32_t            channel_id;     /**< @brief 通道 ID */
    char                name[IC2_CHANNEL_NAME_MAX]; /**< @brief 通道名 */
    ic2_channel_state_t state;          /**< @brief 通道状态 */
    ic2_ringbuf_t      *ring_ab;       /**< @brief A→B 方向环形缓冲区 */
    ic2_ringbuf_t      *ring_ba;       /**< @brief B→A 方向环形缓冲区 */
    uint32_t            owner_a;        /**< @brief A 端进程 ID */
    uint32_t            owner_b;        /**< @brief B 端进程 ID */
    uint32_t            lock;           /**< @brief 通道锁 */
    uint32_t            stats_tx;       /**< @brief 发送计数 */
    uint32_t            stats_rx;       /**< @brief 接收计数 */
} ic2_channel_t;

/* ========================================================================
 * IC2 API
 * ======================================================================== */

/**
 * @brief 初始化 IC2 子系统
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t ic2_init(void);

/**
 * @brief 创建 IC2 通道
 *
 * @param name       通道名
 * @param owner_a    A 端进程 ID
 * @param owner_b    B 端进程 ID
 * @param buf_size   环形缓冲区大小（每方向）
 *
 * @return 成功返回通道 ID，失败返回负错误码
 */
int32_t ic2_channel_create(const char *name, uint32_t owner_a,
                            uint32_t owner_b, uint32_t buf_size);

/**
 * @brief 销毁 IC2 通道
 *
 * @param channel_id 通道 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t ic2_channel_destroy(uint32_t channel_id);

/**
 * @brief 发送数据到通道
 *
 * @param channel_id 通道 ID
 * @param data       数据指针
 * @param length     数据长度
 * @param type       数据包类型
 * @param flags      标志
 *
 * @return 成功发送的字节数，失败返回负错误码
 */
int32_t ic2_send(uint32_t channel_id, const void *data,
                  uint32_t length, uint32_t type, uint32_t flags);

/**
 * @brief 从通道接收数据
 *
 * @param channel_id 通道 ID
 * @param buf        接收缓冲区
 * @param buf_size   缓冲区大小
 * @param[out] type  输出数据包类型（可为 NULL）
 *
 * @return 成功接收的字节数，失败返回负错误码
 */
int32_t ic2_recv(uint32_t channel_id, void *buf,
                  uint32_t buf_size, uint32_t *type);

/**
 * @brief 查询通道可读数据量
 *
 * @param channel_id 通道 ID
 *
 * @return 可读字节数
 */
uint32_t ic2_readable(uint32_t channel_id);

/**
 * @brief 查询通道可写空间
 *
 * @param channel_id 通道 ID
 *
 * @return 可写字节数
 */
uint32_t ic2_writable(uint32_t channel_id);

/**
 * @brief 获取通道描述符
 *
 * @param channel_id 通道 ID
 *
 * @return 通道描述符指针
 */
ic2_channel_t *ic2_get_channel(uint32_t channel_id);

#endif /* KERNEL_IPC_IC2_H */
