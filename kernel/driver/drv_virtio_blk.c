/**
 * @file    drv_virtio_blk.c
 * @brief   VirtIO Block 设备内核驱动 — 完整 virtqueue 块读写实现
 * @author  AISafe64 Team
 * @date    2026-04-09
 * @version 2.0
 *
 * @details VirtIO MMIO Block 设备驱动：
 *          - VirtIO MMIO 寄存器操作
 *          - 完整 VirtQueue 环形缓冲区管理（描述符链）
 *          - 同步块设备读写（轮询模式）
 *          - DMA 连续内存布局
 *
 * @note MISRA-C:2012 合规
 * @note 体系架构独立：所有硬件操作通过 MMIO 辅助函数和 HAL 接口
 * @note 对应需求: DV-020~023
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/driver.h>
#include <kernel/config.h>
#include <kernel/hal_irq.h>
#include <kernel/irq.h>
#include <kernel/ipc_notification.h>
#include <kernel/klog.h>
#include <sched/thread.h>
#include <hal.h>
#include <kernel/virt_phys.h>
#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * DMA 屏障：ARMv8 要求 DSB（而非 DMB）保证 cache 维护操作完成
 * dc cvac 之后必须使用 dsb ish 才能确保数据已刷到 PoC
 * ========================================================================

 */
#define virtio_dsb()   hal_dsb_ish()

/**
 * @brief WFI 指令：让出 CPU 给 QEMU 主循环处理异步 I/O
 *
 * @details QEMU TCG 模式下，WRITE 操作通过 blk_aio_pwritev 异步提交，
 *          完成回调作为 BH 调度。WFI 使 cpu_exec 退出，让主循环运行 BH，
 *          更新 used ring 后通过 GIC 中断唤醒 CPU。
 */
#define virtio_wfi()   hal_wfi()

#if CONFIG_DEBUG_VERBOSE
/**
 * @brief 调试用十六进制输出
 */
static void blk_dbg_hex(uint64_t value)
{
    klog_putc('0');
    klog_putc('x');
    klog_hex64(value);
}
#endif

/* ========================================================================
 * VirtIO MMIO 寄存器偏移
 * ======================================================================== */

#define VIRTIO_MMIO_MAGIC_VALUE     0x000U   /**< @brief Magic 值寄存器 */
#define VIRTIO_MMIO_VERSION         0x004U   /**< @brief 版本寄存器 */
#define VIRTIO_MMIO_DEVICE_ID       0x008U   /**< @brief 设备 ID 寄存器 */
#define VIRTIO_MMIO_VENDOR_ID       0x00CU   /**< @brief 厂商 ID 寄存器 */
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010U   /**< @brief 设备特性寄存器 */
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020U   /**< @brief 驱动特性寄存器 */
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028U   /**< @brief Guest 页大小 */
#define VIRTIO_MMIO_QUEUE_SEL       0x030U   /**< @brief 队列选择寄存器 */
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034U   /**< @brief 队列最大长度 */
#define VIRTIO_MMIO_QUEUE_NUM       0x038U   /**< @brief 队列长度 */
#define VIRTIO_MMIO_QUEUE_READY     0x044U   /**< @brief 队列就绪标志 */
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050U   /**< @brief 队列通知寄存器 */
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060U  /**< @brief 中断状态 */
#define VIRTIO_MMIO_INTERRUPT_ACK   0x064U   /**< @brief 中断确认 */
#define VIRTIO_MMIO_STATUS          0x070U   /**< @brief 设备状态寄存器 */
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080U   /**< @brief 描述符表低32位 */
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084U   /**< @brief 描述符表高32位 */
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090U   /**< @brief Available 环低32位 */
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094U  /**< @brief Available 环高32位 */
#define VIRTIO_MMIO_QUEUE_USED_LOW  0x0A0U   /**< @brief Used 环低32位 */
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0A4U   /**< @brief Used 环高32位 */
#define VIRTIO_MMIO_CONFIG          0x100U   /**< @brief 设备配置空间 */

/* ========================================================================
 * VirtIO 常量
 * ======================================================================== */

#define VIRTIO_MAGIC                0x74726976U   /**< @brief 'virt' 小端 */
#define VIRTIO_BLK_DEVICE_ID        2U            /**< @brief Block 设备类型 */
#define VIRTIO_BLK_SECTOR_SIZE      512U          /**< @brief 扇区大小 */

/** @brief VirtIO 状态位 */
#define VIRTIO_STATUS_ACKNOWLEDGE   0x01U
#define VIRTIO_STATUS_DRIVER        0x02U
#define VIRTIO_STATUS_FEATURES_OK   0x08U
#define VIRTIO_STATUS_DRIVER_OK     0x04U
#define VIRTIO_STATUS_FAILED        0x80U

/** @brief VirtQueue 描述符标志 */
#define VIRTQ_DESC_F_NEXT           (1U << 0U)   /**< @brief 链中下一个有效 */
#define VIRTQ_DESC_F_WRITE          (1U << 1U)   /**< @brief 设备可写 */

/** @brief 无效描述符索引 */
#define VIRTQ_DESC_INVALID          0xFFFFU

/** @brief VirtIO Block 请求类型 */
#define VIRTIO_BLK_T_IN             0U            /**< @brief 读请求 */
#define VIRTIO_BLK_T_OUT            1U            /**< @brief 写请求 */
#define VIRTIO_BLK_T_FLUSH          4U            /**< @brief 刷新请求 */

/** @brief VirtIO Block 响应状态 */
#define VIRTIO_BLK_S_OK             0U            /**< @brief 成功 */
#define VIRTIO_BLK_S_IOERR          1U            /**< @brief I/O 错误 */
#define VIRTIO_BLK_S_UNSUPP         2U            /**< @brief 不支持 */

/** @brief QEMU virt 平台 VirtIO MMIO SPI 中断号基址（slot 0 = IRQ 48） */
#define VIRTIO_MMIO_IRQ_BASE        48U

/* ========================================================================
 * VirtQueue 数据结构（内核内嵌）
 * ======================================================================== */

/** @brief VirtQueue 队列大小（必须是 2 的幂） */
#define VIRTQ_QUEUE_SIZE            8U

