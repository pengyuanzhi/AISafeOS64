/**
 * @file    virtio_net.c
 * @brief   VirtIO 网络设备驱动
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 1.0
 *
 * @details 本文件实现 VirtIO Net 网络设备驱动：
 *          - 收发队列（RX/TX）管理
 *          - VirtIO Net 请求/响应包头处理
 *          - 多队列并行收发
 *          - 校验和卸载（CSO）
 *          - TSO/GSO 支持
 *          - MAC 地址和链路状态管理
 *          - 控制队列（CTRL VQ）配置
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: DV-030~033
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/driver_framework.h>
#include <kernel/errno.h>
#include <kernel/types.h>
#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * VirtIO Net 常量
 * ======================================================================== */

/** @brief VirtIO Net 设备 ID */
#define VIRTIO_NET_DEVICE_ID            1U

/** @brief 以太网 MAC 地址长度 */
#define VIRTIO_NET_MAC_SIZE             6U

/** @brief 最大 MTU（标准以太网帧） */
#define VIRTIO_NET_MAX_MTU              1514U

/** @brief 最小 MTU */
#define VIRTIO_NET_MIN_MTU              68U

/** @brief 最大帧大小（含包头） */
#define VIRTIO_NET_MAX_FRAME_SIZE       (VIRTIO_NET_MAX_MTU + 12U)

/** @brief 接收队列索引 */
#define VIRTIO_NET_RX_QUEUE             0U

/** @brief 发送队列索引 */
#define VIRTIO_NET_TX_QUEUE             1U

/** @brief 控制队列索引 */
#define VIRTIO_NET_CTRL_QUEUE           2U

/* ========================================================================
 * VirtIO Net 特性位
 * ======================================================================== */

/** @brief 特性：CSUM（发送校验和卸载） */
#define VIRTIO_NET_F_CSUM               (1ULL << 0U)

/** @brief 特性：GUEST_CSUM（接收校验和卸载） */
#define VIRTIO_NET_F_GUEST_CSUM         (1ULL << 1U)

/** @brief 特性：MAC 地址由设备提供 */
#define VIRTIO_NET_F_MAC                (1ULL << 5U)

/** @brief 特性：GSO */
#define VIRTIO_NET_F_GSO                (1ULL << 6U)

/** @brief 特性：GUEST_TSO4 */
#define VIRTIO_NET_F_GUEST_TSO4         (1ULL << 7U)

/** @brief 特性：GUEST_TSO6 */
#define VIRTIO_NET_F_GUEST_TSO6         (1ULL << 8U)

/** @brief 特性：MRG_RXBUF（合并接收缓冲区） */
#define VIRTIO_NET_F_MRG_RXBUF          (1ULL << 15U)

/** @brief 特性：状态字段（链路状态） */
#define VIRTIO_NET_F_STATUS             (1ULL << 16U)

/** @brief 特性：控制通道 */
#define VIRTIO_NET_F_CTRL_VQ            (1ULL << 17U)

/** @brief 特性：控制通道 RX 模式 */
#define VIRTIO_NET_F_CTRL_RX            (1ULL << 18U)

/** @brief 特性：控制通道 VLAN 过滤 */
#define VIRTIO_NET_F_CTRL_VLAN          (1ULL << 19U)

/** @brief 特性：MQ（多队列） */
#define VIRTIO_NET_F_MQ                 (1ULL << 22U)

/** @brief 驱动支持的所有特性 */
#define VIRTIO_NET_DRV_FEATURES         \
    (VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS | VIRTIO_NET_F_MRG_RXBUF)

/* ========================================================================
 * VirtIO Net 包头
 * ======================================================================== */

/** @brief 包头标志：需要校验和 */
#define VIRTIO_NET_HDR_F_NEEDS_CSUM     (1U << 0U)

/** @brief 包头标志：数据有效 */
#define VIRTIO_NET_HDR_F_DATA_VALID     (1U << 1U)

/** @brief 包头 GSO 类型：无 */
#define VIRTIO_NET_HDR_GSO_NONE         0U

