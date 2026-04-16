/**
 * @file    drv_virtio_net.c
 * @brief   VirtIO Net 设备内核驱动 — 完全中断驱动模式
 * @author  AISafe64 Team
 * @date    2026-04-16
 * @version 1.0
 *
 * @details VirtIO MMIO Net 设备驱动：
 *          - VirtIO MMIO 寄存器操作
 *          - RX/TX VirtQueue 环形缓冲区管理（双队列）
 *          - 网络数据包收发（完全中断驱动）
 *          - 与网络协议栈集成
 *
 * @note MISRA-C:2012 合规
 * @note 体系架构独立：所有硬件操作通过 MMIO 辅助函数和 HAL 接口
 * @note 对应需求: NW-001~005, DV-024~027
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/driver.h>
#include <kernel/config.h>
#include <kernel/gic.h>
#include <kernel/interrupt.h>
#include <kernel/ipc_notification.h>
#include <kernel/netstack.h>
#include <kernel/string.h>
#include <sched/thread.h>
#include <hal.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ========================================================================
 * DMA 屏障：ARMv8 要求 DSB（而非 DMB）保证 cache 维护操作完成
 * ======================================================================== */
#define virtio_dsb()   __asm__ volatile("dsb ish" ::: "memory")

/**
 * @brief WFI 指令：让出 CPU 给 QEMU 主循环处理异步 I/O
 */
#define virtio_wfi()   __asm__ volatile("wfi" ::: "memory")

/* ========================================================================
 * VirtIO MMIO 寄存器偏移
 * ======================================================================== */

#define VIRTIO_MMIO_MAGIC_VALUE     0x000U
#define VIRTIO_MMIO_VERSION         0x004U
#define VIRTIO_MMIO_DEVICE_ID       0x008U
#define VIRTIO_MMIO_VENDOR_ID       0x00CU
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010U
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020U
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028U
#define VIRTIO_MMIO_QUEUE_SEL       0x030U
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034U
#define VIRTIO_MMIO_QUEUE_NUM       0x038U
#define VIRTIO_MMIO_QUEUE_PFN       0x040U
#define VIRTIO_MMIO_QUEUE_READY     0x044U
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050U
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060U
#define VIRTIO_MMIO_INTERRUPT_ACK   0x064U
#define VIRTIO_MMIO_STATUS          0x070U
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080U
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084U
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090U
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094U
#define VIRTIO_MMIO_QUEUE_USED_LOW  0x0A0U
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0A4U
#define VIRTIO_MMIO_CONFIG          0x100U

/* ========================================================================
 * VirtIO 常量
 * ======================================================================== */

#define VIRTIO_MAGIC                0x74726976U   /* 'virt' */
#define VIRTIO_NET_DEVICE_ID        1U            /* Net 设备类型 */
#define VIRTIO_NET_HDR_LEN          10U           /* VirtIO Net 头部长度 */

/** @brief VirtIO 状态位 */
#define VIRTIO_STATUS_ACKNOWLEDGE   0x01U
#define VIRTIO_STATUS_DRIVER        0x02U
#define VIRTIO_STATUS_DRIVER_OK     0x04U
#define VIRTIO_STATUS_FEATURES_OK   0x08U
#define VIRTIO_STATUS_FAILED        0x80U

/** @brief VirtQueue 描述符标志 */
#define VIRTQ_DESC_F_NEXT           0x0001U
#define VIRTQ_DESC_F_WRITE          0x0002U
#define VIRTQ_DESC_F_INDIRECT       0x0004U

/** @brief VirtIO Net 设备特性 */
#define VIRTIO_NET_F_MAC            0x0001U  /* MAC 地址 */
#define VIRTIO_NET_F_STATUS         0x0010U  /* 状态 */

/** @brief 描述符链无效标记 */
#define VIRTQ_DESC_INVALID          0xFFFFU

/* ========================================================================
 * VirtQueue 配置
 * ======================================================================== */

#define VIRTQ_QUEUE_SIZE            256U      /* VirtQueue 大小 */

/* ========================================================================
 * VirtIO 网络配置（QEMU virtio-net-device）
 * ======================================================================== */

#define VIRTIO_MMIO_IRQ_BASE        48U       /* VirtIO MMIO SPI 基址 */
#define VIRTIO_NET_MAX_MTU          1514U     /* 最大 MTU */
#define VIRTIO_NET_MAX_PACKET_SIZE  2048U     /* 最大数据包大小 */

/* ========================================================================
 * 轮询配置（完全中断驱动模式的回退）
 * ======================================================================== */

#define VIRTQ_POLL_TIMEOUT          1000000U   /* 轮询超时 */
#define VIRTQ_YIELD_THRESH          999999U    /* 禁用 WFI，仅 MMIO 探针 */

/* ========================================================================
 * VirtQueue 描述符
 * ======================================================================== */

/**
 * @brief VirtQueue 描述符
 */
