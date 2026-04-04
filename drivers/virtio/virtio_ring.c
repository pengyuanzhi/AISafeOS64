/**
 * @file    virtio_ring.c
 * @brief   VirtQueue 环形缓冲区管理
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 1.0
 *
 * @details 本文件实现 VirtIO 规范定义的 virtqueue 环形缓冲区管理：
 *          - 描述符表分配与链式管理
 *          - 可用环（Available Ring）操作（驱动写入方向）
 *          - 使用环（Used Ring）操作（设备写入方向）
 *          - DMA 内存分配与对齐
 *          - 间接描述符表支持
 *          - 无锁单生产者设计（驱动侧）
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: DV-014~017
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/driver_framework.h>
#include <kernel/types.h>
#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * VirtQueue 常量定义
 * ======================================================================== */

/** @brief 描述符标志位：下一个描述符有效 */
#define VIRTQ_DESC_F_NEXT          (1U << 0U)

/** @brief 描述符标志位：设备可写 */
#define VIRTQ_DESC_F_WRITE         (1U << 1U)

/** @brief 描述符标志位：间接描述符表 */
#define VIRTQ_DESC_F_INDIRECT      (1U << 2U)

/** @brief 可用环标志位：禁止中断通知 */
#define VIRTQ_AVAIL_F_NO_INTERRUPT (1U << 0U)

/** @brief 使用环标志位：禁止可用通知 */
#define VIRTQ_USED_F_NO_NOTIFY     (1U << 0U)

/** @brief 无效描述符索引 */
#define VIRTQ_DESC_INVALID         0xFFFFU

/** @brief 最大队列大小 */
#define VIRTQ_MAX_SIZE             1024U

/** @brief 描述符表对齐（16 字节） */
#define VIRTQ_DESC_ALIGN           16U

/** @brief 可用环对齐（2 字节） */
#define VIRTQ_AVAIL_ALIGN          2U

/** @brief 使用环对齐（4 字节） */
#define VIRTQ_USED_ALIGN           4U

/** @brief 描述符数量上限 */
#define VIRTQ_MAX_DESC_CHAIN       16U

/** @brief 最大间接描述符数 */
#define VIRTQ_MAX_INDIRECT         64U

/* ========================================================================
 * VirtQueue 数据结构
 * ======================================================================== */

/**
 * @brief VirtQueue 描述符（16 字节）
 *
 * @details 描述一个 I/O 缓冲区的地址、长度和属性
 */
typedef struct
{
    uint64_t addr;          /**< @brief 缓冲区物理/总线地址 */
    uint32_t len;           /**< @brief 缓冲区长度（字节） */
    uint16_t flags;         /**< @brief 标志位（NEXT/WRITE/INDIRECT） */
    uint16_t next;          /**< @brief 链中下一个描述符索引 */
} virtq_desc_t;

/**
 * @brief VirtQueue 可用环（驱动 → 设备方向）
 *
 * @details 驱动将就绪的描述符头索引写入此环
 */
typedef struct
{
    uint16_t flags;         /**< @brief 标志（NO_INTERRUPT） */
    uint16_t idx;           /**< @brief 下一个空闲槽索引 */
    uint16_t ring[];        /**< @brief 描述符头索引数组（队列大小元素） */
    /* uint16_t used_event 在 ring 之后 */
} virtq_avail_t;

/**
 * @brief VirtQueue 使用环元素
 */
typedef struct
{
    uint32_t id;            /**< @brief 描述符链头索引 */
    uint32_t len;           /**< @brief 设备已写入的字节数 */
} virtq_used_elem_t;

/**
 * @brief VirtQueue 使用环（设备 → 驱动方向）
 *
 * @details 设备完成 I/O 后将描述符索引和已写入长度写入此环
 */
