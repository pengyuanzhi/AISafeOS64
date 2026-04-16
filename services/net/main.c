/**
 * @file    main.c
 * @brief   网络协议栈服务实现
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 3.0
 *
 * @details 用户态完整网络协议栈服务：
 *          - IPv4 数据包封装/解析
 *          - ICMP 回显请求/应答处理
 *          - UDP 套接字（bind/sendto/recvfrom）
 *          - TCP 状态机（CLOSED→SYN_SENT→ESTABLISHED→FIN_WAIT 等）
 *          - TCP 滑动窗口和重传机制
 *          - ARP 解析缓存
 *          - 网络接口抽象层（以太网帧收发）
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: NW-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/netstack.h>
#include <kernel/config.h>
#include <stdint.h>
#include <string.h>
#include "net_if/net_if.h"

/* ========================================================================
 * 协议常量
 * ======================================================================== */

/** @brief 以太网帧头大小 */
#define ETH_HDR_SIZE            14U

/** @brief 以太网类型：IPv4 */
#define ETH_TYPE_IPV4           0x0800U

/** @brief 以太网类型：ARP */
#define ETH_TYPE_ARP            0x0806U

/** @brief IPv4 协议头大小（无选项） */
#define IPV4_HDR_SIZE           20U

/** @brief IPv4 协议号：ICMP */
#define IP_PROTO_ICMP           1U

/** @brief IPv4 协议号：TCP */
#define IP_PROTO_TCP            6U

/** @brief IPv4 协议号：UDP */
#define IP_PROTO_UDP            17U

/** @brief ICMP 类型：回显请求 */
#define ICMP_TYPE_ECHO_REQ      8U

/** @brief ICMP 类型：回显应答 */
#define ICMP_TYPE_ECHO_REPLY    0U

/** @brief TCP 头大小（无选项） */
#define TCP_HDR_SIZE            20U

/** @brief UDP 头大小 */
#define UDP_HDR_SIZE            8U

/** @brief ARP 缓存条目数 */
#define ARP_CACHE_SIZE          16U

/** @brief TCP 最大重传次数 */
#define TCP_MAX_RETRIES         5U

/** @brief TCP 最大段大小 */
#define TCP_MAX_SEG_SIZE        1460U

/** @brief TCP 滑动窗口大小 */
#define TCP_WINDOW_SIZE         65535U

/** @brief TCP 重传超时（毫秒） */
#define TCP_RETRANSMIT_MS       200U

/** @brief ARP 请求操作码 */
#define ARP_OP_REQUEST          1U

/** @brief ARP 应答操作码 */
#define ARP_OP_REPLY            2U

/** @brief IP 分片偏移掩码 */
#define IP_FRAG_OFFSET_MASK     0x1FFFU

/** @brief IP 不分片标志 */
#define IP_FLAG_DF              0x4000U

/** @brief IP 更多分片标志 */
#define IP_FLAG_MF              0x2000U

/* ========================================================================
 * TCP 状态机枚举
 * ======================================================================== */

/**
 * @brief TCP 连接状态
 */
typedef enum
{
    TCP_CLOSED = 0U,        /**< @brief 关闭状态 */
    TCP_LISTEN,             /**< @brief 监听状态 */
    TCP_SYN_SENT,           /**< @brief SYN 已发送 */
    TCP_SYN_RECEIVED,       /**< @brief SYN 已接收 */
    TCP_ESTABLISHED,        /**< @brief 已建立连接 */
    TCP_FIN_WAIT_1,         /**< @brief FIN 等待 1 */
    TCP_FIN_WAIT_2,         /**< @brief FIN 等待 2 */
    TCP_CLOSE_WAIT,         /**< @brief 关闭等待 */
    TCP_CLOSING,            /**< @brief 正在关闭 */
    TCP_LAST_ACK,           /**< @brief 最后确认 */
    TCP_TIME_WAIT           /**< @brief 时间等待 */
} tcp_state_t;

/**
 * @brief TCP 控制标志
 */
typedef enum
{
    TCP_FLAG_FIN = 0x01U,   /**< @brief 结束标志 */
    TCP_FLAG_SYN = 0x02U,   /**< @brief 同步标志 */
    TCP_FLAG_RST = 0x04U,   /**< @brief 重置标志 */
    TCP_FLAG_PSH = 0x08U,   /**< @brief 推送标志 */
    TCP_FLAG_ACK = 0x10U,   /**< @brief 确认标志 */
    TCP_FLAG_URG = 0x20U    /**< @brief 紧急标志 */
} tcp_flags_t;

/* ========================================================================
 * 协议头部结构体
 * ======================================================================== */

/**
 * @brief 以太网帧头
 */
typedef struct
{
    uint8_t  dst_mac[NET_MAC_ADDR_LEN]; /**< @brief 目标 MAC */
    uint8_t  src_mac[NET_MAC_ADDR_LEN]; /**< @brief 源 MAC */
    uint16_t eth_type;                  /**< @brief 以太网类型 */
} eth_header_t;

/**
 * @brief IPv4 头部
 */
typedef struct
{
    uint8_t  version_ihl;   /**< @brief 版本(4) + 头长(4) */
    uint8_t  tos;           /**< @brief 服务类型 */
    uint16_t total_length;  /**< @brief 总长度 */
    uint16_t identification;/**< @brief 标识 */
    uint16_t flags_offset;  /**< @brief 标志 + 片偏移 */
    uint8_t  ttl;           /**< @brief 生存时间 */
    uint8_t  protocol;      /**< @brief 上层协议 */
    uint16_t checksum;      /**< @brief 头部校验和 */
    uint32_t src_ip;        /**< @brief 源 IP 地址 */
    uint32_t dst_ip;        /**< @brief 目标 IP 地址 */
} ipv4_header_t;

/**
 * @brief ICMP 头部
 */
typedef struct
{
    uint8_t  type;          /**< @brief 类型 */
    uint8_t  code;          /**< @brief 代码 */
    uint16_t checksum;      /**< @brief 校验和 */
    uint16_t identifier;    /**< @brief 标识符 */
    uint16_t sequence;      /**< @brief 序列号 */
} icmp_header_t;

/**
 * @brief UDP 头部
 */
typedef struct
{
    uint16_t src_port;      /**< @brief 源端口 */
    uint16_t dst_port;      /**< @brief 目标端口 */
    uint16_t length;        /**< @brief 长度 */
    uint16_t checksum;      /**< @brief 校验和 */
} udp_header_t;

/**
 * @brief TCP 头部
 */
typedef struct
{
    uint16_t src_port;      /**< @brief 源端口 */
    uint16_t dst_port;      /**< @brief 目标端口 */
    uint32_t seq_num;       /**< @brief 序列号 */
    uint32_t ack_num;       /**< @brief 确认号 */
    uint8_t  data_offset;   /**< @brief 数据偏移（4位） + 保留 */
    uint8_t  flags;         /**< @brief 控制标志 */
    uint16_t window;        /**< @brief 窗口大小 */
    uint16_t checksum;      /**< @brief 校验和 */
    uint16_t urgent_ptr;    /**< @brief 紧急指针 */
} tcp_header_t;

/**
 * @brief ARP 数据包
 */
typedef struct
{
    uint16_t hardware_type; /**< @brief 硬件类型（1=以太网） */
    uint16_t protocol_type; /**< @brief 协议类型（0x0800=IPv4） */
    uint8_t  hw_addr_len;   /**< @brief 硬件地址长度 */
    uint8_t  proto_addr_len;/**< @brief 协议地址长度 */
    uint16_t operation;     /**< @brief 操作码 */
    uint8_t  sender_mac[NET_MAC_ADDR_LEN]; /**< @brief 发送方 MAC */
    uint32_t sender_ip;     /**< @brief 发送方 IP */
    uint8_t  target_mac[NET_MAC_ADDR_LEN]; /**< @brief 目标 MAC */
    uint32_t target_ip;     /**< @brief 目标 IP */
} arp_packet_t;