/** @brief 单次 I/O 最大扇区数 */
#define VIRTIO_BLK_MAX_SECTORS      8U

/** @brief 单次 I/O 最大字节数 (8 * 512 = 4096) */
#define VIRTIO_BLK_MAX_IO_SIZE      (VIRTIO_BLK_SECTOR_SIZE * VIRTIO_BLK_MAX_SECTORS)

/** @brief 轮询超时计数 */
#define VIRTQ_POLL_TIMEOUT          1000000U

/** @brief 完全禁用 WFI，只使用 MMIO 探针
 *         QEMU TCG 模式下，MMIO 探针强制退出翻译块并处理事件 */
#define VIRTQ_YIELD_THRESH          999999U

/**
 * @brief VirtQueue 描述符（16 字节）
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
 * @brief VirtIO Block 请求头（驱动 → 设备）
 */
typedef struct
{
    uint32_t type;      /**< @brief 请求类型 */
    uint32_t reserved;  /**< @brief 保留 */
    uint64_t sector;    /**< @brief 起始扇区号 */
} virtio_blk_req_hdr_t;

/**
 * @brief VirtIO Block 响应状态（设备 → 驱动）
 */
typedef struct
{
    uint8_t status;     /**< @brief 完成状态 */
} virtio_blk_resp_t;

/**
 * @brief VirtIO Block 完整请求（DMA 连续布局）
 *
 * @details 内存布局：req_hdr | data | resp
 *          描述符链：  desc[0]  desc[1] desc[2]
 */
typedef struct
{
    virtio_blk_req_hdr_t hdr;                           /**< @brief 请求头 */
    uint8_t              data[VIRTIO_BLK_MAX_IO_SIZE];  /**< @brief 数据缓冲区 */
    virtio_blk_resp_t    resp;                          /**< @brief 响应状态 */
} virtio_blk_request_t;

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

/**
 * @brief 描述符表大小
 */
#define VIRTQ_DESC_TABLE_SIZE   ((uint32_t)VIRTQ_QUEUE_SIZE * (uint32_t)sizeof(virtq_desc_t))

/**
 * @brief Available Ring 大小（header + ring[queue_size] + used_event）
 */
#define VIRTQ_AVAIL_RING_SIZE   (sizeof(virtq_avail_hdr_t) + \
                                 (uint32_t)VIRTQ_QUEUE_SIZE * sizeof(uint16_t) + \
                                 sizeof(uint16_t))

/**
 * @brief Used Ring 大小（header + ring[queue_size] + avail_event）
 */
#define VIRTQ_USED_RING_SIZE    (sizeof(virtq_used_hdr_t) + \
                                 (uint32_t)VIRTQ_QUEUE_SIZE * sizeof(virtq_used_elem_t) + \
                                 sizeof(uint16_t))

/** @brief 整个 VirtQueue 所需内存（页对齐） */
#define VIRTQ_TOTAL_SIZE        ((4096U + VIRTQ_USED_RING_SIZE + 4095U) & ~4095U)

/** @brief 驱动私有数据 */
typedef struct
{
    uint64_t    mmio_base;          /**< @brief VirtIO MMIO 基地址 */
    uint32_t    irq;                /**< @brief 中断号 */
    uint64_t    capacity;           /**< @brief 块设备容量（扇区数） */
    uint32_t    sector_size;        /**< @brief 扇区大小 */
    uint32_t    initialized;        /**< @brief 初始化标志 */

    /** @brief VirtQueue 内存（4KB 对齐） */
    uint8_t     vq_mem[VIRTQ_TOTAL_SIZE]
                        __attribute__((aligned(4096)));

    /** @brief VirtQueue 描述符表指针 */
    virtq_desc_t       *desc_table;

    /** @brief VirtQueue Available Ring 指针 */
    virtq_avail_hdr_t  *avail_ring;
    uint16_t           *avail_ring_entries;

    /** @brief VirtQueue Used Ring 指针 */
    virtq_used_hdr_t   *used_ring;
    virtq_used_elem_t  *used_ring_entries;

    /** @brief VirtQueue 运行时状态 */
    virtq_state_t      vq_state;

    /** @brief DMA 请求缓冲区 */
    virtio_blk_request_t req_buf
                        __attribute__((aligned(64)));

    /** @brief 完全中断驱动模式：通知对象 */
    kobj_id_t            notify_id;          /**< @brief 通知对象 ID */
    volatile bool        completed;          /**< @brief 请求完成标志 */

} virtio_blk_priv_t;

/** @brief VirtIO Block 驱动私有数据实例 */
static virtio_blk_priv_t s_blk_priv;

/* ========================================================================
 * 函数前向声明
 * ======================================================================== */
static kernel_status_t virtq_wait_completion(virtio_blk_priv_t *priv);
static kernel_status_t virtq_poll_completion(virtio_blk_priv_t *priv);

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
 *
 * @param priv 驱动私有数据指针
 */
static void virtq_init_free_list(virtio_blk_priv_t *priv)
{
    uint16_t i;
    virtq_desc_t *desc;
    desc = priv->desc_table;

    for (i = 0U; i < priv->vq_state.queue_size; i++)
    {
        desc[i].next = (uint16_t)(i + 1U);
        desc[i].flags = 0U;
        desc[i].addr = 0U;
        desc[i].len = 0U;
    }

    /* 链尾标记 */
    desc[priv->vq_state.queue_size - 1U].next = VIRTQ_DESC_INVALID;

    priv->vq_state.free_head = 0U;
    priv->vq_state.num_free = priv->vq_state.queue_size;
    priv->vq_state.last_used_idx = 0U;
}

/**
 * @brief 从空闲链分配一个描述符
 *
 * @param priv 驱动私有数据指针
 *
 * @return 描述符索引，VIRTQ_DESC_INVALID 表示无空闲
 */
