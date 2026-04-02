/**
 * @file    netstack.h
 * @brief   网络协议栈框架接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 用户态网络协议栈框架：
 *          - 网络接口管理（NIC 抽象）
 *          - L2/L3/L4 分层协议框架
 *          - 套接字接口（简化 POSIX socket）
 *          - 数据包收发缓冲区管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: NW-001~005
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_NETSTACK_H
#define KERNEL_NETSTACK_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 网络常量
 * ======================================================================== */

/** @brief 最大网络接口数 */
#define NET_MAX_INTERFACES          4U

/** @brief MAC 地址长度 */
#define NET_MAC_ADDR_LEN            6U

/** @brief IPv4 地址长度 */
#define NET_IPV4_ADDR_LEN           4U

/** @brief IPv6 地址长度 */
#define NET_IPV6_ADDR_LEN           16U

/** @brief 最大数据包大小 */
#define NET_MAX_PACKET_SIZE         1514U

/** @brief 最大套接字数 */
#define NET_MAX_SOCKETS             32U

/** @brief 最大套接字缓冲区 */
#define NET_SOCKET_BUF_SIZE         8192U

/** @brief 接收队列深度 */
#define NET_RX_QUEUE_DEPTH          64U

/* ========================================================================
 * 网络地址类型
 * ======================================================================== */

/**
 * @brief MAC 地址
 */
typedef struct
{
    uint8_t bytes[NET_MAC_ADDR_LEN]; /**< @brief MAC 地址字节 */
} net_mac_t;

/**
 * @brief IPv4 地址
 */
typedef struct
{
    uint8_t bytes[NET_IPV4_ADDR_LEN]; /**< @brief IPv4 地址字节 */
} net_ipv4_t;

/**
 * @brief IPv6 地址
 */
typedef struct
{
    uint8_t bytes[NET_IPV6_ADDR_LEN]; /**< @brief IPv6 地址字节 */
} net_ipv6_t;

/**
 * @brief 通用地址联合体
 */
typedef union
{
    net_ipv4_t ipv4;                 /**< @brief IPv4 地址 */
    net_ipv6_t ipv6;                 /**< @brief IPv6 地址 */
} net_addr_t;

/* ========================================================================
 * 网络接口类型
 * ======================================================================== */

/**
 * @brief 网络接口链路类型
 */
typedef enum
{
    NET_LINK_LOOPBACK = 0U,  /**< @brief 回环 */
    NET_LINK_ETHERNET,        /**< @brief 以太网 */
    NET_LINK_WIFI,            /**< @brief Wi-Fi */
    NET_LINK_PPP              /**< @brief 点对点 */
} net_link_type_t;

/**
 * @brief 网络接口状态
 */
typedef enum
{
    NET_IF_DOWN = 0U,        /**< @brief 已关闭 */
    NET_IF_UP,               /**< @brief 已启用 */
    NET_IF_RUNNING           /**< @brief 运行中 */
} net_if_state_t;

/**
 * @brief 套接字域
 */
typedef enum
{
    NET_AF_INET = 0U,        /**< @brief IPv4 */
    NET_AF_INET6,            /**< @brief IPv6 */
    NET_AF_PACKET            /**< @brief 原始数据包 */
} net_af_t;

/**
 * @brief 套接字类型
 */
typedef enum
{
    NET_SOCK_STREAM = 0U,    /**< @brief TCP 流 */
    NET_SOCK_DGRAM,          /**< @brief UDP 数据报 */
    NET_SOCK_RAW             /**< @brief 原始套接字 */
} net_sock_type_t;

/**
 * @brief 套接字状态
 */
typedef enum
{
    NET_SOCK_CLOSED = 0U,    /**< @brief 已关闭 */
    NET_SOCK_LISTENING,      /**< @brief 监听中 */
    NET_SOCK_CONNECTING,     /**< @brief 连接中 */
    NET_SOCK_CONNECTED,      /**< @brief 已连接 */
    NET_SOCK_BOUND           /**< @brief 已绑定 */
} net_sock_state_t;

/* ========================================================================
 * 网络接口描述符
 * ======================================================================== */

/**
 * @brief 网络接口统计
 */
typedef struct
{
    uint64_t    rx_packets;      /**< @brief 接收包数 */
    uint64_t    tx_packets;      /**< @brief 发送包数 */
    uint64_t    rx_bytes;        /**< @brief 接收字节数 */
    uint64_t    tx_bytes;        /**< @brief 发送字节数 */
    uint64_t    rx_errors;       /**< @brief 接收错误 */
    uint64_t    tx_errors;       /**< @brief 发送错误 */
    uint64_t    rx_dropped;      /**< @brief 接收丢包 */
    uint64_t    tx_dropped;      /**< @brief 发送丢包 */
} net_if_stats_t;

/**
 * @brief 网络接口描述符
 */
typedef struct
{
    uint32_t         if_id;          /**< @brief 接口 ID */
    char             name[16];       /**< @brief 接口名（如 "eth0"） */
    net_link_type_t  link_type;      /**< @brief 链路类型 */
    net_if_state_t   state;          /**< @brief 接口状态 */
    net_mac_t        mac_addr;       /**< @brief MAC 地址 */
    net_ipv4_t       ipv4_addr;      /**< @brief IPv4 地址 */
    net_ipv4_t       ipv4_mask;      /**< @brief 子网掩码 */
    net_ipv4_t       ipv4_gw;        /**< @brief 默认网关 */
    uint32_t         mtu;            /**< @brief 最大传输单元 */
    uint32_t         driver_id;      /**< @brief 关联驱动 ID */
    net_if_stats_t   stats;          /**< @brief 统计信息 */
    bool             in_use;         /**< @brief 使用标记 */
} net_interface_t;