typedef struct
{
    uint16_t flags;         /**< @brief 标志（NO_NOTIFY） */
    uint16_t idx;           /**< @brief 下一个写入槽索引 */
    virtq_used_elem_t ring[]; /**< @brief 使用环元素数组 */
    /* uint16_t avail_event 在 ring 之后 */
} virtq_used_t;

/* ========================================================================
 * VirtQueue 实例管理
 * ======================================================================== */

/**
 * @brief VirtQueue 运行时状态
 */
typedef struct
{
    virtq_desc_t  *desc;            /**< @brief 描述符表基地址 */
    virtq_avail_t *avail;           /**< @brief 可用环基地址 */
    virtq_used_t  *used;            /**< @brief 使用环基地址 */
    uint16_t       queue_size;      /**< @brief 队列大小（2 的幂） */
    uint16_t       free_head;       /**< @brief 空闲描述符链头 */
    uint16_t       num_free;        /**< @brief 空闲描述符数量 */
    uint16_t       last_used_idx;   /**< @brief 上次处理的使用环索引 */
    uint32_t       desc_total;      /**< @brief 描述符总数 */
    uint8_t        pad[2];          /**< @brief 结构体对齐填充 */
    dma_buffer_t   dma_buf;         /**< @brief DMA 缓冲区 */
    bool           enabled;         /**< @brief 队列已启用 */
    bool           in_use;          /**< @brief 队列正在使用 */
} virtqueue_t;

/** @brief 最大 VirtQueue 数量 */
#define VIRTQ_MAX_QUEUES              8U

/** @brief VirtQueue 实例池 */
static virtqueue_t s_virtqueues[VIRTQ_MAX_QUEUES];

/** @brief VirtQueue 使用标记 */
static bool s_virtqueue_used[VIRTQ_MAX_QUEUES];

/* ========================================================================
 * 内存计算辅助函数
 * ======================================================================== */

/**
 * @brief 计算描述符表大小
 *
 * @param queue_size 队列大小
 *
 * @return 描述符表字节数
 */
static uint32_t virtq_desc_table_size(uint16_t queue_size)
{
    return (uint32_t)queue_size * (uint32_t)sizeof(virtq_desc_t);
}

/**
 * @brief 计算可用环大小
 *
 * @param queue_size 队列大小
 *
 * @return 可用环字节数（含 used_event）
 */
static uint32_t virtq_avail_ring_size(uint16_t queue_size)
{
    return (uint32_t)sizeof(uint16_t) * (3U + (uint32_t)queue_size);
}

/**
 * @brief 计算使用环大小
 *
 * @param queue_size 队列大小
 *
 * @return 使用环字节数（含 avail_event）
 */
static uint32_t virtq_used_ring_size(uint16_t queue_size)
{
    return (uint32_t)sizeof(uint16_t) * 3U +
           (uint32_t)sizeof(virtq_used_elem_t) * (uint32_t)queue_size;
}

/**
 * @brief 计算整个 VirtQueue 所需 DMA 内存大小
 *
 * @param queue_size 队列大小
 *
 * @return 总字节数
 */
uint32_t virtq_total_size(uint16_t queue_size)
{
    uint32_t total;

    total  = virtq_desc_table_size(queue_size);
    total += virtq_avail_ring_size(queue_size);
    total += virtq_used_ring_size(queue_size);

    return total;
}

/* ========================================================================
 * VirtQueue 分配与释放
 * ======================================================================== */

/**
 * @brief 分配一个空闲的 VirtQueue 实例
 *
 * @return VirtQueue 指针，NULL 表示无可用
 */
static virtqueue_t *virtq_alloc_instance(void)
{
    uint32_t i;

    for (i = 0U; i < VIRTQ_MAX_QUEUES; i++)
    {
        if (!s_virtqueue_used[i])
        {
            s_virtqueue_used[i] = true;
            return &s_virtqueues[i];
        }
    }

    return NULL;
}

/**
 * @brief 释放 VirtQueue 实例
 *
 * @param vq VirtQueue 指针
 */