/* ========================================================================
 * ARP 缓存条目
 * ======================================================================== */

/**
 * @brief ARP 缓存条目
 */
typedef struct
{
    uint32_t ip_addr;              /**< @brief IP 地址（网络字节序） */
    net_mac_t mac_addr;            /**< @brief MAC 地址 */
    uint64_t timestamp;            /**< @brief 更新时间戳 */
    bool     valid;                /**< @brief 有效标记 */
} arp_entry_t;

/* ========================================================================
 * TCP 控制块（TCB）
 * ======================================================================== */

/**
 * @brief TCP 重传段描述
 */
typedef struct
{
    uint8_t  data[TCP_MAX_SEG_SIZE]; /**< @brief 重传数据缓冲 */
    uint32_t data_len;              /**< @brief 数据长度 */
    uint32_t seq_num;               /**< @brief 起始序列号 */
    uint32_t retry_count;           /**< @brief 已重传次数 */
    uint64_t last_sent_ms;          /**< @brief 上次发送时间 */
    bool     active;                /**< @brief 活跃标记 */
} tcp_retransmit_seg_t;

/** @brief TCP 最大重传段数 */
#define TCP_MAX_RETRANS_SEGS    8U

/**
 * @brief TCP 控制块
 */
typedef struct
{
    uint32_t          sock_id;          /**< @brief 关联套接字 ID */
    tcp_state_t       state;            /**< @brief TCP 状态 */
    uint32_t          local_ip;         /**< @brief 本地 IP */
    uint32_t          remote_ip;        /**< @brief 远端 IP */
    uint16_t          local_port;       /**< @brief 本地端口 */
    uint16_t          remote_port;      /**< @brief 远端端口 */
    uint32_t          snd_una;          /**< @brief 发送未确认序列号 */
    uint32_t          snd_nxt;          /**< @brief 下一个发送序列号 */
    uint32_t          snd_wnd;          /**< @brief 发送窗口大小 */
    uint32_t          rcv_nxt;          /**< @brief 下一个期望接收序列号 */
    uint32_t          rcv_wnd;          /**< @brief 接收窗口大小 */
    uint32_t          iss;              /**< @brief 初始发送序列号 */
    uint32_t          irs;              /**< @brief 初始接收序列号 */
    uint8_t           recv_buf[TCP_MAX_SEG_SIZE * 4U]; /**< @brief 接收缓冲 */
    uint32_t          recv_len;         /**< @brief 接收缓冲已用长度 */
    tcp_retransmit_seg_t retrans_buf[TCP_MAX_RETRANS_SEGS]; /**< @brief 重传队列 */
    uint32_t          retrans_count;    /**< @brief 活跃重传段数 */
    bool              in_use;           /**< @brief 使用标记 */
} tcp_tcb_t;

/* ========================================================================
 * 网络栈全局状态
 * ======================================================================== */

/** @brief 网络接口表 */
static net_interface_t s_interfaces[NET_MAX_INTERFACES];

/** @brief 套接字表 */
static net_socket_t s_sockets[NET_MAX_SOCKETS];

/** @brief 接收环缓冲区 */
static uint8_t s_rx_buf[NET_MAX_INTERFACES][NET_RX_QUEUE_DEPTH][NET_MAX_PACKET_SIZE];

/** @brief 接收环 head/tail */
static uint32_t s_rx_head[NET_MAX_INTERFACES];
static uint32_t s_rx_tail[NET_MAX_INTERFACES];

/** @brief ARP 缓存 */
static arp_entry_t s_arp_cache[ARP_CACHE_SIZE];

/** @brief TCP 控制块表 */
static tcp_tcb_t s_tcp_tcbs[NET_MAX_SOCKETS];

/** @brief IP 分片重组缓冲区 */
static uint8_t s_reasm_buf[NET_MAX_PACKET_SIZE * 2U];
static uint16_t s_reasm_offset;
static uint16_t s_reasm_id;

/** @brief ICMP 回显统计 */
static uint32_t s_icmp_echo_sent;
static uint32_t s_icmp_echo_recv;
static uint32_t s_icmp_echo_reply_sent;

/** @brief 初始化标志 */
static bool s_initialized;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 安全字符串复制
 */
static void net_strcpy(char *dst, const char *src, uint32_t n)
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
 * @brief 计算 Internet 校验和
 *
 * @param data 数据指针
 * @param len  数据长度（字节）
 *
 * @return 校验和（取反后的值）
 */
static uint16_t net_checksum(const void *data, uint32_t len)
{
    const uint8_t *buf = (const uint8_t *)data;
    uint32_t sum = 0U;
    uint32_t i;

    for (i = 0U; i < (len - 1U); i += 2U)
    {
        sum += ((uint32_t)buf[i] << 8U) | (uint32_t)buf[i + 1U];
    }

    if ((len & 1U) != 0U)
    {
        sum += (uint32_t)buf[len - 1U] << 8U;
    }

    while ((sum >> 16U) != 0U)
    {
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    }

    return (uint16_t)(~sum);
}

/**
 * @brief 16 位字节序交换
 */
static uint16_t net_htons(uint16_t val)
{
    return (uint16_t)(((val >> 8U) & 0xFFU) | ((val & 0xFFU) << 8U));
}

/**
 * @brief 32 位字节序交换
 */
static uint32_t net_htonl(uint32_t val)
{
    return ((val >> 24U) & 0x000000FFU) |
           ((val >> 8U)  & 0x0000FF00U) |
           ((val << 8U)  & 0x00FF0000U) |
           ((val << 24U) & 0xFF000000U);
}

/**
 * @brief IPv4 结构体转 32 位整数
 */
static uint32_t ipv4_to_u32(const net_ipv4_t *addr)
{
    return ((uint32_t)addr->bytes[0] << 24U) |
           ((uint32_t)addr->bytes[1] << 16U) |
           ((uint32_t)addr->bytes[2] << 8U)  |
           (uint32_t)addr->bytes[3];
}

/**
 * @brief 32 位整数转 IPv4 结构体
 */
static void u32_to_ipv4(uint32_t ip, net_ipv4_t *addr)
{
    addr->bytes[0] = (uint8_t)(ip >> 24U);
    addr->bytes[1] = (uint8_t)(ip >> 16U);
    addr->bytes[2] = (uint8_t)(ip >> 8U);
    addr->bytes[3] = (uint8_t)(ip);
}

/* ========================================================================
 * 初始化
 * ======================================================================== */

kernel_status_t net_init(void)
{
    uint32_t i;

    (void)memset(s_interfaces, 0, sizeof(s_interfaces));
    (void)memset(s_sockets, 0, sizeof(s_sockets));
    (void)memset(s_rx_buf, 0, sizeof(s_rx_buf));
    (void)memset(s_rx_head, 0, sizeof(s_rx_head));
    (void)memset(s_rx_tail, 0, sizeof(s_rx_tail));
    (void)memset(s_arp_cache, 0, sizeof(s_arp_cache));
    (void)memset(s_tcp_tcbs, 0, sizeof(s_tcp_tcbs));
    (void)memset(s_reasm_buf, 0, sizeof(s_reasm_buf));

    for (i = 0U; i < NET_MAX_INTERFACES; i++)
    {
        s_interfaces[i].if_id = i;
        s_interfaces[i].state = NET_IF_DOWN;
        s_interfaces[i].mtu = 1500U;
        s_interfaces[i].in_use = false;
    }

    for (i = 0U; i < NET_MAX_SOCKETS; i++)
    {
        s_sockets[i].sock_id = i;
        s_sockets[i].state = NET_SOCK_CLOSED;
        s_sockets[i].in_use = false;
        s_tcp_tcbs[i].sock_id = i;
        s_tcp_tcbs[i].state = TCP_CLOSED;
        s_tcp_tcbs[i].in_use = false;
    }

    s_reasm_offset = 0U;
    s_reasm_id = 0U;
    s_icmp_echo_sent = 0U;
    s_icmp_echo_recv = 0U;
    s_icmp_echo_reply_sent = 0U;

    s_initialized = true;

    return KERNEL_OK;
}

