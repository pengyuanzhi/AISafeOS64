/**
 * @file    net_types.h
 * @brief   网络协议栈类型定义
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details 网络协议栈类型定义：
 *          - 套接字类型
 *          - IP 数据包结构
 *          - TCP/UDP 首部结构
 *          - ARP 数据包结构
 *          - 以太网帧结构
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef NET_TYPES_H
#define NET_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 套接字类型
 * ======================================================================== */

/** @brief 套接字类型 */
typedef enum
{
    NET_SOCK_STREAM = 1U,     /**< @brief TCP 套接字 */
    NET_SOCK_DGRAM  = 2U,     /**< @brief UDP 套接字 */
    NET_SOCK_RAW    = 3U      /**< @brief Raw 套接字 */
} net_sock_type_t;

/** @brief 协议类型 */
typedef enum
{
    NET_IPPROTO_IP   = 0U,    /**< @brief IP 协议 */
    NET_IPPROTO_ICMP = 1U,    /**< @brief ICMP 协议 */
    NET_IPPROTO_TCP  = 6U,    /**< @brief TCP 协议 */
    NET_IPPROTO_UDP  = 17U     /**< @brief UDP 协议 */
} net_ipproto_t;

/** @brief 套接字标志 */
#define NET_SOCK_NONBLOCK      0x1000U  /**< @brief 非阻塞 */

/* ========================================================================
 * TCP 状态
 * ======================================================================== */

/** @brief TCP 状态机状态 */
typedef enum
{
    NET_TCP_CLOSED    = 0U,     /**< @brief 关闭 */
    NET_TCP_LISTEN    = 1U,     /**< @brief 监听 */
    NET_TCP_SYN_SENT  = 2U,     /**< @brief SYN 已发送 */
    NET_TCP_SYN_RCVD  = 3U,     /**< @brief SYN 已接收 */
    NET_TCP_ESTABLISHED = 4U,   /**< @brief 已建立 */
    NET_TCP_FIN_WAIT_1 = 5U,    /**< @brief FIN 等待 1 */
    NET_TCP_FIN_WAIT_2 = 6U,    /**< @brief FIN 等待 2 */
    NET_TCP_CLOSE_WAIT = 7U,    /**< @brief CLOSE 等待 */
    NET_TCP_CLOSING    = 8U,    /**< @brief 关闭中 */
    NET_TCP_LAST_ACK   = 9U,     /**< @brief 最后 ACK */
    NET_TCP_TIME_WAIT  = 10U,    /**< @brief TIME 等待 */
    NET_TCP_ACK_WAIT   = 11U     /**< @brief ACK 等待 */
} net_tcp_state_t;

/* ========================================================================
 * IP 数据包结构
 * ======================================================================== */

/** @brief IP 数据包首部 */
typedef struct
{
    uint8_t  version_ihl;        /**< @brief 版本和首部长度 */
    uint8_t  tos;                /**< @brief 服务类型 */
    uint16_t total_len;          /**< @brief 总长度 */
    uint16_t id;                 /**< @brief 标识 */
    uint16_t frag_off;           /**< @brief 片偏移 */
    uint8_t  ttl;                /**< @brief 生存时间 */
    uint8_t  protocol;           /**< @brief 协议 */
    uint16_t checksum;           /**< @brief 校验和 */
    uint32_t src_ip;            /**< @brief 源 IP 地址 */
    uint32_t dst_ip;            /**< @brief 目的 IP 地址 */
} net_ip_header_t;

/** @brief IP 版本和首部长度掩码 */
#define IP_VERSION_MASK          0xF0U
#define IP_VERSION_SHIFT         4U
#define IP_IHL_MASK             0x0FU
#define IP_VERSION_4            4U

/** @brief IP 首部长度（以 32 位字为单位） */
#define IP_IHL_TO_BYTES(ihl)    ((ihl) << 2U)
#define IP_BYTES_TO_IHL(bytes)  ((bytes) >> 2U)

/** @brief IP 最小首部长度 */
#define IP_MIN_HEADER_LEN       20U

/** @brief IP 最大首部长度 */
#define IP_MAX_HEADER_LEN       60U

/* ========================================================================
 * TCP 首部结构
 * ======================================================================== */

/** @brief TCP 数据包首部 */
typedef struct
{
    uint16_t src_port;          /**< @brief 源端口 */
    uint16_t dst_port;          /**< @brief 目的端口 */
    uint32_t seq_num;           /**< @brief 序列号 */
    uint32_t ack_num;           /**< @brief 确认号 */
    uint8_t  data_offset;       /**< @brief 数据偏移 */
    uint8_t  flags;             /**< @brief 标志 */
    uint16_t window;            /**< @brief 窗口 */
    uint16_t checksum;          /**< @brief 校验和 */
    uint16_t urgent_ptr;        /**< @brief 紧急指针 */
} net_tcp_header_t;

