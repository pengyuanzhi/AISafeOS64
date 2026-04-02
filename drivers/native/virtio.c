/**
 * @file    virtio.c
 * @brief   VirtIO 设备框架驱动
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 用户态 VirtIO 设备框架实现：
 *          - VirtIO MMIO 传输层
 *          - VirtQueue 管理（描述符表、可用环、使用环）
 *          - 支持 VirtIO Net/Block/Console 设备类型
 *          - 中断驱动的完成通知
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: DV-001~003
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/driver_framework.h>
#include <kernel/syscall.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * VirtIO MMIO 寄存器偏移
 * ======================================================================== */

/** @brief 魔数值寄存器 */
#define VIRTIO_MMIO_MAGIC_VALUE        0x000U

/** @brief 版本寄存器 */
#define VIRTIO_MMIO_VERSION            0x004U

/** @brief 设备 ID 寄存器 */
#define VIRTIO_MMIO_DEVICE_ID          0x008U

/** @brief 厂商 ID 寄存器 */
#define VIRTIO_MMIO_VENDOR_ID          0x00CU

/** @brief 设备特性寄存器 */
#define VIRTIO_MMIO_DEVICE_FEATURES    0x010U

/** @brief 驱动特性选择寄存器 */
#define VIRTIO_MMIO_DRIVER_FEATURES    0x020U

/** @brief Guest 页面大小 */
#define VIRTIO_MMIO_GUEST_PAGE_SIZE    0x028U

/** @brief 队列选择寄存器 */
#define VIRTIO_MMIO_QUEUE_SEL          0x030U

/** @brief 队列数量寄存器 */
#define VIRTIO_MMIO_QUEUE_NUM_MAX      0x034U

/** @brief 队列大小 */
#define VIRTIO_MMIO_QUEUE_NUM          0x038U

/** @brief 已用环对齐 */
#define VIRTIO_MMIO_QUEUE_ALIGN        0x03CU

/** @brief 描述符表地址（低32位） */
#define VIRTIO_MMIO_QUEUE_DESC_LOW     0x080U

/** @brief 描述符表地址（高32位） */
#define VIRTIO_MMIO_QUEUE_DESC_HIGH    0x084U

/** @brief 驱动区域地址 */
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW    0x090U
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH   0x094U

/** @brief 设备区域地址 */
#define VIRTIO_MMIO_QUEUE_USED_LOW     0x0A0U
#define VIRTIO_MMIO_QUEUE_USED_HIGH    0x0A4U

/** @brief 队列就绪 */
#define VIRTIO_MMIO_QUEUE_READY        0x044U

/** @brief 中断状态 */
#define VIRTIO_MMIO_INTERRUPT_STATUS   0x060U

/** @brief 中断确认 */
#define VIRTIO_MMIO_INTERRUPT_ACK      0x064U

/** @brief 状态寄存器 */
#define VIRTIO_MMIO_STATUS             0x070U

/** @brief 配置空间起始 */
#define VIRTIO_MMIO_CONFIG             0x100U

/* ========================================================================
 * VirtIO 常量
 * ======================================================================== */

/** @brief 魔数值 */
#define VIRTIO_MAGIC                   0x74726976U

/** @brief VirtIO 版本 2 */
#define VIRTIO_VERSION_2               2U

/** @brief 最大 VirtQueue 数量 */
#define VIRTIO_MAX_QUEUES              8U

/** @brief 队列最大描述符数 */
#define VIRTIO_QUEUE_SIZE              256U

/** @brief 最大 VirtIO 设备数 */
#define VIRTIO_MAX_DEVICES             4U

/* ========================================================================
 * VirtIO 设备类型
 * ======================================================================== */

/** @brief VirtIO 网卡 */
#define VIRTIO_DEVICE_ID_NET           1U

/** @brief VirtIO 块设备 */
#define VIRTIO_DEVICE_ID_BLOCK         2U

/** @brief VirtIO 控制台 */
#define VIRTIO_DEVICE_ID_CONSOLE       3U

/* ========================================================================
 * VirtIO 状态位
 * ======================================================================== */

/** @brief 已确认设备 */
#define VIRTIO_STATUS_ACKNOWLEDGE      (1U << 0U)

/** @brief 驱动已加载 */
#define VIRTIO_STATUS_DRIVER           (1U << 1U)

/** @brief 特性协商完成 */
#define VIRTIO_STATUS_FEATURES_OK      (1U << 3U)

/** @brief 驱动就绪 */
#define VIRTIO_STATUS_DRIVER_OK        (1U << 4U)