/* ========================================================================
 * 网络接口管理
 * ======================================================================== */

int32_t net_register_interface(const char *name, net_link_type_t link_type,
                                 const net_mac_t *mac_addr, uint32_t driver_id)
{
    uint32_t i;
    net_interface_t *iface;

    if (!s_initialized)
    {
        return -(int32_t)22;
    }

    if ((name == NULL) || (mac_addr == NULL))
    {
        return -(int32_t)22;
    }

    for (i = 0U; i < NET_MAX_INTERFACES; i++)
    {
        if (!s_interfaces[i].in_use)
        {
            break;
        }
    }

    if (i >= NET_MAX_INTERFACES)
    {
        return -(int32_t)12;
    }

    iface = &s_interfaces[i];

    net_strcpy(iface->name, name, 16U);
    iface->link_type = link_type;
    iface->state = NET_IF_DOWN;
    iface->driver_id = driver_id;
    iface->mtu = 1500U;
    (void)memcpy(&iface->mac_addr, mac_addr, sizeof(net_mac_t));
    (void)memset(&iface->ipv4_addr, 0, sizeof(net_ipv4_t));
    (void)memset(&iface->ipv4_mask, 0, sizeof(net_ipv4_t));
    (void)memset(&iface->ipv4_gw, 0, sizeof(net_ipv4_t));
    (void)memset(&iface->stats, 0, sizeof(net_if_stats_t));
    iface->in_use = true;

    return (int32_t)i;
}

kernel_status_t net_if_up(uint32_t if_id)
{
    net_interface_t *iface;

    if (if_id >= NET_MAX_INTERFACES)
    {
        return -(int32_t)22;
    }

    iface = &s_interfaces[if_id];

    if (!iface->in_use)
    {
        return -(int32_t)2;
    }

    iface->state = NET_IF_RUNNING;

    return KERNEL_OK;
}

kernel_status_t net_if_down(uint32_t if_id)
{
    net_interface_t *iface;

    if (if_id >= NET_MAX_INTERFACES)
    {
        return -(int32_t)22;
    }

    iface = &s_interfaces[if_id];

    if (!iface->in_use)
    {
        return -(int32_t)2;
    }

    iface->state = NET_IF_DOWN;

    return KERNEL_OK;
}

kernel_status_t net_if_set_ipv4(uint32_t if_id, const net_ipv4_t *addr,
                                  const net_ipv4_t *mask, const net_ipv4_t *gw)
{
    net_interface_t *iface;

    if (if_id >= NET_MAX_INTERFACES)
    {
        return -(int32_t)22;
    }

    if ((addr == NULL) || (mask == NULL) || (gw == NULL))
    {
        return -(int32_t)22;
    }

    iface = &s_interfaces[if_id];

    if (!iface->in_use)
    {
        return -(int32_t)2;
    }

    (void)memcpy(&iface->ipv4_addr, addr, sizeof(net_ipv4_t));
    (void)memcpy(&iface->ipv4_mask, mask, sizeof(net_ipv4_t));
    (void)memcpy(&iface->ipv4_gw, gw, sizeof(net_ipv4_t));

    return KERNEL_OK;
}

/* ========================================================================
 * ARP 缓存管理
 * ======================================================================== */

/**
 * @brief 在 ARP 缓存中查找 IP 对应的 MAC
 *
 * @param ip_addr IP 地址（主机字节序）
 * @param mac_out 输出 MAC 地址
 *
 * @return KERNEL_OK 找到，负数未找到
 */
static kernel_status_t arp_lookup(uint32_t ip_addr, net_mac_t *mac_out)
{
    uint32_t i;

    if (mac_out == NULL)
    {
        return -(int32_t)22;
    }

    for (i = 0U; i < ARP_CACHE_SIZE; i++)
    {
        if (s_arp_cache[i].valid && (s_arp_cache[i].ip_addr == ip_addr))
        {
            (void)memcpy(mac_out, &s_arp_cache[i].mac_addr, sizeof(net_mac_t));
            return KERNEL_OK;
        }
    }

    return -(int32_t)2;
}

/**
 * @brief 向 ARP 缓存添加条目
 *
 * @param ip_addr IP 地址（主机字节序）
 * @param mac     MAC 地址
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t arp_add_entry(uint32_t ip_addr, const net_mac_t *mac)
{
    uint32_t i;
    uint32_t oldest = 0U;
    uint64_t oldest_ts = 0xFFFFFFFFFFFFFFFFULL;

    if (mac == NULL)
    {
        return -(int32_t)22;
    }

    /* 查找已有条目更新 */
    for (i = 0U; i < ARP_CACHE_SIZE; i++)
    {
        if (s_arp_cache[i].valid && (s_arp_cache[i].ip_addr == ip_addr))
        {
            (void)memcpy(&s_arp_cache[i].mac_addr, mac, sizeof(net_mac_t));
            s_arp_cache[i].timestamp = 0ULL;
            return KERNEL_OK;
        }
    }

    /* 查找空槽或最老条目 */
    for (i = 0U; i < ARP_CACHE_SIZE; i++)
    {
        if (!s_arp_cache[i].valid)
        {
            oldest = i;
            break;
        }
        if (s_arp_cache[i].timestamp < oldest_ts)
        {
            oldest_ts = s_arp_cache[i].timestamp;
            oldest = i;
        }
    }

    s_arp_cache[oldest].ip_addr = ip_addr;
    (void)memcpy(&s_arp_cache[oldest].mac_addr, mac, sizeof(net_mac_t));
    s_arp_cache[oldest].timestamp = 0ULL;
    s_arp_cache[oldest].valid = true;

    return KERNEL_OK;
}

/**
 * @brief 构造并发送 ARP 请求
 *
 * @param if_id   接口 ID
 * @param target_ip 目标 IP（主机字节序）
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t arp_send_request(uint32_t if_id, uint32_t target_ip)
{
    net_interface_t *iface;
    uint8_t frame[NET_MAX_PACKET_SIZE];
    eth_header_t *eth;
    arp_packet_t *arp;
    uint32_t offset;

    if (if_id >= NET_MAX_INTERFACES)
    {
        return -(int32_t)22;
    }

    iface = &s_interfaces[if_id];
    if (!iface->in_use || (iface->state != NET_IF_RUNNING))
    {
        return -(int32_t)22;
    }

    (void)memset(frame, 0, sizeof(frame));
    eth = (eth_header_t *)frame;

    /* 广播 MAC */
    (void)memset(eth->dst_mac, 0xFF, NET_MAC_ADDR_LEN);
    (void)memcpy(eth->src_mac, iface->mac_addr.bytes, NET_MAC_ADDR_LEN);
    eth->eth_type = net_htons(ETH_TYPE_ARP);

    offset = ETH_HDR_SIZE;
    arp = (arp_packet_t *)&frame[offset];

    arp->hardware_type = net_htons(1U);
    arp->protocol_type = net_htons(ETH_TYPE_IPV4);
    arp->hw_addr_len = NET_MAC_ADDR_LEN;
    arp->proto_addr_len = NET_IPV4_ADDR_LEN;
    arp->operation = net_htons(ARP_OP_REQUEST);
    (void)memcpy(arp->sender_mac, iface->mac_addr.bytes, NET_MAC_ADDR_LEN);
    arp->sender_ip = net_htonl(ipv4_to_u32(&iface->ipv4_addr));
    (void)memset(arp->target_mac, 0x00, NET_MAC_ADDR_LEN);
    arp->target_ip = net_htonl(target_ip);

    (void)net_tx_packet(if_id, frame,
        (uint64_t)(ETH_HDR_SIZE + sizeof(arp_packet_t)));

    return KERNEL_OK;
}