/** @brief TCP 首部长度（以 32 位字为单位） */
#define TCP_DATA_OFFSET_MASK    0xF0U
#define TCP_DATA_OFFSET_SHIFT   4U
#define TCP_DATA_OFFSET_TO_BYTES(do)  ((do) << 2U)

/** @brief TCP 最小首部长度 */
#define TCP_MIN_HEADER_LEN     20U

/** @brief TCP 最大首部长度 */
#define TCP_MAX_HEADER_LEN     60U

/** @brief TCP 标志 */
#define TCP_FLAG_FIN           0x01U
#define TCP_FLAG_SYN           0x02U
#define TCP_FLAG_RST           0x04U
#define TCP_FLAG_PSH           0x08U
#define TCP_FLAG_ACK           0x10U
#define TCP_FLAG_URG           0x20U

/* ========================================================================
 * UDP 首部结构
 * ======================================================================== */

/** @brief UDP 数据包首部 */
typedef struct
{
    uint16_t src_port;          /**< @brief 源端口 */
    uint16_t dst_port;          /**< @brief 目的端口 */
    uint16_t length;            /**< @brief 长度 */
    uint16_t checksum;          /**< @brief 校验和 */
} net_udp_header_t;

/** @brief UDP 最小首部长度 */
#define UDP_HEADER_LEN         8U

/* ========================================================================
 * ARP 数据包结构
 * ======================================================================== */

/** @brief ARP 硬件类型 */
#define ARP_HTYPE_ETHER        1U

/** @brief ARP 协议类型 */
#define ARP_PTYPE_IP           0x0800U

/** @brief ARP 操作类型 */
#define ARP_OP_REQUEST         1U
#define ARP_OP_REPLY           2U

/** @brief ARP 数据包 */
typedef struct
{
    uint16_t htype;             /**< @brief 硬件类型 */
    uint16_t ptype;             /**< @brief 协议类型 */
    uint8_t  hlen;              /**< @brief 硬件地址长度 */
    uint8_t  plen;              /**< @brief 协议地址长度 */
    uint16_t oper;              /**< @brief 操作 */
    uint8_t  sha[6];           /**< @brief 源硬件地址 */
    uint32_t spa;               /**< @brief 源协议地址 */
    uint8_t  tha[6];           /**< @brief 目标硬件地址 */
    uint32_t tpa;               /**< @brief 目标协议地址 */
} net_arp_packet_t;

/** @brief MAC 地址长度 */
#define ARP_MAC_ADDR_LEN      6U

/** @brief IP 地址长度 */
#define ARP_IP_ADDR_LEN       4U

/* ========================================================================
 * 以太网帧结构
 * ======================================================================== */

/** @brief 以太网帧首部 */
typedef struct
{
    uint8_t  dst_mac[6];        /**< @brief 目的 MAC 地址 */
    uint8_t  src_mac[6];        /**< @brief 源 MAC 地址 */
    uint16_t ether_type;        /**< @brief 以太网类型 */
} net_ether_header_t;

/** @brief 以太网类型 */
#define ETHER_TYPE_IP         0x0800U
#define ETHER_TYPE_ARP        0x0806U
#define ETHER_TYPE_IPV6       0x86DDU

/** @brief MAC 地址长度 */
#define ETHER_MAC_ADDR_LEN    6U

/** @brief 以太网首部长度 */
#define ETHER_HEADER_LEN      14U

/** @brief 最大传输单元 */
#define ETHER_MTU            1500U

/* ========================================================================
 * 套接字结构
 * ======================================================================== */

/** @brief 套接字结构 */
typedef struct
{
    net_sock_type_t type;        /**< @brief 套接字类型 */
    uint32_t        protocol;    /**< @brief 协议 */
    uint16_t        local_port;  /**< @brief 本地端口 */
    uint32_t        local_ip;    /**< @brief 本地 IP */
    uint16_t        remote_port; /**< @brief 远程端口 */
    uint32_t        remote_ip;   /**< @brief 远程 IP */
    uint32_t        state;       /**< @brief TCP 状态 */
    uint32_t        seq_num;     /**< @brief 序列号 */
    uint32_t        ack_num;     /**< @brief 确认号 */
    uint16_t        window;      /**< @brief 窗口 */
    bool            in_use;      /**< @brief 使用标记 */
} net_socket_t;

/* ========================================================================
 * 网络配置结构
 * ======================================================================== */

/** @brief 网络配置 */
typedef struct
{
    uint32_t ip_addr;            /**< @brief IP 地址 */
    uint32_t netmask;           /**< @brief 子网掩码 */
    uint32_t gateway;           /**< @brief 网关 */
    uint8_t  mac_addr[6];       /**< @brief MAC 地址 */
    bool     dhcp_enabled;      /**< @brief DHCP 使能 */
    bool     up;                /**< @brief 接口启动 */
} net_config_t;

#endif /* NET_TYPES_H */