typedef struct
{
    uint64_t addr;      /**< @brief 缓冲区地址 */
    uint32_t len;       /**< @brief 缓冲区长度 */
    uint16_t flags;     /**< @brief 标志位 */
    uint16_t next;      /**< @brief 链中下一个描述符索引 */
} virtq_desc_t;

/**
 * @brief VirtQueue Available Ring 头部
 */
typedef struct
{
    uint16_t flags;     /**< @brief 标志 */
    uint16_t idx;       /**< @brief 下一个可用槽索引 */
} virtq_avail_hdr_t;

/**
 * @brief VirtQueue Used Ring 元素
 */
typedef struct
{
    uint32_t id;        /**< @brief 描述符链头索引 */
    uint32_t len;       /**< @brief 设备写入字节数 */
} virtq_used_elem_t;

/**
 * @brief VirtQueue Used Ring 头部
 */
typedef struct
{
    uint16_t flags;     /**< @brief 标志 */
    uint16_t idx;       /**< @brief 下一个写入槽索引 */
} virtq_used_hdr_t;

/**
 * @brief VirtIO Net 头部
 */
typedef struct
{
    uint8_t  flags;     /**< @brief 标志 */
    uint8_t  gso_type;  /**< @brief GSO 类型 */
    uint16_t hdr_len;   /**< @brief 头部长度 */
    uint16_t gso_size;  /**< @brief GSO 分段大小 */
    uint16_t csum_start; /**< @brief 校验和起始偏移 */
    uint16_t csum_offset; /**< @brief 校验和偏移 */
    uint16_t csum_flags; /**< @brief 校验和标志 */
} virtio_net_hdr_t;

/* ========================================================================
 * VirtQueue 运行时状态
 * ======================================================================== */

/**
 * @brief VirtQueue 运行时管理结构
 */
typedef struct
{
    uint16_t    free_head;      /**< @brief 空闲描述符链头 */
    uint16_t    num_free;       /**< @brief 空闲描述符数量 */
    uint16_t    last_used_idx;  /**< @brief 上次处理的 used ring 索引 */
    uint16_t    queue_size;     /**< @brief 队列大小 */
} virtq_state_t;

/* ========================================================================
 * VirtQueue 静态内存分配
 * ======================================================================== */

#define VIRTQ_DESC_TABLE_SIZE   ((uint32_t)VIRTQ_QUEUE_SIZE * (uint32_t)sizeof(virtq_desc_t))
#define VIRTQ_AVAIL_RING_SIZE   (sizeof(virtq_avail_hdr_t) + \
                                 (uint32_t)VIRTQ_QUEUE_SIZE * sizeof(uint16_t) + \
                                 sizeof(uint16_t))
#define VIRTQ_USED_RING_SIZE    (sizeof(virtq_used_hdr_t) + \
                                 (uint32_t)VIRTQ_QUEUE_SIZE * sizeof(virtq_used_elem_t) + \
                                 sizeof(uint16_t))
#define VIRTQ_TOTAL_SIZE        (VIRTQ_DESC_TABLE_SIZE + VIRTQ_AVAIL_RING_SIZE + \
                                 VIRTQ_USED_RING_SIZE + 4096U)

/* ========================================================================
 * VirtIO Net 驱动私有数据
 * ======================================================================== */

/**
 * @brief VirtIO Net 驱动私有数据
 */
typedef struct
{
    /** @brief MMIO 基地址 */
    uint64_t            mmio_base;

    /** @brief IRQ 号 */
    uint32_t            irq;

    /** @brief 驱动 ID */
    uint32_t            driver_id;

    /** @brief 网络接口 ID */
    int32_t             net_if_id;

    /** @brief MAC 地址 */
    net_mac_t           mac_addr;

    /** @brief MTU */
    uint32_t            mtu;

    /** @brief 初始化标志 */
    volatile uint32_t    initialized;

    /** @brief RX VirtQueue 内存（4KB 对齐） */
    uint8_t             rx_vq_mem[VIRTQ_TOTAL_SIZE]
                         __attribute__((aligned(4096)));

    /** @brief TX VirtQueue 内存（4KB 对齐） */
    uint8_t             tx_vq_mem[VIRTQ_TOTAL_SIZE]
                         __attribute__((aligned(4096)));

    /** @brief VirtQueue 描述符表指针（RX） */
    virtq_desc_t        *rx_desc_table;

    /** @brief VirtQueue Available Ring 指针（RX） */
    virtq_avail_hdr_t   *rx_avail_ring;
    uint16_t            *rx_avail_ring_entries;

    /** @brief VirtQueue Used Ring 指针（RX） */
    virtq_used_hdr_t    *rx_used_ring;
    virtq_used_elem_t   *rx_used_ring_entries;

    /** @brief VirtQueue 运行时状态（RX） */
    virtq_state_t       rx_vq_state;

    /** @brief VirtQueue 描述符表指针（TX） */
    virtq_desc_t        *tx_desc_table;

    /** @brief VirtQueue Available Ring 指针（TX） */
    virtq_avail_hdr_t   *tx_avail_ring;
    uint16_t            *tx_avail_ring_entries;

    /** @brief VirtQueue Used Ring 指针（TX） */
    virtq_used_hdr_t    *tx_used_ring;
    virtq_used_elem_t   *tx_used_ring_entries;

    /** @brief VirtQueue 运行时状态（TX） */
    virtq_state_t       tx_vq_state;

    /** @brief 完全中断驱动模式：通知对象 */
    kobj_id_t            notify_id;
    volatile bool        completed;

} virtio_net_priv_t;