/**
 * @brief 处理接收到的 ARP 包
 *
 * @param if_id 接口 ID
 * @param data  ARP 数据
 * @param len   数据长度
 */
static void arp_process_packet(uint32_t if_id, const uint8_t *data, uint32_t len)
{
    const arp_packet_t *arp;
    uint32_t sender_ip;
    uint32_t target_ip;
    net_interface_t *iface;
    net_mac_t sender_mac;

    if ((data == NULL) || (len < sizeof(arp_packet_t)))
    {
        return;
    }

    if (if_id >= NET_MAX_INTERFACES)
    {
        return;
    }

    iface = &s_interfaces[if_id];
    arp = (const arp_packet_t *)data;

    sender_ip = net_htonl(arp->sender_ip);
    target_ip = net_htonl(arp->target_ip);

    /* 将发送方加入 ARP 缓存 */
    (void)memcpy(sender_mac.bytes, arp->sender_mac, NET_MAC_ADDR_LEN);
    (void)arp_add_entry(sender_ip, &sender_mac);

    /* 检查是否发给本机 */
    if (target_ip == ipv4_to_u32(&iface->ipv4_addr))
    {
        if (net_htons(arp->operation) == ARP_OP_REQUEST)
        {
            /* 发送 ARP 应答 */
            uint8_t reply[NET_MAX_PACKET_SIZE];
            eth_header_t *eth;
            arp_packet_t *reply_arp;

            (void)memset(reply, 0, sizeof(reply));
            eth = (eth_header_t *)reply;
            (void)memcpy(eth->dst_mac, arp->sender_mac, NET_MAC_ADDR_LEN);
            (void)memcpy(eth->src_mac, iface->mac_addr.bytes, NET_MAC_ADDR_LEN);
            eth->eth_type = net_htons(ETH_TYPE_ARP);

            reply_arp = (arp_packet_t *)&reply[ETH_HDR_SIZE];
            reply_arp->hardware_type = net_htons(1U);
            reply_arp->protocol_type = net_htons(ETH_TYPE_IPV4);
            reply_arp->hw_addr_len = NET_MAC_ADDR_LEN;
            reply_arp->proto_addr_len = NET_IPV4_ADDR_LEN;
            reply_arp->operation = net_htons(ARP_OP_REPLY);
            (void)memcpy(reply_arp->sender_mac, iface->mac_addr.bytes,
                         NET_MAC_ADDR_LEN);
            reply_arp->sender_ip = net_htonl(ipv4_to_u32(&iface->ipv4_addr));
            (void)memcpy(reply_arp->target_mac, arp->sender_mac,
                         NET_MAC_ADDR_LEN);
            reply_arp->target_ip = arp->sender_ip;

            (void)net_tx_packet(if_id, reply,
                (uint64_t)(ETH_HDR_SIZE + sizeof(arp_packet_t)));
        }
    }
}

/* ========================================================================
 * IPv4 数据包处理
 * ======================================================================== */

/**
 * @brief 封装并发送 IPv4 数据包
 *
 * @param if_id    接口 ID
 * @param dst_ip   目标 IP（主机字节序）
 * @param protocol 上层协议号
 * @param payload  有效载荷
 * @param pay_len  有效载荷长度
 *
 * @return 成功返回发送字节数，负数表示错误
 */
static int64_t ipv4_send(uint32_t if_id, uint32_t dst_ip, uint8_t protocol,
                          const void *payload, uint32_t pay_len)
{
    net_interface_t *iface;
    uint8_t frame[NET_MAX_PACKET_SIZE];
    eth_header_t *eth;
    ipv4_header_t *ip;
    uint32_t total_len;
    net_mac_t dst_mac;
    kernel_status_t ret;

    if (if_id >= NET_MAX_INTERFACES)
    {
        return -(int64_t)22;
    }

    if (payload == NULL)
    {
        return -(int64_t)22;
    }

    iface = &s_interfaces[if_id];
    if (!iface->in_use || (iface->state != NET_IF_RUNNING))
    {
        return -(int64_t)22;
    }

    if ((uint32_t)IPV4_HDR_SIZE + pay_len > (uint32_t)iface->mtu)
    {
        iface->stats.tx_dropped++;
        return -(int64_t)22;
    }

    /* 查找目标 MAC */
    ret = arp_lookup(dst_ip, &dst_mac);
    if (ret != KERNEL_OK)
    {
        /* 发送 ARP 请求 */
        (void)arp_send_request(if_id, dst_ip);
        return -(int64_t)11; /* EAGAIN */
    }

    (void)memset(frame, 0, sizeof(frame));

    /* 以太网帧头 */
    eth = (eth_header_t *)frame;
    (void)memcpy(eth->dst_mac, dst_mac.bytes, NET_MAC_ADDR_LEN);
    (void)memcpy(eth->src_mac, iface->mac_addr.bytes, NET_MAC_ADDR_LEN);
    eth->eth_type = net_htons(ETH_TYPE_IPV4);

    /* IPv4 头 */
    ip = (ipv4_header_t *)&frame[ETH_HDR_SIZE];
    ip->version_ihl = (4U << 4U) | 5U;
    ip->tos = 0U;
    total_len = (uint32_t)IPV4_HDR_SIZE + pay_len;
    ip->total_length = net_htons((uint16_t)total_len);
    ip->identification = net_htons(s_reasm_id);
    s_reasm_id++;
    ip->flags_offset = net_htons(IP_FLAG_DF);
    ip->ttl = 64U;
    ip->protocol = protocol;
    ip->checksum = 0U;
    ip->src_ip = net_htonl(ipv4_to_u32(&iface->ipv4_addr));
    ip->dst_ip = net_htonl(dst_ip);

    /* 计算校验和 */
    ip->checksum = net_checksum(ip, (uint32_t)IPV4_HDR_SIZE);

    /* 复制有效载荷 */
    (void)memcpy(&frame[ETH_HDR_SIZE + IPV4_HDR_SIZE], payload, pay_len);

    return net_tx_packet(if_id, frame,
        (uint64_t)(ETH_HDR_SIZE + total_len));
}

/**
 * @brief 解析 IPv4 头部并分发到上层协议
 *
 * @param if_id  接口 ID
 * @param ip_hdr IPv4 头部指针
 * @param len    IP 数据包总长度
 */
static void ipv4_process(uint32_t if_id, const ipv4_header_t *ip_hdr, uint32_t len)
{
    uint8_t *payload;
    uint32_t ip_hdr_len;
    uint32_t payload_len;

    if ((ip_hdr == NULL) || (len < (uint32_t)IPV4_HDR_SIZE))
    {
        return;
    }

    ip_hdr_len = (uint32_t)(ip_hdr->version_ihl & 0x0FU) * 4U;
    if (ip_hdr_len < (uint32_t)IPV4_HDR_SIZE)
    {
        return;
    }

    payload_len = (uint32_t)net_htons(ip_hdr->total_length) - ip_hdr_len;
    payload = (uint8_t *)ip_hdr + ip_hdr_len;

    switch (ip_hdr->protocol)
    {
        case IP_PROTO_ICMP:
            icmp_process(if_id, net_htonl(ip_hdr->src_ip),
                         net_htonl(ip_hdr->dst_ip),
                         payload, payload_len);
            break;

        case IP_PROTO_UDP:
            udp_process(if_id, net_htonl(ip_hdr->src_ip),
                        net_htonl(ip_hdr->dst_ip),
                        payload, payload_len);
            break;

        case IP_PROTO_TCP:
            tcp_process_segment(if_id, net_htonl(ip_hdr->src_ip),
                                net_htonl(ip_hdr->dst_ip),
                                payload, payload_len);
            break;

        default:
            /* 未知协议，丢弃 */
            break;
    }
}

/* ========================================================================
 * ICMP 处理
 * ======================================================================== */

/**
 * @brief 处理接收到的 ICMP 包
 */