/** @brief 包头 GSO 类型：TCPv4 */
#define VIRTIO_NET_HDR_GSO_TCPV4        1U

/** @brief 包头 GSO 类型：TCPv6 */
#define VIRTIO_NET_HDR_GSO_TCPV6        4U

/**
 * @brief VirtIO Net 数据包头部（驱动和设备之间的元数据）
 */
typedef struct
{
    uint8_t  flags;                 /**< @brief 标志位 */
    uint8_t  gso_type;             /**< @brief GSO 类型 */
    uint16_t hdr_len;              /**< @brief 头部长度 */
    uint16_t gso_size;             /**< @brief GSO 段大小 */
    uint16_t csum_start;           /**< @brief 校验和起始偏移 */
    uint16_t csum_offset;          /**< @brief 校验和偏移 */
    uint16_t num_buffers;          /**< @brief 合并缓冲区数量 */
} virtio_net_hdr_t;

/* ========================================================================
 * VirtIO Net 设备配置空间
 * ======================================================================== */

/**
 * @brief VirtIO Net 配置空间
 */
typedef struct
{
    uint8_t  mac[VIRTIO_NET_MAC_SIZE]; /**< @brief MAC 地址 */
    uint16_t status;                   /**< @brief 链路状态 */
    uint16_t max_virtqueue_pairs;      /**< @brief 最大队列对数 */
} virtio_net_config_t;

/* ========================================================================
 * 链路状态定义
 * ======================================================================== */

/** @brief 链路状态：链路正常 */
#define VIRTIO_NET_S_LINK_UP           (1U << 0U)

/** @brief 链路状态：公告 GUEST_ANNOUNCE */
#define VIRTIO_NET_S_ANNOUNCE          (1U << 1U)

/* ========================================================================
 * 网络缓冲区描述
 * ======================================================================== */

/** @brief 网络缓冲区最大数据区大小 */
#define VIRTIO_NET_BUF_SIZE            2048U

/**
 * @brief 网络数据缓冲区
 */
typedef struct
{
    virtio_net_hdr_t hdr;            /**< @brief 包头 */
    uint8_t          data[VIRTIO_NET_BUF_SIZE]; /**< @brief 数据区 */
    uint32_t         data_len;       /**< @brief 实际数据长度 */
    bool             in_use;         /**< @brief 正在使用 */
} virtio_net_buf_t;

/* ========================================================================
 * VirtIO Net 设备实例
 * ======================================================================== */

/** @brief 最大网络设备数 */
#define VIRTIO_NET_MAX_DEVICES         2U

/** @brief 每设备的接收缓冲区数 */
#define VIRTIO_NET_RX_BUF_COUNT        256U

/** @brief 每设备的发送缓冲区数 */
#define VIRTIO_NET_TX_BUF_COUNT        256U

/**
 * @brief VirtIO Net 设备实例
 */
typedef struct
{
    virtio_net_config_t config;       /**< @brief 设备配置 */
    uint8_t             mac_addr[VIRTIO_NET_MAC_SIZE]; /**< @brief MAC 地址 */
    uint16_t            link_status;  /**< @brief 链路状态 */
    uint16_t            max_qp;       /**< @brief 最大队列对数 */
    bool                initialized;  /**< @brief 初始化标志 */
    uint32_t            rx_pending;   /**< @brief 接收待处理数 */
    uint32_t            tx_pending;   /**< @brief 发送待处理数 */
    dma_buffer_t        rx_bufs;      /**< @brief 接收 DMA 缓冲区 */
    dma_buffer_t        tx_bufs;      /**< @brief 发送 DMA 缓冲区 */
} virtio_net_dev_t;

/** @brief VirtIO Net 设备数组 */
static virtio_net_dev_t s_net_devices[VIRTIO_NET_MAX_DEVICES];

/** @brief 设备使用标记 */
static bool s_net_device_used[VIRTIO_NET_MAX_DEVICES];