static uint16_t virtq_alloc_desc(virtio_blk_priv_t *priv)
{
    uint16_t desc_idx;
    virtq_desc_t *desc;
    desc = priv->desc_table;

    if (priv->vq_state.num_free == 0U)
    {
        return VIRTQ_DESC_INVALID;
    }

    desc_idx = priv->vq_state.free_head;
    priv->vq_state.free_head = desc[desc_idx].next;
    priv->vq_state.num_free = (uint16_t)(priv->vq_state.num_free - 1U);

    desc[desc_idx].flags = 0U;
    desc[desc_idx].next = VIRTQ_DESC_INVALID;

    return desc_idx;
}

/**
 * @brief 释放描述符链回空闲链头部
 *
 * @param priv    驱动私有数据指针
 * @param head_idx 链头描述符索引
 */
static void virtq_free_chain(virtio_blk_priv_t *priv, uint16_t head_idx)
{
    uint16_t idx;
    uint16_t next;
    uint32_t count;
    virtq_desc_t *desc;
    desc = priv->desc_table;

    idx = head_idx;
    count = 0U;

    while (idx != VIRTQ_DESC_INVALID)
    {
        next = desc[idx].next;

        desc[idx].next = priv->vq_state.free_head;
        desc[idx].flags = 0U;
        desc[idx].addr = 0U;
        desc[idx].len = 0U;
        priv->vq_state.free_head = idx;
        priv->vq_state.num_free = (uint16_t)(priv->vq_state.num_free + 1U);

        idx = next;
        count++;

        /* 防止死循环 */
        if (count > (uint32_t)priv->vq_state.queue_size)
        {
            break;
        }
    }
}

/**
 * @brief 初始化 VirtQueue 内存布局
 *
 * @details 将静态内存区划分为描述符表、available ring、used ring
 *
 * @param priv 驱动私有数据指针
 * @param queue_size 队列大小
 */
static void virtq_setup_memory(virtio_blk_priv_t *priv, uint16_t queue_size)
{
    uint8_t *base;
    uint32_t offset;

    /* VirtIO MMIO Legacy 模式: vq_mem 必须从 4KB 边界开始 */
    base = (uint8_t *)(((uintptr_t)priv->vq_mem + 4095U) & ~4095ULL);
    priv->vq_state.queue_size = queue_size;

    /* 描述符表 */
    priv->desc_table = (virtq_desc_t *)(void *)base;
    offset = VIRTQ_DESC_TABLE_SIZE;

    /* Available Ring */
    priv->avail_ring = (virtq_avail_hdr_t *)(void *)(base + offset);
    priv->avail_ring_entries = (uint16_t *)(void *)(base + offset +
                              sizeof(virtq_avail_hdr_t));
    offset += VIRTQ_AVAIL_RING_SIZE;

    /* Used Ring: Legacy 模式必须 4KB 对齐 */
    offset = (offset + 4095U) & ~4095U;
    priv->used_ring = (virtq_used_hdr_t *)(void *)(base + offset);
    priv->used_ring_entries = (virtq_used_elem_t *)(void *)(base + offset +
                              sizeof(virtq_used_hdr_t));

    /* 初始化 ring 状态 */
    priv->avail_ring->flags = 0U;
    priv->avail_ring->idx = 0U;
    priv->used_ring->flags = 0U;
    priv->used_ring->idx = 0U;

    /* 初始化空闲描述符链 */
    virtq_init_free_list(priv);
}

/**
 * @brief VirtIO MMIO Legacy 寄存器偏移 (version 1)
 */
#define VIRTIO_MMIO_LEGACY_QUEUE_ALIGN   0x03CU   /**< @brief 队列对齐 */
#define VIRTIO_MMIO_LEGACY_QUEUE_PFN     0x040U   /**< @brief 队列页帧号 */

/**
 * @brief 将 VirtQueue 地址写入 MMIO 寄存器（支持 Legacy 和 Modern）
 *
 * @param priv 驱动私有数据指针
 */
static void virtq_write_mmio_regs(virtio_blk_priv_t *priv)
{
    uint64_t desc_addr;
    uint32_t version;

    desc_addr = virt_to_phys(priv->desc_table);

    /* 选择 queue 0 */
    mmio_write32(priv->mmio_base, VIRTIO_MMIO_QUEUE_SEL, 0U);

    /* 设置队列大小 */
    mmio_write32(priv->mmio_base, VIRTIO_MMIO_QUEUE_NUM,
                 (uint32_t)priv->vq_state.queue_size);

    /* DMA 一致性：清理 vring 内存（描述符表 + avail + used），
     * 确保设备看到初始化后的 ring 状态 */
    hal_dcache_clean((uint64_t)(uintptr_t)priv->vq_mem,
                     (uint64_t)VIRTQ_TOTAL_SIZE);
    virtio_dsb();

    /* 检测 VirtIO MMIO 版本 */
    version = mmio_read32(priv->mmio_base, VIRTIO_MMIO_VERSION);

    if (version == 1U)
    {
        /* Legacy mode: 使用 desc_table 地址传递页帧号
         *
         * QEMU virtio-mmio Legacy 模式使用 QUEUE_PFN 寄存器，
         * 页帧号必须是整个 vring（desc_table）的地址 >> 12（4KB 对齐）
         */
        uint32_t pfn = (uint32_t)(desc_addr >> 12U);

        /* 设置队列对齐 (4096) */
        mmio_write32(priv->mmio_base, VIRTIO_MMIO_LEGACY_QUEUE_ALIGN, 4096U);

        /* 写入页帧号（desc_table 的 PFN） */
        mmio_write32(priv->mmio_base, VIRTIO_MMIO_LEGACY_QUEUE_PFN, pfn);
    }
    else
    {
        /* Modern mode: 使用 DESC/AVAIL/USED 寄存器 */
        uint64_t avail_addr;
        uint64_t used_addr;

        avail_addr = virt_to_phys(priv->avail_ring);
        used_addr = virt_to_phys(priv->used_ring);

        mmio_write32(priv->mmio_base, VIRTIO_MMIO_QUEUE_DESC_LOW,
                     (uint32_t)(desc_addr & 0xFFFFFFFFU));
        mmio_write32(priv->mmio_base, VIRTIO_MMIO_QUEUE_DESC_HIGH,
                     (uint32_t)(desc_addr >> 32U));

        mmio_write32(priv->mmio_base, VIRTIO_MMIO_QUEUE_AVAIL_LOW,
                     (uint32_t)(avail_addr & 0xFFFFFFFFU));
        mmio_write32(priv->mmio_base, VIRTIO_MMIO_QUEUE_AVAIL_HIGH,
                     (uint32_t)(avail_addr >> 32U));

        mmio_write32(priv->mmio_base, VIRTIO_MMIO_QUEUE_USED_LOW,
                     (uint32_t)(used_addr & 0xFFFFFFFFU));
        mmio_write32(priv->mmio_base, VIRTIO_MMIO_QUEUE_USED_HIGH,
                     (uint32_t)(used_addr >> 32U));

        /* 标记队列就绪 */
        mmio_write32(priv->mmio_base, VIRTIO_MMIO_QUEUE_READY, 1U);
    }
}

