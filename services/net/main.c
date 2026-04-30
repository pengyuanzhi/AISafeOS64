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
#include "net_if/net_if_auto.h"

/* ========================================================================
 * TCP/IP 优化功能头文件
 * ======================================================================== */

#include "tcp_cubic.h"
#include "tcp_timestamp.h"
#include "tcp_keepalive.h"
#include "tcp_options.h"

#include <stdio.h>

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
 * TCP 常量
 * ======================================================================== */

/** @brief TCP 最大段大小 */
#define TCP_MSS                 1460U

/* ========================================================================
 * 拥塞控制常量
 * ======================================================================== */

/** @brief CUBIC 慢启动阈值调节因子 */
#define CUBIC_ALPHA             0.7f

/** @brief CUBIC 减速因子 */
#define CUBIC_BETA              0.7f

/** @brief CUBIC 拥塞窗口缩放因子 */
#define CUBIC_CWND_SCALE        10U

/** @brief TCP 最大重复 ACK 次数 */
#define TCP_MAX_DUP_ACKS        3U

/** @brief TCP 延迟 ACK 间隔（毫秒） */
#define TCP_DELAYED_ACK_MS      40U

/** @brief TCP SACK 最大块数 */
#define TCP_MAX_SACK_BLOCKS     4U

/** @brief TCP 重传定时器检查周期（毫秒） */
#define TCP_RETRANSMIT_PERIOD_MS    10U

/** @brief TCP Keepalive 空闲超时（秒） */
#define TCP_KEEPALIVE_IDLE_SEC      7200U

/** @brief TCP Keepalive 探测间隔（秒） */
#define TCP_KEEPALIVE_INTERVAL_SEC  75U

/** @brief TCP Keepalive 最大探测次数 */
#define TCP_KEEPALIVE_MAX_PROBES    9U

/** @brief RTT 初始估计值（毫秒） */
#define TCP_RTT_INITIAL_MS          200U

/** @brief RTO 最小值（毫秒） */
#define TCP_RTO_MIN_MS              200U

/** @brief RTO 最大值（毫秒） */
#define TCP_RTO_MAX_MS              60000U

/** @brief SRTT 平滑因子（alpha = 1/8） */
#define TCP_RTT_ALPHA_SHIFT         3U

/** @brief RTT 偏差平滑因子（beta = 1/4） */
#define TCP_RTT_BETA_SHIFT          2U

/* ========================================================================
 * ICMP 错误消息常量
 * ======================================================================== */

/** @brief ICMP 类型：目的不可达 */
#define ICMP_TYPE_DEST_UNREACH   3U

/** @brief ICMP 类型：超时 */
#define ICMP_TYPE_TIME_EXCEEDED  11U

/** @brief ICMP 类型：参数问题 */
#define ICMP_TYPE_PARAM_PROB     12U

/** @brief ICMP 代码：网络不可达 */
#define ICMP_CODE_NET_UNREACH    0U

/** @brief ICMP 代码：主机不可达 */
#define ICMP_CODE_HOST_UNREACH   1U

/** @brief ICMP 代码：端口不可达 */
#define ICMP_CODE_PORT_UNREACH   3U

/** @brief ICMP 代码：需要分片 */
#define ICMP_CODE_FRAG_NEEDED    4U

/** @brief ICMP 代码：TTL 过期 */
#define ICMP_CODE_TTL_EXPIRED    0U

/** @brief ICMP 代码：重组超时 */
#define ICMP_CODE_REASS_TIME_EXPIRED 1U

/** @brief ICMP 代码：坏的 IP 头 */
#define ICMP_CODE_BAD_HEADER     0U

/* ========================================================================
 * IP 分片重组常量
 * ======================================================================== */

/** @brief IP 分片重组最大队列数 */
#define NET_MAX_REASS_QUEUE     8U

/** @brief IP 分片重组超时（毫秒） */
#define REASS_TIMEOUT_MS        60000U

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
/* ========================================================================
 * 拥塞控制状态枚举
 * ======================================================================== */

/**
 * @brief 拥塞控制状态
 */
typedef enum
{
    CONG_OPEN = 0,             /**< @brief 开启状态 */
    CONG_SLOW_START,           /**< @brief 慢启动 */
    CONG_CONGESTION_AVOIDANCE,  /**< @brief 拥塞避免 */
    CONG_FAST_RECOVERY,        /**< @brief 快速恢复 */
    CONG_TIMEOUT               /**< @brief 超时 */
} congestion_state_t;

/**
 * @brief RTT 测量数据结构
 */
typedef struct
{
    uint32_t rtt_sample;       /**< @brief 最新 RTT 样本 */
    uint32_t rtt_min;          /**< @brief 最小 RTT */
    uint32_t rtt_var;          /**< @brief RTT 偏差 */
    uint32_t srtt;             /**< @brief 平滑 RTT */
    uint32_t rto;              /**< @brief 重传超时 */
} tcp_rtt_t;

/**
 * @brief 拥塞控制数据结构
 */
typedef struct
{
    congestion_state_t state;  /**< @brief 拥塞状态 */
    uint32_t ssthresh;         /**< @brief 慢启动阈值 */
    uint32_t cwnd;             /**< @brief 拥塞窗口 */
    uint32_t w_max;            /**< @brief 峰值窗口 */
    uint64_t last_acks;        /**< @brief 最后确认序列号 */
    uint64_t last_retrans;     /**< @brief 最后重传时间 */
    uint64_t last_rtt_sample;  /**< @brief 最后 RTT 样本 */
    tcp_rtt_t rtt;             /**< @brief RTT 测量 */
    uint32_t dup_acks;         /**< @brief 重复 ACK 计数 */
    uint32_t last_ack;         /**< @brief 最后 ACK */
} tcp_congestion_ctrl_t;

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

    /* 新增：拥塞控制字段 */
    tcp_congestion_ctrl_t cong_ctrl; /**< @brief 拥塞控制 */

    /* 新增：SACK 字段 */
    uint8_t           sack_permitted;   /**< @brief SACK 允许标志 */
    uint32_t          sack_left[4];     /**< @brief SACK 左边界 */
    uint32_t          sack_right[4];    /**< @brief SACK 右边界 */
    uint8_t           sack_count;       /**< @brief SACK 块数量 */

    /* 新增：Nagle 算法字段 */
    uint8_t           nagle_enabled;   /**< @brief Nagle 算法启用 */
    uint8_t           tcp_cork;        /**< @brief Cork 模式 */
    uint8_t           delayed_ack;     /**< @brief 延迟 ACK 计数 */

    /* 新增：CUBIC 算法字段 */
    uint32_t          cubic_cwnd;      /**< @brief CUBIC 窗口 */
    uint32_t          cubic_ssthresh;  /**< @brief CUBIC 慢启动阈值 */
    uint32_t          cubic_w_max;     /**< @brief CUBIC 峰值窗口 */
    uint32_t          cubic_epoch;     /**< @brief CUBIC 时代开始时间 */
    uint32_t          cubic_k;         /**< @brief CUBIC 参数 K */
    uint8_t           cubic_state;     /**< @brief CUBIC 状态 */

    /* 新增：TCP 时间戳字段 */
    uint32_t          ts_val;          /**< @brief 时间戳值 */
    uint32_t          ts_echo_rpl;     /**< @brief 时间戳回显 */
    uint32_t          recent_ts;       /**< @brief 最近接收时间戳 */
    bool              ts_enabled;      /**< @brief 时间戳启用 */

    /* 新增：TCP Keepalive 字段 */
    uint32_t          keepalive_last_active; /**< @brief 最后活跃时间 */
    uint32_t          keepalive_probe_count; /**< @brief 当前探测次数 */
    uint32_t          keepalive_next_probe;  /**< @brief 下一次探测时间 */
    bool              keepalive_enabled;      /**< @brief Keepalive 启用 */
    bool              keepalive_timeout;      /**< @brief Keepalive 超时 */

    /* 新增：TCP 选项字段 */
    uint16_t          mss;             /**< @brief 最大段大小 */
    uint8_t           window_scale;    /**< @brief 窗口缩放因子 */
    uint8_t           mss_negotiated;  /**< @brief MSS 已协商 */
    uint8_t           window_scale_negotiated; /**< @brief 窗口缩放已协商 */

    bool              in_use;           /**< @brief 使用标记 */
} tcp_tcb_t;