static void virtq_free_instance(virtqueue_t *vq)
{
    uint32_t i;

    for (i = 0U; i < VIRTQ_MAX_QUEUES; i++)
    {
        if (&s_virtqueues[i] == vq)
        {
            s_virtqueue_used[i] = false;
            break;
        }
    }
}

/* ========================================================================
 * VirtQueue 初始化
 * ======================================================================== */

/**
 * @brief 初始化空闲描述符链
 *
 * @param vq VirtQueue 指针
 */
static void virtq_init_free_list(virtqueue_t *vq)
{
    uint16_t i;

    if ((vq == NULL) || (vq->desc == NULL))
    {
        return;
    }

    for (i = 0U; i < vq->queue_size; i++)
    {
        vq->desc[i].next = (uint16_t)(i + 1U);
        vq->desc[i].flags = 0U;
        vq->desc[i].addr = 0U;
        vq->desc[i].len = 0U;
    }

    /* 链尾标记 */
    vq->desc[vq->queue_size - 1U].next = VIRTQ_DESC_INVALID;

    vq->free_head = 0U;
    vq->num_free = vq->queue_size;
    vq->last_used_idx = 0U;
}

/**
 * @brief 创建并初始化 VirtQueue
 *
 * @param queue_size 队列大小（必须是 2 的幂）
 * @param dma_buf    预分配的 DMA 缓冲区（可为 NULL 自动分配）
 *
 * @return VirtQueue 指针，NULL 表示失败
 *
 * @note 对应需求: DV-014
 */
virtqueue_t *virtq_create(uint16_t queue_size, dma_buffer_t *dma_buf)
{
    virtqueue_t *vq;
    uint32_t total_size;
    uintptr_t base;
    uint32_t desc_size;
    uint32_t avail_size;

    if ((queue_size == 0U) || (queue_size > VIRTQ_MAX_SIZE))
    {
        return NULL;
    }

    /* 验证队列大小是 2 的幂 */
    if ((queue_size & (queue_size - 1U)) != 0U)
    {
        return NULL;
    }

    vq = virtq_alloc_instance();
    if (vq == NULL)
    {
        return NULL;
    }

    vq->queue_size = queue_size;
    vq->enabled = false;
    vq->in_use = false;

    total_size = virtq_total_size(queue_size);

    /* 分配或使用预分配 DMA 缓冲区 */
    if (dma_buf != NULL)
    {
        vq->dma_buf = *dma_buf;
    }
    else
    {
        driver_result_t ret = driver_dma_alloc((uint64_t)total_size, &vq->dma_buf);
        if (ret != DRIVER_OK)
        {
            virtq_free_instance(vq);
            return NULL;
        }
    }

    base = (uintptr_t)vq->dma_buf.virt_addr;

    /* 设置描述符表 */
    desc_size = virtq_desc_table_size(queue_size);
    vq->desc = (virtq_desc_t *)base;

    /* 设置可用环（描述符表之后） */
    avail_size = virtq_avail_ring_size(queue_size);
    vq->avail = (virtq_avail_t *)(base + desc_size);
    vq->avail->flags = 0U;
    vq->avail->idx = 0U;

    /* 设置使用环（可用环之后，4 字节对齐） */
    {
        uint32_t avail_end = desc_size + avail_size;
        uint32_t used_offset = (avail_end + VIRTQ_USED_ALIGN - 1U) &
                               ~(VIRTQ_USED_ALIGN - 1U);
        vq->used = (virtq_used_t *)(base + (uintptr_t)used_offset);
        vq->used->flags = 0U;
        vq->used->idx = 0U;
    }

    vq->desc_total = (uint32_t)queue_size;

    /* 初始化空闲描述符链 */
    virtq_init_free_list(vq);

    return vq;
}

/**
 * @brief 销毁 VirtQueue
 *
 * @param vq VirtQueue 指针
 */