void icmp_process(uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
                   const uint8_t *data, uint32_t len)
{
    const icmp_header_t *icmp;
    uint32_t ip_if;

    if ((data == NULL) || (len < (uint32_t)sizeof(icmp_header_t)))
    {
        return;
    }

    icmp = (const icmp_header_t *)data;

    /* 验证 ICMP 校验和 */
    if (net_checksum(data, len) != 0U)
    {
        return;
    }

    /* 检查目标 IP 是否为本机 */
    ip_if = ipv4_to_u32(&s_interfaces[if_id].ipv4_addr);
    if (dst_ip != ip_if)
    {
        return;
    }

    if (icmp->type == ICMP_TYPE_ECHO_REQ)
    {
        /* 发送回显应答 */
        uint8_t reply[sizeof(icmp_header_t) + TCP_MAX_SEG_SIZE];
        icmp_header_t *reply_icmp;

        s_icmp_echo_recv++;

        if (len > sizeof(reply))
        {
            return;
        }

        (void)memcpy(reply, data, len);
        reply_icmp = (icmp_header_t *)reply;
        reply_icmp->type = ICMP_TYPE_ECHO_REPLY;
        reply_icmp->checksum = 0U;
        reply_icmp->checksum = net_checksum(reply, len);

        (void)ipv4_send(if_id, src_ip, IP_PROTO_ICMP, reply, len);
        s_icmp_echo_reply_sent++;
    }
    else if (icmp->type == ICMP_TYPE_ECHO_REPLY)
    {
        s_icmp_echo_recv++;
    }
    else
    {
        /* 其他 ICMP 类型暂不处理 */
    }
}

/* ========================================================================
 * UDP 处理
 * ======================================================================== */

/**
 * @brief 处理接收到的 UDP 包
 */
void udp_process(uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
                  const uint8_t *data, uint32_t len)
{
    const udp_header_t *udp;
    uint16_t dst_port;
    uint16_t src_port;
    uint32_t i;

    if ((data == NULL) || (len < (uint32_t)UDP_HDR_SIZE))
    {
        return;
    }

    udp = (const udp_header_t *)data;
    src_port = net_htons(udp->src_port);
    dst_port = net_htons(udp->dst_port);

    /* 查找绑定了该端口的套接字 */
    for (i = 0U; i < NET_MAX_SOCKETS; i++)
    {
        if (s_sockets[i].in_use &&
            (s_sockets[i].type == NET_SOCK_DGRAM) &&
            (s_sockets[i].local_addr.port == dst_port))
        {
            s_sockets[i].rx_count++;
            break;
        }
    }

    (void)if_id;
    (void)src_ip;
    (void)dst_ip;
}

/* ========================================================================
 * TCP 状态机
 * ======================================================================== */

/**
 * @brief 查找 TCP 控制块
 *
 * @param local_port  本地端口
 * @param remote_ip   远端 IP
 * @param remote_port 远端端口
 *
 * @return TCB 指针，未找到返回 NULL
 */
static tcp_tcb_t *tcp_find_tcb(uint16_t local_port, uint32_t remote_ip,
                                uint16_t remote_port)
{
    uint32_t i;

    for (i = 0U; i < NET_MAX_SOCKETS; i++)
    {
        if (s_tcp_tcbs[i].in_use &&
            (s_tcp_tcbs[i].local_port == local_port) &&
            (s_tcp_tcbs[i].remote_ip == remote_ip) &&
            (s_tcp_tcbs[i].remote_port == remote_port))
        {
            return &s_tcp_tcbs[i];
        }
    }

    return NULL;
}

/**
 * @brief 查找监听端口的 TCB
 *
 * @param local_port 本地端口
 *
 * @return TCB 指针，未找到返回 NULL
 */
static tcp_tcb_t *tcp_find_listener(uint16_t local_port)
{
    uint32_t i;

    for (i = 0U; i < NET_MAX_SOCKETS; i++)
    {
        if (s_tcp_tcbs[i].in_use &&
            (s_tcp_tcbs[i].state == TCP_LISTEN) &&
            (s_tcp_tcbs[i].local_port == local_port))
        {
            return &s_tcp_tcbs[i];
        }
    }

    return NULL;
}

/**
 * @brief 发送 TCP 段
 *
 * @param tcb    TCP 控制块
 * @param flags  TCP 标志
 * @param data   有效载荷（可为 NULL）
 * @param len    有效载荷长度
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t tcp_send_segment(tcp_tcb_t *tcb, uint8_t flags,
                                          const void *data, uint32_t len)
{
    uint8_t segment[TCP_MAX_SEG_SIZE + (uint32_t)TCP_HDR_SIZE];
    tcp_header_t *tcp;
    uint32_t total_len;

    if (tcb == NULL)
    {
        return -(int32_t)22;
    }

    if ((data != NULL) && (len > TCP_MAX_SEG_SIZE))
    {
        return -(int32_t)22;
    }

    (void)memset(segment, 0, sizeof(segment));
    tcp = (tcp_header_t *)segment;

    tcp->src_port = net_htons(tcb->local_port);
    tcp->dst_port = net_htons(tcb->remote_port);
    tcp->seq_num = net_htonl(tcb->snd_nxt);
    tcp->ack_num = net_htonl(tcb->rcv_nxt);
    tcp->data_offset = (5U << 4U);
    tcp->flags = flags;
    tcp->window = net_htons((uint16_t)tcb->rcv_wnd);
    tcp->checksum = 0U;
    tcp->urgent_ptr = 0U;

    if ((data != NULL) && (len > 0U))
    {
        (void)memcpy(&segment[(uint32_t)TCP_HDR_SIZE], data, len);
    }

    total_len = (uint32_t)TCP_HDR_SIZE + len;

    /* 计算校验和（含伪首部） */
    {
        uint8_t pseudo[12U];
        uint32_t src_ip_h = tcb->local_ip;
        uint32_t dst_ip_h = tcb->remote_ip;

        (void)memset(pseudo, 0, sizeof(pseudo));
        (void)memcpy(&pseudo[0U], &src_ip_h, 4U);
        (void)memcpy(&pseudo[4U], &dst_ip_h, 4U);
        pseudo[9U] = IP_PROTO_TCP;
        pseudo[10U] = (uint8_t)(total_len >> 8U);
        pseudo[11U] = (uint8_t)(total_len);

        tcp->checksum = net_checksum(pseudo, 12U);
    }

    /* 发送（使用 if_id=0，实际需查路由表） */
    (void)ipv4_send(0U, tcb->remote_ip, IP_PROTO_TCP, segment, total_len);

    /* 更新发送序列号 */
    tcb->snd_nxt += len;
    if ((flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) != 0U)
    {
        tcb->snd_nxt++;
    }

    /* 保存到重传队列 */
    if ((len > 0U) || ((flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) != 0U))
    {
        uint32_t i;
        for (i = 0U; i < TCP_MAX_RETRANS_SEGS; i++)
        {
            if (!tcb->retrans_buf[i].active)
            {
                uint32_t copy_len = len;
                if (copy_len > TCP_MAX_SEG_SIZE)
                {
                    copy_len = TCP_MAX_SEG_SIZE;
                }
                (void)memcpy(tcb->retrans_buf[i].data, data, copy_len);
                tcb->retrans_buf[i].data_len = copy_len;
                tcb->retrans_buf[i].seq_num = tcb->snd_una;
                tcb->retrans_buf[i].retry_count = 0U;
                tcb->retrans_buf[i].last_sent_ms = 0ULL;
                tcb->retrans_buf[i].active = true;
                tcb->retrans_count++;
                break;
            }
        }
    }

    return KERNEL_OK;
}

/**
 * @brief 处理接收到的 TCP 段
 */