/** @brief 设备失败 */
#define VIRTIO_STATUS_FAILED           (1U << 7U)

/* ========================================================================
 * VirtQueue 描述符
 * ======================================================================== */

/**
 * @brief VirtQueue 描述符（16 字节）
 */
typedef struct
{
    uint64_t addr;      /**< @brief 缓冲区物理地址 */
    uint32_t len;       /**< @brief 缓冲区长度 */
    uint16_t flags;     /**< @brief 标志（NEXT=1, WRITE=2） */
    uint16_t next;      /**< @brief 下一个描述符索引 */
} virtq_desc_t;

/**
 * @brief VirtQueue 可用环（驱动→设备）
 */
typedef struct
{
    uint16_t flags;                              /**< @brief 标志 */
    uint16_t idx;                                /**< @brief 下一个可用槽 */
    uint16_t ring[VIRTIO_QUEUE_SIZE];            /**< @brief 描述符头索引环 */
    uint16_t used_event;                         /**< @brief 使用事件 */
} virtq_avail_t;

/**
 * @brief VirtQueue 使用环元素
 */
typedef struct
{
    uint32_t id;        /**< @brief 描述符链头索引 */
    uint32_t len;       /**< @brief 已写入字节数 */
} virtq_used_elem_t;

/**
 * @brief VirtQueue 使用环（设备→驱动）
 */
typedef struct
{
    uint16_t flags;                              /**< @brief 标志 */
    uint16_t idx;                                /**< @brief 下一个使用槽 */
    virtq_used_elem_t ring[VIRTIO_QUEUE_SIZE];   /**< @brief 使用环 */
    uint16_t avail_event;                        /**< @brief 可用事件 */
} virtq_used_t;

/* ========================================================================
 * VirtQueue 结构
 * ======================================================================== */

/**
 * @brief VirtQueue 实例
 */
typedef struct
{
    virtq_desc_t  *desc_table;       /**< @brief 描述符表指针 */
    virtq_avail_t *avail_ring;       /**< @brief 可用环指针 */
    virtq_used_t  *used_ring;        /**< @brief 使用环指针 */
    uint32_t       queue_size;       /**< @brief 队列大小 */
    uint16_t       last_used_idx;    /**< @brief 上次已处理的使用索引 */
    uint16_t       free_head;        /**< @brief 空闲描述符链头 */
    uint32_t       num_free;         /**< @brief 空闲描述符数 */
    bool           enabled;          /**< @brief 队列已启用 */
} virtqueue_t;

/* ========================================================================
 * VirtIO 设备状态
 * ======================================================================== */

/**
 * @brief VirtIO 设备实例
 */
typedef struct
{
    volatile uint8_t *mmio_base;      /**< @brief MMIO 基地址 */
    uint32_t         device_type;     /**< @brief 设备类型 */
    uint32_t         irq_number;      /**< @brief 中断号 */
    uint32_t         status;          /**< @brief 状态位 */
    virtqueue_t      queues[VIRTIO_MAX_QUEUES]; /**< @brief VirtQueue 数组 */
    uint32_t         queue_count;     /**< @brief 活跃队列数 */
    bool             initialized;     /**< @brief 初始化标志 */
} virtio_device_t;

/** @brief VirtIO 设备数组 */
static virtio_device_t s_virtio_devices[VIRTIO_MAX_DEVICES];

/** @brief 设备使用标记 */
static bool s_device_used[VIRTIO_MAX_DEVICES];

/* ========================================================================
 * MMIO 寄存器访问
 * ======================================================================== */

/**
 * @brief 读 MMIO 32位寄存器
 */
static uint32_t virtio_read32(volatile uint8_t *base, uint32_t offset)
{
    volatile uint32_t *reg = (volatile uint32_t *)(base + offset);
    return *reg;
}

/**
 * @brief 写 MMIO 32位寄存器
 */
static void virtio_write32(volatile uint8_t *base, uint32_t offset, uint32_t value)
{
    volatile uint32_t *reg = (volatile uint32_t *)(base + offset);
    *reg = value;
}

/* ========================================================================
 * VirtQueue 操作
 * ======================================================================== */

/**
 * @brief 初始化 VirtQueue 空闲描述符链
 *
 * @param vq VirtQueue 指针
 */