/** @brief VirtIO Net 驱动私有数据实例 */
static virtio_net_priv_t s_net_priv;

/* ========================================================================
 * MMIO 寄存器访问辅助
 * ======================================================================== */

/**
 * @brief 从 MMIO 地址读取 32 位值
 */
static uint32_t mmio_read32(uint64_t base, uint32_t offset)
{
    volatile uint32_t *reg;
    reg = (volatile uint32_t *)(base + (uint64_t)offset);
    return *reg;
}

/**
 * @brief 向 MMIO 地址写入 32 位值
 */
static void mmio_write32(uint64_t base, uint32_t offset, uint32_t value)
{
    volatile uint32_t *reg;
    reg = (volatile uint32_t *)(base + (uint64_t)offset);
    *reg = value;
}

/* ========================================================================
 * VirtQueue 初始化
 * ======================================================================== */

/**
 * @brief 初始化空闲描述符链
 */
static void virtq_init_free_list(virtq_state_t *state, virtq_desc_t *desc,
                                  uint16_t queue_size)
{
    uint16_t i;

    for (i = 0U; i < queue_size; i++)
    {
        desc[i].next = (uint16_t)(i + 1U);
        desc[i].flags = 0U;
        desc[i].addr = 0U;
        desc[i].len = 0U;
    }

    desc[queue_size - 1U].next = VIRTQ_DESC_INVALID;

    state->free_head = 0U;
    state->num_free = queue_size;
    state->last_used_idx = 0U;
    state->queue_size = queue_size;
}

/**
 * @brief 从空闲链分配一个描述符
 */
static uint16_t virtq_alloc_desc(virtq_state_t *state, virtq_desc_t *desc)
{
    uint16_t desc_idx;

    if (state->num_free == 0U)
    {
        return VIRTQ_DESC_INVALID;
    }

    desc_idx = state->free_head;
    state->free_head = desc[desc_idx].next;
    state->num_free = (uint16_t)(state->num_free - 1U);

    desc[desc_idx].flags = 0U;
    desc[desc_idx].next = VIRTQ_DESC_INVALID;

    return desc_idx;
}

/**
 * @brief 释放描述符链回空闲链头部
 */
static void virtq_free_chain(virtq_state_t *state, virtq_desc_t *desc,
                              uint16_t head_idx)
{
    uint16_t idx;
    uint16_t next;
    uint32_t count = 0U;

    idx = head_idx;

    while (idx != VIRTQ_DESC_INVALID)
    {
        next = desc[idx].next;

        desc[idx].next = state->free_head;
        desc[idx].flags = 0U;
        desc[idx].addr = 0U;
        desc[idx].len = 0U;

        state->free_head = idx;
        state->num_free = (uint16_t)(state->num_free + 1U);

        idx = next;
        count++;

        if (count > state->queue_size)
        {
            /* 描述符链损坏，防止无限循环 */
            break;
        }
    }
}

/**
 * @brief 设置 VirtQueue 内存布局
 */