/* ========================================================================
 * VirtQueue 提交与轮询
 * ======================================================================== */

/**
 * @brief 构建描述符链并提交到 Available Ring
 *
 * @details 构建三段描述符链：hdr(设备读) → data(设备读/写) → resp(设备写)
 *
 * @param priv     驱动私有数据指针
 * @param req_type 请求类型 (VIRTIO_BLK_T_IN / VIRTIO_BLK_T_OUT)
 * @param sector   起始扇区号
 * @param data_buf 数据缓冲区地址
 * @param data_len 数据长度（字节）
 *
 * @return KERNEL_OK 成功，负数错误码失败
 */
static kernel_status_t virtq_submit_request(virtio_blk_priv_t *priv,
                                             uint32_t req_type,
                                             uint64_t sector,
                                             uint32_t data_len)
{
    uint16_t desc_hdr;
    uint16_t desc_data;
    uint16_t desc_resp;
    uint16_t avail_idx;
    virtq_desc_t *desc;

    desc = priv->desc_table;

    if (priv->vq_state.num_free < 3U)
    {
        return -12; /* ENOMEM */
    }

    /* 分配 3 个描述符（已确保 num_free >= 3，无需检查返回值） */
    desc_hdr = virtq_alloc_desc(priv);
    desc_data = virtq_alloc_desc(priv);
    desc_resp = virtq_alloc_desc(priv);

    /* 描述符 0: 请求头（设备只读） */
    desc[desc_hdr].addr = virt_to_phys(&priv->req_buf.hdr);
    desc[desc_hdr].len = (uint32_t)sizeof(virtio_blk_req_hdr_t);
    desc[desc_hdr].flags = (uint16_t)(VIRTQ_DESC_F_NEXT);
    desc[desc_hdr].next = desc_data;

    /* 描述符 1: 数据缓冲区 — 使用 DMA 专用 req_buf.data
     * （读=设备写, 写=设备读） */
    desc[desc_data].addr = virt_to_phys(priv->req_buf.data);
    desc[desc_data].len = data_len;
    if (req_type == VIRTIO_BLK_T_IN)
    {
        /* 读请求：设备写入数据缓冲区 */
        desc[desc_data].flags = (uint16_t)(VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE);
    }
    else
    {
        /* 写请求：设备读取数据缓冲区 */
        desc[desc_data].flags = (uint16_t)(VIRTQ_DESC_F_NEXT);
    }
    desc[desc_data].next = desc_resp;

    /* 描述符 2: 响应状态（设备可写） */
    desc[desc_resp].addr = virt_to_phys(&priv->req_buf.resp);
    desc[desc_resp].len = (uint32_t)sizeof(virtio_blk_resp_t);
    desc[desc_resp].flags = (uint16_t)(VIRTQ_DESC_F_WRITE);
    desc[desc_resp].next = VIRTQ_DESC_INVALID;

    /* 填充请求头 */
    priv->req_buf.hdr.type = req_type;
    priv->req_buf.hdr.reserved = 0U;
    priv->req_buf.hdr.sector = sector;
    priv->req_buf.resp.status = 0xFFU;

#if CONFIG_DEBUG_VERBOSE
    klog_info("[BLK] submit type=");
    blk_dbg_hex((uint64_t)req_type);
    klog_info(" sector=");
    blk_dbg_hex(sector);
    klog_info(" len=");
    blk_dbg_hex((uint64_t)data_len);
    klog_info("\n");

    klog_info("[BLK]   desc[");
    blk_dbg_hex((uint64_t)desc_hdr);
    klog_info("] hdr  addr=");
    blk_dbg_hex(desc[desc_hdr].addr);
    klog_info(" len=");
    blk_dbg_hex((uint64_t)desc[desc_hdr].len);
    klog_info(" fl=");
    blk_dbg_hex((uint64_t)desc[desc_hdr].flags);
    klog_info(" nxt=");
    blk_dbg_hex((uint64_t)desc[desc_hdr].next);
    klog_info("\n");

    klog_info("[BLK]   desc[");
    blk_dbg_hex((uint64_t)desc_data);
    klog_info("] data addr=");
    blk_dbg_hex(desc[desc_data].addr);
    klog_info(" len=");
    blk_dbg_hex((uint64_t)desc[desc_data].len);
    klog_info(" fl=");
    blk_dbg_hex((uint64_t)desc[desc_data].flags);
    klog_info("\n");

    klog_info("[BLK]   desc[");
    blk_dbg_hex((uint64_t)desc_resp);
    klog_info("] resp addr=");
    blk_dbg_hex(desc[desc_resp].addr);
    klog_info(" fl=");
    blk_dbg_hex((uint64_t)desc[desc_resp].flags);
    klog_info("\n");

    klog_info("[BLK]   req_buf=");
    blk_dbg_hex((uint64_t)(uintptr_t)&priv->req_buf);
    klog_info(" desc_tbl=");
    blk_dbg_hex((uint64_t)(uintptr_t)priv->desc_table);
    klog_info("\n");
#endif

    /* DMA 一致性：清理描述符表，使设备可见 */
    hal_dcache_clean((uint64_t)(uintptr_t)priv->desc_table,
                     (uint64_t)VIRTQ_DESC_TABLE_SIZE);

    /* DMA 一致性：清理请求缓冲区（hdr + data + resp） */
    hal_dcache_clean((uint64_t)(uintptr_t)&priv->req_buf,
                     (uint64_t)sizeof(priv->req_buf));

    /* ARMv8: dc cvac 完成需要 dsb，dmb 不足以排序 cache 维护 */
    virtio_dsb();

    /* 添加到 Available Ring */
    avail_idx = priv->avail_ring->idx % priv->vq_state.queue_size;
    priv->avail_ring_entries[avail_idx] = desc_hdr;

    hal_dmb_ishst();
    priv->avail_ring->idx = (uint16_t)(priv->avail_ring->idx + 1U);

    /* DMA 一致性：清理 Available Ring，使设备可见 */
    hal_dcache_clean((uint64_t)(uintptr_t)priv->avail_ring,
                     (uint64_t)VIRTQ_AVAIL_RING_SIZE);

    /* 确保 avail ring 刷到内存后再 kick */
    virtio_dsb();

#if CONFIG_DEBUG_VERBOSE
    klog_info("[BLK]   avail_idx=");
    blk_dbg_hex((uint64_t)avail_idx);
    klog_info(" entry=");
    blk_dbg_hex((uint64_t)desc_hdr);
    klog_info(" ring_idx=");
    blk_dbg_hex((uint64_t)priv->avail_ring->idx);
    klog_info("\n");
#endif

    /* 通知设备（Kick queue 0） */
    mmio_write32(priv->mmio_base, VIRTIO_MMIO_QUEUE_NOTIFY, 0U);

    return KERNEL_OK;
}

