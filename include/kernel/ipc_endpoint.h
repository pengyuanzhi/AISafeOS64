/**
 * @file    ipc_endpoint.h
 * @brief   IPC 端点（Endpoint）管理接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了 IPC 端点的内核管理接口：
 *          - 端点创建/销毁
 *          - 消息发送（阻塞/非阻塞）
 *          - 消息接收
 *          - 消息回复
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-005（同步消息传递）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_IPC_ENDPOINT_H
#define KERNEL_IPC_ENDPOINT_H

#include <kernel/types.h>
#include <kernel/ipc_types.h>

/* ========================================================================
 * 端点管理 API
 * ======================================================================== */

/**
 * @brief 初始化端点子系统
 *
 * @details 初始化全局端点对象池和链表。
 *          必须在调度器启动前调用。
 *
 * @return KERNEL_OK 成功
 * @return 负错误码 失败
 */
kernel_status_t ipc_endpoint_subsys_init(void);

/**
 * @brief 创建 IPC 端点
 *
 * @param owner_tid 拥有者线程 ID
 * @param ep_id     输出参数，返回创建的端点 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOMEM 无空闲端点
 *
 * @note 对应需求: KR-005
 */
kernel_status_t ipc_endpoint_create(thread_id_t owner_tid, kobj_id_t *ep_id);

/**
 * @brief 销毁 IPC 端点
 *
 * @param ep_id 端点 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 端点无效
 *
 * @note 销毁时唤醒所有等待的发送线程
 */
kernel_status_t ipc_endpoint_destroy(kobj_id_t ep_id);

/**
 * @brief 发送同步消息（阻塞）
 *
 * @details 向目标端点发送消息并阻塞等待回复。
 *          - 如果端点有待处理队列，消息加入队列
 *          - 如果端点正在接收，直接切换到接收线程
 *          - 发送方线程阻塞直到收到回复
 *
 * @param ep_id     目标端点 ID
 * @param tag       消息标签
 * @param send_buf  发送缓冲区（NULL 表示仅使用内联数据）
 * @param send_size 发送大小（字节）
 * @param recv_buf  接收缓冲区（用于存放回复）
 * @param recv_size 接收大小（字节）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EINTR   被信号中断
 *
 * @note 对应需求: KR-005（同步消息传递）
 */
kernel_status_t ipc_msg_send(kobj_id_t ep_id,
                              ipc_msg_tag_t tag,
                              const void *send_buf,
                              uint32_t send_size,
                              void *recv_buf,
                              uint32_t recv_size);

/**
 * @brief 接收消息（阻塞）
 *
 * @details 从端点的待处理队列中接收消息。
 *          如果队列为空，阻塞等待。
 *
 * @param ep_id     端点 ID
 * @param tag       输出参数，返回接收到的消息标签
 * @param recv_buf  接收缓冲区
 * @param recv_size 缓冲区大小（字节）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EINTR   被信号中断
 *
 * @note 对应需求: KR-005
 */
kernel_status_t ipc_msg_receive(kobj_id_t ep_id,
                                 ipc_msg_tag_t *tag,
                                 void *recv_buf,
                                 uint32_t recv_size);

/**
 * @brief 回复消息
 *
 * @details 回复之前通过 ipc_msg_receive 接收的消息。
 *          唤醒阻塞在 ipc_msg_send 中的发送方线程。
 *
 * @param ep_id     端点 ID
 * @param status    回复状态码
 * @param reply_buf 回复数据缓冲区
 * @param reply_size 回复数据大小（字节）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: KR-005
 */
kernel_status_t ipc_msg_reply(kobj_id_t ep_id,
                               int32_t status,
                               const void *reply_buf,
                               uint32_t reply_size);

/**
 * @brief 非阻塞发送消息
 *
 * @details 尝试发送消息，如果目标端点不在线则立即返回错误。
 *
 * @param ep_id     目标端点 ID
 * @param tag       消息标签
 * @param send_buf  发送缓冲区
 * @param send_size 发送大小（字节）
 *
 * @return KERNEL_OK 成功
 * @return -EBUSY   目标端点忙
 * @return -EINVAL  参数无效
 */
kernel_status_t ipc_msg_try_send(kobj_id_t ep_id,
                                  ipc_msg_tag_t tag,
                                  const void *send_buf,
                                  uint32_t send_size);

/**
 * @brief 带超时的消息发送
 *
 * @param ep_id       目标端点 ID
 * @param tag         消息标签
 * @param send_buf    发送缓冲区
 * @param send_size   发送大小
 * @param recv_buf    接收缓冲区
 * @param recv_size   接收大小
 * @param timeout_ms  超时时间（毫秒），IPC_TIMEOUT_INFINITE 为无限等待
 *
 * @return KERNEL_OK 成功
 * @return IPC_ERR_TIMEOUT 超时
 */
kernel_status_t ipc_msg_send_timeout(kobj_id_t ep_id,
                                      ipc_msg_tag_t tag,
                                      const void *send_buf,
                                      uint32_t send_size,
                                      void *recv_buf,
                                      uint32_t recv_size,
                                      uint32_t timeout_ms);

#endif /* KERNEL_IPC_ENDPOINT_H */