static void virtq_setup_memory(virtio_net_priv_t *priv, bool is_rx)
{
    uint8_t *vq_mem;
    uint64_t desc_addr;

    if (is_rx)
    {
        vq_mem = priv->rx_vq_mem;

        priv->rx_desc_table = (virtq_desc_t *)vq_mem;
        desc_addr = (uint64_t)(uintptr_t)vq_mem;
        priv->rx_avail_ring = (virtq_avail_hdr_t *)
            (vq_mem + VIRTQ_DESC_TABLE_SIZE);
        priv->rx_avail_ring_entries = (uint16_t *)
            (vq_mem + VIRTQ_DESC_TABLE_SIZE + sizeof(virtq_avail_hdr_t));
        priv->rx_used_ring = (virtq_used_hdr_t *)
            (vq_mem + VIRTQ_DESC_TABLE_SIZE + VIRTQ_AVAIL_RING_SIZE);
        priv->rx_used_ring_entries = (virtq_used_elem_t *)
            (vq_mem + VIRTQ_DESC_TABLE_SIZE + VIRTQ_AVAIL_RING_SIZE + \
             sizeof(virtq_used_hdr_t));

        (void)memset(priv->rx_vq_mem, 0U, VIRTQ_TOTAL_SIZE);
        virtq_init_free_list(&priv->rx_vq_state, priv->rx_desc_table, VIRTQ_QUEUE_SIZE);
    }
    else
    {
        vq_mem = priv->tx_vq_mem;

        priv->tx_desc_table = (virtq_desc_t *)vq_mem;
        desc_addr = (uint64_t)(uintptr_t)vq_mem;
        priv->tx_avail_ring = (virtq_avail_hdr_t *)
            (vq_mem + VIRTQ_DESC_TABLE_SIZE);
        priv->tx_avail_ring_entries = (uint16_t *)
            (vq_mem + VIRTQ_DESC_TABLE_SIZE + sizeof(virtq_avail_hdr_t));
        priv->tx_used_ring = (virtq_used_hdr_t *)
            (vq_mem + VIRTQ_DESC_TABLE_SIZE + VIRTQ_AVAIL_RING_SIZE);
        priv->tx_used_ring_entries = (virtq_used_elem_t *)
            (vq_mem + VIRTQ_DESC_TABLE_SIZE + VIRTQ_AVAIL_RING_SIZE + \
             sizeof(virtq_used_hdr_t));

        (void)memset(priv->tx_vq_mem, 0U, VIRTQ_TOTAL_SIZE);
        virtq_init_free_list(&priv->tx_vq_state, priv->tx_desc_table, VIRTQ_QUEUE_SIZE);
    }

    /* DMA 一致性：清理 VirtQueue 内存 */
    hal_dcache_clean((uint64_t)(uintptr_t)vq_mem, (uint64_t)VIRTQ_TOTAL_SIZE);
    virtio_dsb();
}

/**
 * @brief 写入 VirtQueue MMIO 寄存器（Legacy 模式）
 */
static void virtq_write_mmio_regs(virtio_net_priv_t *priv, bool is_rx)
{
    uint64_t desc_addr;
    uint32_t pfn;

    if (is_rx)
    {
        desc_addr = (uint64_t)(uintptr_t)priv->rx_desc_table;
    }
    else
    {
        desc_addr = (uint64_t)(uintptr_t)priv->tx_desc_table;
    }

    /* Legacy 模式：写入 PFN（页帧号）而非物理地址 */
    pfn = (uint32_t)(desc_addr >> 12U);

    /* 写入队列号 */
    mmio_write32(priv->mmio_base, VIRTIO_MMIO_QUEUE_SEL, is_rx ? 0U : 1U);

    /* Legacy 模式：设置 Guest 页大小（4KB） */
    mmio_write32(priv->mmio_base, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096U);

    /* Legacy 模式：写入 PFN */
    mmio_write32(priv->mmio_base, VIRTIO_MMIO_QUEUE_PFN, pfn);

    /* 写入队列大小 */
    mmio_write32(priv->mmio_base, VIRTIO_MMIO_QUEUE_NUM, VIRTQ_QUEUE_SIZE);

    /* 设置就绪标志 */
    mmio_write32(priv->mmio_base, VIRTIO_MMIO_QUEUE_READY, 1U);
}

/* ========================================================================
 * 中断驱动模式支持
 * ======================================================================== */

static kernel_status_t virtq_poll_completion(virtio_net_priv_t *priv, bool is_rx);
static kernel_status_t virtq_wait_completion(virtio_net_priv_t *priv, bool is_rx);

/**
 * @brief 确保通知对象已创建（延迟初始化）
 */
static kernel_status_t virtio_ensure_notify(virtio_net_priv_t *priv)
{
    if (priv->notify_id == 0U)
    {
        thread_id_t current_tid = kthread_get_current_tid();
        if (current_tid == THREAD_ID_INVALID)
        {
            return -12; /* ENOMEM */
        }

        kernel_status_t ret = ipc_notification_create(current_tid, &priv->notify_id);
        if (ret != KERNEL_OK)
        {
            return ret;
        }
    }

    return KERNEL_OK;
}

/**
 * @brief 等待 VirtIO 设备完成请求（完全中断驱动模式）
 */
static kernel_status_t virtq_wait_completion(virtio_net_priv_t *priv, bool is_rx)
{
    kernel_status_t ret;
    ret = virtio_ensure_notify(priv);
    if (ret != KERNEL_OK)
    {
        return virtq_poll_completion(priv, is_rx);
    }

    ret = ipc_notification_wait(priv->notify_id, 1U, NULL);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    return KERNEL_OK;
}

/**
 * @brief 轮询 VirtQueue 完成状态（回退模式）
 */
static kernel_status_t virtq_poll_completion(virtio_net_priv_t *priv, bool is_rx)
{
    (void)priv;
    (void)is_rx;
    return -110; /* ETIMEDOUT */
}

/* ========================================================================
 * 网络数据包收发
 * ======================================================================== */