/**
 * @brief 轮询 Used Ring 等待请求完成
 *
 * @details 使用 WFI 模式等待 VirtIO 设备完成请求。
 *          QEMU TCG 单线程模式下，WRITE 通过 blk_aio_pwritev 异步提交，
 *          完成 BH 仅在 cpu_exec() 返回 EXCP_HLT（WFI）后才能运行。
 *
 *          关键约束：QEMU helper_wfi() 通过 arm_cpu_exec_interrupt() 判断
 *          是否有挂起中断。若 PSTATE.I 置位（IRQ 屏蔽），QEMU 认为"无挂起
 *          中断"而永久停机。因此必须在轮询前临时使能 IRQ，使 GIC SPI 可被
 *          WFI 感知，轮询结束后恢复屏蔽。
 *
 *          工作流程：
 *          1. 临时使能 IRQ（清除 PSTATE.I）
 *          2. 检查 used ring 是否有新完成项
 *          3. 若无，执行 WFI 暂停 CPU
 *          4. QEMU 主循环运行 VirtIO BH，更新 used ring，
 *             通过 GIC SPI 唤醒 CPU
 *          5. CPU 唤醒后回到步骤 2
 *          6. 完成后恢复 IRQ 屏蔽
 *
 * @param priv 驱动私有数据指针
 *
 * @return KERNEL_OK 成功，负数超时或 I/O 错误
 */
/**
 * @brief 确保 notify 对象已创建（延迟初始化）
 *
 * @details 在第一次 I/O 操作时创建通知对象，避免在内核早期
 *          初始化阶段（调度器未就绪时）创建失败。
 *
 * @param priv 驱动私有数据指针
 *
 * @return KERNEL_OK 成功，负数错误码失败
 */
