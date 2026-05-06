/**
 * @file    virtio_net.h
 * @brief   VirtIO-Net 网络设备定义
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details 本文件定义了 VirtIO-Net 网络设备的数据结构和接口：
 *          - 网络设备描述符
 *          - 网络设备配置空间
 *          - 公共 API 接口
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_VMM_DEVICE_VIRTIO_NET_H
#define SERVICES_VMM_DEVICE_VIRTIO_NET_H

#include <kernel/types.h>
#include <stdint.h>
#include <stdbool.h>
#include "virtio.h"

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief VirtIO-Net 队列数量（rxq + txq） */
#define VIRTIO_NET_NUM_QUEUES           2U

/** @brief VirtIO-Net 队列大小 */
#define VIRTIO_NET_QUEUE_SIZE           256U

/** @brief VirtIO-Net 配置空间大小 */
#define VIRTIO_NET_CONFIG_SIZE          64U

/** @brief 最大 MAC 地址长度 */
#define VIRTIO_NET_MAC_LEN             6U

/** @brief 最大链表长度 */
#define VIRTIO_NET_MAX_LINKS           256U

/* ========================================================================
 * 设备特性位
 * ======================================================================== */

/** @brief 设备支持 MTU 配置 */
#define VIRTIO_NET_F_MTU                (1U << 3)

/** @brief 设备支持 MAC 地址 */
#define VIRTIO_NET_F_MAC                (1U << 5)

/** @brief 设备支持控制队列 */
#define VIRTIO_NET_F_CTRL_VQ            (1U << 17)

/** @brief 设备支持事件索引 */
#define VIRTIO_NET_F_EVENT_IDX          (1U << 29)

/** @brief 设备支持多个 RX 队列 */
#define VIRTIO_NET_F_MQ                 (1U << 22)

/* ========================================================================
 * 网络设备队列索引
 * ======================================================================== */

/** @brief 接收队列索引 */
#define VIRTIO_NET_RXQ_IDX              0U

/** @brief 发送队列索引 */
#define VIRTIO_NET_TXQ_IDX              1U

/* ========================================================================
 * 网络设备包类型
 * ======================================================================== */

/** @brief TCP 包 */
#define VIRTIO_NET_HDR_F_TCP_CSUM       (1U << 0)

/** @brief UDP 包 */
#define VIRTIO_NET_HDR_F_UDP_CSUM       (1U << 1)

/** @brief 其他包 */
#define VIRTIO_NET_HDR_F_DATA_VALID     (1U << 2)

/* ========================================================================
 * 网络设备头
 * ======================================================================== */

/**
 * @brief VirtIO-Net 网络头
 */
typedef struct
{
    uint8_t flags;                      /**< @brief 标志 */
    uint8_t gso_type;                   /**< @brief GSO 类型 */
    uint16_t hdr_len;                   /**< @brief 头长度 */
    uint16_t gso_size;                  /**< @brief GSO 大小 */
    uint16_t csum_start;                /**< @brief 校验和起始位置 */
    uint16_t csum_offset;               /**< @brief 校验和偏移 */
} virtio_net_hdr_t;

/* ========================================================================
 * 网络设备配置空间
 * ======================================================================== */

/**
 * @brief VirtIO-Net MAC 地址
 */
typedef struct
{
    uint8_t mac[VIRTIO_NET_MAC_LEN];    /**< @brief MAC 地址 */
} virtio_net_mac_t;

/**
 * @brief VirtIO-Net 链路状态
 */
typedef enum
{
    VIRTIO_NET_LINK_DOWN = 0U,          /**< @brief 链路断开 */
    VIRTIO_NET_LINK_UP                 /**< @brief 链路连接 */
} virtio_net_link_state_t;

/**
 * @brief VirtIO-Net 链路信息
 */
typedef struct
{
    uint8_t speed;                      /**< @brief 链路速度 */
    uint8_t duplex;                     /**< @brief 双工模式（0=半，1=全） */
    uint8_t port;                       /**< @brief 端口类型 */
    uint8_t io_padding[3];              /**< @brief 填充 */
} virtio_net_link_t;

/**
 * @brief VirtIO-Net 配置空间
 */
typedef struct
{
    virtio_net_mac_t mac;               /**< @brief MAC 地址 */
    uint16_t status;                    /**< @brief 状态（bit 0 = 链路） */
    uint16_t max_virtqueue_pairs;       /**< @brief 最大虚拟队列对 */
    virtio_net_link_t link;             /**< @brief 链路信息 */
    uint8_t mtu;                        /**< @brief MTU */
    uint16_t speed;                     /**< @brief 链路速度 */
    uint8_t duplex;                     /**< @brief 双工模式 */
    uint8_t rss_max_key_size;           /**< @brief RSS 最大密钥大小 */
    uint16_t rss_max_indirection_table_size; /**< @brief RSS 最大间接表大小 */
    uint16_t supported_hash_types;     /**< @brief 支持的哈希类型 */
} virtio_net_config_t;

/* ========================================================================
 * 网络设备请求类型
 * ======================================================================== */

/**
 * @brief VirtIO-Net 块设备请求类型
 */
typedef enum
{
    VIRTIO_NET_TSO_NONE = 0U,           /**< @brief 无 TSO */
    VIRTIO_NET_TSO_ECN,                 /**< @brief ECN TSO */
    VIRTIO_NET_TSO_UFO,                 /**< @brief UFO */
    VIRTIO_NET_TSO_TCP,                 /**< @brief TCP TSO */
    VIRTIO_NET_TSO_UDP,                 /**< @brief UDP TSO */
    VIRTIO_NET_TSO_MAX
} virtio_net_tso_t;

/* ========================================================================
 * 网络设备数据包缓冲区
 * ======================================================================== */