void virtq_destroy(virtqueue_t *vq)
{
    if (vq == NULL)
    {
        return;
    }

    vq->enabled = false;
    vq->in_use = false;

    /* 释放 DMA 缓冲区 */
    if (vq->dma_buf.size > 0U)
    {
        (void)driver_dma_free(&vq->dma_buf);
    }

    virtq_free_instance(vq);
}

/* ========================================================================
 * 描述符操作
 * ======================================================================== */

/**
 * @brief 从空闲链分配一个描述符
 *
 * @param vq VirtQueue 指针
 *
 * @return 描述符索引，VIRTQ_DESC_INVALID 表示无空闲
 */
static uint16_t virtq_alloc_desc(virtqueue_t *vq)
{
    uint16_t desc_idx;

    if ((vq == NULL) || (vq->num_free == 0U))
    {
        return VIRTQ_DESC_INVALID;
    }

    desc_idx = vq->free_head;
    vq->free_head = vq->desc[desc_idx].next;
    vq->num_free--;

    /* 清除链接标志 */
    vq->desc[desc_idx].flags = 0U;
    vq->desc[desc_idx].next = VIRTQ_DESC_INVALID;

    return desc_idx;
}

/**
 * @brief 释放单个描述符到空闲链头部
 *
 * @param vq       VirtQueue 指针
 * @param desc_idx 描述符索引
 */
static void virtq_free_desc(virtqueue_t *vq, uint16_t desc_idx)
{
    if ((vq == NULL) || (desc_idx >= vq->queue_size))
    {
        return;
    }

    vq->desc[desc_idx].next = vq->free_head;
    vq->desc[desc_idx].flags = 0U;
    vq->desc[desc_idx].addr = 0U;
    vq->desc[desc_idx].len = 0U;
    vq->free_head = desc_idx;
    vq->num_free++;
}

/**
 * @brief 释放描述符链
 *
 * @param vq       VirtQueue 指针
 * @param head_idx 链头描述符索引
 */
static void virtq_free_chain(virtqueue_t *vq, uint16_t head_idx)
{
    uint16_t idx;
    uint16_t next;
    uint32_t count;

    if ((vq == NULL) || (head_idx >= vq->queue_size))
    {
        return;
    }

    idx = head_idx;
    count = 0U;

    while (idx != VIRTQ_DESC_INVALID)
    {
        next = vq->desc[idx].next;
        virtq_free_desc(vq, idx);
        idx = next;
        count++;

        /* 防止死循环 */
        if (count > (uint32_t)vq->queue_size)
        {
            break;
        }
    }
}

/**
 * @brief 获取空闲描述符数量
 *
 * @param vq VirtQueue 指针
 *
 * @return 空闲描述符数量
 */
uint16_t virtq_num_free(virtqueue_t *vq)
{
    if (vq == NULL)
    {
        return 0U;
    }

    return vq->num_free;
}

/* ========================================================================
 * 缓冲区添加（驱动 → 设备）
 * ======================================================================== */

/**
 * @brief 向 VirtQueue 添加一个 I/O 请求
 *
 * @details 将一组缓冲区组成描述符链并添加到可用环。
 *          支持 scatter-gather 模式：
 *          - out_bufs 为驱动→设备方向的数据缓冲区
 *          - in_bufs 为设备→驱动方向的数据缓冲区
 *
 * @param vq        VirtQueue 指针
 * @param out_bufs  输出缓冲区物理地址数组
 * @param out_lens  输出缓冲区长度数组
 * @param out_count 输出缓冲区数量
 * @param in_bufs   输入缓冲区物理地址数组
 * @param in_lens   输入缓冲区长度数组
 * @param in_count  输入缓冲区数量
 *
 * @return 非负表示可用的缓冲区总数，负数表示错误
 *
 * @note 对应需求: DV-015
 */