/**
 * @brief 发送网络数据包
 *
 * @param buf      数据包缓冲区（以太网帧）
 * @param size     数据包大小
 *
 * @return 实际发送的字节数，负数错误码
 */
static int64_t virtio_net_tx_packet(const void *buf, uint64_t size)
{
    virtq_desc_t *desc;
    uint16_t desc_hdr;
    uint16_t desc_data;
    uint16_t avail_idx;
    kernel_status_t ret;
    uint8_t tx_buf[2048U] __attribute__((aligned(64)));
    virtio_net_hdr_t *net_hdr;

    (void)size;

    desc = s_net_priv.tx_desc_table;

    /* 检查 VirtQueue 是否有空闲描述符 */
    if (s_net_priv.tx_vq_state.num_free < 2U)
    {
        return -12; /* ENOMEM */
    }

    /* 检查数据包大小 */
    if (size > VIRTIO_NET_MAX_PACKET_SIZE)
    {
        return -22; /* EINVAL */
    }

    /* 分配 2 个描述符（VirtIO Net Header + Data） */
    desc_hdr = virtq_alloc_desc(&s_net_priv.tx_vq_state, s_net_priv.tx_desc_table);
    desc_data = virtq_alloc_desc(&s_net_priv.tx_vq_state, s_net_priv.tx_desc_table);

    /* 构造发送缓冲区（VirtIO Net Header + Ethernet Frame） */
    net_hdr = (virtio_net_hdr_t *)tx_buf;
    (void)memset(net_hdr, 0U, sizeof(virtio_net_hdr_t));

    /* 复制以太网帧 */
    (void)memcpy(tx_buf + sizeof(virtio_net_hdr_t), buf, size);

    /* 描述符 0: VirtIO Net Header（设备只读） */
    desc[desc_hdr].addr = (uint64_t)(uintptr_t)&s_net_priv.tx_vq_mem[0];
    desc[desc_hdr].len = sizeof(virtio_net_hdr_t);
    desc[desc_hdr].flags = VIRTQ_DESC_F_NEXT;
    desc[desc_hdr].next = desc_data;

    /* 描述符 1: Data（设备只读） */
    desc[desc_data].addr = (uint64_t)(uintptr_t)(&s_net_priv.tx_vq_mem[0] + sizeof(virtio_net_hdr_t));
    desc[desc_data].len = (uint32_t)size;
    desc[desc_data].flags = 0U; /* 不使用 NEXT，这是最后一个描述符 */
    desc[desc_data].next = VIRTQ_DESC_INVALID;

    /* DMA 一致性：清理发送缓冲区 */
    hal_dcache_clean((uint64_t)(uintptr_t)&s_net_priv.tx_vq_mem[0],
                      sizeof(virtio_net_hdr_t) + (uint32_t)size);
    virtio_dsb();

    /* 添加到 TX Available Ring */
    avail_idx = s_net_priv.tx_avail_ring->idx % s_net_priv.tx_vq_state.queue_size;
    s_net_priv.tx_avail_ring_entries[avail_idx] = desc_hdr;

    hal_dmb_ishst();
    s_net_priv.tx_avail_ring->idx = (uint16_t)(s_net_priv.tx_avail_ring->idx + 1U);

    /* DMA 一致性：清理 TX Available Ring */
    hal_dcache_clean((uint64_t)(uintptr_t)s_net_priv.tx_avail_ring,
                      VIRTQ_AVAIL_RING_SIZE);
    virtio_dsb();

    /* Kick TX 设备（queue 1） */
    mmio_write32(s_net_priv.mmio_base, VIRTIO_MMIO_QUEUE_NOTIFY, 1U);

    /* 等待完成 */
    ret = virtq_wait_completion(&s_net_priv, false);
    if (ret != KERNEL_OK)
    {
        return (int64_t)ret;
    }

    /* 处理 TX Used Ring */
    {
        uint16_t used_idx = s_net_priv.tx_vq_state.last_used_idx % s_net_priv.tx_vq_state.queue_size;
        uint32_t desc_id = s_net_priv.tx_used_ring_entries[used_idx].id;

        /* 释放描述符链 */
        virtq_free_chain(&s_net_priv.tx_vq_state, s_net_priv.tx_desc_table,
                          (uint16_t)desc_id);

        s_net_priv.tx_vq_state.last_used_idx =
            (uint16_t)(s_net_priv.tx_vq_state.last_used_idx + 1U);
    }

    /* 更新统计 */
    s_net_priv.tx_vq_state.queue_size = (uint16_t)(s_net_priv.tx_vq_state.queue_size + 1U);

    return (int64_t)size;
}

/**
 * @brief 接收网络数据包
 *
 * @param buf      输出缓冲区
 * @param size     缓冲区大小
 *
 * @return 实际接收的字节数，负数错误码
 */