/* ========================================================================
 * IP 分片重组数据结构
 * ======================================================================== */

/**
 * @brief IP 分片条目
 */
typedef struct ip_reass_frag_t
{
    uint16_t             frag_offset;     /**< @brief 分片偏移（8 字节单位） */
    uint16_t             frag_id;         /**< @brief 分片标识符 */
    uint32_t             src_ip;          /**< @brief 源 IP 地址 */
    uint32_t             dst_ip;          /**< @brief 目的 IP 地址 */
    uint32_t             len;             /**< @brief 分片长度 */
    uint8_t              data[1500];      /**< @brief 分片数据 */
    uint32_t             data_len;        /**< @brief 数据长度 */
    bool                 in_use;          /**< @brief 使用标记 */
    uint64_t             arrival_time;    /**< @brief 到达时间 */
    struct ip_reass_frag_t *next;         /**< @brief 下一个分片 */
} ip_reass_frag_t;

/**
 * @brief IP 分片重组队列
 */
typedef struct ip_reass_queue_t
{
    ip_reass_frag_t      *head;           /**< @brief 队列头 */
    ip_reass_frag_t      *tail;           /**< @brief 队列尾 */
    uint16_t             frag_id;         /**< @brief 分片标识符 */
    uint32_t             src_ip;          /**< @brief 源 IP 地址 */
    uint32_t             dst_ip;          /**< @brief 目的 IP 地址 */
    uint32_t             total_len;       /**< @brief 总长度 */
    uint16_t             header_offset;   /**< @brief IP 头偏移 */
    uint8_t              protocol;        /**< @brief 上层协议 */
    bool                 in_use;          /**< @brief 使用标记 */
    uint64_t             last_frag_time;  /**< @brief 最后分片到达时间 */
    uint32_t             frag_count;      /**< @brief 分片计数 */
    uint32_t             recv_len;        /**< @brief 已接收长度 */
} ip_reass_queue_t;

/* ========================================================================
 * ICMP 错误消息数据结构
 * ======================================================================== */

/**
 * @brief ICMP 错误消息
 */
typedef struct
{
    uint8_t  type;          /**< @brief 类型 */
    uint8_t  code;          /**< @brief 代码 */
    uint16_t checksum;      /**< @brief 校验和 */
    uint8_t  unused[4];     /**< @brief 未使用 */
    uint8_t  orig_ip[20];   /**< @brief 原始 IP 头（20 字节） */
    uint8_t  orig_data[8];  /**< @brief 原始数据（8 字节） */
} icmp_error_message_t;

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

/** @brief 时间计数器（用于模拟时间） */
static volatile uint64_t s_time_ms = 0ULL;

/** @brief TCP 定时器检查时间累计（毫秒） */
static uint64_t s_tcp_timer_accum_ms = 0ULL;

/** @brief IP 分片重组队列 */
static ip_reass_queue_t s_reass_queues[NET_MAX_REASS_QUEUE];

/** @brief ICMP 回显统计 */
static uint32_t s_icmp_echo_sent;
static uint32_t s_icmp_echo_recv;
static uint32_t s_icmp_echo_reply_sent;

/** @brief 初始化标志 */
static bool s_initialized;

/* ========================================================================
 * UDP 接收队列数据结构
 * ======================================================================== */

/** @brief UDP 接收队列深度 */
#define UDP_RX_QUEUE_DEPTH    16U

/** @brief UDP 接收队列条目 */
typedef struct
{
    bool in_use;                                  /**< @brief 使用标记 */
    uint32_t sock_id;                             /**< @brief 套接字 ID */
    net_sockaddr_t src_addr;                      /**< @brief 源地址 */
    uint8_t data[NET_MAX_PACKET_SIZE];           /**< @brief 数据缓冲区 */
    uint32_t len;                                 /**< @brief 数据长度 */
} udp_rx_entry_t;

/** @brief UDP 接收队列 */
static udp_rx_entry_t s_udp_rx_queue[NET_MAX_SOCKETS][UDP_RX_QUEUE_DEPTH];

/** @brief UDP 接收队列头尾索引 */
static uint32_t s_udp_rx_head[NET_MAX_SOCKETS];
static uint32_t s_udp_rx_tail[NET_MAX_SOCKETS];

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
 * 函数声明（前向声明）
 * ======================================================================== */

/**
 * @brief 更新 RTT 测量和 RTO（Jacobson/Karels 算法）
 */
static void tcp_rtt_update(tcp_tcb_t *tcb, uint32_t rtt_sample);

/**
 * @brief 处理 TCP ACK（重复检测、滑动窗口、RTT 测量）
 */
static void tcp_process_ack(tcp_tcb_t *tcb, uint32_t ack_num);

/**
 * @brief UDP 校验和计算（含伪首部）
 */
static uint16_t udp_checksum_with_pseudo(uint32_t src_ip, uint32_t dst_ip,
                                          const uint8_t *data, uint32_t len);

/**
 * @brief 处理接收到的 UDP 包
 */
static void udp_process(uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
                          const uint8_t *data, uint32_t len);

/**
 * @brief 处理接收到的 ICMP 包
 */
static void icmp_process(uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
                           const uint8_t *data, uint32_t len);

/**
 * @brief 处理接收到的 TCP 段
 */
static void tcp_process_segment(uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
                                  const uint8_t *data, uint32_t len);

/**
 * @brief TCP 重传定时器检查
 */
static void tcp_retransmit_check(void);

/**
 * @brief TCP Keepalive 定时器检查
 */
static void tcp_keepalive_check(void);

/**
 * @brief 初始化 RTT 测量（Jacobson/Karels 默认值）
 */
static void tcp_rtt_init(tcp_tcb_t *tcb);

/**
 * @brief 发送 TCP 段
 */