/**
 * @brief VirtIO-Net 数据包缓冲区
 */
typedef struct
{
    virtio_net_hdr_t header;             /**< @brief 网络头 */
    uint8_t data[1500];                 /**< @brief 数据（最大 MTU） */
    uint16_t len;                       /**< @brief 数据长度 */
    bool in_use;                        /**< @brief 使用标志 */
} virtio_net_pkt_t;

/* ========================================================================
 * 网络设备描述符
 * ======================================================================== */

/**
 * @brief VirtIO-Net 网络设备描述符
 *
 * @details 包含网络设备的完整状态：
 *          - 设备基本信息
 *          - 配置空间
 *          - VirtIO 队列
 *          - 数据包缓冲区
 *          - 设备特性
 */
typedef struct
{
    /** @brief 基本信息 */
    uint32_t         dev_id;            /**< @brief 设备 ID */
    uint32_t         vm_id;             /**< @brief 所属 VM ID */
    char             name[16];          /**< @brief 设备名称 */
    bool             active;            /**< @brief 活跃标志 */

    /** @brief 配置空间 */
    virtio_net_config_t config;        /**< @brief 配置空间 */
    uint32_t         config_size;       /**< @brief 配置空间大小 */

    /** @brief VirtIO 队列 */
    virtio_queue_t   vqs[VIRTIO_NET_NUM_QUEUES]; /**< @brief 队列数组 */
    uint32_t         num_vqs;           /**< @brief 队列数量 */

    /** @brief 设备特性 */
    uint32_t         features;          /**< @brief 设备特性位图 */
    uint16_t         status;            /**< @brief 设备状态 */

    /** @brief MMIO 区域 */
    uint64_t         mmio_base;         /**< @brief MMIO 基址 */
    uint64_t         mmio_size;         /**< @brief MMIO 大小 */

    /** @brief 数据包缓冲区 */
    virtio_net_pkt_t rx_queue[VIRTIO_NET_QUEUE_SIZE]; /**< @brief 接收队列 */
    uint32_t         rx_head;           /**< @brief 接收队列头 */
    uint32_t         rx_tail;           /**< @brief 接收队列尾 */
    uint32_t         rx_count;          /**< @brief 接收队列计数 */

    virtio_net_pkt_t tx_queue[VIRTIO_NET_QUEUE_SIZE]; /**< @brief 发送队列 */
    uint32_t         tx_head;           /**< @brief 发送队列头 */
    uint32_t         tx_tail;           /**< @brief 发送队列尾 */
    uint32_t         tx_count;          /**< @brief 发送队列计数 */

    /** @brief 链路状态 */
    virtio_net_link_state_t link_state; /**< @brief 链路状态 */

    /** @brief 统计信息 */
    uint64_t         rx_packets;        /**< @brief 接收包数 */
    uint64_t         rx_bytes;          /**< @brief 接收字节数 */
    uint64_t         tx_packets;        /**< @brief 发送包数 */
    uint64_t         tx_bytes;          /**< @brief 发送字节数 */
    uint64_t         rx_dropped;        /**< @brief 丢弃接收包数 */
    uint64_t         tx_dropped;        /**< @brief 丢弃发送包数 */

    /** @brief 设备操作 */
    vdev_op_fn       read_fn;            /**< @brief 读回调 */
    vdev_op_fn       write_fn;           /**< @brief 写回调 */
    void*            priv;              /**< @brief 设备私有数据 */
} virtio_net_dev_t;

/* ========================================================================
 * VirtIO-Net 模块 API 接口
 * ======================================================================== */

/**
 * @brief 初始化 VirtIO-Net 网络设备模块
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_net_global_init(void);

/**
 * @brief 销毁 VirtIO-Net 网络设备模块
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_net_global_destroy(void);

/**
 * @brief 创建 VirtIO-Net 网络设备
 *
 * @param vm_id       VM ID
 * @param name        设备名称
 * @param mac_addr    MAC 地址（6 字节）
 * @param mtu         MTU
 * @param mmio_base   MMIO 基址
 *
 * @return 设备 ID，负数表示错误
 */
int32_t virtio_net_create(uint32_t vm_id, const char *name,
                           const uint8_t mac_addr[6], uint16_t mtu,
                           uint64_t mmio_base);

/**
 * @brief 销毁 VirtIO-Net 网络设备
 *
 * @param dev_id      设备 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_net_destroy(uint32_t dev_id);

/**
 * @brief 接收数据包（后端）
 *
 * @param dev_id      设备 ID
 * @param data        数据指针
 * @param len         数据长度
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_net_receive(uint32_t dev_id,
                                    const uint8_t *data, uint32_t len);

/**
 * @brief 发送数据包（后端）
 *
 * @param dev_id      设备 ID
 * @param data        数据指针
 * @param len         数据长度
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_net_transmit(uint32_t dev_id,
                                     const uint8_t *data, uint32_t len);

/**
 * @brief 设置链路状态
 *
 * @param dev_id      设备 ID
 * @param link_state  链路状态
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_net_set_link_state(uint32_t dev_id,
                                            virtio_net_link_state_t link_state);

/**
 * @brief 获取设备统计信息
 *
 * @param dev_id      设备 ID
 * @param rx_packets  输出接收包数
 * @param rx_bytes    输出接收字节数
 * @param tx_packets  输出发送包数
 * @param tx_bytes    输出发送字节数
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_net_get_stats(uint32_t dev_id,
                                       uint64_t *rx_packets,
                                       uint64_t *rx_bytes,
                                       uint64_t *tx_packets,
                                       uint64_t *tx_bytes);

#endif /* SERVICES_VMM_DEVICE_VIRTIO_NET_H */