static int64_t virtio_net_rx_packet(void *buf, uint64_t size)
{
    virtq_desc_t *desc;
    uint16_t desc_hdr;
    uint16_t desc_data;
    uint16_t avail_idx;
    kernel_status_t ret;
    uint16_t used_idx;
    uint32_t desc_id;
    uint32_t data_len;

    desc = s_net_priv.rx_desc_table;

    /* 检查 VirtQueue 是否有空闲描述符 */
    if (s_net_priv.rx_vq_state.num_free < 2U)
    {
        return -12; /* ENOMEM */
    }

    /* 分配 2 个描述符（VirtIO Net Header + Data） */
    desc_hdr = virtq_alloc_desc(&s_net_priv.rx_vq_state, s_net_priv.rx_desc_table);
    desc_data = virtq_alloc_desc(&s_net_priv.rx_vq_state, s_net_priv.rx_desc_table);

    /* 描述符 0: VirtIO Net Header（设备可写） */
    desc[desc_hdr].addr = (uint64_t)(uintptr_t)&s_net_priv.rx_vq_mem[0];
    desc[desc_hdr].len = sizeof(virtio_net_hdr_t);
    desc[desc_hdr].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
    desc[desc_hdr].next = desc_data;

    /* 描述符 1: Data Buffer（设备可写） */
    desc[desc_data].addr = (uint64_t)(uintptr_t)(&s_net_priv.rx_vq_mem[0] + sizeof(virtio_net_hdr_t));
    desc[desc_data].len = (uint32_t)size;
    desc[desc_data].flags = VIRTQ_DESC_F_WRITE; /* 不使用 NEXT，这是最后一个描述符 */
    desc[desc_data].next = VIRTQ_DESC_INVALID;

    /* DMA 一致性：清理描述符表 */
    hal_dcache_clean((uint64_t)(uintptr_t)s_net_priv.rx_desc_table,
                      VIRTQ_DESC_TABLE_SIZE);
    virtio_dsb();

    /* 添加到 RX Available Ring */
    avail_idx = s_net_priv.rx_avail_ring->idx % s_net_priv.rx_vq_state.queue_size;
    s_net_priv.rx_avail_ring_entries[avail_idx] = desc_hdr;

    hal_dmb_ishst();
    s_net_priv.rx_avail_ring->idx = (uint16_t)(s_net_priv.rx_avail_ring->idx + 1U);

    /* DMA 一致性：清理 RX Available Ring */
    hal_dcache_clean((uint64_t)(uintptr_t)s_net_priv.rx_avail_ring,
                      VIRTQ_AVAIL_RING_SIZE);
    virtio_dsb();

    /* Kick RX 设备（queue 0） */
    mmio_write32(s_net_priv.mmio_base, VIRTIO_MMIO_QUEUE_NOTIFY, 0U);

    /* 等待完成 */
    ret = virtq_wait_completion(&s_net_priv, true);
    if (ret != KERNEL_OK)
    {
        return (int64_t)ret;
    }

    /* DMA 一致性：使接收缓冲区对 CPU 可见 */
    hal_dcache_invalidate((uint64_t)(uintptr_t)&s_net_priv.rx_vq_mem[0],
                          sizeof(virtio_net_hdr_t) + (uint32_t)size);
    virtio_dsb();

    /* 处理 RX Used Ring */
    used_idx = s_net_priv.rx_vq_state.last_used_idx % s_net_priv.rx_vq_state.queue_size;
    desc_id = s_net_priv.rx_used_ring_entries[used_idx].id;
    data_len = s_net_priv.rx_used_ring_entries[used_idx].len;

    /* 复制以太网帧（跳过 VirtIO Net Header） */
    if (data_len > sizeof(virtio_net_hdr_t))
    {
        uint32_t pkt_len = data_len - sizeof(virtio_net_hdr_t);
        if (pkt_len > size)
        {
            pkt_len = (uint32_t)size;
        }
        (void)memcpy(buf, &s_net_priv.rx_vq_mem[0] + sizeof(virtio_net_hdr_t), pkt_len);

        /* 释放描述符链 */
        virtq_free_chain(&s_net_priv.rx_vq_state, s_net_priv.rx_desc_table,
                          (uint16_t)desc_id);

        s_net_priv.rx_vq_state.last_used_idx =
            (uint16_t)(s_net_priv.rx_vq_state.last_used_idx + 1U);

        /* 更新统计 */
        s_net_priv.rx_vq_state.queue_size = (uint16_t)(s_net_priv.rx_vq_state.queue_size + 1U);

        return (int64_t)pkt_len;
    }

    /* 释放描述符链 */
    virtq_free_chain(&s_net_priv.rx_vq_state, s_net_priv.rx_desc_table,
                      (uint16_t)desc_id);

    s_net_priv.rx_vq_state.last_used_idx =
        (uint16_t)(s_net_priv.rx_vq_state.last_used_idx + 1U);

    return -5; /* EIO */
}