/* ========================================================================
 * 套接字描述符
 * ======================================================================== */

/**
 * @brief 套接字地址
 */
typedef struct
{
    net_af_t    family;     /**< @brief 地址族 */
    uint16_t    port;       /**< @brief 端口号 */
    net_addr_t  addr;       /**< @brief 地址 */
} net_sockaddr_t;

/**
 * @brief 套接字描述符
 */
typedef struct
{
    uint32_t         sock_id;        /**< @brief 套接字 ID */
    net_af_t         family;         /**< @brief 地址族 */
    net_sock_type_t  type;           /**< @brief 套接字类型 */
    net_sock_state_t state;          /**< @brief 套接字状态 */
    uint32_t         if_id;          /**< @brief 绑定接口 */
    net_sockaddr_t   local_addr;     /**< @brief 本地地址 */
    net_sockaddr_t   remote_addr;    /**< @brief 远端地址 */
    uint32_t         rx_count;       /**< @brief 接收缓冲区包数 */
    uint32_t         tx_count;       /**< @brief 发送缓冲区包数 */
    bool             in_use;         /**< @brief 使用标记 */
} net_socket_t;

/* ========================================================================
 * 网络栈 API
 * ======================================================================== */

/**
 * @brief 初始化网络栈
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: NW-001
 */
kernel_status_t net_init(void);

/**
 * @brief 注册网络接口
 *
 * @param name       接口名
 * @param link_type  链路类型
 * @param mac_addr   MAC 地址
 * @param driver_id  关联驱动 ID
 *
 * @return 成功返回接口 ID，失败返回负错误码
 *
 * @note 对应需求: NW-001
 */
int32_t net_register_interface(const char *name, net_link_type_t link_type,
                                 const net_mac_t *mac_addr, uint32_t driver_id);

/**
 * @brief 启用网络接口
 *
 * @param if_id 接口 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t net_if_up(uint32_t if_id);

/**
 * @brief 禁用网络接口
 *
 * @param if_id 接口 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t net_if_down(uint32_t if_id);

/**
 * @brief 设置接口 IPv4 地址
 *
 * @param if_id  接口 ID
 * @param addr   IPv4 地址
 * @param mask   子网掩码
 * @param gw     默认网关
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: NW-002
 */
kernel_status_t net_if_set_ipv4(uint32_t if_id, const net_ipv4_t *addr,
                                  const net_ipv4_t *mask, const net_ipv4_t *gw);

/**
 * @brief 接收数据包
 *
 * @param if_id 接口 ID
 * @param buf   数据缓冲区
 * @param size  数据大小
 *
 * @return 实际接收大小，负数表示错误
 *
 * @note 对应需求: NW-003
 */
int64_t net_rx_packet(uint32_t if_id, void *buf, uint64_t size);

/**
 * @brief 发送数据包
 *
 * @param if_id 接口 ID
 * @param buf   数据缓冲区
 * @param size  数据大小
 *
 * @return 实际发送大小，负数表示错误
 */
int64_t net_tx_packet(uint32_t if_id, const void *buf, uint64_t size);

/**
 * @brief 创建套接字
 *
 * @param family 地址族
 * @param type   套接字类型
 *
 * @return 成功返回套接字 ID，失败返回负错误码
 *
 * @note 对应需求: NW-004
 */
int32_t net_socket(net_af_t family, net_sock_type_t type);

/**
 * @brief 绑定套接字到地址
 *
 * @param sock_id 套接字 ID
 * @param addr    地址
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t net_bind(uint32_t sock_id, const net_sockaddr_t *addr);

/**
 * @brief 监听连接
 *
 * @param sock_id 套接字 ID
 * @param backlog 待处理连接数
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t net_listen(uint32_t sock_id, uint32_t backlog);

/**
 * @brief 接受连接
 *
 * @param sock_id 套接字 ID
 *
 * @return 新套接字 ID，负数表示错误
 */
int32_t net_accept(uint32_t sock_id);

/**
 * @brief 建立连接
 *
 * @param sock_id 套接字 ID
 * @param addr    远端地址
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t net_connect(uint32_t sock_id, const net_sockaddr_t *addr);

/**
 * @brief 发送数据
 *
 * @param sock_id 套接字 ID
 * @param buf     数据缓冲区
 * @param size    数据大小
 *
 * @return 实际发送字节数，负数表示错误
 *
 * @note 对应需求: NW-005
 */
int64_t net_send(uint32_t sock_id, const void *buf, uint64_t size);

/**
 * @brief 接收数据
 *
 * @param sock_id 套接字 ID
 * @param buf     缓冲区
 * @param size    缓冲区大小
 *
 * @return 实际接收字节数，负数表示错误
 *
 * @note 对应需求: NW-005
 */
int64_t net_recv(uint32_t sock_id, void *buf, uint64_t size);

/**
 * @brief 关闭套接字
 *
 * @param sock_id 套接字 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t net_close(uint32_t sock_id);

/**
 * @brief 获取接口描述符
 *
 * @param if_id 接口 ID
 *
 * @return 接口描述符指针
 */
net_interface_t *net_get_interface(uint32_t if_id);

/**
 * @brief 获取套接字描述符
 *
 * @param sock_id 套接字 ID
 *
 * @return 套接字描述符指针
 */
net_socket_t *net_get_socket(uint32_t sock_id);

#endif /* KERNEL_NETSTACK_H */
