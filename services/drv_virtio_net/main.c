/**
 * @file    main.c
 * @brief   用户态 VirtIO Net 驱动
 * @author  AISafe64 Team
 * @date    2026-04-16
 * @version 1.0
 *
 * @details 用户态 VirtIO Net 网络驱动：
 *          - VirtIO MMIO Legacy 模式操作
 *          - RX/TX VirtQueue 管理
 *          - 网络数据包收发
 *          - 与网络协议栈集成
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: DV-024~027
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/config.h>
#include <kernel/syscall.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * VirtIO MMIO 寄存器定义
 * ======================================================================== */

/** @brief VirtIO MMIO Magic 值 0x74726976 ("virt") */
#define VIRTIO_MMIO_MAGIC_VALUE        0x74726976U

/** @brief VirtIO MMIO Version 1.0 (Legacy) */
#define VIRTIO_MMIO_VERSION            1U

/** @brief VirtIO 设备 ID: Network */
#define VIRTIO_NET_DEVICE_ID          1U

/** @brief VirtIO MMIO 寄存器偏移 */
#define VIRTIO_MMIO_MAGIC             0x000U
#define VIRTIO_MMIO_VERSION           0x004U
#define VIRTIO_MMIO_DEVICE_ID         0x008U
#define VIRTIO_MMIO_VENDOR_ID         0x00CU
#define VIRTIO_MMIO_DEVICE_FEATURES   0x010U
#define VIRTIO_MMIO_DRIVER_FEATURES   0x020U
#define VIRTIO_MMIO_QUEUE_SEL        0x030U
#define VIRTIO_MMIO_QUEUE_NUM_MAX    0x034U
#define VIRTIO_MMIO_QUEUE_NUM        0x038U
#define VIRTIO_MMIO_QUEUE_READY      0x044U
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050U
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060U
#define VIRTIO_MMIO_STATUS           0x070U
#define VIRTIO_MMIO_CONFIG           0x100U

/* Legacy 模式特有的 PFN 寄存器 */
#define VIRTIO_MMIO_QUEUE_PFN        0x040U

/** @brief VirtIO 设备状态位 */
#define VIRTIO_STATUS_ACKNOWLEDGE    0x01U
#define VIRTIO_STATUS_DRIVER         0x02U
#define VIRTIO_STATUS_DRIVER_OK      0x04U
#define VIRTIO_STATUS_FEATURES_OK    0x08U
#define VIRTIO_STATUS_FAILED         0x80U

/* ========================================================================
 * VirtQueue 定义
 * ======================================================================== */

/** @brief VirtQueue 描述符标志 */
#define VIRTQ_DESC_F_NEXT            (1U << 0)
#define VIRTQ_DESC_F_WRITE           (1U << 1)
#define VIRTQ_DESC_F_INDIRECT        (1U << 2)

/** @brief 无效描述符索引 */
#define VIRTQ_DESC_INVALID           0xFFFFU

/** @brief VirtQueue 大小 */
#define VIRTQ_QUEUE_SIZE            256U

/**
 * @brief VirtQueue 描述符
 */
typedef struct
{
    uint64_t addr;    /**< @brief 地址（64 位，物理地址） */
    uint32_t len;     /**< @brief 长度 */
    uint16_t flags;   /**< @brief 标志 */
    uint16_t next;    /**< @brief 下一个描述符索引 */
} __attribute__((packed)) virtq_desc_t;

/**
 * @brief VirtQueue Available Ring
 */
typedef struct
{
    uint16_t flags;   /**< @brief 标志 */
    uint16_t idx;     /**< @brief 索引 */
    uint16_t ring[VIRTQ_QUEUE_SIZE]; /**< @brief 环形缓冲区 */
    uint16_t used_event; /**< @brief 可用事件（可选） */
} __attribute__((packed)) virtq_avail_t;

/**
 * @brief VirtQueue Used Ring
 */
typedef struct
{
    uint16_t flags;   /**< @brief 标志 */
    uint16_t idx;     /**< @brief 索引 */
    struct
    {
        uint32_t id;  /**< @brief 描述符 ID */
        uint32_t len; /**< @brief 长度 */
    } ring[VIRTQ_QUEUE_SIZE]; /**< @brief 环形缓冲区 */
    uint16_t avail_event; /**< @brief 可用事件（可选） */
} __attribute__((packed)) virtq_used_t;

/* ========================================================================
 * VirtIO Net 配置
 * ======================================================================== */

/** @brief VirtIO Net MAC 地址长度 */
#define VIRTIO_NET_MAC_LEN          6U

/** @brief VirtIO Net MTU */
#define VIRTIO_NET_MTU              1514U

/**
 * @brief VirtIO Net 配置空间
 */
