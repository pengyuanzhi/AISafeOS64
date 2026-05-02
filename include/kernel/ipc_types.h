/**
 * @file    ipc_types.h
 * @brief   IPC 子系统核心类型定义
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了 AISafeOS64 微内核 IPC 子系统使用的所有核心类型：
 *          - IPC 消息头和标签
 *          - IPC 端点（Endpoint）内核对象
 *          - 通知（Notification）内核对象
 *          - Pulse 轻量级消息
 *          - 通道（Channel）和连接（Connection）
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-005（同步消息）、KR-006（异步通知）、
 *                 KR-007（Pulse）、KR-008（共享内存）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_IPC_TYPES_H
#define KERNEL_IPC_TYPES_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <kernel/alignment.h>
#include <stdint.h>

/* ========================================================================
 * IPC 消息标签（Message Tag）
 * ======================================================================== */

/**
 * @brief IPC 消息标签
 *
 * @details 消息标签编码了消息的类型和大小信息。
 *          使用位域紧凑存储，减少寄存器传递开销。
 *
 *  位域布局：
 *  [63:56] 类型标志（type_flags）- 消息类型
 *  [55:48] 保留
 *  [47:32] 消息 ID（msg_id）- 用于匹配请求/响应
 *  [31:16] 发送长度（send_size）- 发送缓冲区大小
 *  [15:0]  接收长度（recv_size）- 接收缓冲区大小
 */
typedef struct
{
    uint64_t value; /**< @brief 标签原始值 */
} ipc_msg_tag_t;

/** @brief 从标签提取消息类型 */
#define IPC_TAG_TYPE(tag)       ((uint8_t)((tag).value >> 56U))

/** @brief 从标签提取消息 ID */
#define IPC_TAG_MSG_ID(tag)     ((uint16_t)((tag).value >> 32U))

/** @brief 从标签提取发送大小 */
#define IPC_TAG_SEND_SIZE(tag)  ((uint16_t)((tag).value >> 16U))

/** @brief 从标签提取接收大小 */
#define IPC_TAG_RECV_SIZE(tag)  ((uint16_t)((tag).value))

/** @brief 构造消息标签 */
#define IPC_TAG_MAKE(type, msg_id, send_sz, recv_sz) \
    (((uint64_t)(type) << 56U) | ((uint64_t)(msg_id) << 32U) | \
     ((uint64_t)(send_sz) << 16U) | (uint64_t)(recv_sz))

/* ========================================================================
 * IPC 消息类型标志
 * ======================================================================== */

/** @brief 普通同步消息 */
#define IPC_MSG_TYPE_NORMAL     ((uint8_t)0x00U)

/** @brief 回复消息 */
#define IPC_MSG_TYPE_REPLY      ((uint8_t)0x01U)

/** @brief Pulse 异步消息 */
#define IPC_MSG_TYPE_PULSE      ((uint8_t)0x02U)

/** @brief 通知信号 */
#define IPC_MSG_TYPE_NOTIFY     ((uint8_t)0x03U)

/** @brief 错误响应 */
#define IPC_MSG_TYPE_ERROR      ((uint8_t)0xFFU)

/* ========================================================================
 * IPC 消息头（Message Header）
 * ======================================================================== */

/**
 * @brief IPC 消息头
 *
 * @details 所有 IPC 消息的统一头部结构。
 *          同步消息在寄存器中传递时，仅使用 tag + 内联数据。
 *          大消息通过共享内存传输，此头部描述传输信息。
 */
typedef struct
{
    ipc_msg_tag_t tag;                                          /**< @brief 消息标签 */
    kobj_id_t     src_thread;                                   /**< @brief 发送方线程 ID */
    kobj_id_t     dst_endpoint;                                 /**< @brief 目标端点 ID */
    int32_t       status;                                       /**< @brief 消息状态/返回码 */
    uint64_t      inline_data[CONFIG_IPC_REG_MSG_WORDS];        /**< @brief 内联数据（快速路径） */
} ipc_msg_header_t;

/* ========================================================================
 * Pulse 轻量级消息
 * ======================================================================== */