static kernel_status_t tcp_send_segment(tcp_tcb_t *tcb, uint8_t flags,
                                          const void *data, uint32_t len);

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
    (void)memset(s_udp_rx_queue, 0, sizeof(s_udp_rx_queue));
    (void)memset(s_udp_rx_head, 0, sizeof(s_udp_rx_head));
    (void)memset(s_udp_rx_tail, 0, sizeof(s_udp_rx_tail));

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
        
        /* 初始化拥塞控制 */
        s_tcp_tcbs[i].cong_ctrl.state = CONG_SLOW_START;
        s_tcp_tcbs[i].cong_ctrl.ssthresh = 65535U;
        s_tcp_tcbs[i].cong_ctrl.cwnd = TCP_MSS;
        s_tcp_tcbs[i].cong_ctrl.w_max = 65535U;
        s_tcp_tcbs[i].cong_ctrl.dup_acks = 0U;
        s_tcp_tcbs[i].cong_ctrl.last_ack = 0U;

        /* RTT 测量在 tcp_rtt_init() 中初始化 */

        /* 初始化 Nagle 算法 */
        s_tcp_tcbs[i].nagle_enabled = 1U;
        s_tcp_tcbs[i].tcp_cork = 0U;
        s_tcp_tcbs[i].delayed_ack = 0U;
        
        /* 初始化 SACK */
        s_tcp_tcbs[i].sack_permitted = 1U;
        s_tcp_tcbs[i].sack_count = 0U;
        
        /* 初始化 CUBIC 算法 */
        s_tcp_tcbs[i].cubic_cwnd = TCP_MSS;
        s_tcp_tcbs[i].cubic_ssthresh = 65535U;
        s_tcp_tcbs[i].cubic_w_max = 65535U;
        s_tcp_tcbs[i].cubic_epoch = 0U;
        s_tcp_tcbs[i].cubic_k = 0U;
        s_tcp_tcbs[i].cubic_state = 0U;  /* 慢启动 */
        
        /* 初始化 TCP 时间戳 */
        s_tcp_tcbs[i].ts_val = 0U;
        s_tcp_tcbs[i].ts_echo_rpl = 0U;
        s_tcp_tcbs[i].recent_ts = 0U;
        s_tcp_tcbs[i].ts_enabled = true;
        
        /* 初始化 TCP Keepalive */
        s_tcp_tcbs[i].keepalive_last_active = 0U;
        s_tcp_tcbs[i].keepalive_probe_count = 0U;
        s_tcp_tcbs[i].keepalive_next_probe = 0U;
        s_tcp_tcbs[i].keepalive_enabled = true;
        s_tcp_tcbs[i].keepalive_timeout = false;
        
        /* 初始化 TCP 选项 */
        s_tcp_tcbs[i].mss = TCP_MSS;
        s_tcp_tcbs[i].window_scale = 0U;
        s_tcp_tcbs[i].mss_negotiated = 0U;
        s_tcp_tcbs[i].window_scale_negotiated = 0U;

        /* 初始化 RTT 测量参数 */
        tcp_rtt_init(&s_tcp_tcbs[i]);
    }

    /* 初始化 IP 分片重组队列 */
    for (i = 0U; i < NET_MAX_REASS_QUEUE; i++)
    {
        (void)memset(&s_reass_queues[i], 0, sizeof(ip_reass_queue_t));
        s_reass_queues[i].in_use = false;
    }

    s_reasm_offset = 0U;
    s_reasm_id = 0U;
    s_icmp_echo_sent = 0U;
    s_icmp_echo_recv = 0U;
    s_icmp_echo_reply_sent = 0U;

    s_initialized = true;

    /* ========================================================================
     * 自动发现和注册网络接口（使用 net_if_auto 接口）
     * ======================================================================== */
    {
        uint32_t if_count = net_if_auto_get_count();
        uint32_t if_idx;
        
        for (if_idx = 0U; if_idx < if_count; if_idx++)
        {
            /* 获取网络接口的接口名称 */
            char if_name[16] = {0};
            int32_t ret_name = net_if_auto_get_name(if_idx, if_name, sizeof(if_name));
            if (ret_name != 0)
            {
                /* 获取接口名称失败，跳过此接口 */
                continue;
            }
            
            /* 检查网络接口是否有此接口 */
            const net_if_ops_auto_t *ops = net_if_auto_get_ops(if_name);
            if (ops != NULL)
            {
                /* 网络接口有此接口，注册到网络协议栈 */
                net_mac_t mac_addr = {0};
                
                /* 获取 MAC 地址 */
                ret_name = net_if_auto_get_mac_addr(if_idx, mac_addr.bytes);
                if (ret_name != 0)
                {
                    /* 获取 MAC 地址失败，跳过此接口 */
                    continue;
                }
                
                /* 调用驱动的 init 接口 */
                if (ops->init != NULL)
                {
                    int32_t ret_init = ops->init();
                    if (ret_init != 0)
                    {
                        /* 初始化失败，跳过此接口 */
                        continue;
                    }
                }
                
                /* 注册到网络协议栈 */
                int32_t if_id = net_register_interface(if_name, NET_LINK_ETHERNET,
                                                       &mac_addr, 0);
                if (if_id >= 0)
                {
                    /* 启动接口 */
                    kernel_status_t ret_up = net_if_up((uint32_t)if_id);
                    if (ret_up == KERNEL_OK)
                    {
                        /* 接口启动成功 */
                        /* 保存操作接口到接口表 */
                        s_interfaces[(uint32_t)if_id].ops_auto = *ops;
                    }
                }
            }
        }
    }

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
 * @brief 计算 UDP 校验和（含伪首部）
 *
 * @param src_ip  源 IP 地址（主机字节序）
 * @param dst_ip  目标 IP 地址（主机字节序）
 * @param data    UDP 头部 + 数据
 * @param len     UDP 总长度（头部 + 数据）
 *
 * @return 校验和值（0xFFFF 表示校验和为 0 即禁用）
 */
static uint16_t udp_checksum_with_pseudo(uint32_t src_ip, uint32_t dst_ip,
                                          const uint8_t *data, uint32_t len)
{
    uint8_t pseudo[12U];
    uint32_t sum;
    uint16_t cksum;

    if ((data == NULL) || (len == 0U))
    {
        return 0U;
    }

    /* 构造伪首部：源 IP + 目标 IP + 零 + 协议 + UDP 长度 */
    (void)memset(pseudo, 0, sizeof(pseudo));
    (void)memcpy(&pseudo[0U], &src_ip, 4U);
    (void)memcpy(&pseudo[4U], &dst_ip, 4U);
    pseudo[9U] = IP_PROTO_UDP;
    pseudo[10U] = (uint8_t)(len >> 8U);
    pseudo[11U] = (uint8_t)(len);

    /* 计算伪首部校验和 */
    cksum = net_checksum(pseudo, 12U);

    /* 累加 UDP 数据校验和 */
    sum = (uint32_t)cksum + (uint32_t)net_checksum(data, len);

    /* 折叠进位 */
    while ((sum >> 16U) != 0U)
    {
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    }

    return (uint16_t)(~sum);
}

/**
 * @brief 处理接收到的 UDP 包
 *
 * @details 接收 UDP 数据包时计算并验证校验和
 */