typedef struct
{
    uint8_t  mac[VIRTIO_NET_MAC_LEN]; /**< @brief MAC 地址 */
    uint16_t status;                   /**< @brief 状态 */
    uint16_t max_virtqueue_pairs;     /**< @brief 最大 VirtQueue 对数 */
    uint16_t mtu;                     /**< @brief MTU */
} __attribute__((packed)) virtio_net_config_t;

/* ========================================================================
 * 驱动私有数据
 * ======================================================================== */

/** @brief 驱动私有数据 */
typedef struct
{
    uint64_t               mmio_base;     /**< @brief MMIO 基地址 */
    uint32_t               irq;           /**< @brief IRQ 编号 */

    /* VirtQueue */
    virtq_desc_t           rx_desc[VIRTQ_QUEUE_SIZE]; /**< @brief RX 描述符表 */
    virtq_desc_t           tx_desc[VIRTQ_QUEUE_SIZE]; /**< @brief TX 描述符表 */
    virtq_avail_t          rx_avail;      /**< @brief RX Available Ring */
    virtq_avail_t          tx_avail;      /**< @brief TX Available Ring */
    virtq_used_t           rx_used;       /**< @brief RX Used Ring */
    virtq_used_t           tx_used;       /**< @brief TX Used Ring */

    uint8_t                rx_buffer[2048U] __attribute__((aligned(4096))); /**< @brief RX 缓冲区 */
    uint8_t                tx_buffer[2048U] __attribute__((aligned(4096))); /**< @brief TX 缓冲区 */

    uint16_t               rx_free_idx;   /**< @brief RX 空闲描述符索引 */
    uint16_t               tx_free_idx;   /**< @brief TX 空闲描述符索引 */
    uint16_t               rx_last_used;  /**< @brief RX 最后使用索引 */
    uint16_t               tx_last_used;  /**< @brief TX 最后使用索引 */

    virtio_net_config_t    config;        /**< @brief VirtIO Net 配置 */

    bool                   initialized;   /**< @brief 初始化标记 */
} virtio_net_priv_t;

/** @brief 全局驱动私有数据 */
static virtio_net_priv_t s_priv;

/* ========================================================================
 * MMIO 读写函数
 * ======================================================================== */

/**
 * @brief MMIO 读 32 位
 */
static inline uint32_t mmio_read32(uint64_t base, uint32_t offset)
{
    volatile uint32_t *reg = (volatile uint32_t *)(base + offset);
    return *reg;
}

/**
 * @brief MMIO 写 32 位
 */
static inline void mmio_write32(uint64_t base, uint32_t offset, uint32_t value)
{
    volatile uint32_t *reg = (volatile uint32_t *)(base + offset);
    *reg = value;
}

/* ========================================================================
 * 内部函数
 * ======================================================================== */

/**
 * @brief VirtIO 设备探测
 */
static int32_t virtio_net_probe(uint64_t mmio_base, uint32_t irq)
{
    uint32_t magic;
    uint32_t version;
    uint32_t device_id;
    uint32_t status;

    /* 检查 Magic 值 */
    magic = mmio_read32(mmio_base, VIRTIO_MMIO_MAGIC);
    if (magic != VIRTIO_MMIO_MAGIC_VALUE)
    {
        return -1; /* 非 VirtIO 设备 */
    }

    /* 检查版本 */
    version = mmio_read32(mmio_base, VIRTIO_MMIO_VERSION);
    if (version != VIRTIO_MMIO_VERSION)
    {
        return -2; /* 不支持的版本 */
    }

    /* 检查设备 ID */
    device_id = mmio_read32(mmio_base, VIRTIO_MMIO_DEVICE_ID);
    if (device_id != VIRTIO_NET_DEVICE_ID)
    {
        return -3; /* 非 VirtIO Net 设备 */
    }

    /* 清除状态 */
    mmio_write32(mmio_base, VIRTIO_MMIO_STATUS, 0U);

    /* ACKNOWLEDGE */
    status = VIRTIO_STATUS_ACKNOWLEDGE;
    mmio_write32(mmio_base, VIRTIO_MMIO_STATUS, status);

    /* DRIVER */
    status |= VIRTIO_STATUS_DRIVER;
    mmio_write32(mmio_base, VIRTIO_MMIO_STATUS, status);

    /* 保存配置 */
    (void)memset(&s_priv.config, 0U, sizeof(virtio_net_config_t));

    /* 读取 MAC 地址 */
    {
        uint32_t mac_low = mmio_read32(mmio_base, VIRTIO_MMIO_CONFIG);
        uint16_t mac_high = (uint16_t)mmio_read32(mmio_base, VIRTIO_MMIO_CONFIG + 4U);
        s_priv.config.mac[0] = (uint8_t)(mac_low & 0xFFU);
        s_priv.config.mac[1] = (uint8_t)((mac_low >> 8) & 0xFFU);
        s_priv.config.mac[2] = (uint8_t)((mac_low >> 16) & 0xFFU);
        s_priv.config.mac[3] = (uint8_t)((mac_low >> 24) & 0xFFU);
        s_priv.config.mac[4] = (uint8_t)(mac_high & 0xFFU);
        s_priv.config.mac[5] = (uint8_t)((mac_high >> 8) & 0xFFU);
    }

    s_priv.mmio_base = mmio_base;
    s_priv.irq = irq;

    return 0;
}