/**
 * @brief Pulse 轻量级异步消息
 *
 * @details Pulse 携带优先级、代码和值三个元素，
 *          用于中断转发、状态变化通知等轻量级场景。
 *          Pulse 投递不阻塞发送方。
 *
 * @note 对应需求: KR-007
 */
typedef struct CACHE_ALIGN(64)
{
    priority_t  prio;           /**< @brief Pulse 优先级（决定投递顺序） */
    uint8_t     reserved[3];    /**< @brief 保留对齐 */
    int32_t     code;           /**< @brief Pulse 代码（用户定义） */
    int32_t     value;          /**< @brief Pulse 值（用户定义） */
    kobj_id_t   conn_id;        /**< @brief 来源连接 ID */
    struct list_head node;      /**< @brief 队列链表节点 */
} ipc_pulse_t;

/* ========================================================================
 * 通知（Notification）
 * ======================================================================== */

/**
 * @brief 通知对象状态
 */
typedef enum
{
    IPC_NOTIFY_IDLE = 0U,       /**< @brief 空闲：无待处理信号 */
    IPC_NOTIFY_PENDING,         /**< @brief 待处理：有信号等待被消费 */
    IPC_NOTIFY_WAITING          /**< @brief 等待中：有线程在等待信号 */
} ipc_notify_state_t;

/**
 * @brief 通知对象（Notification）
 *
 * @details 通知对象是异步事件信号机制的核心内核对象。
 *          - 生产者（中断/其他线程）调用 Signal 置位信号
 *          - 消费者线程调用 Wait 阻塞等待信号
 *          - 通知支持位掩码（64 位），可区分多种事件源
 *
 * @note 对应需求: KR-006（通知延迟 < 500ns）
 */
typedef struct CACHE_ALIGN(64)
{
    kobj_id_t           id;             /**< @brief 通知对象 ID */
    ipc_notify_state_t  state;          /**< @brief 当前状态 */
    uint64_t            signals;        /**< @brief 待处理信号位掩码 */
    uint64_t            waited_mask;    /**< @brief 等待的信号掩码 */
    thread_id_t         waiter_tid;     /**< @brief 等待线程 ID */
    struct list_head    node;           /**< @brief 全局通知链表节点 */
} ipc_notification_t;

/* ========================================================================
 * IPC 端点（Endpoint）
 * ======================================================================== */

/**
 * @brief 端点状态
 */
typedef enum
{
    IPC_EP_IDLE = 0U,           /**< @brief 空闲：无消息等待处理 */
    IPC_EP_PENDING,             /**< @brief 待处理：有消息在队列中 */
    IPC_EP_RECEIVING,           /**< @brief 接收中：线程阻塞在 Receive */
    IPC_EP_REPLYING             /**< @brief 回复中：正在构造回复 */
} ipc_ep_state_t;

/**
 * @brief IPC 端点（Endpoint）
 *
 * @details 端点是同步消息传递的内核对象。每个端点绑定到一个服务端线程，
 *          提供消息的接收和回复能力。
 *
 *          消息流：
 *          1. 客户端 Send → 消息挂入端点的待处理队列
 *          2. 服务端 Receive → 从队列取出消息
 *          3. 服务端 Reply → 唤醒客户端线程
 *
 * @note 对应需求: KR-005（同步消息传递）
 */
typedef struct CACHE_ALIGN(64)
{
    kobj_id_t           id;             /**< @brief 端点 ID */
    ipc_ep_state_t      state;          /**< @brief 端点当前状态 */
    thread_id_t         owner_tid;      /**< @brief 拥有者线程 ID（接收方） */
    thread_id_t         sender_tid;     /**< @brief 发送方线程 ID（用于 reply 唤醒） */
    struct list_head    pending_list;   /**< @brief 待处理消息队列 */
    struct list_head    node;           /**< @brief 全局端点链表节点 */
    TicketLock_t        lock;           /**< @brief 端点自旋锁 */
    void               *recv_buf;       /**< @brief 接收方缓冲区指针（RECV 时保存） */
    uint32_t            recv_size;      /**< @brief 接收方缓冲区大小 */
    const void         *send_buf;       /**< @brief 发送方缓冲区指针（SEND 时保存） */
    uint32_t            send_size;      /**< @brief 发送方数据大小 */
    void               *sender_recv_buf; /**< @brief 发送方回复缓冲区指针 */
    uint32_t            sender_recv_size; /**< @brief 发送方回复缓冲区大小 */
    ipc_msg_tag_t       saved_tag;       /**< @brief 保存的消息标签（Send→Receive 传递） */
    uint16_t            generation;       /**< @brief 端点 ID 世代号（防 use-after-free） */
} ipc_endpoint_t;