static kernel_status_t virtblk_ensure_notify(virtio_blk_priv_t *priv)
{
    if (priv->notify_id == 0U)
    {
        thread_id_t current_tid = kthread_get_current_tid();
        if (current_tid == THREAD_ID_INVALID)
        {
            /* 调度器未就绪，延迟创建 */
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

static kernel_status_t virtq_wait_completion(virtio_blk_priv_t *priv)
{
    uint16_t used_idx;
    uint32_t desc_id;
    uint64_t triggered;
    kernel_status_t ret;

    (void)desc_id;
    (void)used_idx;

    /* 确保通知对象已创建 */
    ret = virtblk_ensure_notify(priv);
    if (ret != KERNEL_OK)
    {
        /* 通知对象创建失败（例如调度器未就绪），回退到轮询模式 */
#if CONFIG_DEBUG_VERBOSE
        klog_info("[BLK] fallback to poll\n");
#endif
        return virtq_poll_completion(priv);
    }

#if CONFIG_DEBUG_VERBOSE
    klog_info("[BLK] wait start notify_id=");
    blk_dbg_hex((uint64_t)priv->notify_id);
    klog_info("\n");
#endif

    /* 初始化完成标志 */
    priv->completed = false;

    /* 阻塞等待中断通知 */
    ret = ipc_notification_wait(priv->notify_id, 1U, &triggered);
    if (ret != KERNEL_OK)
    {
#if CONFIG_DEBUG_VERBOSE
        klog_info("[BLK] wait failed ret=");
        blk_dbg_hex((uint64_t)ret);
        klog_info("\n");
#endif
        return ret;
    }

    /* 中断已唤醒，检查 Used Ring 处理完成 */
    used_idx = priv->vq_state.last_used_idx % priv->vq_state.queue_size;

    /* DMA 一致性：使 Used Ring 条目对 CPU 可见 */
    hal_dcache_invalidate(
        (uint64_t)(uintptr_t)&priv->used_ring_entries[used_idx],
        (uint64_t)sizeof(virtq_used_elem_t));
    virtio_dsb();

    desc_id = priv->used_ring_entries[used_idx].id;

#if CONFIG_DEBUG_VERBOSE
    klog_info("[BLK] wait done used_idx=");
    blk_dbg_hex((uint64_t)used_idx);
    klog_info(" desc_id=");
    blk_dbg_hex((uint64_t)desc_id);
    klog_info(" triggered=");
    blk_dbg_hex(triggered);
    klog_info("\n");
#endif

    /* 释放描述符链 */
    virtq_free_chain(priv, (uint16_t)desc_id);

    priv->vq_state.last_used_idx =
        (uint16_t)(priv->vq_state.last_used_idx + 1U);

    /* DMA 一致性：使响应数据和读缓冲区对 CPU 可见 */
    hal_dcache_invalidate((uint64_t)(uintptr_t)&priv->req_buf,
                          (uint64_t)sizeof(priv->req_buf));
    virtio_dsb();

    /* 检查响应状态 */
    if (priv->req_buf.resp.status == VIRTIO_BLK_S_OK)
    {
        return KERNEL_OK;
    }
    else
    {
#if CONFIG_DEBUG_VERBOSE
        klog_info("[BLK] resp status=");
        blk_dbg_hex((uint64_t)priv->req_buf.resp.status);
        klog_info("\n");
#endif

        /* 错误处理：根据 VirtIO 响应状态返回对应错误码 */
        if (priv->req_buf.resp.status == VIRTIO_BLK_S_IOERR)
        {
            return -5; /* EIO */
        }
        else if (priv->req_buf.resp.status == VIRTIO_BLK_S_UNSUPP)
        {
            return -95; /* ENOTSUP */
        }
        else
        {
            return -5; /* EIO */
        }
    }
}

/*
 * ========================================================================
 * 轮询模式实现（作为中断驱动模式的回退）
 * ========================================================================
 */
static kernel_status_t virtq_poll_completion(virtio_blk_priv_t *priv)
{
    uint32_t timeout;
    uint16_t used_idx;
    uint32_t desc_id;
    uint32_t saved_irq_state;

    timeout = 0U;

#if CONFIG_DEBUG_VERBOSE
    klog_info("[BLK] poll start last_used=");
    blk_dbg_hex((uint64_t)priv->vq_state.last_used_idx);
    klog_info("\n");
#endif

    /*
     * 保存 IRQ 状态并临时使能 IRQ（清除 PSTATE.I）
     */
    saved_irq_state = hal_local_irq_saved_state();
    hal_local_irq_enable();

    while (timeout < VIRTQ_POLL_TIMEOUT)
    {
        /* DMA 一致性：使 Used Ring 头部对 CPU 可见 */
        hal_dcache_invalidate((uint64_t)(uintptr_t)priv->used_ring,
                              (uint64_t)sizeof(virtq_used_hdr_t));
        virtio_dsb();

        if (priv->vq_state.last_used_idx != priv->used_ring->idx)
        {
            /* 有已完成的请求 */
            used_idx = priv->vq_state.last_used_idx % priv->vq_state.queue_size;

            /* DMA 一致性：使 Used Ring 条目对 CPU 可见 */
            hal_dcache_invalidate(
                (uint64_t)(uintptr_t)&priv->used_ring_entries[used_idx],
                (uint64_t)sizeof(virtq_used_elem_t));
            virtio_dsb();

            desc_id = priv->used_ring_entries[used_idx].id;

            /* 释放描述符链 */
            virtq_free_chain(priv, (uint16_t)desc_id);

            priv->vq_state.last_used_idx =
                (uint16_t)(priv->vq_state.last_used_idx + 1U);

            /* DMA 一致性：使响应数据和读缓冲区对 CPU 可见 */
            hal_dcache_invalidate((uint64_t)(uintptr_t)&priv->req_buf,
                                  (uint64_t)sizeof(priv->req_buf));
            virtio_dsb();

            /* 恢复 IRQ 状态 */
            hal_local_irq_restore(saved_irq_state);

            /* 检查响应状态 */
            if (priv->req_buf.resp.status == VIRTIO_BLK_S_OK)
            {
                return KERNEL_OK;
            }
            else
            {
                if (priv->req_buf.resp.status == VIRTIO_BLK_S_IOERR)
                {
                    return -5; /* EIO */
                }
                else if (priv->req_buf.resp.status == VIRTIO_BLK_S_UNSUPP)
                {
                    return -95; /* ENOTSUP */
                }
                else
                {
                    return -5; /* EIO */
                }
            }
        }

        /*
         * 优化轮询策略：
         */
        if (timeout < VIRTQ_YIELD_THRESH)
        {
            (void)mmio_read32(priv->mmio_base, VIRTIO_MMIO_INTERRUPT_STATUS);
        }
        else
        {
            virtio_wfi();
        }

        timeout++;
    }

    /* 恢复 IRQ 状态 */
    hal_local_irq_restore(saved_irq_state);

    return -110; /* ETIMEDOUT */
}

/* ========================================================================
 * 驱动操作函数实现
 * ======================================================================== */

/** @brief 前向声明：VirtIO Block 中断处理函数 */
static void virtio_blk_irq(uint32_t irq, void *dev_data);

/**
 * @brief 探测并初始化 VirtIO Block 设备
 *
 * @details 执行完整的 VirtIO MMIO 初始化：
 *          1. 验证 Magic 值和设备类型
 *          2. 状态转换: ACK → DRIVER → FEATURES_OK → DRIVER_OK
 *          3. 初始化 VirtQueue 环形缓冲区
 *          4. 读取设备容量
 *
 * @param dev_data 设备描述符指针
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t virtio_blk_probe(void *dev_data)
{
    device_desc_t *dev;
    uint32_t magic;
    uint32_t device_id;
    uint32_t status;
    uint32_t q_num_max;
    uint16_t queue_size;

    dev = (device_desc_t *)dev_data;

    if (dev == NULL)
    {
        return -22; /* EINVAL */
    }

    s_blk_priv.mmio_base = (uint64_t)dev->mmio_base;
    s_blk_priv.irq = dev->irq;
    s_blk_priv.sector_size = VIRTIO_BLK_SECTOR_SIZE;
    s_blk_priv.initialized = 0U;

    /* 调试: 打印 mmio_base (简化 hex) */
    /* 步骤 0: 扫描所有 virtio-mmio slot 找到 block 设备 (device_id=2) */
    {
        uint32_t slot;
        uint32_t found = 0U;
        for (slot = 0U; slot < 32U; slot++)
        {
            /* TTBR1 高地址线性映射：物理 0x0A000000 → 虚拟 0xFFFF00000A000000 */
            uint64_t addr = 0xFFFF00000A000000ULL + ((uint64_t)slot * 0x200ULL);
            uint32_t mg = mmio_read32(addr, 0x000U);
            uint32_t did = mmio_read32(addr, 0x008U);
            if ((mg == VIRTIO_MAGIC) && (did == VIRTIO_BLK_DEVICE_ID))
            {
                s_blk_priv.mmio_base = addr;
                /* 更新 IRQ 为实际 VirtIO MMIO SPI 中断号 */
                s_blk_priv.irq = VIRTIO_MMIO_IRQ_BASE + slot;
                found = 1U;
                break;
            }
        }
        if (found == 0U)
        {
            /* [BLK] no virtio-blk device found */
            return -6; /* ENXIO */
        }
    }

    /* 步骤 1: 验证 VirtIO Magic */
    magic = mmio_read32(s_blk_priv.mmio_base, VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != VIRTIO_MAGIC)
    {
        return -6; /* ENXIO */
    }

    /* 步骤 2: 检查设备类型 (block = 2) */
    device_id = mmio_read32(s_blk_priv.mmio_base, VIRTIO_MMIO_DEVICE_ID);
    if (device_id != VIRTIO_BLK_DEVICE_ID)
    {
        return -6; /* ENXIO */
    }

#if CONFIG_DEBUG_VERBOSE
    klog_info("[BLK] probe: OK\n");
#endif

    /* 步骤 3: VirtIO Legacy 模式初始化序列 */
    status = VIRTIO_STATUS_ACKNOWLEDGE;
    mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_STATUS, status);

    status |= VIRTIO_STATUS_DRIVER;
    mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_STATUS, status);

    /* 不协商任何特性（简化实现） */
    mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_DRIVER_FEATURES, 0U);

    /* Legacy 模式: 设置 Guest 页大小 (4KB)，必须在 QUEUE_PFN 之前 */
    mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096U);

    /* 步骤 4: 初始化 VirtQueue (必须在 FEATURES_OK 之前) */
    /* 查询设备支持的最大队列大小 */
    mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_QUEUE_SEL, 0U);
    q_num_max = mmio_read32(s_blk_priv.mmio_base, VIRTIO_MMIO_QUEUE_NUM_MAX);

    if (q_num_max == 0U)
    {
        mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_STATUS,
                     VIRTIO_STATUS_FAILED);
        return -5; /* EIO: 设备不支持任何队列 */
    }

    /* 取设备最大值和驱动期望值的较小者 */
    queue_size = VIRTQ_QUEUE_SIZE;
    if ((uint32_t)queue_size > q_num_max)
    {
        queue_size = (uint16_t)q_num_max;
    }

    /* 设置 VirtQueue 内存布局 */
    virtq_setup_memory(&s_blk_priv, queue_size);

    /* 将 VirtQueue 地址写入 MMIO 寄存器 */
    virtq_write_mmio_regs(&s_blk_priv);