/**
 * @brief VirtQueue 初始化
 */
static int32_t virtio_virtq_init(uint16_t queue_sel, virtq_desc_t *desc_table,
                                  virtq_avail_t *avail_ring, virtq_used_t *used_ring)
{
    uint32_t queue_num_max;
    uint32_t pfn;

    /* 选择队列 */
    mmio_write32(s_priv.mmio_base, VIRTIO_MMIO_QUEUE_SEL, queue_sel);

    /* 检查队列是否存在 */
    queue_num_max = mmio_read32(s_priv.mmio_base, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (queue_num_max == 0U)
    {
        return -1; /* 队列不存在 */
    }

    /* 设置队列大小 */
    if (VIRTQ_QUEUE_SIZE > queue_num_max)
    {
        return -2; /* 队列大小过大 */
    }
    mmio_write32(s_priv.mmio_base, VIRTIO_MMIO_QUEUE_NUM, VIRTQ_QUEUE_SIZE);

    /* 设置队列地址（Legacy 模式：PFN） */
    pfn = (uint32_t)((uint64_t)desc_table >> 12U);
    mmio_write32(s_priv.mmio_base, VIRTIO_MMIO_QUEUE_PFN, pfn);

    /* 通知队列就绪 */
    mmio_write32(s_priv.mmio_base, VIRTIO_MMIO_QUEUE_READY, 1U);

    return 0;
}

/**
 * @brief VirtIO Net 驱动初始化
 */
static int32_t virtio_net_init(uint64_t mmio_base, uint32_t irq)
{
    int32_t ret;
    uint32_t status;

    /* 探测设备 */
    ret = virtio_net_probe(mmio_base, irq);
    if (ret != 0)
    {
        return ret;
    }

    /* 清零 VirtQueue 内存 */
    (void)memset(s_priv.rx_desc, 0U, sizeof(s_priv.rx_desc));
    (void)memset(s_priv.tx_desc, 0U, sizeof(s_priv.tx_desc));
    (void)memset(&s_priv.rx_avail, 0U, sizeof(s_priv.rx_avail));
    (void)memset(&s_priv.tx_avail, 0U, sizeof(s_priv.tx_avail));
    (void)memset(&s_priv.rx_used, 0U, sizeof(s_priv.rx_used));
    (void)memset(&s_priv.tx_used, 0U, sizeof(s_priv.tx_used));

    /* 初始化索引 */
    s_priv.rx_free_idx = 0U;
    s_priv.tx_free_idx = 0U;
    s_priv.rx_last_used = 0U;
    s_priv.tx_last_used = 0U;

    /* 初始化 RX VirtQueue (queue 0) */
    ret = virtio_virtq_init(0U, s_priv.rx_desc, &s_priv.rx_avail, &s_priv.rx_used);
    if (ret != 0)
    {
        return ret;
    }

    /* 初始化 TX VirtQueue (queue 1) */
    ret = virtio_virtq_init(1U, s_priv.tx_desc, &s_priv.tx_avail, &s_priv.tx_used);
    if (ret != 0)
    {
        return ret;
    }

    /* FEATURES_OK */
    status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
    mmio_write32(s_priv.mmio_base, VIRTIO_MMIO_STATUS, status);

    /* DRIVER_OK */
    status |= VIRTIO_STATUS_DRIVER_OK;
    mmio_write32(s_priv.mmio_base, VIRTIO_MMIO_STATUS, status);

    s_priv.initialized = true;

    return 0;
}

/* ========================================================================
 * 公共接口（网络协议栈调用）
 * ======================================================================== */

/**
 * @brief 发送网络数据包
 */
int64_t virtio_net_send(const void *buf, uint64_t size)
{
    (void)buf;
    (void)size;
    return -95; /* ENOTSUP - 待实现 */
}

/**
 * @brief 接收网络数据包
 */
int64_t virtio_net_recv(void *buf, uint64_t size)
{
    (void)buf;
    (void)size;
    return -95; /* ENOTSUP - 待实现 */
}

/* ========================================================================
 * 服务入口
 * ======================================================================== */

/**
 * @brief 用户态 VirtIO Net 驱动服务入口
 */
int32_t main(void)
{
    int32_t ret;

    /* QEMU virt 平台 VirtIO Net MMIO 地址 */
    const uint64_t mmio_base = 0x0A003C00ULL;
    const uint32_t irq = 78U;

    /* 初始化驱动 */
    ret = virtio_net_init(mmio_base, irq);
    if (ret != 0)
    {
        return ret;
    }

    /* TODO: 进入主循环，处理网络数据包 */

    return 0;
}
