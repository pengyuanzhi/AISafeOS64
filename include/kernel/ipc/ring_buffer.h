/**
 * @file    ring_buffer.h
 * @brief   环形缓冲区接口
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @details 本文件定义了环形缓冲区接口：
 *          - 环形缓冲区结构
 *          - 环形缓冲区初始化
 *          - 环形缓冲区写入
 *          - 环形缓冲区读取
 *          - 环形缓冲区查询
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.2.2 - IPC 队列优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_IPC_RING_BUFFER_H
#define KERNEL_IPC_RING_BUFFER_H

#include <kernel/types.h>
#include <kernel/spinlock.h>
#include <stdint.h>

/* ========================================================================
 * 环形缓冲区结构
 * ======================================================================== */

/**
 * @brief 环形缓冲区结构
 *
 * @details 环形缓冲区用于 IPC 消息队列的优化，
 *          相比链表队列，具有更好的缓存局部性和 O(1) 操作复杂度。
 */
typedef struct
{
    void        *buffer;    /**< @brief 缓冲区指针 */
    uint64_t    size;       /**< @brief 缓冲区大小 */
    uint64_t    head;       /**< @brief 写入位置 */
    uint64_t    tail;       /**< @brief 读取位置 */
    uint64_t    count;      /**< @brief 当前数据量 */
    TicketLock_t lock;      /**< @brief 自旋锁 */
} ring_buffer_t;

/* ========================================================================
 * 环形缓冲区操作接口
 * ======================================================================== */

/**
 * @brief 初始化环形缓冲区
 *
 * @param rb     环形缓冲区指针
 * @param buffer 缓冲区指针
 * @param size   缓冲区大小
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t ring_buffer_init(ring_buffer_t *rb, void *buffer, uint64_t size);

/**
 * @brief 写入数据到环形缓冲区
 *
 * @param rb     环形缓冲区指针
 * @param data   数据指针
 * @param len    数据长度
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOMEM 缓冲区空间不足
 */
kernel_status_t ring_buffer_write(ring_buffer_t *rb, const void *data, uint64_t len);

/**
 * @brief 从环形缓冲区读取数据
 *
 * @param rb     环形缓冲区指针
 * @param data   数据指针
 * @param len    数据长度
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EAGAIN 缓冲区为空
 */
kernel_status_t ring_buffer_read(ring_buffer_t *rb, void *data, uint64_t len);

/**
 * @brief 获取环形缓冲区可用空间
 *
 * @param rb 环形缓冲区指针
 *
 * @return 可用空间大小（字节）
 */
uint64_t ring_buffer_space(ring_buffer_t *rb);

/**
 * @brief 获取环形缓冲区可用数据量
 *
 * @param rb 环形缓冲区指针
 *
 * @return 可用数据量（字节）
 */
uint64_t ring_buffer_available(ring_buffer_t *rb);

/**
 * @brief 清空环形缓冲区
 *
 * @param rb 环形缓冲区指针
 */
void ring_buffer_clear(ring_buffer_t *rb);

#endif /* KERNEL_IPC_RING_BUFFER_H */