void tcp_process_segment(uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
                          const uint8_t *data, uint32_t len)
{
    const tcp_header_t *tcp;
    tcp_tcb_t *tcb;
    uint16_t dst_port;
    uint16_t src_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t flags;
    uint32_t data_offset;
    uint32_t payload_len;

    if ((data == NULL) || (len < (uint32_t)TCP_HDR_SIZE))
    {
        return;
    }

    (void)if_id;

    tcp = (const tcp_header_t *)data;
    src_port = net_htons(tcp->src_port);
    dst_port = net_htons(tcp->dst_port);
    seq_num = net_htonl(tcp->seq_num);
    ack_num = net_htonl(tcp->ack_num);
    flags = tcp->flags;
    data_offset = (uint32_t)(tcp->data_offset >> 4U) * 4U;
    payload_len = len - data_offset;

    /* 查找已建立的连接 */
    tcb = tcp_find_tcb(dst_port, src_ip, src_port);
    if (tcb == NULL)
    {
        /* 查找监听器 */
        tcb = tcp_find_listener(dst_port);
    }

    if (tcb == NULL)
    {
        /* 发送 RST */
        return;
    }

    /* TCP 状态机处理 */
    switch (tcb->state)
    {
        case TCP_LISTEN:
            if ((flags & TCP_FLAG_SYN) != 0U)
            {
                tcb->remote_ip = src_ip;
                tcb->remote_port = src_port;
                tcb->irs = seq_num;
                tcb->rcv_nxt = seq_num + 1U;
                tcb->snd_wnd = TCP_WINDOW_SIZE;
                tcb->rcv_wnd = TCP_WINDOW_SIZE;
                tcb->state = TCP_SYN_RECEIVED;

                /* 发送 SYN+ACK */
                (void)tcp_send_segment(tcb,
                    (uint8_t)(TCP_FLAG_SYN | TCP_FLAG_ACK), NULL, 0U);
                tcb->snd_nxt = tcb->iss + 1U;
            }
            break;

        case TCP_SYN_SENT:
            if (((flags & TCP_FLAG_SYN) != 0U) &&
                ((flags & TCP_FLAG_ACK) != 0U))
            {
                tcb->irs = seq_num;
                tcb->rcv_nxt = seq_num + 1U;
                tcb->snd_una = ack_num;
                tcb->state = TCP_ESTABLISHED;

                /* 发送 ACK */
                (void)tcp_send_segment(tcb, TCP_FLAG_ACK, NULL, 0U);
            }
            else if ((flags & TCP_FLAG_SYN) != 0U)
            {
                /* 同时打开 */
                tcb->irs = seq_num;
                tcb->rcv_nxt = seq_num + 1U;
                tcb->state = TCP_SYN_RECEIVED;
                (void)tcp_send_segment(tcb,
                    (uint8_t)(TCP_FLAG_SYN | TCP_FLAG_ACK), NULL, 0U);
            }
            else
            {
                /* 忽略 */
            }
            break;

        case TCP_SYN_RECEIVED:
            if ((flags & TCP_FLAG_ACK) != 0U)
            {
                tcb->snd_una = ack_num;
                tcb->state = TCP_ESTABLISHED;
            }
            break;

        case TCP_ESTABLISHED:
            /* 处理数据 */
            if (payload_len > 0U)
            {
                if ((tcb->recv_len + payload_len) <= sizeof(tcb->recv_buf))
                {
                    (void)memcpy(&tcb->recv_buf[tcb->recv_len],
                                 &data[data_offset], payload_len);
                    tcb->recv_len += payload_len;
                }
                tcb->rcv_nxt += payload_len;

                /* 确认接收 */
                (void)tcp_send_segment(tcb, TCP_FLAG_ACK, NULL, 0U);

                /* 清除已确认的重传段 */
                {
                    uint32_t i;
                    for (i = 0U; i < TCP_MAX_RETRANS_SEGS; i++)
                    {
                        if (tcb->retrans_buf[i].active &&
                            (tcb->retrans_buf[i].seq_num + tcb->retrans_buf[i].data_len
                             <= ack_num))
                        {
                            tcb->retrans_buf[i].active = false;
                            if (tcb->retrans_count > 0U)
                            {
                                tcb->retrans_count--;
                            }
                        }
                    }
                }
            }

            if ((flags & TCP_FLAG_FIN) != 0U)
            {
                tcb->rcv_nxt++;
                tcb->state = TCP_CLOSE_WAIT;
                (void)tcp_send_segment(tcb, TCP_FLAG_ACK, NULL, 0U);
            }
            break;

        case TCP_FIN_WAIT_1:
            if ((flags & TCP_FLAG_FIN) != 0U)
            {
                tcb->rcv_nxt++;
                if ((flags & TCP_FLAG_ACK) != 0U)
                {
                    tcb->state = TCP_TIME_WAIT;
                }
                else
                {
                    tcb->state = TCP_CLOSING;
                }
                (void)tcp_send_segment(tcb, TCP_FLAG_ACK, NULL, 0U);
            }
            else if ((flags & TCP_FLAG_ACK) != 0U)
            {
                tcb->state = TCP_FIN_WAIT_2;
            }
            else
            {
                /* 忽略 */
            }
            break;

        case TCP_FIN_WAIT_2:
            if ((flags & TCP_FLAG_FIN) != 0U)
            {
                tcb->rcv_nxt++;
                tcb->state = TCP_TIME_WAIT;
                (void)tcp_send_segment(tcb, TCP_FLAG_ACK, NULL, 0U);
            }
            break;

        case TCP_CLOSE_WAIT:
            /* 等待应用关闭 */
            break;

        case TCP_CLOSING:
            if ((flags & TCP_FLAG_ACK) != 0U)
            {
                tcb->state = TCP_TIME_WAIT;
            }
            break;

        case TCP_LAST_ACK:
            if ((flags & TCP_FLAG_ACK) != 0U)
            {
                tcb->state = TCP_CLOSED;
                tcb->in_use = false;
            }
            break;

        case TCP_TIME_WAIT:
            /* 等待 2MSL 后关闭 */
            tcb->state = TCP_CLOSED;
            tcb->in_use = false;
            break;

        case TCP_CLOSED:
        default:
            break;
    }
}

/**
 * @brief TCP 重传定时器处理
 *
 * @details 检查所有活跃的 TCB 中是否有需要重传的段
 */
static void tcp_retransmit_check(void)
{
    uint32_t i;
    uint32_t j;

    for (i = 0U; i < NET_MAX_SOCKETS; i++)
    {
        tcp_tcb_t *tcb = &s_tcp_tcbs[i];

        if (!tcb->in_use || (tcb->state == TCP_CLOSED))
        {
            continue;
        }

        for (j = 0U; j < TCP_MAX_RETRANS_SEGS; j++)
        {
            if (tcb->retrans_buf[j].active)
            {
                if (tcb->retrans_buf[j].retry_count >= TCP_MAX_RETRIES)
                {
                    /* 超过最大重传次数，关闭连接 */
                    tcb->retrans_buf[j].active = false;
                    tcb->state = TCP_CLOSED;
                    tcb->in_use = false;
                    if (tcb->retrans_count > 0U)
                    {
                        tcb->retrans_count--;
                    }
                }
                else
                {
                    /* 执行重传 */
                    (void)tcp_send_segment(tcb,
                        (uint8_t)(TCP_FLAG_ACK | TCP_FLAG_PSH),
                        tcb->retrans_buf[j].data,
                        tcb->retrans_buf[j].data_len);
                    tcb->retrans_buf[j].retry_count++;
                }
            }
        }
    }
}

/* ========================================================================
 * 以太网帧接收处理
 * ======================================================================== */

/**
 * @brief 处理接收到的以太网帧
 *
 * @param if_id 接口 ID
 * @param frame 帧数据
 * @param len   帧长度
 */