/* ========================================================================
 * 通道（Channel）
 * ======================================================================== */

/**
 * @brief 通道状态
 */
typedef enum
{
    IPC_CH_CLOSED = 0U,         /**< @brief 已关闭 */
    IPC_CH_OPEN                 /**< @brief 已打开 */
} ipc_ch_state_t;

/**
 * @brief IPC 通道（Channel）
 *
 * @details 通道是 QNX 风格消息传递的服务端入口点。
 *          服务端线程创建通道后，客户端通过 ConnectAttach 连接到通道。
 *          通道包含同步消息队列和 Pulse 队列。
 *
 * @note 对应需求: KR-023
 */
typedef struct CACHE_ALIGN(64)
{
    kobj_id_t           id;             /**< @brief 通道 ID */
    ipc_ch_state_t      state;          /**< @brief 通道状态 */
    thread_id_t         owner_tid;      /**< @brief 拥有者线程 ID */
    struct list_head    conn_list;      /**< @brief 挂载的连接列表 */
    struct list_head    pulse_queue;    /**< @brief Pulse 队列（按优先级排序） */
    uint32_t            pulse_count;    /**< @brief Pulse 队列当前深度 */
    struct list_head    node;           /**< @brief 全局通道链表节点 */
    TicketLock_t        lock;           /**< @brief 通道自旋锁 */
} ipc_channel_t;

/* ========================================================================
 * 连接（Connection）
 * ======================================================================== */

/**
 * @brief 连接状态
 */
typedef enum
{
    IPC_CONN_DISCONNECTED = 0U, /**< @brief 已断开 */
    IPC_CONN_CONNECTED           /**< @brief 已连接 */
} ipc_conn_state_t;

/**
 * @brief IPC 连接（Connection）
 *
 * @details 连接代表客户端到通道的附着关系。
 *          客户端通过连接发送同步消息或 Pulse。
 *          连接维护了发送方阻塞时的等待信息。
 *
 * @note 对应需求: KR-023
 */
typedef struct
{
    kobj_id_t           id;             /**< @brief 连接 ID */
    ipc_conn_state_t    state;          /**< @brief 连接状态 */
    kobj_id_t           channel_id;     /**< @brief 目标通道 ID */
    thread_id_t         client_tid;     /**< @brief 客户端线程 ID */
    ipc_msg_header_t    *pending_msg;   /**< @brief 正在处理的发送消息 */
    struct list_head    ch_node;        /**< @brief 通道的连接链表节点 */
    struct list_head    node;           /**< @brief 全局连接链表节点 */
} ipc_connection_t;

/* ========================================================================
 * IPC 超时常量
 * ======================================================================== */

/** @brief 无限等待（阻塞直到操作完成） */
#define IPC_TIMEOUT_INFINITE    ((uint32_t)0xFFFFFFFFU)

/** @brief 非阻塞（立即返回） */
#define IPC_TIMEOUT_NONBLOCK    ((uint32_t)0U)

/* ========================================================================
 * IPC 错误码
 * ======================================================================== */

/** @brief 通道已关闭 */
#define IPC_ERR_CHANNEL_CLOSED  ((int32_t)(-100))

/** @brief 连接已断开 */
#define IPC_ERR_CONN_DISCONNECTED ((int32_t)(-101))

/** @brief 端点已销毁 */
#define IPC_ERR_ENDPOINT_DESTROYED ((int32_t)(-102))

/** @brief 消息超时 */
#define IPC_ERR_TIMEOUT         ((int32_t)(-103))

/** @brief Pulse 队列已满 */
#define IPC_ERR_PULSE_QUEUE_FULL ((int32_t)(-104))

/** @brief 通知无等待者 */
#define IPC_ERR_NO_WAITER       ((int32_t)(-105))

#endif /* KERNEL_IPC_TYPES_H */