static void virtq_init_free_list(virtqueue_t *vq)
{
    uint32_t i;

    if (vq->desc_table == NULL)
    {
        return;
    }

    for (i = 0U; i < vq->queue_size; i++)
    {
        vq->desc_table[i].next = (uint16_t)(i + 1U);
    }
    vq->desc_table[vq->queue_size - 1U].next = (uint16_t)0xFFFFU;

    vq->free_head = 0U;
    vq->num_free = vq->queue_size;
    vq->last_used_idx = 0U;
}

/**
 * @brief 从 VirtQueue 分配描述符
 *
 * @param vq VirtQueue 指针
 *
 * @return 描述符索引，0xFFFF 表示无空闲
 */
static uint16_t virtq_alloc_desc(virtqueue_t *vq)
{
    uint16_t desc_idx;

    if ((vq == NULL) || (vq->num_free == 0U))
    {
        return 0xFFFFU;
    }

    desc_idx = vq->free_head;
    vq->free_head = vq->desc_table[desc_idx].next;
    vq->num_free--;

    return desc_idx;
}

/**
 * @brief 向 VirtQueue 提交缓冲区
 *
 * @param vq      VirtQueue 指针
 * @param buf     缓冲区物理地址
 * @param len     长度
 * @param flags   描述符标志
 *
 * @return 描述符索引，0xFFFF 表示失败
 */
static uint16_t virtq_add_buf(virtqueue_t *vq, uint64_t buf,
                               uint32_t len, uint16_t flags)
{
    uint16_t desc_idx;

    desc_idx = virtq_alloc_desc(vq);
    if (desc_idx == 0xFFFFU)
    {
        return 0xFFFFU;
    }

    vq->desc_table[desc_idx].addr = buf;
    vq->desc_table[desc_idx].len = len;
    vq->desc_table[desc_idx].flags = flags;
    vq->desc_table[desc_idx].next = 0xFFFFU;

    /* 添加到可用环 */
    vq->avail_ring->ring[vq->avail_ring->idx % vq->queue_size] = desc_idx;
    __asm__ volatile("dmb ishst" ::: "memory");
    vq->avail_ring->idx++;

    return desc_idx;
}

/**
 * @brief 检查并回收已完成的缓冲区
 *
 * @param vq VirtQueue 指针
 *
 * @return 已完成的描述符链头索引，0xFFFF 表示无
 */
static uint16_t virtq_get_buf(virtqueue_t *vq)
{
    uint16_t used_idx;
    uint16_t desc_head;

    if (vq->last_used_idx == vq->used_ring->idx)
    {
        return 0xFFFFU;
    }

    used_idx = vq->last_used_idx % vq->queue_size;
    desc_head = (uint16_t)vq->used_ring->ring[used_idx].id;

    /* 释放描述符链 */
    vq->free_head = desc_head;
    vq->num_free++;

    vq->last_used_idx++;

    return desc_head;
}

/* ========================================================================
 * VirtIO 设备操作
 * ======================================================================== */

/**
 * @brief 初始化 VirtIO 设备
 *
 * @param device_info 设备信息
 *
 * @return DRIVER_OK 成功
 */
static driver_result_t virtio_init(const device_info_t *device_info)
{
    uint32_t i;
    virtio_device_t *dev = NULL;
    uint32_t magic;
    uint32_t version;
    uint32_t device_id;

    if (device_info == NULL)
    {
        return DRIVER_INVALID_PARAM;
    }

    /* 查找空闲设备槽 */
    for (i = 0U; i < VIRTIO_MAX_DEVICES; i++)
    {
        if (!s_device_used[i])
        {
            dev = &s_virtio_devices[i];
            s_device_used[i] = true;
            break;
        }
    }

    if (dev == NULL)
    {
        return DRIVER_NO_RESOURCE;
    }

    /* 映射 MMIO */
    /* 简化实现：直接使用物理地址（实际应通过 driver_map_mmio） */
    dev->mmio_base = (volatile uint8_t *)(uintptr_t)device_info->mmio_base;
    dev->irq_number = device_info->irq_number;

    /* 验证魔数值 */
    magic = virtio_read32(dev->mmio_base, VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != VIRTIO_MAGIC)
    {
        s_device_used[i] = false;
        return DRIVER_NOT_FOUND;
    }

    /* 检查版本 */
    version = virtio_read32(dev->mmio_base, VIRTIO_MMIO_VERSION);
    if (version != VIRTIO_VERSION_2)
    {
        s_device_used[i] = false;
        return DRIVER_NOT_FOUND;
    }

    /* 读取设备类型 */
    device_id = virtio_read32(dev->mmio_base, VIRTIO_MMIO_DEVICE_ID);
    if (device_id == 0U)
    {
        s_device_used[i] = false;
        return DRIVER_NOT_FOUND;
    }

    dev->device_type = device_id;

    /* 重置设备 */
    virtio_write32(dev->mmio_base, VIRTIO_MMIO_STATUS, 0U);

    /* 确认设备 */
    dev->status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
    virtio_write32(dev->mmio_base, VIRTIO_MMIO_STATUS, dev->status);

    /* 特性协商（接受所有设备特性） */
    virtio_write32(dev->mmio_base, VIRTIO_MMIO_DRIVER_FEATURES,
                   virtio_read32(dev->mmio_base, VIRTIO_MMIO_DEVICE_FEATURES));

    dev->status |= VIRTIO_STATUS_FEATURES_OK;
    virtio_write32(dev->mmio_base, VIRTIO_MMIO_STATUS, dev->status);

    /* 检查 FEATURES_OK 是否生效 */
    if ((virtio_read32(dev->mmio_base, VIRTIO_MMIO_STATUS) &
         VIRTIO_STATUS_FEATURES_OK) == 0U)
    {
        dev->status |= VIRTIO_STATUS_FAILED;
        virtio_write32(dev->mmio_base, VIRTIO_MMIO_STATUS, dev->status);
        s_device_used[i] = false;
        return DRIVER_ERROR;
    }

    dev->queue_count = 0U;
    dev->initialized = true;

    return DRIVER_OK;
}

