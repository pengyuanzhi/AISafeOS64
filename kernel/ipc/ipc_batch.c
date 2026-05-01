/**
 * @file    ipc_batch.c
 * @brief   IPC 批量处理接口实现
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @details IPC 批量处理接口：
 *          - 批量发送接口
 *          - 批量接收接口
 *          - 批量传输优化
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.2.3 - IPC 批量处理
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/ipc/ipc_batch.h>
#include <kernel/ipc/channel.h>
#include <kernel/ipc/endpoint.h>
#include <kernel/kernel.h>
#include <kernel/errno.h>
#include <kernel/mutex.h>

/* ========================================================================
 * IPC 批量传输结构
 * ======================================================================== */

/**
 * @brief IPC 批量传输对象
 */
typedef struct
{
    channel_t *channel;     /**< @brief IPC 通道 */
    void      *buffer;      /**< @brief 缓冲区 */
    size_t     buffer_size; /**< @brief 缓冲区大小 */
    size_t     msg_count;   /**< @brief 消息数量 */
    size_t     msg_size;    /**< @brief 每个消息大小 */
} ipc_batch_t;

/* ========================================================================
 * 批量发送接口实现
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
 */
int32_t ipc_batch_send(channel_t *channel, void *buffer,
                       size_t msg_size, size_t msg_count, uint32_t flags)
{
    int32_t ret;
    size_t total_size;
    size_t i;

    if (channel == NULL || buffer == NULL)
    {
        return -EINVAL;
    }

    /* 计算总大小 */
    total_size = msg_size * msg_count;

    /* 检查缓冲区大小 */
    if (total_size > SLAB_OBJECT_SIZE)
    {
        return -ENOSPC;
    }

    /* 获取通道锁 */
    ret = channel_lock(channel);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    /* 批量发送消息 */
    for (i = 0; i < msg_count; i++)
    {
        ret = channel_send(channel, buffer + (i * msg_size), msg_size, flags);
        if (ret != KERNEL_OK)
        {
            channel_unlock(channel);
            return ret;
        }
    }

    /* 释放通道锁 */
    channel_unlock(channel);

    return KERNEL_OK;
}

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
 */
int32_t ipc_batch_send_to(endpoint_t *endpoint, void *buffer,
                          size_t msg_size, size_t msg_count, uint32_t flags)
{
    if (endpoint == NULL || buffer == NULL)
    {
        return -EINVAL;
    }

    return ipc_batch_send(endpoint->channel, buffer,
                          msg_size, msg_count, flags);
}

/* ========================================================================
 * 批量接收接口实现
 * ======================================================================== */

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
 */
int32_t ipc_batch_recv(channel_t *channel, void *buffer,
                       size_t msg_size, size_t msg_count,
                       uint32_t flags, size_t *received)
{
    int32_t ret;
    size_t i;
    size_t actual_count = 0;

    if (channel == NULL || buffer == NULL || received == NULL)
    {
        return -EINVAL;
    }

    /* 获取通道锁 */
    ret = channel_lock(channel);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    /* 批量接收消息 */
    for (i = 0; i < msg_count; i++)
    {
        ret = channel_recv(channel, buffer + (i * msg_size), msg_size, flags);
        if (ret == -ENODATA)
        {
            break;  /* 没有更多消息 */
        }
        else if (ret != KERNEL_OK)
        {
            channel_unlock(channel);
            return ret;
        }

        actual_count++;
    }

    /* 释放通道锁 */
    channel_unlock(channel);

    *received = actual_count;

    return KERNEL_OK;
}

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
 */
int32_t ipc_batch_recv_from(endpoint_t *endpoint, void *buffer,
                            size_t msg_size, size_t msg_count,
                            uint32_t flags, size_t *received)
{
    if (endpoint == NULL || buffer == NULL || received == NULL)
    {
        return -EINVAL;
    }

    return ipc_batch_recv(endpoint->channel, buffer,
                          msg_size, msg_count, flags, received);
}

/* ========================================================================
 * 批量传输优化
 * ======================================================================== */

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
 */
int32_t ipc_batch_transfer(channel_t *channel, void *send_buffer,
                           void *recv_buffer, size_t msg_size,
                           size_t msg_count, uint32_t flags,
                           size_t *sent, size_t *received)
{
    int32_t ret;
    size_t i;

    if (channel == NULL || send_buffer == NULL || recv_buffer == NULL)
    {
        return -EINVAL;
    }

    *sent = 0;
    *received = 0;

    /* 获取通道锁 */
    ret = channel_lock(channel);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    /* 批量发送 */
    for (i = 0; i < msg_count; i++)
    {
        ret = channel_send(channel,
                          send_buffer + (i * msg_size),
                          msg_size, flags);
        if (ret != KERNEL_OK)
        {
            channel_unlock(channel);
            return ret;
        }
        (*sent)++;
    }

    /* 批量接收 */
    for (i = 0; i < msg_count; i++)
    {
        ret = channel_recv(channel,
                          recv_buffer + (i * msg_size),
                          msg_size, flags);
        if (ret == -ENODATA)
        {
            break;
        }
        else if (ret != KERNEL_OK)
        {
            channel_unlock(channel);
            return ret;
        }
        (*received)++;
    }

    /* 释放通道锁 */
    channel_unlock(channel);

    return KERNEL_OK;
}
