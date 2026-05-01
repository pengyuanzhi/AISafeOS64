/**
 * @file    ring_buffer.c
 * @brief   环形缓冲区实现
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @details 本文件实现了环形缓冲区：
 *          - 环形缓冲区初始化
 *          - 环形缓冲区写入（支持环绕）
 *          - 环形缓冲区读取（支持环绕）
 *          - 环形缓冲区查询
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.2.2 - IPC 队列优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/ipc/ring_buffer.h>
#include <kernel/errno.h>
#include <string.h>

/* ========================================================================
 * 环形缓冲区初始化
 * ======================================================================== */

/**
 * @brief 初始化环形缓冲区
 */
kernel_status_t ring_buffer_init(ring_buffer_t *rb, void *buffer, uint64_t size)
{
    /* 参数检查 */
    if ((rb == NULL) || (buffer == NULL) || (size == 0U))
    {
        return -(int32_t)EINVAL;
    }

    /* 初始化环形缓冲区 */
    (void)memset(rb, 0, sizeof(ring_buffer_t));
    rb->buffer = buffer;
    rb->size = size;
    rb->head = 0U;
    rb->tail = 0U;
    rb->count = 0U;

    /* 初始化自旋锁 */
    ticket_lock_init(&rb->lock);

    return KERNEL_OK;
}

/* ========================================================================
 * 环形缓冲区写入
 * ======================================================================== */

/**
 * @brief 写入数据到环形缓冲区
 */
kernel_status_t ring_buffer_write(ring_buffer_t *rb, const void *data, uint64_t len)
{
    uint64_t space;
    uint8_t *write_ptr;

    /* 参数检查 */
    if ((rb == NULL) || (data == NULL) || (len == 0U))
    {
        return -(int32_t)EINVAL;
    }

    /* 获取锁 */
    ticket_lock_acquire(&rb->lock);

    /* 检查是否有足够空间 */
    space = rb->size - rb->count;
    if (len > space)
    {
        ticket_lock_release(&rb->lock);
        return -(int32_t)ENOMEM;
    }

    /* 普通写入（不跨越边界） */
    if ((rb->head + len) <= rb->size)
    {
        write_ptr = (uint8_t *)rb->buffer + rb->head;
        (void)memcpy(write_ptr, data, len);
        rb->head += len;
    }
    else
    {
        /* 跨越边界写入 */
        uint64_t first_part;
        uint8_t *first_ptr;
        uint8_t *second_ptr;

        first_part = rb->size - rb->head;
        first_ptr = (uint8_t *)rb->buffer + rb->head;
        second_ptr = (uint8_t *)rb->buffer;

        (void)memcpy(first_ptr, data, first_part);
        (void)memcpy(second_ptr, (const uint8_t *)data + first_part, len - first_part);
        rb->head = len - first_part;
    }

    /* 更新计数 */
    rb->count += len;

    /* 释放锁 */
    ticket_lock_release(&rb->lock);

    return KERNEL_OK;
}

/* ========================================================================
 * 环形缓冲区读取
 * ======================================================================== */

/**
 * @brief 从环形缓冲区读取数据
 */
kernel_status_t ring_buffer_read(ring_buffer_t *rb, void *data, uint64_t len)
{
    uint64_t avail;
    uint8_t *read_ptr;

    /* 参数检查 */
    if ((rb == NULL) || (data == NULL) || (len == 0U))
    {
        return -(int32_t)EINVAL;
    }

    /* 获取锁 */
    ticket_lock_acquire(&rb->lock);

    /* 检查是否有数据 */
    if (rb->count == 0U)
    {
        ticket_lock_release(&rb->lock);
        return -(int32_t)EAGAIN;
    }

    /* 检查是否有足够数据 */
    avail = rb->count;
    if (len > avail)
    {
        len = avail;
    }

    /* 普通读取（不跨越边界） */
    if ((rb->tail + len) <= rb->size)
    {
        read_ptr = (uint8_t *)rb->buffer + rb->tail;
        (void)memcpy(data, read_ptr, len);
        rb->tail += len;
    }
    else
    {
        /* 跨越边界读取 */
        uint64_t first_part;
        uint8_t *first_ptr;
        uint8_t *second_ptr;

        first_part = rb->size - rb->tail;
        first_ptr = (uint8_t *)rb->buffer + rb->tail;
        second_ptr = (uint8_t *)rb->buffer;

        (void)memcpy(data, first_ptr, first_part);
        (void)memcpy((uint8_t *)data + first_part, second_ptr, len - first_part);
        rb->tail = len - first_part;
    }

    /* 更新计数 */
    rb->count -= len;

    /* 释放锁 */
    ticket_lock_release(&rb->lock);

    return KERNEL_OK;
}

/* ========================================================================
 * 环形缓冲区查询
 * ======================================================================== */

/**
 * @brief 获取环形缓冲区可用空间
 */
uint64_t ring_buffer_space(ring_buffer_t *rb)
{
    uint64_t space;

    if (rb == NULL)
    {
        return 0U;
    }

    /* 获取锁 */
    ticket_lock_acquire(&rb->lock);

    space = rb->size - rb->count;

    /* 释放锁 */
    ticket_lock_release(&rb->lock);

    return space;
}

/**
 * @brief 获取环形缓冲区可用数据量
 */
uint64_t ring_buffer_available(ring_buffer_t *rb)
{
    uint64_t count;

    if (rb == NULL)
    {
        return 0U;
    }

    /* 获取锁 */
    ticket_lock_acquire(&rb->lock);

    count = rb->count;

    /* 释放锁 */
    ticket_lock_release(&rb->lock);

    return count;
}

/* ========================================================================
 * 环形缓冲区清空
 * ======================================================================== */

/**
 * @brief 清空环形缓冲区
 */
void ring_buffer_clear(ring_buffer_t *rb)
{
    if (rb == NULL)
    {
        return;
    }

    /* 获取锁 */
    ticket_lock_acquire(&rb->lock);

    /* 重置 head、tail 和 count */
    rb->head = 0U;
    rb->tail = 0U;
    rb->count = 0U;

    /* 释放锁 */
    ticket_lock_release(&rb->lock);
}