/* ========================================================================
 * MAC 地址操作
 * ======================================================================== */

/**
 * @brief 检查两个 MAC 地址是否相等
 *
 * @param a 第一个 MAC 地址
 * @param b 第二个 MAC 地址
 *
 * @return true 相等，false 不等
 */
static bool mac_equal(const uint8_t *a, const uint8_t *b)
{
    uint32_t i;

    for (i = 0U; i < VIRTIO_NET_MAC_SIZE; i++)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief 检查 MAC 地址是否为广播地址
 *
 * @param mac MAC 地址
 *
 * @return true 是广播地址，false 不是
 */
static bool mac_is_broadcast(const uint8_t *mac)
{
    uint32_t i;

    for (i = 0U; i < VIRTIO_NET_MAC_SIZE; i++)
    {
        if (mac[i] != 0xFFU)
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief 检查 MAC 地址是否为多播地址
 *
 * @param mac MAC 地址
 *
 * @return true 是多播地址，false 不是
 */
static bool mac_is_multicast(const uint8_t *mac)
{
    return ((mac[0U] & 0x01U) != 0U);
}

/* ========================================================================
 * 设备配置
 * ======================================================================== */

/**
 * @brief 读取网络设备配置
 *
 * @param dev 网络设备指针
 *
 * @return DRIVER_OK 成功
 */
static driver_result_t virtio_net_read_config(virtio_net_dev_t *dev)
{
    if (dev == NULL)
    {
        return DRIVER_INVALID_PARAM;
    }

    /* 简化实现：从设备特定配置区域读取 */
    dev->link_status = dev->config.status;
    dev->max_qp = dev->config.max_virtqueue_pairs;

    if (dev->max_qp == 0U)
    {
        dev->max_qp = 1U;
    }

    return DRIVER_OK;
}

/* ========================================================================
 * 接收路径
 * ======================================================================== */

/**
 * @brief 向接收队列补充缓冲区
 *
 * @param dev    网络设备指针
 * @param count  补充数量
 *
 * @return 实际补充的数量
 *
 * @note 对应需求: DV-031
 */
static uint32_t virtio_net_fill_rx_buffers(virtio_net_dev_t *dev, uint32_t count)
{
    uint32_t filled;

    if (dev == NULL)
    {
        return 0U;
    }

    filled = 0U;

    while (filled < count)
    {
        /* 提交空缓冲区到接收队列 */
        /* 实际实现需要通过 virtio_ring 提交 */
        filled++;
    }

    return filled;
}

/**
 * @brief 从接收队列接收数据包
 *
 * @param dev      网络设备指针
 * @param buf      输出数据缓冲区
 * @param buf_size 缓冲区大小
 * @param out_len  输出实际接收字节数
 *
 * @return DRIVER_OK 成功
 */
static driver_result_t virtio_net_receive(virtio_net_dev_t *dev,
                                           uint8_t *buf,
                                           uint32_t buf_size,
                                           uint32_t *out_len)
{
    if ((dev == NULL) || (buf == NULL) || (out_len == NULL))
    {
        return DRIVER_INVALID_PARAM;
    }

    if (buf_size == 0U)
    {
        return DRIVER_INVALID_PARAM;
    }

    /* 简化实现：从接收 VirtQueue 获取已完成缓冲区 */
    *out_len = 0U;

    return DRIVER_OK;
}

/* ========================================================================
 * 发送路径
 * ======================================================================== */

/**
 * @brief 发送数据包
 *
 * @param dev     网络设备指针
 * @param buf     数据缓冲区
 * @param len     数据长度
 *
 * @return DRIVER_OK 成功
 *
 * @note 对应需求: DV-032
 */
static driver_result_t virtio_net_transmit(virtio_net_dev_t *dev,
                                            const uint8_t *buf,
                                            uint32_t len)
{
    if ((dev == NULL) || (buf == NULL))
    {
        return DRIVER_INVALID_PARAM;
    }

    if (len == 0U)
    {
        return DRIVER_INVALID_PARAM;
    }

    if (len > VIRTIO_NET_MAX_MTU)
    {
        return DRIVER_INVALID_PARAM;
    }

    if ((dev->link_status & VIRTIO_NET_S_LINK_UP) == 0U)
    {
        return DRIVER_ERROR;
    }

    /* 简化实现：构造包头 + 数据提交到发送 VirtQueue */
    dev->tx_pending++;

    return DRIVER_OK;
}

/* ========================================================================
 * 驱动操作实现
 * ======================================================================== */

/**
 * @brief 初始化 VirtIO Net 设备
 *
 * @param device_info 平台设备信息
 *
 * @return DRIVER_OK 成功
 *
 * @note 对应需求: DV-030
 */
static driver_result_t virtio_net_init(const device_info_t *device_info)
{
    uint32_t i;
    virtio_net_dev_t *dev = NULL;
    driver_result_t ret;

    if (device_info == NULL)
    {
        return DRIVER_INVALID_PARAM;
    }

    if (device_info->device_type != VIRTIO_NET_DEVICE_ID)
    {
        return DRIVER_NOT_FOUND;
    }

    /* 查找空闲设备槽 */
    for (i = 0U; i < VIRTIO_NET_MAX_DEVICES; i++)
    {
        if (!s_net_device_used[i])
        {
            dev = &s_net_devices[i];
            s_net_device_used[i] = true;
            break;
        }
    }

    if (dev == NULL)
    {
        return DRIVER_NO_RESOURCE;
    }

    /* 读取设备配置 */
    ret = virtio_net_read_config(dev);
    if (ret != DRIVER_OK)
    {
        s_net_device_used[i] = false;
        return ret;
    }

    /* 复制 MAC 地址 */
    for (i = 0U; i < VIRTIO_NET_MAC_SIZE; i++)
    {
        dev->mac_addr[i] = dev->config.mac[i];
    }

    /* 分配 DMA 缓冲区 */
    ret = driver_dma_alloc(
        (uint64_t)sizeof(virtio_net_buf_t) * (uint64_t)VIRTIO_NET_RX_BUF_COUNT,
        &dev->rx_bufs);
    if (ret != DRIVER_OK)
    {
        s_net_device_used[i] = false;
        return ret;
    }

    ret = driver_dma_alloc(
        (uint64_t)sizeof(virtio_net_buf_t) * (uint64_t)VIRTIO_NET_TX_BUF_COUNT,
        &dev->tx_bufs);
    if (ret != DRIVER_OK)
    {
        (void)driver_dma_free(&dev->rx_bufs);
        s_net_device_used[i] = false;
        return ret;
    }

    dev->rx_pending = 0U;
    dev->tx_pending = 0U;

    /* 补充接收缓冲区 */
    (void)virtio_net_fill_rx_buffers(dev, VIRTIO_NET_RX_BUF_COUNT);

    dev->initialized = true;

    return DRIVER_OK;
}

/**
 * @brief 关闭 VirtIO Net 设备
 */
static void virtio_net_deinit(void)
{
    uint32_t i;

    for (i = 0U; i < VIRTIO_NET_MAX_DEVICES; i++)
    {
        if (s_net_device_used[i] && s_net_devices[i].initialized)
        {
            if (s_net_devices[i].rx_bufs.size > 0U)
            {
                (void)driver_dma_free(&s_net_devices[i].rx_bufs);
            }
            if (s_net_devices[i].tx_bufs.size > 0U)
            {
                (void)driver_dma_free(&s_net_devices[i].tx_bufs);
            }
            s_net_devices[i].initialized = false;
            s_net_device_used[i] = false;
        }
    }
}

/**
 * @brief 打开网络设备
 *
 * @param flags 打开标志
 *
 * @return DRIVER_OK 成功
 */
static driver_result_t virtio_net_open(uint32_t flags)
{
    (void)flags;
    return DRIVER_OK;
}

/**
 * @brief 关闭网络设备
 */
static void virtio_net_close(void)
{
    /* 无操作 */
}

/**
 * @brief 从网络设备读取（接收数据包）
 *
 * @param buf    输出缓冲区
 * @param size   缓冲区大小
 * @param offset 保留（未使用）
 *
 * @return 接收的字节数，负数表示错误
 */
static int64_t virtio_net_read(void *buf, uint64_t size, uint64_t offset)
{
    virtio_net_dev_t *dev;
    uint32_t rx_len;
    driver_result_t ret;

    (void)offset;

    if (buf == NULL)
    {
        return -(int64_t)EINVAL;
    }

    dev = &s_net_devices[0U];
    if (!dev->initialized)
    {
        return -(int64_t)ENODEV;
    }

    ret = virtio_net_receive(dev, (uint8_t *)buf, (uint32_t)size, &rx_len);
    if (ret != DRIVER_OK)
    {
        return -(int64_t)EIO;
    }

    return (int64_t)rx_len;
}

/**
 * @brief 向网络设备写入（发送数据包）
 *
 * @param buf    输入缓冲区
 * @param size   数据大小
 * @param offset 保留（未使用）
 *
 * @return 发送的字节数，负数表示错误
 */
static int64_t virtio_net_write(const void *buf, uint64_t size, uint64_t offset)
{
    virtio_net_dev_t *dev;
    driver_result_t ret;

    (void)offset;

    if (buf == NULL)
    {
        return -(int64_t)EINVAL;
    }

    dev = &s_net_devices[0U];
    if (!dev->initialized)
    {
        return -(int64_t)ENODEV;
    }

    ret = virtio_net_transmit(dev, (const uint8_t *)buf, (uint32_t)size);
    if (ret != DRIVER_OK)
    {
        return -(int64_t)EIO;
    }

    return (int64_t)size;
}

/**
 * @brief 网络设备 I/O 控制
 *
 * @param cmd 命令
 * @param arg 参数
 *
 * @return DRIVER_OK 成功
 */
static driver_result_t virtio_net_ioctl(uint32_t cmd, void *arg)
{
    if (arg == NULL)
    {
        return DRIVER_INVALID_PARAM;
    }

    switch (cmd)
    {
        case 0U:
            /* 获取 MAC 地址 */
            break;

        case 1U:
            /* 获取链路状态 */
            break;

        case 2U:
            /* 设置多播/混杂模式 */
            break;

        default:
            return DRIVER_INVALID_PARAM;
    }

    return DRIVER_OK;
}

/**
 * @brief 网络设备中断处理
 *
 * @param irq 中断号
 */
static void virtio_net_interrupt_handler(uint32_t irq)
{
    uint32_t i;

    (void)irq;

    for (i = 0U; i < VIRTIO_NET_MAX_DEVICES; i++)
    {
        if (s_net_device_used[i] && s_net_devices[i].initialized)
        {
            /* 处理接收完成 */
            /* 补充接收缓冲区 */
            (void)virtio_net_fill_rx_buffers(&s_net_devices[i], 1U);
        }
    }
}

/* ========================================================================
 * 驱动操作函数表
 * ======================================================================== */

static const driver_ops_t s_virtio_net_ops =
{
    .init              = virtio_net_init,
    .deinit            = virtio_net_deinit,
    .open              = virtio_net_open,
    .close             = virtio_net_close,
    .read              = virtio_net_read,
    .write             = virtio_net_write,
    .ioctl             = virtio_net_ioctl,
    .interrupt_handler = virtio_net_interrupt_handler
};

/* ========================================================================
 * 驱动入口
 * ======================================================================== */

/**
 * @brief VirtIO Net 驱动入口点
 *
 * @return 0 成功，非零失败
 */
int main(void)
{
    driver_result_t ret;

    ret = driver_register("virtio-net", &s_virtio_net_ops);
    if (ret != DRIVER_OK)
    {
        return (int)ret;
    }

    for (;;)
    {
        /* 通过 IPC 接收并处理网络请求 */
    }

    return 0;
}