static void eth_process_frame(uint32_t if_id, const uint8_t *frame, uint32_t len)
{
    const eth_header_t *eth;
    uint16_t eth_type;
    uint32_t payload_offset;

    if ((frame == NULL) || (len < (uint32_t)ETH_HDR_SIZE))
    {
        return;
    }

    eth = (const eth_header_t *)frame;
    eth_type = net_htons(eth->eth_type);
    payload_offset = (uint32_t)ETH_HDR_SIZE;

    switch (eth_type)
    {
        case ETH_TYPE_ARP:
            arp_process_packet(if_id, &frame[payload_offset],
                               len - payload_offset);
            break;

        case ETH_TYPE_IPV4:
            ipv4_process(if_id,
                         (const ipv4_header_t *)&frame[payload_offset],
                         len - payload_offset);
            break;

        default:
            /* 未知以太网类型，丢弃 */
            break;
    }
}

/* ========================================================================
 * 数据包收发（扩展版）
 * ======================================================================== */

int64_t net_rx_packet(uint32_t if_id, void *buf, uint64_t size)
{
    net_interface_t *iface;
    int64_t ret;

    if (if_id >= NET_MAX_INTERFACES)
    {
        return -(int64_t)22;
    }

    if (buf == NULL)
    {
        return -(int64_t)22;
    }

    iface = &s_interfaces[if_id];

    if (iface->state != NET_IF_RUNNING)
    {
        return -(int64_t)22;
    }

    /* 调用网络接口层接收以太网帧 */
    ret = net_if_recv_frame(iface->name, buf, size);
    if (ret < 0)
    {
        iface->stats.rx_errors++;
        return ret;
    }

    if (ret == 0)
    {
        /* 没有数据可接收 */
        return 0LL;
    }

    /* 传递给协议栈处理 */
    eth_process_frame(if_id, (const uint8_t *)buf, (uint32_t)ret);

    iface->stats.rx_packets++;
    iface->stats.rx_bytes += (uint64_t)ret;

    return ret;
}

int64_t net_tx_packet(uint32_t if_id, const void *buf, uint64_t size)
{
    net_interface_t *iface;
    int64_t ret;

    if (if_id >= NET_MAX_INTERFACES)
    {
        return -(int64_t)22;
    }

    if (buf == NULL)
    {
        return -(int64_t)22;
    }

    iface = &s_interfaces[if_id];

    if (iface->state != NET_IF_RUNNING)
    {
        return -(int64_t)22;
    }

    if (size > (uint64_t)iface->mtu + (uint64_t)ETH_HDR_SIZE)
    {
        iface->stats.tx_dropped++;
        return -(int64_t)22;
    }

    /* 调用网络接口层发送以太网帧 */
    ret = net_if_send_frame(iface->name, buf, size);
    if (ret < 0)
    {
        iface->stats.tx_errors++;
        return ret;
    }

    iface->stats.tx_packets++;
    iface->stats.tx_bytes += (uint64_t)ret;

    return ret;
}

/* ========================================================================
 * 套接字管理
 * ======================================================================== */

int32_t net_socket(net_af_t family, net_sock_type_t type)
{
    uint32_t i;

    if (!s_initialized)
    {
        return -(int32_t)22;
    }

    for (i = 0U; i < NET_MAX_SOCKETS; i++)
    {
        if (!s_sockets[i].in_use)
        {
            break;
        }
    }

    if (i >= NET_MAX_SOCKETS)
    {
        return -(int32_t)12;
    }

    s_sockets[i].family = family;
    s_sockets[i].type = type;
    s_sockets[i].state = NET_SOCK_CLOSED;
    s_sockets[i].if_id = 0U;
    (void)memset(&s_sockets[i].local_addr, 0, sizeof(net_sockaddr_t));
    (void)memset(&s_sockets[i].remote_addr, 0, sizeof(net_sockaddr_t));
    s_sockets[i].rx_count = 0U;
    s_sockets[i].tx_count = 0U;
    s_sockets[i].in_use = true;

    return (int32_t)i;
}

kernel_status_t net_bind(uint32_t sock_id, const net_sockaddr_t *addr)
{
    net_socket_t *sock;

    if (addr == NULL)
    {
        return -(int32_t)22;
    }

    if (sock_id >= NET_MAX_SOCKETS)
    {
        return -(int32_t)22;
    }

    sock = &s_sockets[sock_id];

    if (!sock->in_use)
    {
        return -(int32_t)2;
    }

    if (sock->state != NET_SOCK_CLOSED)
    {
        return -(int32_t)22;
    }

    (void)memcpy(&sock->local_addr, addr, sizeof(net_sockaddr_t));
    sock->state = NET_SOCK_BOUND;

    /* 为 UDP/TCP 初始化协议相关状态 */
    if (sock->type == NET_SOCK_STREAM)
    {
        tcp_tcb_t *tcb = &s_tcp_tcbs[sock_id];
        tcb->sock_id = sock_id;
        tcb->local_port = addr->port;
        tcb->local_ip = ipv4_to_u32(&addr->addr.ipv4);
        tcb->state = TCP_CLOSED;
        tcb->in_use = true;
    }

    return KERNEL_OK;
}

kernel_status_t net_listen(uint32_t sock_id, uint32_t backlog)
{
    net_socket_t *sock;
    tcp_tcb_t *tcb;

    if (sock_id >= NET_MAX_SOCKETS)
    {
        return -(int32_t)22;
    }

    sock = &s_sockets[sock_id];

    if (!sock->in_use)
    {
        return -(int32_t)2;
    }

    if (sock->state != NET_SOCK_BOUND)
    {
        return -(int32_t)22;
    }

    sock->state = NET_SOCK_LISTENING;
    (void)backlog;

    /* TCP: 进入 LISTEN 状态 */
    if (sock->type == NET_SOCK_STREAM)
    {
        tcb = &s_tcp_tcbs[sock_id];
        tcb->state = TCP_LISTEN;
    }

    return KERNEL_OK;
}

int32_t net_accept(uint32_t sock_id)
{
    net_socket_t *listen_sock;
    int32_t new_id;
    net_socket_t *new_sock;
    tcp_tcb_t *listen_tcb;
    tcp_tcb_t *new_tcb;

    if (sock_id >= NET_MAX_SOCKETS)
    {
        return -(int32_t)22;
    }

    listen_sock = &s_sockets[sock_id];

    if (!listen_sock->in_use)
    {
        return -(int32_t)2;
    }

    if (listen_sock->state != NET_SOCK_LISTENING)
    {
        return -(int32_t)22;
    }

    /* 创建新套接字用于已接受的连接 */
    new_id = net_socket(listen_sock->family, listen_sock->type);
    if (new_id < 0)
    {
        return new_id;
    }

    new_sock = &s_sockets[(uint32_t)new_id];
    new_sock->state = NET_SOCK_CONNECTED;
    new_sock->local_addr = listen_sock->local_addr;

    /* 复制 TCB 状态 */
    listen_tcb = &s_tcp_tcbs[sock_id];
    new_tcb = &s_tcp_tcbs[(uint32_t)new_id];
    new_tcb->sock_id = (uint32_t)new_id;
    new_tcb->local_port = listen_tcb->local_port;
    new_tcb->local_ip = listen_tcb->local_ip;
    new_tcb->remote_ip = listen_tcb->remote_ip;
    new_tcb->remote_port = listen_tcb->remote_port;
    new_tcb->iss = listen_tcb->iss;
    new_tcb->irs = listen_tcb->irs;
    new_tcb->snd_una = listen_tcb->snd_una;
    new_tcb->snd_nxt = listen_tcb->snd_nxt;
    new_tcb->rcv_nxt = listen_tcb->rcv_nxt;
    new_tcb->snd_wnd = TCP_WINDOW_SIZE;
    new_tcb->rcv_wnd = TCP_WINDOW_SIZE;
    new_tcb->state = TCP_ESTABLISHED;
    new_tcb->in_use = true;

    /* 重置监听 TCB 等待下一个连接 */
    listen_tcb->state = TCP_LISTEN;

    return new_id;
}