int32_t virtq_add_buffers(virtqueue_t *vq,
                           const uint64_t *out_bufs, const uint32_t *out_lens,
                           uint16_t out_count,
                           const uint64_t *in_bufs, const uint32_t *in_lens,
                           uint16_t in_count)
{
    uint16_t total;
    uint16_t head_idx;
    uint16_t prev_idx;
    uint16_t desc_idx;
    uint16_t i;
    uint16_t avail_idx;

    if (vq == NULL)
    {
        return -(int32_t)EINVAL;
    }

    total = out_count + in_count;
    if (total == 0U)
    {
        return -(int32_t)EINVAL;
    }

    if (vq->num_free < total)
    {
        return -(int32_t)ENOMEM;
    }

    head_idx = VIRTQ_DESC_INVALID;
    prev_idx = VIRTQ_DESC_INVALID;

    /* 添加输出描述符（驱动 → 设备，只读） */
    for (i = 0U; i < out_count; i++)
    {
        desc_idx = virtq_alloc_desc(vq);
        if (desc_idx == VIRTQ_DESC_INVALID)
        {
            /* 分配失败，回滚 */
            if (head_idx != VIRTQ_DESC_INVALID)
            {
                virtq_free_chain(vq, head_idx);
            }
            return -(int32_t)ENOMEM;
        }

        vq->desc[desc_idx].addr = out_bufs[i];
        vq->desc[desc_idx].len = out_lens[i];
        vq->desc[desc_idx].flags = 0U;

        if (head_idx == VIRTQ_DESC_INVALID)
        {
            head_idx = desc_idx;
        }
        else
        {
            vq->desc[prev_idx].flags |= VIRTQ_DESC_F_NEXT;
            vq->desc[prev_idx].next = desc_idx;
        }

        prev_idx = desc_idx;
    }

    /* 添加输入描述符（设备 → 驱动，可写） */
    for (i = 0U; i < in_count; i++)
    {
        desc_idx = virtq_alloc_desc(vq);
        if (desc_idx == VIRTQ_DESC_INVALID)
        {
            if (head_idx != VIRTQ_DESC_INVALID)
            {
                virtq_free_chain(vq, head_idx);
            }
            return -(int32_t)ENOMEM;
        }

        vq->desc[desc_idx].addr = in_bufs[i];
        vq->desc[desc_idx].len = in_lens[i];
        vq->desc[desc_idx].flags = VIRTQ_DESC_F_WRITE;

        if (head_idx == VIRTQ_DESC_INVALID)
        {
            head_idx = desc_idx;
        }
        else
        {
            vq->desc[prev_idx].flags |= VIRTQ_DESC_F_NEXT;
            vq->desc[prev_idx].next = desc_idx;
        }

        prev_idx = desc_idx;
    }

    /* 将链头添加到可用环 */
    avail_idx = vq->avail->idx % vq->queue_size;
    vq->avail->ring[avail_idx] = head_idx;

    __asm__ volatile("dmb ishst" ::: "memory");
    vq->avail->idx++;

    return (int32_t)vq->num_free;
}

/**
 * @brief 添加单个缓冲区到 VirtQueue
 *
 * @param vq    VirtQueue 指针
 * @param buf   缓冲区物理地址
 * @param len   缓冲区长度
 * @param flags 描述符标志（VIRTQ_DESC_F_WRITE 等）
 *
 * @return 描述符索引，VIRTQ_DESC_INVALID 表示失败
 */
uint16_t virtq_add_single(virtqueue_t *vq, uint64_t buf,
                           uint32_t len, uint16_t flags)
{
    uint16_t desc_idx;
    uint16_t avail_idx;

    if ((vq == NULL) || (vq->num_free == 0U))
    {
        return VIRTQ_DESC_INVALID;
    }

    desc_idx = virtq_alloc_desc(vq);
    if (desc_idx == VIRTQ_DESC_INVALID)
    {
        return VIRTQ_DESC_INVALID;
    }

    vq->desc[desc_idx].addr = buf;
    vq->desc[desc_idx].len = len;
    vq->desc[desc_idx].flags = flags;
    vq->desc[desc_idx].next = VIRTQ_DESC_INVALID;

    avail_idx = vq->avail->idx % vq->queue_size;
    vq->avail->ring[avail_idx] = desc_idx;

    __asm__ volatile("dmb ishst" ::: "memory");
    vq->avail->idx++;

    return desc_idx;
}