/* ========================================================================
 * 驱动操作函数实现
 * ======================================================================== */

/**
 * @brief VirtIO Net 中断处理函数
 */
static void virtio_net_irq(uint32_t irq, void *dev_data)
{
    uint32_t int_status;
    (void)irq;
    (void)dev_data;

    int_status = mmio_read32(s_net_priv.mmio_base, VIRTIO_MMIO_INTERRUPT_STATUS);
    if (int_status != 0U)
    {
        /* ACK 中断 */
        mmio_write32(s_net_priv.mmio_base, VIRTIO_MMIO_INTERRUPT_ACK,
                     int_status);

        /* 触发通知唤醒等待线程 */
        if (s_net_priv.notify_id != 0U)
        {
            (void)ipc_notification_signal(s_net_priv.notify_id, 1U);
        }
    }
}

/**
 * @brief 探测并初始化 VirtIO Net 设备
 */
static kernel_status_t virtio_net_probe(void *dev_data)
{
    device_desc_t *dev;
    uint32_t magic;
    uint32_t device_id;
    uint32_t status;
    uint32_t q_num_max;

    dev = (device_desc_t *)dev_data;

    if (dev == NULL)
    {
        return -22; /* EINVAL */
    }

    s_net_priv.mmio_base = (uint64_t)dev->mmio_base;
    s_net_priv.irq = dev->irq;
    s_net_priv.driver_id = dev->drv_id;
    s_net_priv.initialized = 0U;
    s_net_priv.notify_id = 0U;
    s_net_priv.mtu = VIRTIO_NET_MAX_MTU;

    /* 步骤 1: 验证 VirtIO Magic */
    magic = mmio_read32(s_net_priv.mmio_base, VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != VIRTIO_MAGIC)
    {
        return -6; /* ENXIO */
    }

    /* 步骤 2: 检查设备类型 (net = 1) */
    device_id = mmio_read32(s_net_priv.mmio_base, VIRTIO_MMIO_DEVICE_ID);
    if (device_id != VIRTIO_NET_DEVICE_ID)
    {
        return -6; /* ENXIO */
    }

    /* 步骤 3: VirtIO Legacy 模式初始化序列 */
    status = VIRTIO_STATUS_ACKNOWLEDGE;
    mmio_write32(s_net_priv.mmio_base, VIRTIO_MMIO_STATUS, status);

    status |= VIRTIO_STATUS_DRIVER;
    mmio_write32(s_net_priv.mmio_base, VIRTIO_MMIO_STATUS, status);

    /* 不协商任何特性（简化实现） */
    mmio_write32(s_net_priv.mmio_base, VIRTIO_MMIO_DRIVER_FEATURES, 0U);

    /* 步骤 4: 初始化 RX/TX VirtQueue */
    mmio_write32(s_net_priv.mmio_base, VIRTIO_MMIO_QUEUE_SEL, 0U);
    q_num_max = mmio_read32(s_net_priv.mmio_base, VIRTIO_MMIO_QUEUE_NUM_MAX);

    if (q_num_max == 0U)
    {
        mmio_write32(s_net_priv.mmio_base, VIRTIO_MMIO_STATUS,
                     VIRTIO_STATUS_FAILED);
        return -5; /* EIO */
    }

    /* 设置 RX VirtQueue */
    virtq_setup_memory(&s_net_priv, true);
    virtq_write_mmio_regs(&s_net_priv, true);

    /* 设置 TX VirtQueue */
    virtq_setup_memory(&s_net_priv, false);
    virtq_write_mmio_regs(&s_net_priv, false);

    /* 步骤 5: 特性确认 */
    status |= VIRTIO_STATUS_FEATURES_OK;
    mmio_write32(s_net_priv.mmio_base, VIRTIO_MMIO_STATUS, status);

    if ((mmio_read32(s_net_priv.mmio_base, VIRTIO_MMIO_STATUS)
         & VIRTIO_STATUS_FEATURES_OK) == 0U)
    {
        mmio_write32(s_net_priv.mmio_base, VIRTIO_MMIO_STATUS,
                     VIRTIO_STATUS_FAILED);
        return -5; /* EIO */
    }

    /* 步骤 6: DRIVER_OK */
    status |= VIRTIO_STATUS_DRIVER_OK;
    mmio_write32(s_net_priv.mmio_base, VIRTIO_MMIO_STATUS, status);

    /* 步骤 7: 使能 VirtIO Net 中断 */
    (void)gic_set_priority(s_net_priv.irq, (uint8_t)GIC_PRIORITY_DEFAULT);
    (void)gic_set_target(s_net_priv.irq, 0x01U);
    (void)gic_enable_irq(s_net_priv.irq);
    (void)interrupt_register(s_net_priv.irq, virtio_net_irq, NULL);

    /* 步骤 8: 读取 MAC 地址 */
    {
        uint32_t mac_low = mmio_read32(s_net_priv.mmio_base, VIRTIO_MMIO_CONFIG);
        uint32_t mac_high = mmio_read32(s_net_priv.mmio_base, VIRTIO_MMIO_CONFIG + 4U);

        s_net_priv.mac_addr.bytes[0] = (uint8_t)(mac_low);
        s_net_priv.mac_addr.bytes[1] = (uint8_t)(mac_low >> 8U);
        s_net_priv.mac_addr.bytes[2] = (uint8_t)(mac_low >> 16U);
        s_net_priv.mac_addr.bytes[3] = (uint8_t)(mac_low >> 24U);
        s_net_priv.mac_addr.bytes[4] = (uint8_t)(mac_high);
        s_net_priv.mac_addr.bytes[5] = (uint8_t)(mac_high >> 8U);
    }

    /* 步骤 9: 注册到网络协议栈（暂时注释，待网络协议栈实现） */
#if 0
    s_net_priv.net_if_id = net_register_interface("eth0", NET_LINK_ETHERNET,
                                                    &s_net_priv.mac_addr,
                                                    s_net_priv.driver_id);
    if (s_net_priv.net_if_id < 0)
    {
        /* 网络协议栈未实现，但驱动仍然可以工作 */
        s_net_priv.net_if_id = -1;
    }
#else
    (void)s_net_priv.driver_id;
    s_net_priv.net_if_id = -1; /* 待实现 */
#endif

    s_net_priv.initialized = 1U;

    return KERNEL_OK;
}