void udp_process(uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
                  const uint8_t *data, uint32_t len)
{
    const udp_header_t *udp;
    uint16_t dst_port;
    uint16_t src_port;
    uint16_t udp_len;
    uint16_t recv_cksum;
    uint16_t calc_cksum;
    uint32_t i;

    if ((data == NULL) || (len < (uint32_t)UDP_HDR_SIZE))
    {
        return;
    }

    udp = (const udp_header_t *)data;
    udp_len = net_htons(udp->length);
    src_port = net_htons(udp->src_port);
    dst_port = net_htons(udp->dst_port);

    /* UDP 校验和验证 */
    recv_cksum = udp->checksum;
    if (recv_cksum != 0U)
    {
        /* 校验和不为 0，执行验证 */
        /* 临时将校验和字段置零进行计算 */
        udp_header_t tmp_udp;
        uint8_t *tmp_data;

        (void)memcpy(&tmp_udp, udp, sizeof(udp_header_t));
        tmp_udp.checksum = 0U;

        /* 使用伪首部 + UDP 数据计算校验和 */
        tmp_data = (uint8_t *)data;
        tmp_data = (uint8_t *)((uintptr_t)tmp_data); /* 保持指针 */

        /* 计算校验和 */
        {
            uint8_t udp_buf[NET_MAX_PACKET_SIZE];

            if ((uint32_t)udp_len > len)
            {
                /* 长度不一致，丢弃 */
                return;
            }

            (void)memcpy(udp_buf, &tmp_udp, (uint32_t)UDP_HDR_SIZE);
            if (udp_len > (uint16_t)UDP_HDR_SIZE)
            {
                (void)memcpy(&udp_buf[(uint32_t)UDP_HDR_SIZE],
                             &data[(uint32_t)UDP_HDR_SIZE],
                             (uint32_t)udp_len - (uint32_t)UDP_HDR_SIZE);
            }

            calc_cksum = udp_checksum_with_pseudo(src_ip, dst_ip,
                                                   udp_buf, (uint32_t)udp_len);
        }

        if (calc_cksum != recv_cksum)
        {
            /* 校验和不匹配，丢弃数据包 */
            return;
        }
    }

    /* 查找绑定了该端口的套接字 */
    for (i = 0U; i < NET_MAX_SOCKETS; i++)
    {
        if (s_sockets[i].in_use &&
            (s_sockets[i].type == NET_SOCK_DGRAM) &&
            (s_sockets[i].local_addr.port == dst_port))
        {
            /* 将数据包加入接收队列 */
            uint32_t head = s_udp_rx_head[i];
            uint32_t next_head = (head + 1U) % UDP_RX_QUEUE_DEPTH;
            
            /* 检查队列是否已满 */
            if (next_head != s_udp_rx_tail[i])
            {
                uint32_t data_len;
                
                /* 计算数据长度（不包含 UDP 头部） */
                data_len = (udp_len > (uint16_t)UDP_HDR_SIZE) ?
                           (uint32_t)udp_len - (uint32_t)UDP_HDR_SIZE : 0U;
                
                /* 填充队列条目 */
                s_udp_rx_queue[i][head].in_use = true;
                s_udp_rx_queue[i][head].sock_id = i;
                
                /* 设置源地址 */
                s_udp_rx_queue[i][head].src_addr.family = NET_AF_INET;
                s_udp_rx_queue[i][head].src_addr.port = src_port;
                u32_to_ipv4(src_ip, &s_udp_rx_queue[i][head].src_addr.addr.ipv4);
                
                /* 复制数据 */
                if (data_len > 0U && data_len < NET_MAX_PACKET_SIZE)
                {
                    (void)memcpy(s_udp_rx_queue[i][head].data,
                                 &data[(uint32_t)UDP_HDR_SIZE], data_len);
                }
                s_udp_rx_queue[i][head].len = data_len;
                
                /* 更新队列头指针 */
                s_udp_rx_head[i] = next_head;
                
                s_sockets[i].rx_count++;
                s_sockets[i].rx_bytes += (uint64_t)data_len;
            }
            else
            {
                /* 队列已满，丢弃数据包 */
                s_sockets[i].rx_errors++;
            }
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
 * @brief 获取当前时间（毫秒）
 *
 * @return 当前时间（毫秒）
 */
static uint64_t get_current_time_ms(void)
{
    return s_time_ms;
}

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

    /* 更新发送时间戳（用于 RTT 测量） */
    if (tcb->ts_enabled)
    {
        tcb->ts_val = (uint32_t)get_current_time_ms();
    }
    tcp->window = net_htons((uint16_t)tcb->rcv_wnd);
    tcp->checksum = 0U;
    tcp->urgent_ptr = 0U;

    /* TCP 选项构造 */
    uint8_t options[40];  /* 最大 TCP 选项长度 */
    uint16_t opt_len = 0U;

    /* 添加 MSS 选项（仅在 SYN 段中） */
    if ((flags & TCP_FLAG_SYN) != 0U)
    {
        options[opt_len++] = 2U;  /* MSS kind */
        options[opt_len++] = 4U;  /* length */
        options[opt_len++] = (tcb->mss >> 8U) & 0xFFU;
        options[opt_len++] = tcb->mss & 0xFFU;
    }

    /* 添加窗口缩放选项（仅在 SYN 段中） */
    if ((flags & TCP_FLAG_SYN) != 0U)
    {
        options[opt_len++] = 3U;  /* Window Scale kind */
        options[opt_len++] = 3U;  /* length */
        options[opt_len++] = tcb->window_scale;
    }

    /* 添加 SACK 选项（仅在 SYN 段中） */
    if ((flags & TCP_FLAG_SYN) != 0U)
    {
        options[opt_len++] = 4U;  /* SACK Permitted kind */
        options[opt_len++] = 2U;  /* length */
    }

    /* 添加时间戳选项（所有段） */
    if (tcb->ts_enabled)
    {
        options[opt_len++] = 8U;  /* Timestamp kind */
        options[opt_len++] = 10U; /* length */
        options[opt_len++] = (tcb->ts_val >> 24U) & 0xFFU;
        options[opt_len++] = (tcb->ts_val >> 16U) & 0xFFU;
        options[opt_len++] = (tcb->ts_val >> 8U) & 0xFFU;
        options[opt_len++] = tcb->ts_val & 0xFFU;
        options[opt_len++] = (tcb->ts_echo_rpl >> 24U) & 0xFFU;
        options[opt_len++] = (tcb->ts_echo_rpl >> 16U) & 0xFFU;
        options[opt_len++] = (tcb->ts_echo_rpl >> 8U) & 0xFFU;
        options[opt_len++] = tcb->ts_echo_rpl & 0xFFU;
    }

    /* 更新 data_offset 以包含选项 */
    if (opt_len > 0U)
    {
        tcp->data_offset = (5U + (opt_len / 4U)) << 4U;
        
        /* 复制选项到 TCP 头部 */
        (void)memcpy(&segment[(uint32_t)TCP_HDR_SIZE], options, opt_len);
    }

    if ((data != NULL) && (len > 0U))
    {
        (void)memcpy(&segment[(uint32_t)TCP_HDR_SIZE + opt_len], data, len);
    }

    total_len = (uint32_t)TCP_HDR_SIZE + opt_len + len;

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

    /* ========================================================================
     * TCP 选项解析（MSS、窗口缩放、SACK、时间戳）
     * ======================================================================== */
    if ((data_offset > (uint32_t)TCP_HDR_SIZE) && (tcb != NULL))
    {
        uint16_t opt_offset = (uint16_t)((tcp->data_offset >> 4U) - 5U) * 4U;
        uint16_t opt_len = data_offset - (uint32_t)TCP_HDR_SIZE;
        const uint8_t *opt_data = &data[TCP_HDR_SIZE];
        uint16_t opt_offset_temp = 0U;

        while (opt_offset_temp < opt_len)
        {
            uint8_t opt_kind = opt_data[opt_offset_temp];

            if (opt_kind == 0U)  /* End of options */
            {
                break;
            }

            if (opt_kind == 1U)  /* NOP */
            {
                opt_offset_temp++;
                continue;
            }

            if ((opt_offset_temp + 1U) >= opt_len)
            {
                break;
            }

            uint8_t opt_length = opt_data[opt_offset_temp + 1U];

            /* 处理 MSS 选项 */
            if (opt_kind == 2U)  /* MSS */
            {
                if (opt_length == 4U)
                {
                    uint16_t mss = (opt_data[opt_offset_temp + 2U] << 8U) |
                                    opt_data[opt_offset_temp + 3U];
                    tcb->mss = mss;
                    tcb->mss_negotiated = 1U;
                }
            }
            /* 处理窗口缩放选项 */
            else if (opt_kind == 3U)  /* Window Scale */
            {
                if (opt_length == 3U)
                {
                    uint8_t scale = opt_data[opt_offset_temp + 2U];
                    tcb->window_scale = scale;
                    tcb->window_scale_negotiated = 1U;
                }
            }
            /* 处理 SACK 选项 */
            else if (opt_kind == 4U)  /* SACK Permitted */
            {
                if (opt_length == 2U)
                {
                    tcb->sack_permitted = 1U;
                }
            }
            /* 处理时间戳选项 */
            else if (opt_kind == 8U)  /* Timestamp */
            {
                if (opt_length == 10U)
                {
                    uint32_t ts_val = (opt_data[opt_offset_temp + 2U] << 24U) |
                                    (opt_data[opt_offset_temp + 3U] << 16U) |
                                    (opt_data[opt_offset_temp + 4U] << 8U) |
                                    opt_data[opt_offset_temp + 5U];
                    uint32_t ts_echo_rpl = (opt_data[opt_offset_temp + 6U] << 24U) |
                                          (opt_data[opt_offset_temp + 7U] << 16U) |
                                          (opt_data[opt_offset_temp + 8U] << 8U) |
                                          opt_data[opt_offset_temp + 9U];
                    tcb->ts_val = ts_val;
                    tcb->ts_echo_rpl = ts_echo_rpl;
                    tcb->recent_ts = ts_val;
                    tcb->ts_enabled = true;

                    /* RTT 测量（使用 Timestamp 选项） */
                    if (ts_echo_rpl > 0U)
                    {
                        uint32_t current_time = (uint32_t)get_current_time_ms();
                        if (current_time > ts_echo_rpl)
                        {
                            uint32_t rtt = current_time - ts_echo_rpl;
                            tcp_rtt_update(tcb, rtt);
                        }
                    }
                }
            }

            if (opt_length == 0U)
            {
                opt_offset_temp++;
            }
            else
            {
                opt_offset_temp += opt_length;
            }
        }
    }

    /* ========================================================================
     * ACK 处理：重复检测 + 滑动窗口 + RTT 测量
     * ======================================================================== */
    if (((flags & TCP_FLAG_ACK) != 0U) && (tcb != NULL) &&
        (tcb->state == TCP_ESTABLISHED))
    {
        /* 调用统一的 ACK 处理函数 */
        tcp_process_ack(tcb, ack_num);

        /* CUBIC 拥塞控制（新 ACK 时更新窗口） */
        if (ack_num > tcb->snd_una)
        {
            if (tcb->cubic_state == 0U)  /* 慢启动 */
            {
                tcb->cubic_cwnd += TCP_MSS;
                if (tcb->cubic_cwnd >= tcb->cubic_ssthresh)
                {
                    tcb->cubic_state = 1U;  /* 拥塞避免 */
                }
            }
            else if (tcb->cubic_state == 1U)  /* 拥塞避免 */
            {
                tcb->cubic_cwnd += (TCP_MSS * TCP_MSS) / tcb->cubic_cwnd;
            }
            else
            {
                /* 快速恢复或其他状态，不更新 */
            }
        }
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
            /* 更新 Keepalive 最后活跃时间 */
            tcb->keepalive_last_active =
                (uint32_t)(get_current_time_ms() / 1000U);
            tcb->keepalive_probe_count = 0U;
            tcb->keepalive_timeout = false;

            /* 处理接收数据 */
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

                /* 清除已确认的重传段（由 tcp_process_ack 统一处理） */
                {
                    uint32_t k;
                    for (k = 0U; k < TCP_MAX_RETRANS_SEGS; k++)
                    {
                        if (tcb->retrans_buf[k].active &&
                            ((tcb->retrans_buf[k].seq_num +
                              tcb->retrans_buf[k].data_len) <= ack_num))
                        {
                            tcb->retrans_buf[k].active = false;
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

/* ========================================================================
 * RTT 测量和 RTO 计算
 * ======================================================================== */

/**
 * @brief 初始化 RTT 测量参数
 *
 * @param tcb TCP 控制块
 */
static void tcp_rtt_init(tcp_tcb_t *tcb)
{
    if (tcb == NULL)
    {
        return;
    }

    tcb->cong_ctrl.rtt.srtt = TCP_RTT_INITIAL_MS;
    tcb->cong_ctrl.rtt.rtt_var = TCP_RTT_INITIAL_MS >> 1U;
    tcb->cong_ctrl.rtt.rto = TCP_RTT_INITIAL_MS;
    tcb->cong_ctrl.rtt.rtt_sample = 0U;
    tcb->cong_ctrl.rtt.rtt_min = TCP_RTT_INITIAL_MS;
}

/**
 * @brief 更新 RTT 测量和 RTO
 *
 * @details 使用 TCP Timestamp 选项测量 RTT，采用 Jacobson/Karels 算法：
 *          - SRTT = (7/8)*SRTT + (1/8)*RTT_sample
 *          - RTT_var = (3/4)*RTT_var + (1/4)*|SRTT - RTT_sample|
 *          - RTO = SRTT + 4*RTT_var
 *
 * @param tcb        TCP 控制块
 * @param rtt_sample RTT 样本（毫秒）
 */
static void tcp_rtt_update(tcp_tcb_t *tcb, uint32_t rtt_sample)
{
    uint32_t srtt;
    uint32_t rtt_var;
    uint32_t rto;
    int32_t diff;

    if (tcb == NULL)
    {
        return;
    }

    tcb->cong_ctrl.rtt.rtt_sample = rtt_sample;

    /* 更新最小 RTT */
    if (rtt_sample < tcb->cong_ctrl.rtt.rtt_min)
    {
        tcb->cong_ctrl.rtt.rtt_min = rtt_sample;
    }

    /* Jacobson/Karels 算法 */
    srtt = tcb->cong_ctrl.rtt.srtt;
    rtt_var = tcb->cong_ctrl.rtt.rtt_var;

    /* SRTT = (7/8)*SRTT + (1/8)*sample */
    srtt = srtt - (srtt >> TCP_RTT_ALPHA_SHIFT) + rtt_sample;

    /* RTT_var = (3/4)*RTT_var + (1/4)*|SRTT - sample| */
    diff = (int32_t)srtt - (int32_t)rtt_sample;
    if (diff < 0)
    {
        diff = -diff;
    }
    rtt_var = rtt_var - (rtt_var >> TCP_RTT_BETA_SHIFT)
              + (uint32_t)diff;

    /* RTO = SRTT + 4*RTT_var */
    rto = srtt + (rtt_var << 2U);

    /* 限制 RTO 范围 */
    if (rto < TCP_RTO_MIN_MS)
    {
        rto = TCP_RTO_MIN_MS;
    }
    if (rto > TCP_RTO_MAX_MS)
    {
        rto = TCP_RTO_MAX_MS;
    }

    tcb->cong_ctrl.rtt.srtt = srtt;
    tcb->cong_ctrl.rtt.rtt_var = rtt_var;
    tcb->cong_ctrl.rtt.rto = rto;
}

/* ========================================================================
 * ACK 处理优化
 * ======================================================================== */

/**
 * @brief 处理接收到的 ACK，检测重复并更新滑动窗口
 *
 * @details 检查 ACK 序列号：
 *          - 新 ACK：更新 snd_una，释放已确认数据，重置 dup_acks
 *          - 重复 ACK：增加 dup_acks，达到 3 次触发快速重传
 *
 * @param tcb     TCP 控制块
 * @param ack_num ACK 序列号
 */
static void tcp_process_ack(tcp_tcb_t *tcb, uint32_t ack_num)
{
    if (tcb == NULL)
    {
        return;
    }

    /* 检查是否为重复 ACK */
    if (ack_num == tcb->cong_ctrl.last_ack)
    {
        /* 重复 ACK */
        tcb->cong_ctrl.dup_acks++;

        /* 3 个重复 ACK 触发快速重传 */
        if (tcb->cong_ctrl.dup_acks >= TCP_MAX_DUP_ACKS)
        {
            /* 快速重传：重新发送第一个未确认的段 */
            uint32_t i;
            for (i = 0U; i < TCP_MAX_RETRANS_SEGS; i++)
            {
                if (tcb->retrans_buf[i].active)
                {
                    (void)tcp_send_segment(tcb,
                        (uint8_t)(TCP_FLAG_ACK | TCP_FLAG_PSH),
                        tcb->retrans_buf[i].data,
                        tcb->retrans_buf[i].data_len);
                    break;
                }
            }

            /* 调整拥塞窗口 */
            tcb->cubic_w_max = tcb->cubic_cwnd;
            tcb->cubic_cwnd = tcb->cubic_cwnd / 2U;
            if (tcb->cubic_cwnd < TCP_MSS)
            {
                tcb->cubic_cwnd = TCP_MSS;
            }
            tcb->cubic_ssthresh = tcb->cubic_cwnd;
            tcb->cubic_state = 2U;  /* 快速恢复 */

            /* 重置重复 ACK 计数 */
            tcb->cong_ctrl.dup_acks = 0U;
        }
    }
    else if (ack_num > tcb->cong_ctrl.last_ack)
    {
        /* 新 ACK：更新滑动窗口 */
        tcb->cong_ctrl.last_ack = ack_num;
        tcb->cong_ctrl.dup_acks = 0U;

        /* ACK 滑动窗口：更新 snd_una */
        if (ack_num > tcb->snd_una)
        {
            tcb->snd_una = ack_num;
        }

        /* 释放已确认的重传段 */
        {
            uint32_t i;
            for (i = 0U; i < TCP_MAX_RETRANS_SEGS; i++)
            {
                if (tcb->retrans_buf[i].active &&
                    ((tcb->retrans_buf[i].seq_num +
                      tcb->retrans_buf[i].data_len) <= ack_num))
                {
                    tcb->retrans_buf[i].active = false;
                    if (tcb->retrans_count > 0U)
                    {
                        tcb->retrans_count--;
                    }
                }
            }
        }

        /* RTT 测量（使用 TCP Timestamp） */
        if (tcb->ts_enabled && (tcb->ts_echo_rpl > 0U))
        {
            uint32_t current_time = (uint32_t)get_current_time_ms();
            if (current_time > tcb->ts_echo_rpl)
            {
                uint32_t rtt = current_time - tcb->ts_echo_rpl;
                tcp_rtt_update(tcb, rtt);
            }
        }
    }
    else
    {
        /* 旧的 ACK，忽略 */
    }
}

/* ========================================================================
 * TCP 定时器处理
 * ======================================================================== */

/**
 * @brief TCP 重传定时器处理
 *
 * @details 每 10ms 检查一次所有活跃 TCB 的重传队列：
 *          - 检查段的 RTO 是否超时
 *          - 超时则重传该段
 *          - 超过 5 次重传则丢弃连接
 */
static void tcp_retransmit_check(void)
{
    uint32_t i;
    uint32_t j;
    uint64_t now_ms;

    now_ms = get_current_time_ms();

    for (i = 0U; i < NET_MAX_SOCKETS; i++)
    {
        tcp_tcb_t *tcb = &s_tcp_tcbs[i];

        if (!tcb->in_use || (tcb->state == TCP_CLOSED))
        {
            continue;
        }

        for (j = 0U; j < TCP_MAX_RETRANS_SEGS; j++)
        {
            if (!tcb->retrans_buf[j].active)
            {
                continue;
            }

            /* 检查是否超过最大重传次数 */
            if (tcb->retrans_buf[j].retry_count >= TCP_MAX_RETRIES)
            {
                /* 超过 5 次重传，丢弃连接 */
                tcb->retrans_buf[j].active = false;
                tcb->state = TCP_CLOSED;
                tcb->in_use = false;
                if (tcb->retrans_count > 0U)
                {
                    tcb->retrans_count--;
                }
                continue;
            }

            /* 检查 RTO 是否超时 */
            if (tcb->retrans_buf[j].last_sent_ms == 0ULL)
            {
                /* 尚未记录发送时间，设置当前时间 */
                tcb->retrans_buf[j].last_sent_ms = now_ms;
                continue;
            }

            if ((now_ms - tcb->retrans_buf[j].last_sent_ms) >=
                (uint64_t)tcb->cong_ctrl.rtt.rto)
            {
                /* RTO 超时，执行重传 */
                (void)tcp_send_segment(tcb,
                    (uint8_t)(TCP_FLAG_ACK | TCP_FLAG_PSH),
                    tcb->retrans_buf[j].data,
                    tcb->retrans_buf[j].data_len);
                tcb->retrans_buf[j].retry_count++;
                tcb->retrans_buf[j].last_sent_ms = now_ms;

                /* 指数退避 RTO */
                tcb->cong_ctrl.rtt.rto = tcb->cong_ctrl.rtt.rto << 1U;
                if (tcb->cong_ctrl.rtt.rto > TCP_RTO_MAX_MS)
                {
                    tcb->cong_ctrl.rtt.rto = TCP_RTO_MAX_MS;
                }
            }
        }
    }
}

/**
 * @brief TCP Keepalive 定时器处理
 *
 * @details 检查所有 ESTABLISHED 状态的 TCB：
 *          - 超过 2 小时无数据则发送 Keepalive 探测
 *          - 每 75 秒发送一次探测（最多 9 次）
 *          - 超时后关闭连接
 */
static void tcp_keepalive_check(void)
{
    uint32_t i;
    uint32_t current_time;

    current_time = (uint32_t)(get_current_time_ms() / 1000U);

    for (i = 0U; i < NET_MAX_SOCKETS; i++)
    {
        tcp_tcb_t *tcb = &s_tcp_tcbs[i];

        /* 仅检查 ESTABLISHED 状态的活跃连接 */
        if (!tcb->in_use || (tcb->state != TCP_ESTABLISHED))
        {
            continue;
        }

        if (!tcb->keepalive_enabled)
        {
            continue;
        }

        /* 检查是否超过空闲超时（2 小时） */
        if ((current_time - tcb->keepalive_last_active) >=
            TCP_KEEPALIVE_IDLE_SEC)
        {
            /* 检查探测间隔和最大探测次数 */
            if ((current_time >= tcb->keepalive_next_probe) &&
                (tcb->keepalive_probe_count < TCP_KEEPALIVE_MAX_PROBES))
            {
                /* 发送 Keepalive 探测（ACK 段，无数据） */
                (void)tcp_send_segment(tcb, TCP_FLAG_ACK, NULL, 0U);
                tcb->keepalive_probe_count++;
                tcb->keepalive_next_probe = current_time +
                    TCP_KEEPALIVE_INTERVAL_SEC;

                /* 检查是否超过最大探测次数 */
                if (tcb->keepalive_probe_count >= TCP_KEEPALIVE_MAX_PROBES)
                {
                    tcb->keepalive_timeout = true;
                    /* 关闭连接 */
                    tcb->state = TCP_CLOSED;
                    tcb->in_use = false;
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
    
    /* 初始化套接字选项 */
    s_sockets[i].reuse_addr = false;
    s_sockets[i].keepalive = false;
    s_sockets[i].broadcast = false;
    s_sockets[i].nonblocking = false;
    s_sockets[i].shutdown_rd = false;
    s_sockets[i].shutdown_wr = false;
    s_sockets[i].rcv_buf_size = 8192;
    s_sockets[i].snd_buf_size = 8192;
    
    /* 初始化统计信息 */
    s_sockets[i].rx_bytes = 0ULL;
    s_sockets[i].tx_bytes = 0ULL;
    s_sockets[i].rx_errors = 0ULL;
    s_sockets[i].tx_errors = 0ULL;

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
        uint32_t local_ip;
        uint16_t cksum;

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
        local_ip = ipv4_to_u32(&sock->local_addr.addr.ipv4);

        /* 计算 UDP 校验和（含伪首部） */
        cksum = udp_checksum_with_pseudo(local_ip, remote_ip,
                                          dgram, dgram_len);
        /* 将校验和写入 UDP 头部（0 表示禁用，用 0xFFFF 代替） */
        if (cksum == 0U)
        {
            cksum = 0xFFFFU;
        }
        ((udp_header_t *)dgram)->checksum = cksum;

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
 * Socket API 扩展接口
 * ======================================================================== */

/**
 * @brief 发送数据到指定地址（UDP）
 * @param sock_id     套接字 ID
 * @param buf         数据缓冲区
 * @param size        数据大小
 * @param dest_addr   目标地址
 * @return 实际发送字节数，负数表示错误
 */
int64_t net_sendto(uint32_t sock_id, const void *buf, uint64_t size,
                    const net_sockaddr_t *dest_addr)
{
    net_socket_t *sock;
    udp_header_t udp_hdr;
    uint8_t dgram[NET_MAX_PACKET_SIZE];
    uint32_t dgram_len;
    uint32_t remote_ip;
    uint32_t local_ip;
    uint16_t cksum;
    uint32_t if_id;

    /* 参数验证 */
    if (buf == NULL || dest_addr == NULL)
    {
        return -(int64_t)22; /* EINVAL */
    }

    if (sock_id >= NET_MAX_SOCKETS)
    {
        return -(int64_t)22;
    }

    sock = &s_sockets[sock_id];

    if (!sock->in_use)
    {
        return -(int64_t)2; /* ENOENT */
    }

    if (sock->type != NET_SOCK_DGRAM)
    {
        return -(int64_t)22; /* EOPNOTSUPP */
    }

    if (sock->shutdown_wr)
    {
        return -(int64_t)32; /* EPIPE */
    }

    /* 查找网络接口 */
    if_id = sock->if_id;
    if (if_id >= NET_MAX_INTERFACES || !s_interfaces[if_id].in_use)
    {
        return -(int64_t)19; /* ENODEV */
    }

    /* 设置 UDP 头部 */
    udp_hdr.src_port = net_htons(sock->local_addr.port);
    udp_hdr.dst_port = net_htons(dest_addr->port);
    udp_hdr.length = net_htons((uint16_t)((uint32_t)UDP_HDR_SIZE + (uint32_t)size));
    udp_hdr.checksum = 0U;

    /* 复制数据 */
    (void)memcpy(dgram, &udp_hdr, (uint32_t)UDP_HDR_SIZE);
    if (size > 0U)
    {
        (void)memcpy(&dgram[(uint32_t)UDP_HDR_SIZE], buf, (uint32_t)size);
    }

    dgram_len = (uint32_t)UDP_HDR_SIZE + (uint32_t)size;
    remote_ip = ipv4_to_u32(&dest_addr->addr.ipv4);
    local_ip = ipv4_to_u32(&s_interfaces[if_id].ipv4_addr);

    /* 计算 UDP 校验和（含伪首部） */
    cksum = udp_checksum_with_pseudo(local_ip, remote_ip,
                                      dgram, dgram_len);
    if (cksum == 0U)
    {
        cksum = 0xFFFFU;
    }
    ((udp_header_t *)dgram)->checksum = cksum;

    /* 发送 IPv4 数据包 */
    if (ipv4_send(if_id, remote_ip, IP_PROTO_UDP,
                  dgram, dgram_len) < (int64_t)dgram_len)
    {
        sock->tx_errors++;
        return -(int64_t)5; /* EIO */
    }

    sock->tx_count++;
    sock->tx_bytes += size;
    return (int64_t)size;
}

/**
 * @brief 从指定地址接收数据（UDP）
 * @param sock_id     套接字 ID
 * @param buf         数据缓冲区
 * @param size        缓冲区大小
 * @param src_addr    源地址（输出）
 * @return 实际接收字节数，负数表示错误
 */
int64_t net_recvfrom(uint32_t sock_id, void *buf, uint64_t size,
                      net_sockaddr_t *src_addr)
{
    net_socket_t *sock;
    uint32_t tail;

    /* 参数验证 */
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

    if (sock->type != NET_SOCK_DGRAM)
    {
        return -(int64_t)22;
    }

    if (sock->shutdown_rd)
    {
        return 0LL; /* EOF */
    }

    /* 检查接收队列 */
    tail = s_udp_rx_tail[sock_id];
    if (tail == s_udp_rx_head[sock_id])
    {
        /* 队列为空 */
        if (sock->nonblocking)
        {
            return -(int64_t)11; /* EAGAIN */
        }
        /* 阻塞等待（简化实现：立即返回 EAGAIN） */
        return -(int64_t)11;
    }

    /* 从队列中取出数据 */
    if (s_udp_rx_queue[sock_id][tail].in_use &&
        s_udp_rx_queue[sock_id][tail].sock_id == sock_id)
    {
        uint32_t copy_len = s_udp_rx_queue[sock_id][tail].len;
        if (copy_len > size)
        {
            copy_len = (uint32_t)size;
        }
        (void)memcpy(buf, s_udp_rx_queue[sock_id][tail].data, copy_len);

        /* 复制源地址 */
        if (src_addr != NULL)
        {
            (void)memcpy(src_addr, &s_udp_rx_queue[sock_id][tail].src_addr,
                         sizeof(net_sockaddr_t));
        }

        /* 释放队列条目 */
        s_udp_rx_queue[sock_id][tail].in_use = false;
        s_udp_rx_tail[sock_id] = (tail + 1U) % UDP_RX_QUEUE_DEPTH;

        sock->rx_count++;
        sock->rx_bytes += copy_len;
        return (int64_t)copy_len;
    }

    return -(int64_t)11; /* EAGAIN */
}

/**
 * @brief 优雅关闭连接
 * @param sock_id     套接字 ID
 * @param how         关闭方式（NET_SHUT_RD/NET_SHUT_WR/NET_SHUT_RDWR）
 * @return KERNEL_OK 成功
 */
kernel_status_t net_shutdown(uint32_t sock_id, uint32_t how)
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

    if (sock->type != NET_SOCK_STREAM)
    {
        return -(int32_t)22; /* EOPNOTSUPP */
    }

    tcb = &s_tcp_tcbs[sock_id];

    switch (how)
    {
        case NET_SHUT_RD:
            /* 关闭读方向：发送 FIN */
            sock->shutdown_rd = true;
            if (tcb->state == TCP_ESTABLISHED || tcb->state == TCP_CLOSE_WAIT)
            {
                (void)tcp_send_segment(tcb, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0U);
                if (tcb->state == TCP_ESTABLISHED)
                {
                    tcb->state = TCP_FIN_WAIT_1;
                }
                else
                {
                    tcb->state = TCP_LAST_ACK;
                }
            }
            break;

        case NET_SHUT_WR:
            /* 关闭写方向：仅标记不可写 */
            sock->shutdown_wr = true;
            break;

        case NET_SHUT_RDWR:
            /* 关闭读写 */
            sock->shutdown_rd = true;
            sock->shutdown_wr = true;
            if (tcb->state == TCP_ESTABLISHED || tcb->state == TCP_CLOSE_WAIT)
            {
                (void)tcp_send_segment(tcb, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0U);
                if (tcb->state == TCP_ESTABLISHED)
                {
                    tcb->state = TCP_FIN_WAIT_1;
                }
                else
                {
                    tcb->state = TCP_LAST_ACK;
                }
            }
            break;

        default:
            return -(int32_t)22; /* EINVAL */
    }

    return KERNEL_OK;
}

/**
 * @brief 设置套接字选项
 * @param sock_id     套接字 ID
 * @param level       选项级别（NET_SOL_SOCKET/NET_IPPROTO_TCP）
 * @param optname     选项名称
 * @param optval      选项值
 * @param optlen      选项值长度
 * @return KERNEL_OK 成功
 */
kernel_status_t net_setsockopt(uint32_t sock_id, uint32_t level,
                                uint32_t optname, const void *optval,
                                uint32_t optlen)
{
    net_socket_t *sock;
    tcp_tcb_t *tcb;

    if (optval == NULL)
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

    tcb = &s_tcp_tcbs[sock_id];

    switch (level)
    {
        case NET_SOL_SOCKET:
            switch (optname)
            {
                case NET_SO_REUSEADDR:
                    /* 地址复用（TCP/UDP） */
                    if (optlen >= sizeof(int))
                    {
                        sock->reuse_addr = (*((const int *)optval) != 0);
                    }
                    break;

                case NET_SO_KEEPALIVE:
                    /* TCP Keepalive */
                    if (sock->type == NET_SOCK_STREAM && optlen >= sizeof(int))
                    {
                        sock->keepalive = (*((const int *)optval) != 0);
                    }
                    break;

                case NET_SO_BROADCAST:
                    /* 广播（UDP） */
                    if (sock->type == NET_SOCK_DGRAM && optlen >= sizeof(int))
                    {
                        sock->broadcast = (*((const int *)optval) != 0);
                    }
                    break;

                case NET_SO_RCVBUF:
                    /* 接收缓冲区大小 */
                    if (optlen >= sizeof(int))
                    {
                        sock->rcv_buf_size = *((const int *)optval);
                    }
                    break;

                case NET_SO_SNDBUF:
                    /* 发送缓冲区大小 */
                    if (optlen >= sizeof(int))
                    {
                        sock->snd_buf_size = *((const int *)optval);
                    }
                    break;

                default:
                    return -(int32_t)22;
            }
            break;

        case NET_IPPROTO_TCP:
            switch (optname)
            {
                case NET_TCP_NODELAY:
                    /* 禁用 Nagle 算法 */
                    if (sock->type == NET_SOCK_STREAM && optlen >= sizeof(int))
                    {
                        /* nodelay = true 表示禁用 Nagle 算法 */
                        tcb->nagle_enabled = (*((const int *)optval) == 0);
                    }
                    break;

                default:
                    return -(int32_t)22;
            }
            break;

        default:
            return -(int32_t)22;
    }

    return KERNEL_OK;
}

/**
 * @brief 获取套接字选项
 * @param sock_id     套接字 ID
 * @param level       选项级别（NET_SOL_SOCKET/NET_IPPROTO_TCP）
 * @param optname     选项名称
 * @param optval      选项值（输出）
 * @param optlen      选项值长度（输入输出）
 * @return KERNEL_OK 成功
 */
kernel_status_t net_getsockopt(uint32_t sock_id, uint32_t level,
                                uint32_t optname, void *optval,
                                uint32_t *optlen)
{
    net_socket_t *sock;
    tcp_tcb_t *tcb;

    if (optval == NULL || optlen == NULL)
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

    tcb = &s_tcp_tcbs[sock_id];

    switch (level)
    {
        case NET_SOL_SOCKET:
            switch (optname)
            {
                case NET_SO_REUSEADDR:
                    if (*optlen >= sizeof(int))
                    {
                        *((int *)optval) = sock->reuse_addr ? 1 : 0;
                        *optlen = sizeof(int);
                    }
                    break;

                case NET_SO_KEEPALIVE:
                    if (sock->type == NET_SOCK_STREAM && *optlen >= sizeof(int))
                    {
                        *((int *)optval) = sock->keepalive ? 1 : 0;
                        *optlen = sizeof(int);
                    }
                    break;

                case NET_SO_BROADCAST:
                    if (sock->type == NET_SOCK_DGRAM && *optlen >= sizeof(int))
                    {
                        *((int *)optval) = sock->broadcast ? 1 : 0;
                        *optlen = sizeof(int);
                    }
                    break;

                case NET_SO_RCVBUF:
                    if (*optlen >= sizeof(int))
                    {
                        *((int *)optval) = sock->rcv_buf_size;
                        *optlen = sizeof(int);
                    }
                    break;

                case NET_SO_SNDBUF:
                    if (*optlen >= sizeof(int))
                    {
                        *((int *)optval) = sock->snd_buf_size;
                        *optlen = sizeof(int);
                    }
                    break;

                default:
                    return -(int32_t)22;
            }
            break;

        case NET_IPPROTO_TCP:
            switch (optname)
            {
                case NET_TCP_NODELAY:
                    if (sock->type == NET_SOCK_STREAM && *optlen >= sizeof(int))
                    {
                        /* nodelay = true 表示禁用 Nagle 算法 */
                        *((int *)optval) = tcb->nagle_enabled ? 0 : 1;
                        *optlen = sizeof(int);
                    }
                    break;

                default:
                    return -(int32_t)22;
            }
            break;

        default:
            return -(int32_t)22;
    }

    return KERNEL_OK;
}

/**
 * @brief 套接字控制操作
 * @param sock_id     套接字 ID
 * @param request     请求类型（NET_FIONBIO/NET_FIONREAD）
 * @param arg         参数
 * @return KERNEL_OK 成功
 */
kernel_status_t net_ioctl(uint32_t sock_id, uint32_t request, void *arg)
{
    net_socket_t *sock;

    if (arg == NULL)
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

    switch (request)
    {
        case NET_FIONBIO:
            /* 设置非阻塞模式 */
            sock->nonblocking = (*((int *)arg) != 0);
            break;

        case NET_FIONREAD:
            /* 获取可读字节数 */
            if (sock->type == NET_SOCK_STREAM)
            {
                tcp_tcb_t *tcb = &s_tcp_tcbs[sock_id];
                *((int *)arg) = (int)tcb->recv_len;
            }
            else
            {
                /* UDP: 计算接收队列中的数据包数量 */
                uint32_t count = s_udp_rx_head[sock_id] - s_udp_rx_tail[sock_id];
                *((int *)arg) = (int)count;
            }
            break;

        default:
            return -(int32_t)22;
    }

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

/* ========================================================================
 * 网络协议栈测试
 * ======================================================================== */

/**
 * @brief 打印测试报告
 */
static void print_test_report(void)
{
    printf("\n==========================================\n");
    printf("Network Stack Test Report\n");
    printf("==========================================\n");
    printf("ICMP Echo Requests: %u\n", s_icmp_echo_sent);
    printf("ICMP Echo Receives: %u\n", s_icmp_echo_recv);
    printf("ICMP Echo Replies:  %u\n", s_icmp_echo_reply_sent);
    printf("==========================================\n");
}

/**
 * @brief 网络协议栈初始化测试
 */
static void test_network_stack_init(void)
{
    uint32_t if_count;
    uint32_t sock_count;

    printf("\n==========================================\n");
    printf("Network Stack Initialization Test\n");
    printf("==========================================\n");

    /* 检查网络接口数量 */
    if_count = 0U;
    for (uint32_t i = 0U; i < NET_MAX_INTERFACES; i++)
    {
        if (s_interfaces[i].in_use)
        {
            if_count++;
        }
    }
    printf("Interfaces: %u / %u\n", if_count, NET_MAX_INTERFACES);

    /* 检查套接字数量 */
    sock_count = 0U;
    for (uint32_t i = 0U; i < NET_MAX_SOCKETS; i++)
    {
        if (s_sockets[i].in_use)
        {
            sock_count++;
        }
    }
    printf("Sockets:    %u / %u\n", sock_count, NET_MAX_SOCKETS);

    /* 检查 TCP TCB 数量 */
    sock_count = 0U;
    for (uint32_t i = 0U; i < NET_MAX_SOCKETS; i++)
    {
        if (s_tcp_tcbs[i].in_use)
        {
            sock_count++;
        }
    }
    printf("TCP TCBs:   %u / %u\n", sock_count, NET_MAX_SOCKETS);

    printf("==========================================\n");
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    kernel_status_t ret;

    ret = net_init();
    if (ret != KERNEL_OK)
    {
        return (int)ret;
    }

    /* 运行网络协议栈初始化测试 */
    test_network_stack_init();

    /* 打印测试报告 */
    print_test_report();

    for (;;)
    {
        /* 更新时间计数器（每 10ms） */
        s_time_ms += 10ULL;
        s_tcp_timer_accum_ms += 10ULL;

        /* 处理接收包 */
        {
            uint32_t if_id;
            uint8_t rx_tmp[NET_MAX_PACKET_SIZE];

            for (if_id = 0U; if_id < NET_MAX_INTERFACES; if_id++)
            {
                if (s_interfaces[if_id].state == NET_IF_RUNNING)
                {
                    int32_t rx_ret;

                    rx_ret = net_rx_packet(if_id, rx_tmp,
                        (uint64_t)NET_MAX_PACKET_SIZE);

                    if (rx_ret > 0)
                    {
                        /* 成功接收到数据包，处理协议栈 */
                        /* net_rx_packet 内部已处理以太网帧解析 */
                    }
                }
            }
        }

        /* TCP 定时器检查（每 TCP_RETRANSMIT_PERIOD_MS 执行一次） */
        if (s_tcp_timer_accum_ms >= TCP_RETRANSMIT_PERIOD_MS)
        {
            /* TCP 重传定时器检查 */
            tcp_retransmit_check();

            /* TCP Keepalive 定时器检查 */
            tcp_keepalive_check();

            s_tcp_timer_accum_ms = 0ULL;
        }
    }

    return 0;
}