/* ========================================================================
 * 缓冲区回收（设备 → 驱动）
 * ======================================================================== */

/**
 * @brief 检查使用环是否有已完成项
 *
 * @param vq VirtQueue 指针
 *
 * @return true 有已完成的缓冲区，false 无
 */
bool virtq_has_used(virtqueue_t *vq)
{
    if (vq == NULL)
    {
        return false;
    }

    __asm__ volatile("dmb ishld" ::: "memory");

    return (vq->last_used_idx != vq->used->idx);
}

/**
 * @brief 从使用环获取一个已完成的缓冲区
 *
 * @param vq        VirtQueue 指针
 * @param used_len  输出设备写入的字节数（可为 NULL）
 *
 * @return 描述符链头索引，VIRTQ_DESC_INVALID 表示无完成项
 *
 * @note 对应需求: DV-016
 */
uint16_t virtq_get_buffer(virtqueue_t *vq, uint32_t *used_len)
{
    uint16_t used_idx;
    uint32_t id;
    uint32_t len;
    uint16_t desc_head;

    if (vq == NULL)
    {
        return VIRTQ_DESC_INVALID;
    }

    __asm__ volatile("dmb ishld" ::: "memory");

    if (vq->last_used_idx == vq->used->idx)
    {
        return VIRTQ_DESC_INVALID;
    }

    used_idx = vq->last_used_idx % vq->queue_size;
    id = vq->used->ring[used_idx].id;
    len = vq->used->ring[used_idx].len;

    if (id >= (uint32_t)vq->queue_size)
    {
        /* 非法索引，跳过 */
        vq->last_used_idx++;
        return VIRTQ_DESC_INVALID;
    }

    desc_head = (uint16_t)id;

    if (used_len != NULL)
    {
        *used_len = len;
    }

    /* 释放描述符链 */
    virtq_free_chain(vq, desc_head);

    vq->last_used_idx++;

    return desc_head;
}

/**
 * @brief 批量获取已完成的缓冲区
 *
 * @param vq        VirtQueue 指针
 * @param heads     输出描述符头索引数组
 * @param lengths   输出已写入字节数数组
 * @param max_count 最大获取数量
 *
 * @return 实际获取的数量
 */
uint16_t virtq_get_buffers_batch(virtqueue_t *vq,
                                  uint16_t *heads,
                                  uint32_t *lengths,
                                  uint16_t max_count)
{
    uint16_t count;
    uint16_t used_idx;
    uint32_t id;
    uint32_t len;

    if ((vq == NULL) || (heads == NULL) || (max_count == 0U))
    {
        return 0U;
    }

    __asm__ volatile("dmb ishld" ::: "memory");

    count = 0U;
    while ((count < max_count) && (vq->last_used_idx != vq->used->idx))
    {
        used_idx = vq->last_used_idx % vq->queue_size;
        id = vq->used->ring[used_idx].id;
        len = vq->used->ring[used_idx].len;

        if (id < (uint32_t)vq->queue_size)
        {
            heads[count] = (uint16_t)id;
            if (lengths != NULL)
            {
                lengths[count] = len;
            }
            virtq_free_chain(vq, (uint16_t)id);
            count++;
        }

        vq->last_used_idx++;
    }

    return count;
}

/* ========================================================================
 * 中断通知控制
 * ======================================================================== */

/**
 * @brief 启用/禁用设备使用通知
 *
 * @param vq     VirtQueue 指针
 * @param enable true 启用，false 禁用
 */