/**
 * @brief 移除 VirtIO Net 设备
 */
static kernel_status_t virtio_net_remove(void *dev_data)
{
    (void)dev_data;

    if (s_net_priv.initialized != 0U)
    {
        /* 销毁通知对象 */
        if (s_net_priv.notify_id != 0U)
        {
            (void)ipc_notification_destroy(s_net_priv.notify_id);
            s_net_priv.notify_id = 0U;
        }

        mmio_write32(s_net_priv.mmio_base, VIRTIO_MMIO_STATUS, 0U);
        s_net_priv.initialized = 0U;
    }

    return KERNEL_OK;
}

/**
 * @brief VirtIO Net 设备控制命令
 */
static kernel_status_t virtio_net_ioctl(void *dev_data, uint32_t cmd, void *arg)
{
    (void)dev_data;
    (void)cmd;
    (void)arg;
    return -95; /* ENOTSUP */
}

/**
 * @brief VirtIO Net 数据收发（网络接口操作）
 */
static int64_t virtio_net_read(void *dev_data, void *buf,
                                uint64_t size, uint64_t offset)
{
    (void)dev_data;
    (void)offset;

    if (s_net_priv.initialized == 0U)
    {
        return -6; /* ENXIO */
    }

    if ((buf == NULL) || (size == 0U))
    {
        return -22; /* EINVAL */
    }

    if (size > VIRTIO_NET_MAX_PACKET_SIZE)
    {
        return -22; /* EINVAL */
    }

    /* 接收网络数据包 */
    return virtio_net_rx_packet(buf, size);
}

static int64_t virtio_net_write(void *dev_data, const void *buf,
                                 uint64_t size, uint64_t offset)
{
    (void)dev_data;
    (void)offset;

    if (s_net_priv.initialized == 0U)
    {
        return -6; /* ENXIO */
    }

    if ((buf == NULL) || (size == 0U))
    {
        return -22; /* EINVAL */
    }

    if (size > VIRTIO_NET_MAX_PACKET_SIZE)
    {
        return -22; /* EINVAL */
    }

    /* 发送网络数据包 */
    return virtio_net_tx_packet(buf, size);
}

/* ========================================================================
 * 驱动操作函数表
 * ======================================================================== */

static const driver_ops_t s_drv_virtio_net_ops =
{
    virtio_net_probe,   /**< @brief probe */
    virtio_net_remove,  /**< @brief remove */
    NULL,               /**< @brief suspend */
    NULL,               /**< @brief resume */
    virtio_net_read,    /**< @brief read */
    virtio_net_write,   /**< @brief write */
    virtio_net_ioctl,   /**< @brief ioctl */
    virtio_net_irq      /**< @brief irq_handler */
};

/* ========================================================================
 * 驱动注册入口
 * ======================================================================== */

/**
 * @brief 注册 VirtIO Net 驱动到驱动框架
 */
kernel_status_t drv_virtio_net_register(void)
{
    driver_match_t match;

    /* 清零 match 结构 */
    (void)memset(&match, 0U, sizeof(match));

    /* 设置兼容字符串（VirtIO Net 设备） */
    (void)strncpy(match.compatible, "virtio,net", sizeof(match.compatible) - 1U);
    match.compatible[sizeof(match.compatible) - 1U] = '\0';

    /* 注册驱动 */
    return driver_register_kern("virtio-net", DRIVER_TYPE_NET, &match, &s_drv_virtio_net_ops);
}