#if CONFIG_DEBUG_VERBOSE
    klog_info("[BLK] virtq setup done\n");
    klog_info("[BLK]   desc=");
    blk_dbg_hex((uint64_t)(uintptr_t)s_blk_priv.desc_table);
    klog_info(" avail=");
    blk_dbg_hex((uint64_t)(uintptr_t)s_blk_priv.avail_ring);
    klog_info(" used=");
    blk_dbg_hex((uint64_t)(uintptr_t)s_blk_priv.used_ring);
    klog_info("\n");
#endif

    /* 步骤 5: 特性确认 */
    status |= VIRTIO_STATUS_FEATURES_OK;
    mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_STATUS, status);

    /* 验证 FEATURES_OK */
    if ((mmio_read32(s_blk_priv.mmio_base, VIRTIO_MMIO_STATUS)
         & VIRTIO_STATUS_FEATURES_OK) == 0U)
    {
        mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_STATUS,
                     VIRTIO_STATUS_FAILED);
        return -5; /* EIO */
    }

    /* 步骤 6: DRIVER_OK — 驱动就绪（队列配置完成后） */
    status |= VIRTIO_STATUS_DRIVER_OK;
    mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_STATUS, status);

    /* 步骤 7: 使能 VirtIO MMIO 中断（中断控制器配置 + 处理函数注册）
     * WRITE 操作在 QEMU TCG 中异步完成，需要中断唤醒 WFI 轮询 */
    hal_irq_set_priority(s_blk_priv.irq, (uint8_t)IRQ_PRIORITY_DEFAULT);
    hal_irq_set_affinity(s_blk_priv.irq, 0x01U);   /* 路由到 CPU 0 */
    hal_irq_enable(s_blk_priv.irq);
    (void)irq_register_handler(s_blk_priv.irq, virtio_blk_irq, NULL);

    /* 步骤 8: 读取设备容量 */
    s_blk_priv.capacity = (uint64_t)mmio_read32(
        s_blk_priv.mmio_base, VIRTIO_MMIO_CONFIG);
    s_blk_priv.capacity |= ((uint64_t)mmio_read32(
        s_blk_priv.mmio_base, VIRTIO_MMIO_CONFIG + 4U)) << 32U;

    s_blk_priv.initialized = 1U;

    /* 通知对象在第一次 I/O 操作时延迟创建（virtblk_ensure_notify） */

    return KERNEL_OK;
}

/**
 * @brief 移除 VirtIO Block 设备
 */
static kernel_status_t virtio_blk_remove(void *dev_data)
{
    (void)dev_data;

    if (s_blk_priv.initialized != 0U)
    {
        /* 销毁通知对象（完全中断驱动模式） */
        if (s_blk_priv.notify_id != 0U)
        {
            (void)ipc_notification_destroy(s_blk_priv.notify_id);
            s_blk_priv.notify_id = 0U;
        }

        mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_STATUS, 0U);
        s_blk_priv.initialized = 0U;
    }

    return KERNEL_OK;
}

/**
 * @brief 读取块设备数据
 *
 * @param dev_data 设备数据（未使用）
 * @param buf      输出缓冲区
 * @param size     读取字节数（必须 <= 4096，扇区对齐）
 * @param offset   字节偏移（必须扇区对齐）
 *
 * @return 实际读取的字节数，负数错误码
 */
