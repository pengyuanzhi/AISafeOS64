/**
 * @file    ipc_batch.h
 * @brief   IPC 批量处理接口
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @details IPC 批量处理接口定义：
 *          - 批量发送接口
 *          - 批量接收接口
 *          - 批量传输接口
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.2.3 - IPC 批量处理
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef AISAFE64_IPC_BATCH_H
#define AISAFE64_IPC_BATCH_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel.h>

/* ========================================================================
 * 批量传输标志
 * ======================================================================== */

#define IPC_BATCH_FLAGS_NONE     0x00000000U  /* 无标志 */
#define IPC_BATCH_FLAGS_BLOCK    0x00000001U  /* 阻塞模式 */
#define IPC_BATCH_FLAGS_NONBLOCK 0x00000002U  /* 非阻塞模式 */
#define IPC_BATCH_FLAGS_DUPLEX   0x00000004U  /* 双向传输 */

/* ========================================================================
 * 接口函数
 * ======================================================================== */

/**
 * @brief 批量发送多个消息
 *
 * @param channel IPC 通道指针
 * @param buffer 缓冲区
 * @param msg_size 每个消息大小
 * @param msg_count 消息数量
 * @param flags 发送标志
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOSPC 缓冲区空间不足
 *
 * @details 批量发送多个消息，减少系统调用次数
 *          适用于大数据量传输场景
 */
int32_t ipc_batch_send(channel_t *channel, void *buffer,
                       size_t msg_size, size_t msg_count, uint32_t flags);

/**
 * @brief 批量发送到端点
 *
 * @param endpoint 端点指针
 * @param buffer 缓冲区
 * @param msg_size 每个消息大小
 * @param msg_count 消息数量
 * @param flags 发送标志
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOSPC 缓冲区空间不足
 *
 * @details 从端点批量发送消息
 */
int32_t ipc_batch_send_to(endpoint_t *endpoint, void *buffer,
                          size_t msg_size, size_t msg_count, uint32_t flags);

/**
 * @brief 批量接收多个消息
 *
 * @param channel IPC 通道指针
 * @param buffer 缓冲区
 * @param msg_size 每个消息大小
 * @param msg_count 最大消息数量
 * @param flags 接收标志
 * @param received 实际接收数量
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENODATA 没有消息可读
 *
 * @details 批量接收多个消息，减少系统调用次数
 *          适用于大数据量接收场景
 */
int32_t ipc_batch_recv(channel_t *channel, void *buffer,
                       size_t msg_size, size_t msg_count,
                       uint32_t flags, size_t *received);

/**
 * @brief 批量接收从端点
 *
 * @param endpoint 端点指针
 * @param buffer 缓冲区
 * @param msg_size 每个消息大小
 * @param msg_count 最大消息数量
 * @param flags 接收标志
 * @param received 实际接收数量
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENODATA 没有消息可读
 *
 * @details 从端点批量接收消息
 */
int32_t ipc_batch_recv_from(endpoint_t *endpoint, void *buffer,
                            size_t msg_size, size_t msg_count,
                            uint32_t flags, size_t *received);

/**
 * @brief 批量传输（发送+接收）
 *
 * @param channel IPC 通道指针
 * @param send_buffer 发送缓冲区
 * @param recv_buffer 接收缓冲区
 * @param msg_size 消息大小
 * @param msg_count 消息数量
 * @param flags 传输标志
 * @param sent 实际发送数量
 * @param received 实际接收数量
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @details 批量发送和接收，减少系统调用次数
 *          适用于双向数据传输场景
 */
int32_t ipc_batch_transfer(channel_t *channel, void *send_buffer,
                           void *recv_buffer, size_t msg_size,
                           size_t msg_count, uint32_t flags,
                           size_t *sent, size_t *received);

#endif /* AISAFE64_IPC_BATCH_H */
