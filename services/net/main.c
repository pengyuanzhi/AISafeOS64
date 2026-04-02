/**
 * @file    main.c
 * @brief   网络协议栈服务实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 用户态网络协议栈服务：
 *          - 网络接口注册与管理
 *          - 套接字生命周期管理
 *          - 数据包收发框架
 *          - 接口统计
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: NW-001~005
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/netstack.h>
#include <kernel/config.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 网络栈全局状态
 * ======================================================================== */

/** @brief 网络接口表 */
static net_interface_t s_interfaces[NET_MAX_INTERFACES];

/** @brief 套接字表 */
static net_socket_t s_sockets[NET_MAX_SOCKETS];

/** @brief 接收环缓冲区（简化：每接口一个） */
static uint8_t s_rx_buf[NET_MAX_INTERFACES][NET_RX_QUEUE_DEPTH][NET_MAX_PACKET_SIZE];

/** @brief 接收环 head/tail */
static uint32_t s_rx_head[NET_MAX_INTERFACES];
static uint32_t s_rx_tail[NET_MAX_INTERFACES];

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
    }

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
 * 数据包收发
 * ======================================================================== */

int64_t net_rx_packet(uint32_t if_id, void *buf, uint64_t size)
{
    net_interface_t *iface;
    uint32_t head;
    uint32_t tail;
    uint32_t used;

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

    /* 检查接收环是否有数据 */
    head = s_rx_head[if_id];
    tail = s_rx_tail[if_id];

    if (head >= tail)
    {
        used = head - tail;
    }
    else
    {
        used = NET_RX_QUEUE_DEPTH - (tail - head);
    }

    if (used == 0U)
    {
        return 0LL;
    }

    /* 从接收环复制数据 */
    uint64_t copy_size = size;
    if (copy_size > NET_MAX_PACKET_SIZE)
    {
        copy_size = NET_MAX_PACKET_SIZE;
    }

    (void)memcpy(buf, s_rx_buf[if_id][tail % NET_RX_QUEUE_DEPTH], (size_t)copy_size);

    s_rx_tail[if_id] = (tail + 1U) % NET_RX_QUEUE_DEPTH;

    iface->stats.rx_packets++;
    iface->stats.rx_bytes += copy_size;

    return (int64_t)copy_size;
}

int64_t net_tx_packet(uint32_t if_id, const void *buf, uint64_t size)
{
    net_interface_t *iface;

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

    if (size > (uint64_t)iface->mtu)
    {
        iface->stats.tx_dropped++;
        return -(int64_t)22;
    }

    /*
     * 实际实现中：
     * 1. 通过 DMA 将数据发送到 NIC
     * 2. 或通过 IC2 通道发送到驱动容器
     *
     * 此处为框架实现
     */
    (void)buf;

    iface->stats.tx_packets++;
    iface->stats.tx_bytes += size;

    return (int64_t)size;
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

    return KERNEL_OK;
}

kernel_status_t net_listen(uint32_t sock_id, uint32_t backlog)
{
    net_socket_t *sock;

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

    return KERNEL_OK;
}

int32_t net_accept(uint32_t sock_id)
{
    net_socket_t *listen_sock;
    int32_t new_id;
    net_socket_t *new_sock;

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

    return new_id;
}

kernel_status_t net_connect(uint32_t sock_id, const net_sockaddr_t *addr)
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

    (void)memcpy(&sock->remote_addr, addr, sizeof(net_sockaddr_t));
    sock->state = NET_SOCK_CONNECTED;

    return KERNEL_OK;
}

int64_t net_send(uint32_t sock_id, const void *buf, uint64_t size)
{
    net_socket_t *sock;

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

    /*
     * 实际实现中：
     * 1. 封装 TCP/UDP 头部
     * 2. 封装 IP 头部
     * 3. 封装以太网帧头
     * 4. 调用 net_tx_packet 发送
     */
    (void)buf;
    sock->tx_count++;

    return (int64_t)size;
}

int64_t net_recv(uint32_t sock_id, void *buf, uint64_t size)
{
    net_socket_t *sock;

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

    /*
     * 实际实现中：
     * 1. 从接收队列中查找匹配端口的数据包
     * 2. 解析协议头部
     * 3. 复制有效负载到用户缓冲区
     */
    (void)size;
    sock->rx_count++;

    return 0LL;
}

kernel_status_t net_close(uint32_t sock_id)
{
    net_socket_t *sock;

    if (sock_id >= NET_MAX_SOCKETS)
    {
        return -(int32_t)22;
    }

    sock = &s_sockets[sock_id];

    if (!sock->in_use)
    {
        return -(int32_t)2;
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
        /* 实际实现中通过 IPC 接收并处理网络请求 */
    }

    return 0;
}
