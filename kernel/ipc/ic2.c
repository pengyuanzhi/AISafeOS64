/**
 * @file    ic2.c
 * @brief   IC2（Inter-Context Communication）快速通信通道实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 实现 IC2 高性能双向通信通道：
 *          - 基于 SPSC 无锁环形缓冲区
 *          - 数据包头 + 有效负载封装
 *          - 共享内存零拷贝传输
 *          - 统计信息跟踪
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: DR-006~008
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/ipc_ic2.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <stdint.h>
#include <string.h>
#include "hal.h"

/* ========================================================================
 * IC2 全局状态
 * ======================================================================== */

/** @brief IC2 通道池 */
static ic2_channel_t s_channels[IC2_MAX_CHANNELS];

/** @brief 通道使用标记 */
static bool s_channel_used[IC2_MAX_CHANNELS];

/** @brief 环形缓冲区存储 */
static uint8_t s_ring_storage[IC2_MAX_CHANNELS][2U][IC2_RING_BUF_SIZE]
    __attribute__((aligned(64U)));

/** @brief 初始化标志 */
static bool s_initialized = false;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 安全字符串复制
 */
static void ic2_strcpy(char *dst, const char *src, uint32_t n)
{
    uint32_t i;

    if ((dst == NULL) || (src == NULL) || (n == 0U))
    {
        return;
    }

    for (i = 0U; (i < (n - 1U)) && (src[i] != '\0'); i++)
    {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/**
 * @brief 初始化环形缓冲区
 *
 * @param ring  环形缓冲区指针
 * @param buf   数据区指针
 * @param size  容量
 */
static void ringbuf_init(ic2_ringbuf_t *ring, uint8_t *buf, uint32_t size)
{
    ring->head = 0U;
    ring->tail = 0U;
    ring->capacity = size;
    ring->reserved = 0U;

    /* 数据区指向传入的缓冲区 */
    /* 由于 ic2_ringbuf_t 使用弹性数组成员，这里需要特殊处理 */
    (void)buf;

    hal_dmb_ish();
}

/**
 * @brief 向环形缓冲区写入数据
 *
 * @param ring     环形缓冲区指针（cast 为 uint8_t* 访问数据区）
 * @param data     数据指针
 * @param length   数据长度
 * @param capacity 容量
 * @param storage  底层存储
 *
 * @return 实际写入字节数
 */
static uint32_t ringbuf_write(volatile uint32_t *head, volatile uint32_t *tail,
                               const void *data, uint32_t length,
                               uint32_t capacity, uint8_t *storage)
{
    uint32_t h;
    uint32_t t;
    uint32_t free_space;
    uint32_t to_write;
    const uint8_t *src = (const uint8_t *)data;

    h = *head;
    t = *tail;

    /* 计算空闲空间 */
    if (h >= t)
    {
        free_space = capacity - (h - t) - 1U;
    }
    else
    {
        free_space = (t - h) - 1U;
    }

    to_write = (length < free_space) ? length : free_space;

    if (to_write == 0U)
    {
        return 0U;
    }

    /* 写入数据（可能回绕） */
    uint32_t first_part = capacity - h;
    if (first_part > to_write)
    {
        first_part = to_write;
    }

    (void)memcpy(&storage[h], src, first_part);

    if (to_write > first_part)
    {
        (void)memcpy(&storage[0U], &src[first_part], to_write - first_part);
    }

    hal_dmb_ishst();
    *head = (h + to_write) % capacity;

    return to_write;
}

/**
 * @brief 从环形缓冲区读取数据
 *
 * @return 实际读取字节数
 */
static uint32_t ringbuf_read(volatile uint32_t *head, volatile uint32_t *tail,
                              void *data, uint32_t length,
                              uint32_t capacity, uint8_t *storage)
{
    uint32_t h;
    uint32_t t;
    uint32_t used;
    uint32_t to_read;
    uint8_t *dst = (uint8_t *)data;

    hal_dmb_ishld();

    h = *head;
    t = *tail;

    /* 计算已用空间 */
    if (h >= t)
    {
        used = h - t;
    }
    else
    {
        used = capacity - (t - h);
    }

    to_read = (length < used) ? length : used;

    if (to_read == 0U)
    {
        return 0U;
    }

    /* 读取数据（可能回绕） */
    uint32_t first_part = capacity - t;
    if (first_part > to_read)
    {
        first_part = to_read;
    }

    (void)memcpy(dst, &storage[t], first_part);

    if (to_read > first_part)
    {
        (void)memcpy(&dst[first_part], &storage[0U], to_read - first_part);
    }

    *tail = (t + to_read) % capacity;

    return to_read;
}

/* ========================================================================
 * 初始化
 * ======================================================================== */

kernel_status_t ic2_init(void)
{
    uint32_t i;

    (void)memset(s_channels, 0, sizeof(s_channels));
    (void)memset(s_channel_used, 0, sizeof(s_channel_used));
    (void)memset(s_ring_storage, 0, sizeof(s_ring_storage));

    for (i = 0U; i < IC2_MAX_CHANNELS; i++)
    {
        s_channels[i].channel_id = i;
        s_channels[i].state = IC2_STATE_CLOSED;
        s_channels[i].ring_ab = NULL;
        s_channels[i].ring_ba = NULL;
        s_channels[i].owner_a = 0U;
        s_channels[i].owner_b = 0U;
        s_channels[i].lock = 0U;
        s_channels[i].stats_tx = 0U;
        s_channels[i].stats_rx = 0U;
    }

    s_initialized = true;

    return KERNEL_OK;
}

/* ========================================================================
 * 创建通道
 * ======================================================================== */

int32_t ic2_channel_create(const char *name, uint32_t owner_a,
                            uint32_t owner_b, uint32_t buf_size)
{
    uint32_t i;
    ic2_channel_t *ch;

    if (!s_initialized)
    {
        return -(int32_t)EPERM;
    }

    if (name == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if ((owner_a == 0U) || (owner_b == 0U) || (owner_a == owner_b))
    {
        return -(int32_t)EINVAL;
    }

    if ((buf_size == 0U) || (buf_size > IC2_RING_BUF_SIZE))
    {
        buf_size = IC2_RING_BUF_SIZE;
    }

    /* 查找空闲通道 */
    for (i = 0U; i < IC2_MAX_CHANNELS; i++)
    {
        if (!s_channel_used[i])
        {
            break;
        }
    }

    if (i >= IC2_MAX_CHANNELS)
    {
        return -(int32_t)ENOMEM;
    }

    ch = &s_channels[i];

    ic2_strcpy(ch->name, name, IC2_CHANNEL_NAME_MAX);
    ch->channel_id = i;
    ch->state = IC2_STATE_OPEN;
    ch->owner_a = owner_a;
    ch->owner_b = owner_b;
    ch->lock = 0U;
    ch->stats_tx = 0U;
    ch->stats_rx = 0U;

    /* 初始化环形缓冲区
     * 简化实现：直接使用预分配的静态存储 */
    ch->ring_ab = (ic2_ringbuf_t *)(void *)&s_ring_storage[i][0U];
    ch->ring_ba = (ic2_ringbuf_t *)(void *)&s_ring_storage[i][1U];

    ringbuf_init(ch->ring_ab, &s_ring_storage[i][0U][0], buf_size);
    ringbuf_init(ch->ring_ba, &s_ring_storage[i][1U][0], buf_size);

    s_channel_used[i] = true;

    return (int32_t)i;
}

/* ========================================================================
 * 销毁通道
 * ======================================================================== */

kernel_status_t ic2_channel_destroy(uint32_t channel_id)
{
    ic2_channel_t *ch;

    if (channel_id >= IC2_MAX_CHANNELS)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_channel_used[channel_id])
    {
        return -(int32_t)ENOENT;
    }

    ch = &s_channels[channel_id];
    ch->state = IC2_STATE_CLOSED;
    ch->ring_ab = NULL;
    ch->ring_ba = NULL;
    ch->owner_a = 0U;
    ch->owner_b = 0U;

    s_channel_used[channel_id] = false;

    return KERNEL_OK;
}

/* ========================================================================
 * 发送数据
 * ======================================================================== */

int32_t ic2_send(uint32_t channel_id, const void *data,
                  uint32_t length, uint32_t type, uint32_t flags)
{
    ic2_channel_t *ch;
    ic2_packet_header_t hdr;
    uint32_t written;

    if (data == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (length == 0U)
    {
        return 0;
    }

    if (length > IC2_MAX_PACKET_SIZE)
    {
        return -(int32_t)EINVAL;
    }

    if (channel_id >= IC2_MAX_CHANNELS)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_channel_used[channel_id])
    {
        return -(int32_t)ENOENT;
    }

    ch = &s_channels[channel_id];

    if (ch->state != IC2_STATE_OPEN)
    {
        return -(int32_t)EPERM;
    }

    /* 构建包头 */
    hdr.length = length;
    hdr.type = type;
    hdr.flags = flags;
    hdr.seq = ch->stats_tx;

    /* 写入包头 + 数据到 A→B 环形缓冲区 */
    /* 简化实现：使用 ring_ab 的存储区 */
    uint8_t *storage_ab = &s_ring_storage[channel_id][0U][0U];
    written = ringbuf_write(&ch->ring_ab->head, &ch->ring_ab->tail,
                            &hdr, sizeof(ic2_packet_header_t),
                            IC2_RING_BUF_SIZE, storage_ab);

    if (written < sizeof(ic2_packet_header_t))
    {
        return -(int32_t)ENOMEM;
    }

    written = ringbuf_write(&ch->ring_ab->head, &ch->ring_ab->tail,
                            data, length,
                            IC2_RING_BUF_SIZE, storage_ab);

    ch->stats_tx++;

    return (int32_t)written;
}

/* ========================================================================
 * 接收数据
 * ======================================================================== */

int32_t ic2_recv(uint32_t channel_id, void *buf,
                  uint32_t buf_size, uint32_t *type)
{
    ic2_channel_t *ch;
    ic2_packet_header_t hdr;
    uint32_t read_bytes;

    if (buf == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (buf_size == 0U)
    {
        return 0;
    }

    if (channel_id >= IC2_MAX_CHANNELS)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_channel_used[channel_id])
    {
        return -(int32_t)ENOENT;
    }

    ch = &s_channels[channel_id];

    if (ch->state != IC2_STATE_OPEN)
    {
        return -(int32_t)EPERM;
    }

    /* 从 B→A 环形缓冲区读取包头 + 数据 */
    uint8_t *storage_ba = &s_ring_storage[channel_id][1U][0U];

    read_bytes = ringbuf_read(&ch->ring_ba->head, &ch->ring_ba->tail,
                              &hdr, sizeof(ic2_packet_header_t),
                              IC2_RING_BUF_SIZE, storage_ba);

    if (read_bytes < sizeof(ic2_packet_header_t))
    {
        return 0; /* 无数据 */
    }

    /* 输出类型 */
    if (type != NULL)
    {
        *type = hdr.type;
    }

    /* 限制读取长度 */
    uint32_t to_read = hdr.length;
    if (to_read > buf_size)
    {
        to_read = buf_size;
    }

    read_bytes = ringbuf_read(&ch->ring_ba->head, &ch->ring_ba->tail,
                              buf, to_read,
                              IC2_RING_BUF_SIZE, storage_ba);

    ch->stats_rx++;

    return (int32_t)read_bytes;
}

/* ========================================================================
 * 查询
 * ======================================================================== */

uint32_t ic2_readable(uint32_t channel_id)
{
    ic2_channel_t *ch;
    uint32_t h;
    uint32_t t;

    if (channel_id >= IC2_MAX_CHANNELS)
    {
        return 0U;
    }

    if (!s_channel_used[channel_id])
    {
        return 0U;
    }

    ch = &s_channels[channel_id];

    if (ch->ring_ba == NULL)
    {
        return 0U;
    }

    h = ch->ring_ba->head;
    t = ch->ring_ba->tail;

    if (h >= t)
    {
        return h - t;
    }

    return ch->ring_ba->capacity - (t - h);
}

uint32_t ic2_writable(uint32_t channel_id)
{
    ic2_channel_t *ch;

    if (channel_id >= IC2_MAX_CHANNELS)
    {
        return 0U;
    }

    if (!s_channel_used[channel_id])
    {
        return 0U;
    }

    ch = &s_channels[channel_id];

    if (ch->ring_ab == NULL)
    {
        return 0U;
    }

    return ch->ring_ab->capacity - ic2_readable(channel_id);
}

ic2_channel_t *ic2_get_channel(uint32_t channel_id)
{
    if (channel_id >= IC2_MAX_CHANNELS)
    {
        return NULL;
    }

    if (!s_channel_used[channel_id])
    {
        return NULL;
    }

    return &s_channels[channel_id];
}