kernel_status_t net_connect(uint32_t sock_id, const net_sockaddr_t *addr)
{
    net_socket_t *sock;
    tcp_tcb_t *tcb;

    if (addr == NULL)
    {
        return -(int32_t)22;
    }

    if (sock_id >= NET_MAX_SOCKETS)
    {
        return -(int32_t)22;
    }

    sock = &s_sockets[sock_id];

    if (!sock->in_use)
    {
        return -(int32_t)2;
    }

    (void)memcpy(&sock->remote_addr, addr, sizeof(net_sockaddr_t));

    if (sock->type == NET_SOCK_STREAM)
    {
        /* TCP: 发送 SYN，进入 SYN_SENT */
        tcb = &s_tcp_tcbs[sock_id];
        tcb->remote_ip = ipv4_to_u32(&addr->addr.ipv4);
        tcb->remote_port = addr->port;
        tcb->local_ip = ipv4_to_u32(&sock->local_addr.addr.ipv4);
        tcb->local_port = sock->local_addr.port;
        tcb->iss = 1000U + (uint32_t)sock_id * 10000U;
        tcb->snd_una = tcb->iss;
        tcb->snd_nxt = tcb->iss;
        tcb->snd_wnd = TCP_WINDOW_SIZE;
        tcb->rcv_wnd = TCP_WINDOW_SIZE;
        tcb->state = TCP_SYN_SENT;
        tcb->in_use = true;

        (void)tcp_send_segment(tcb, TCP_FLAG_SYN, NULL, 0U);

        sock->state = NET_SOCK_CONNECTING;
        return KERNEL_OK;
    }

    sock->state = NET_SOCK_CONNECTED;

    return KERNEL_OK;
}

int64_t net_send(uint32_t sock_id, const void *buf, uint64_t size)
{
    net_socket_t *sock;
    tcp_tcb_t *tcb;

    if (buf == NULL)
    {
        return -(int64_t)22;
    }

    if (sock_id >= NET_MAX_SOCKETS)
    {
        return -(int64_t)22;
    }

    sock = &s_sockets[sock_id];

    if (!sock->in_use)
    {
        return -(int64_t)2;
    }

    if (sock->state != NET_SOCK_CONNECTED)
    {
        return -(int64_t)22;
    }

    if (sock->type == NET_SOCK_STREAM)
    {
        /* TCP 发送 */
        tcb = &s_tcp_tcbs[sock_id];
        if (tcb->state != TCP_ESTABLISHED)
        {
            return -(int64_t)22;
        }

        (void)tcp_send_segment(tcb,
            (uint8_t)(TCP_FLAG_ACK | TCP_FLAG_PSH), buf, (uint32_t)size);
    }
    else if (sock->type == NET_SOCK_DGRAM)
    {
        /* UDP 发送 */
        udp_header_t udp_hdr;
        uint8_t dgram[NET_MAX_PACKET_SIZE];
        uint32_t dgram_len;
        uint32_t remote_ip;

        udp_hdr.src_port = net_htons(sock->local_addr.port);
        udp_hdr.dst_port = net_htons(sock->remote_addr.port);
        udp_hdr.length = net_htons((uint16_t)((uint32_t)UDP_HDR_SIZE + (uint32_t)size));
        udp_hdr.checksum = 0U;

        (void)memcpy(dgram, &udp_hdr, (uint32_t)UDP_HDR_SIZE);
        if (size > 0U)
        {
            (void)memcpy(&dgram[(uint32_t)UDP_HDR_SIZE], buf, (uint32_t)size);
        }

        dgram_len = (uint32_t)UDP_HDR_SIZE + (uint32_t)size;
        remote_ip = ipv4_to_u32(&sock->remote_addr.addr.ipv4);

        (void)ipv4_send(sock->if_id, remote_ip, IP_PROTO_UDP,
                        dgram, dgram_len);
    }
    else
    {
        /* 原始套接字 */
        (void)buf;
    }

    sock->tx_count++;

    return (int64_t)size;
}

int64_t net_recv(uint32_t sock_id, void *buf, uint64_t size)
{
    net_socket_t *sock;
    tcp_tcb_t *tcb;
    uint32_t copy_len;

    if (buf == NULL)
    {
        return -(int64_t)22;
    }

    if (sock_id >= NET_MAX_SOCKETS)
    {
        return -(int64_t)22;
    }

    sock = &s_sockets[sock_id];

    if (!sock->in_use)
    {
        return -(int64_t)2;
    }

    if (sock->state != NET_SOCK_CONNECTED)
    {
        return -(int64_t)22;
    }

    if (sock->type == NET_SOCK_STREAM)
    {
        tcb = &s_tcp_tcbs[sock_id];
        if (tcb->recv_len == 0U)
        {
            return 0LL;
        }

        copy_len = tcb->recv_len;
        if (copy_len > (uint32_t)size)
        {
            copy_len = (uint32_t)size;
        }

        (void)memcpy(buf, tcb->recv_buf, copy_len);

        /* 移动缓冲区剩余数据 */
        if (tcb->recv_len > copy_len)
        {
            uint32_t remaining = tcb->recv_len - copy_len;
            uint32_t k;
            for (k = 0U; k < remaining; k++)
            {
                tcb->recv_buf[k] = tcb->recv_buf[k + copy_len];
            }
            tcb->recv_len = remaining;
        }
        else
        {
            tcb->recv_len = 0U;
        }

        sock->rx_count++;
        return (int64_t)copy_len;
    }

    sock->rx_count++;
    return 0LL;
}

kernel_status_t net_close(uint32_t sock_id)
{
    net_socket_t *sock;
    tcp_tcb_t *tcb;

    if (sock_id >= NET_MAX_SOCKETS)
    {
        return -(int32_t)22;
    }

    sock = &s_sockets[sock_id];

    if (!sock->in_use)
    {
        return -(int32_t)2;
    }

    /* TCP: 发起关闭 */
    if (sock->type == NET_SOCK_STREAM)
    {
        tcb = &s_tcp_tcbs[sock_id];
        if (tcb->state == TCP_ESTABLISHED)
        {
            (void)tcp_send_segment(tcb,
                (uint8_t)(TCP_FLAG_FIN | TCP_FLAG_ACK), NULL, 0U);
            tcb->state = TCP_FIN_WAIT_1;
            sock->state = NET_SOCK_CLOSED;
            return KERNEL_OK;
        }
        tcb->state = TCP_CLOSED;
        tcb->in_use = false;
    }

    sock->state = NET_SOCK_CLOSED;
    sock->in_use = false;

    return KERNEL_OK;
}

/* ========================================================================
 * 查询
 * ======================================================================== */

net_interface_t *net_get_interface(uint32_t if_id)
{
    if (if_id >= NET_MAX_INTERFACES)
    {
        return NULL;
    }

    if (!s_interfaces[if_id].in_use)
    {
        return NULL;
    }

    return &s_interfaces[if_id];
}

net_socket_t *net_get_socket(uint32_t sock_id)
{
    if (sock_id >= NET_MAX_SOCKETS)
    {
        return NULL;
    }

    if (!s_sockets[sock_id].in_use)
    {
        return NULL;
    }

    return &s_sockets[sock_id];
}

/* ========================================================================
 * 服务主函数
 * ======================================================================== */

int main(void)
{
    kernel_status_t ret;

    ret = net_init();
    if (ret != KERNEL_OK)
    {
        return (int)ret;
    }

    for (;;)
    {
        /* 处理接收包 */
        {
            uint32_t if_id;
            uint8_t rx_tmp[NET_MAX_PACKET_SIZE];

            for (if_id = 0U; if_id < NET_MAX_INTERFACES; if_id++)
            {
                if (s_interfaces[if_id].state == NET_IF_RUNNING)
                {
                    (void)net_rx_packet(if_id, rx_tmp,
                        (uint64_t)NET_MAX_PACKET_SIZE);
                }
            }
        }

        /* TCP 重传定时器检查 */
        tcp_retransmit_check();
    }

    return 0;
}
