/**
 * @file    ipc_channel.h
 * @brief   IPC 通道和连接管理接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了 QNX 风格的通道-连接模型内核接口：
 *          - 通道创建/销毁（ChannelCreate/ChannelDestroy）
 *          - 连接附加/分离（ConnectAttach/ConnectDetach）
 *          - Pulse 发送（MsgSendPulse）
 *          - Pulse 接收（MsgReceivePulse）
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-007（Pulse）、KR-023（通道-连接模型）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_IPC_CHANNEL_H
#define KERNEL_IPC_CHANNEL_H

#include <kernel/types.h>
#include <kernel/ipc_types.h>

/* ========================================================================
 * 通道管理 API
 * ======================================================================== */

/**
 * @brief 初始化通道子系统
 *
 * @details 初始化全局通道和连接对象池。
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t ipc_channel_subsys_init(void);

/**
 * @brief 创建 IPC 通道
 *
 * @details 创建一个新的 IPC 通道，调用线程成为通道的拥有者。
 *          通道创建后处于 IPC_CH_OPEN 状态，
 *          其他线程可通过 ConnectAttach 连接到此通道。
 *
 * @param owner_tid 拥有者线程 ID
 * @param ch_id     输出参数，返回通道 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOMEM 无空闲通道
 *
 * @note 对应需求: KR-023
 */
kernel_status_t ipc_channel_create(thread_id_t owner_tid, kobj_id_t *ch_id);

/**
 * @brief 销毁 IPC 通道
 *
 * @details 销毁通道并断开所有连接。
 *          所有挂起的 Pulse 被丢弃。
 *          所有阻塞在通道上的线程被唤醒并收到错误。
 *
 * @param ch_id 通道 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 通道无效
 */
kernel_status_t ipc_channel_destroy(kobj_id_t ch_id);

/**
 * @brief 附加连接到通道
 *
 * @details 客户端线程通过此函数连接到目标通道。
 *          连接建立后，客户端可通过连接发送消息和 Pulse。
 *
 * @param client_tid 客户端线程 ID
 * @param ch_id      目标通道 ID
 * @param conn_id    输出参数，返回连接 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOMEM 无空闲连接
 * @return IPC_ERR_CHANNEL_CLOSED 通道已关闭
 *
 * @note 对应需求: KR-023
 */
kernel_status_t ipc_connect_attach(thread_id_t client_tid,
                                    kobj_id_t ch_id,
                                    kobj_id_t *conn_id);

/**
 * @brief 分离连接
 *
 * @details 断开客户端与通道的连接。
 *          如果连接上有待处理的同步消息，回复错误码。
 *
 * @param conn_id 连接 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 连接无效
 */
kernel_status_t ipc_connect_detach(kobj_id_t conn_id);

/* ========================================================================
 * Pulse 管理 API
 * ======================================================================== */

/**
 * @brief 发送 Pulse 异步消息
 *
 * @details Pulse 是轻量级异步消息，携带优先级 + code + value 三元素。
 *          Pulse 按优先级插入目标通道的 Pulse 队列。
 *          发送方不阻塞。
 *
 * @param conn_id  连接 ID（确定目标通道）
 * @param prio     Pulse 优先级
 * @param code     Pulse 代码（用户定义，-128 到 127 范围建议）
 * @param value    Pulse 值（用户定义）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 连接无效
 * @return IPC_ERR_PULSE_QUEUE_FULL Pulse 队列已满
 *
 * @note 对应需求: KR-007
 * @note 可在中断上下文中调用
 */
kernel_status_t ipc_pulse_send(kobj_id_t conn_id,
                                priority_t prio,
                                int32_t code,
                                int32_t value);

/**
 * @brief 接收 Pulse 消息
 *
 * @details 从通道的 Pulse 队列中取出最高优先级的 Pulse。
 *          如果队列为空，返回 -EAGAIN。
 *
 * @param ch_id  通道 ID
 * @param pulse  输出参数，返回接收到的 Pulse
 *
 * @return KERNEL_OK 成功
 * @return -EAGAIN  Pulse 队列为空
 * @return -EINVAL  通道无效
 */
kernel_status_t ipc_pulse_receive(kobj_id_t ch_id, ipc_pulse_t *pulse);

/**
 * @brief 阻塞接收 Pulse 消息
 *
 * @details 如果 Pulse 队列为空，阻塞当前线程直到有 Pulse 到达。
 *
 * @param ch_id  通道 ID
 * @param pulse  输出参数，返回接收到的 Pulse
 *
 * @return KERNEL_OK 成功
 * @return -EINTR   被信号中断
 * @return -EINVAL  通道无效
 */
kernel_status_t ipc_pulse_receive_blocking(kobj_id_t ch_id, ipc_pulse_t *pulse);

#endif /* KERNEL_IPC_CHANNEL_H */