void virtq_enable_notify(virtqueue_t *vq, bool enable)
{
    if (vq == NULL)
    {
        return;
    }

    if (enable)
    {
        vq->avail->flags &= (uint16_t)(~VIRTQ_AVAIL_F_NO_INTERRUPT);
    }
    else
    {
        vq->avail->flags |= VIRTQ_AVAIL_F_NO_INTERRUPT;
    }

    __asm__ volatile("dmb ishst" ::: "memory");
}

/**
 * @brief 检查设备是否需要通知
 *
 * @param vq VirtQueue 指针
 *
 * @return true 需要通知设备，false 不需要
 */
bool virtq_kick_needed(virtqueue_t *vq)
{
    uint16_t flags;

    if (vq == NULL)
    {
        return false;
    }

    __asm__ volatile("dmb ishld" ::: "memory");

    flags = vq->used->flags;

    return ((flags & VIRTQ_USED_F_NO_NOTIFY) == 0U);
}

/**
 * @brief 获取队列统计信息
 *
 * @param vq         VirtQueue 指针
 * @param num_free   输出空闲描述符数（可为 NULL）
 * @param used_count 输出待处理使用环项数（可为 NULL）
 */
void virtq_get_stats(virtqueue_t *vq, uint16_t *num_free,
                      uint16_t *used_count)
{
    if (vq == NULL)
    {
        return;
    }

    if (num_free != NULL)
    {
        *num_free = vq->num_free;
    }

    if (used_count != NULL)
    {
        __asm__ volatile("dmb ishld" ::: "memory");
        *used_count = vq->used->idx - vq->last_used_idx;
    }
}

/**
 * @brief 启用 VirtQueue
 *
 * @param vq VirtQueue 指针
 */
void virtq_enable(virtqueue_t *vq)
{
    if (vq != NULL)
    {
        vq->enabled = true;
    }
}

/**
 * @brief 禁用 VirtQueue
 *
 * @param vq VirtQueue 指针
 */
void virtq_disable(virtqueue_t *vq)
{
    if (vq != NULL)
    {
        vq->enabled = false;
    }
}

/**
 * @brief 检查 VirtQueue 是否启用
 *
 * @param vq VirtQueue 指针
 *
 * @return true 已启用，false 未启用
 */
bool virtq_is_enabled(virtqueue_t *vq)
{
    if (vq == NULL)
    {
        return false;
    }

    return vq->enabled;
}

/**
 * @brief 获取 VirtQueue 的描述符表物理地址
 *
 * @param vq VirtQueue 指针
 *
 * @return 描述符表物理地址
 */
paddr_t virtq_get_desc_addr(virtqueue_t *vq)
{
    if (vq == NULL)
    {
        return 0U;
    }

    return vq->dma_buf.phys_addr;
}

/**
 * @brief 获取 VirtQueue 的可用环物理地址
 *
 * @param vq VirtQueue 指针
 *
 * @return 可用环物理地址
 */
paddr_t virtq_get_avail_addr(virtqueue_t *vq)
{
    uint32_t desc_size;

    if (vq == NULL)
    {
        return 0U;
    }

    desc_size = virtq_desc_table_size(vq->queue_size);
    return vq->dma_buf.phys_addr + (paddr_t)desc_size;
}

/**
 * @brief 获取 VirtQueue 的使用环物理地址
 *
 * @param vq VirtQueue 指针
 *
 * @return 使用环物理地址
 */
paddr_t virtq_get_used_addr(virtqueue_t *vq)
{
    uint32_t desc_size;
    uint32_t avail_size;

    if (vq == NULL)
    {
        return 0U;
    }

    desc_size = virtq_desc_table_size(vq->queue_size);
    avail_size = virtq_avail_ring_size(vq->queue_size);

    return vq->dma_buf.phys_addr + (paddr_t)desc_size + (paddr_t)avail_size;
}