static int64_t virtio_blk_read(void *dev_data, void *buf,
                                uint64_t size, uint64_t offset)
{
    kernel_status_t ret;
    uint64_t sector;
    uint32_t data_len;
    uint32_t i;
    uint8_t *dst;
    const uint8_t *src;

    (void)dev_data;

    if ((buf == NULL) || (size == 0U))
    {
        return -22; /* EINVAL */
    }

    if (s_blk_priv.initialized == 0U)
    {
        return -6; /* ENXIO */
    }

    if (size > VIRTIO_BLK_MAX_IO_SIZE)
    {
        return -22; /* EINVAL */
    }

    sector = offset / VIRTIO_BLK_SECTOR_SIZE;
    data_len = (uint32_t)size;

    /* 提交读请求（数据将写入 req_buf.data） */
    ret = virtq_submit_request(&s_blk_priv, VIRTIO_BLK_T_IN,
                                sector, data_len);
    if (ret != KERNEL_OK)
    {
        return (int64_t)ret;
    }

    /* 等待中断通知完成 */
    ret = virtq_wait_completion(&s_blk_priv);
    if (ret != KERNEL_OK)
    {
        return (int64_t)ret;
    }

    /* 从 DMA 缓冲区拷贝数据到调用者缓冲区 */
    dst = (uint8_t *)buf;
    src = s_blk_priv.req_buf.data;
    for (i = 0U; i < data_len; i++)
    {
        dst[i] = src[i];
    }

    return (int64_t)size;
}

/**
 * @brief 写入块设备数据
 *
 * @param dev_data 设备数据（未使用）
 * @param buf      输入缓冲区
 * @param size     写入字节数
 * @param offset   字节偏移
 *
 * @return 实际写入的字节数，负数错误码
 */
static int64_t virtio_blk_write(void *dev_data, const void *buf,
                                 uint64_t size, uint64_t offset)
{
    kernel_status_t ret;
    uint64_t sector;
    uint32_t data_len;
    uint32_t i;
    uint8_t *dst;
    const uint8_t *src;

    (void)dev_data;

    if ((buf == NULL) || (size == 0U))
    {
        return -22; /* EINVAL */
    }

    if (s_blk_priv.initialized == 0U)
    {
        return -6; /* ENXIO */
    }

    if (size > VIRTIO_BLK_MAX_IO_SIZE)
    {
        return -22; /* EINVAL */
    }

    sector = offset / VIRTIO_BLK_SECTOR_SIZE;
    data_len = (uint32_t)size;

    /* 将数据拷贝到 DMA 缓冲区 */
    dst = s_blk_priv.req_buf.data;
    src = (const uint8_t *)buf;
    for (i = 0U; i < data_len; i++)
    {
        dst[i] = src[i];
    }

    /* 提交写请求（数据从 req_buf.data 读取） */
    ret = virtq_submit_request(&s_blk_priv, VIRTIO_BLK_T_OUT,
                                sector, data_len);
    if (ret != KERNEL_OK)
    {
        return (int64_t)ret;
    }

    /* 等待中断通知完成 */
    ret = virtq_wait_completion(&s_blk_priv);
    if (ret != KERNEL_OK)
    {
        return (int64_t)ret;
    }

    return (int64_t)size;
}

/**
 * @brief VirtIO Block 设备控制命令
 *
 * @param cmd 0=GET_CAPACITY, 1=GET_SECTOR_SIZE, 2=GET_QUEUE_FREE
 * @param arg 输出参数指针
 */
static kernel_status_t virtio_blk_ioctl(void *dev_data, uint32_t cmd,
                                         void *arg)
{
    (void)dev_data;

    if (arg == NULL)
    {
        return -22;
    }

    if (cmd == 0U) /* GET_CAPACITY */
    {
        *((uint64_t *)arg) = s_blk_priv.capacity;
        return KERNEL_OK;
    }

    if (cmd == 1U) /* GET_SECTOR_SIZE */
    {
        *((uint32_t *)arg) = s_blk_priv.sector_size;
        return KERNEL_OK;
    }

    return -22;
}

/**
 * @brief VirtIO Block 中断处理函数（完全中断驱动模式）
 *
 * @details 读取中断状态，ACK 中断，触发通知唤醒等待线程
 */
static void virtio_blk_irq(uint32_t irq, void *dev_data)
{
    uint32_t int_status;
    (void)irq;
    (void)dev_data;

    int_status = mmio_read32(s_blk_priv.mmio_base, VIRTIO_MMIO_INTERRUPT_STATUS);
    if (int_status != 0U)
    {
        /* ACK 中断 */
        mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_INTERRUPT_ACK,
                     int_status);

        /* 触发通知唤醒等待线程 */
        if (s_blk_priv.notify_id != 0U)
        {
            (void)ipc_notification_signal(s_blk_priv.notify_id, 1U);
        }
    }
}

/* ========================================================================
 * 驱动操作函数表
 * ======================================================================== */

/** @brief VirtIO Block 驱动操作函数表 */
static const driver_ops_t s_drv_virtio_blk_ops =
{
    virtio_blk_probe,   /**< @brief probe */
    virtio_blk_remove,  /**< @brief remove */
    NULL,               /**< @brief suspend */
    NULL,               /**< @brief resume */
    virtio_blk_read,    /**< @brief read */
    virtio_blk_write,   /**< @brief write */
    virtio_blk_ioctl,   /**< @brief ioctl */
    virtio_blk_irq      /**< @brief irq_handler */
};

/* ========================================================================
 * 驱动注册入口
 * ======================================================================== */

/**
 * @brief 注册 VirtIO Block 驱动到驱动框架
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t drv_virtio_blk_register(void)
{
    driver_match_t match;
    uint32_t i;

    /* 清零 match 结构 */
    for (i = 0U; i < sizeof(match.compatible); i++)
    {
        match.compatible[i] = '\0';
    }

    /* compatible = "virtio,blk" */
    match.compatible[0U] = 'v';
    match.compatible[1U] = 'i';
    match.compatible[2U] = 'r';
    match.compatible[3U] = 't';
    match.compatible[4U] = 'i';
    match.compatible[5U] = 'o';
    match.compatible[6U] = ',';
    match.compatible[7U] = 'b';
    match.compatible[8U] = 'l';
    match.compatible[9U] = 'k';
    match.vendor_id = 0U;
    match.device_id = 0U;
    match.class_code = 0U;

    return driver_register_kern("virtio-blk", DRIVER_TYPE_BLOCK,
                                 &match, &s_drv_virtio_blk_ops);
}