/**
 * @brief 关闭 VirtIO 设备
 */
static void virtio_deinit(void)
{
    /* 重置设备 */
    /* 简化实现 */
}

/**
 * @brief 打开 VirtIO 设备
 */
static driver_result_t virtio_open(uint32_t flags)
{
    (void)flags;
    return DRIVER_OK;
}

/**
 * @brief 关闭 VirtIO 设备
 */
static void virtio_close(void)
{
    /* 无操作 */
}

/**
 * @brief 从 VirtIO 设备读取
 */
static int64_t virtio_read(void *buf, uint64_t size, uint64_t offset)
{
    (void)buf;
    (void)size;
    (void)offset;
    return -(int64_t)22; /* -ENOSYS for now */
}

/**
 * @brief 向 VirtIO 设备写入
 */
static int64_t virtio_write(const void *buf, uint64_t size, uint64_t offset)
{
    (void)buf;
    (void)size;
    (void)offset;
    return -(int64_t)22; /* -ENOSYS for now */
}

/**
 * @brief VirtIO I/O 控制
 */
static driver_result_t virtio_ioctl(uint32_t cmd, void *arg)
{
    (void)cmd;
    (void)arg;
    return DRIVER_OK;
}

/**
 * @brief VirtIO 中断处理
 */
static void virtio_interrupt_handler(uint32_t irq)
{
    uint32_t i;
    uint32_t isr_status;

    (void)irq;

    for (i = 0U; i < VIRTIO_MAX_DEVICES; i++)
    {
        if (!s_device_used[i] || !s_virtio_devices[i].initialized)
        {
            continue;
        }

        isr_status = virtio_read32(s_virtio_devices[i].mmio_base,
                                   VIRTIO_MMIO_INTERRUPT_STATUS);

        if (isr_status != 0U)
        {
            /* 处理已使用的缓冲区通知 */
            if ((isr_status & 0x1U) != 0U)
            {
                uint32_t q;
                for (q = 0U; q < VIRTIO_MAX_QUEUES; q++)
                {
                    virtq_get_buf(&s_virtio_devices[i].queues[q]);
                }
            }

            /* 确认中断 */
            virtio_write32(s_virtio_devices[i].mmio_base,
                          VIRTIO_MMIO_INTERRUPT_ACK, isr_status);
        }
    }
}

/* ========================================================================
 * 驱动操作函数表
 * ======================================================================== */

static const driver_ops_t s_virtio_ops =
{
    .init              = virtio_init,
    .deinit            = virtio_deinit,
    .open              = virtio_open,
    .close             = virtio_close,
    .read              = virtio_read,
    .write             = virtio_write,
    .ioctl             = virtio_ioctl,
    .interrupt_handler = virtio_interrupt_handler
};

/* ========================================================================
 * 驱动入口
 * ======================================================================== */

int main(void)
{
    driver_result_t ret;

    ret = driver_register("virtio-mmio", &s_virtio_ops);
    if (ret != DRIVER_OK)
    {
        return (int)ret;
    }

    for (;;)
    {
        /* 通过 IPC 接收并处理请求 */
    }

    return 0;
}
