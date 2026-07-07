/**
 * @file    socket.c
 * @brief   内核 socket 子系统（回环模式）
 * @author  AISafe64 Team
 * @date    2026-07-08
 * @version 1.0
 *
 * @details 最小 socket 实现，支持：
 *          - socket() 创建 socket fd
 *          - bind/listen/accept/connect（回环，不真正连接）
 *          - sendto/recvfrom（回环缓冲）
 *
 *          当前为内核内回环模式（类似 RAMFS 直通）。
 *          后续 net 服务运行后切换到 IPC 模式。
 *
 * @note    后续迁移到用户态 net 服务
 *
 * @revision history
 * v1.0 2026-07-08 初始版本（回环模式）
 */

#include <kernel/socket.h>
#include <kernel/config.h>
#include <kernel/errno.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef SOCKET_MAX_FDS
#define SOCKET_MAX_FDS 8U
#endif

#ifndef SOCKET_BUF_SIZE
#define SOCKET_BUF_SIZE 1024U
#endif

/* ========================================================================
 * 数据结构
 * ======================================================================== */

/**
 * @brief socket 描述符
 */
typedef struct
{
    bool      in_use;            /**< @brief 是否被使用 */
    uint32_t  domain;            /**< @brief 协议族（AF_INET=2） */
    uint32_t  type;              /**< @brief socket 类型（SOCK_STREAM=1） */
    uint32_t  bound_port;        /**< @brief 绑定端口 */
    bool      listening;         /**< @brief 是否在监听 */
    bool      connected;         /**< @brief 是否已连接 */

    /* 回环缓冲（connect/accept 后的数据通道） */
    uint8_t   rx_buf[SOCKET_BUF_SIZE]; /**< @brief 接收缓冲 */
    uint32_t  rx_head;           /**< @brief 接收缓冲读位置 */
    uint32_t  rx_tail;           /**< @brief 接收缓冲写位置 */
    uint32_t  rx_count;          /**< @brief 接收缓冲数据量 */

    /* 配对的 socket（回环连接的另一端） */
    int32_t   peer_fd;           /**< @brief 配对 socket fd（-1=未连接） */
} socket_t;

/** @brief socket 描述符表 */
static socket_t s_sockets[SOCKET_MAX_FDS];

/** @brief 初始化标志 */
static bool s_initialized = false;

/* ========================================================================
 * 内部辅助
 * ======================================================================== */

/**
 * @brief 分配空闲 socket 槽位
 *
 * @return fd（>= 0），无空闲返回 -1
 */
static int32_t socket_alloc(void)
{
    uint32_t i;
    for (i = 0U; i < SOCKET_MAX_FDS; i++)
    {
        if (!s_sockets[i].in_use)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

/**
 * @brief 验证 fd 有效性
 *
 * @param fd socket fd
 * @return true 有效
 */
static bool socket_valid(int32_t fd)
{
    return ((fd >= 0) && ((uint32_t)fd < SOCKET_MAX_FDS) && s_sockets[fd].in_use);
}

/* ========================================================================
 * 公共 API
 * ======================================================================== */

void socket_subsys_init(void)
{
    uint32_t i;
    for (i = 0U; i < SOCKET_MAX_FDS; i++)
    {
        s_sockets[i].in_use = false;
        s_sockets[i].listening = false;
        s_sockets[i].connected = false;
        s_sockets[i].bound_port = 0U;
        s_sockets[i].peer_fd = -1;
        s_sockets[i].rx_count = 0U;
        s_sockets[i].rx_head = 0U;
        s_sockets[i].rx_tail = 0U;
    }
    s_initialized = true;
}

int32_t sys_socket(uint32_t domain, uint32_t type, uint32_t protocol)
{
    int32_t fd;
    socket_t *s;

    (void)protocol;

    if (!s_initialized || (domain != 2U)) /* AF_INET */
    {
        return -(int32_t)EINVAL;
    }

    fd = socket_alloc();
    if (fd < 0)
    {
        return -(int32_t)ENOMEM;
    }

    s = &s_sockets[fd];
    (void)memset(s, 0, sizeof(socket_t));
    s->in_use = true;
    s->domain = domain;
    s->type = type;
    s->peer_fd = -1;

    return fd;
}

int32_t sys_bind(int32_t fd, uint32_t port)
{
    if (!socket_valid(fd))
    {
        return -(int32_t)EBADF;
    }

    s_sockets[fd].bound_port = port;
    return 0;
}

int32_t sys_listen(int32_t fd, uint32_t backlog)
{
    (void)backlog;

    if (!socket_valid(fd))
    {
        return -(int32_t)EBADF;
    }

    s_sockets[fd].listening = true;
    return 0;
}

int32_t sys_accept(int32_t fd)
{
    int32_t new_fd;

    if (!socket_valid(fd) || !s_sockets[fd].listening)
    {
        return -(int32_t)EBADF;
    }

    /* 回环模式：创建一个新 socket 并配对 */
    new_fd = socket_alloc();
    if (new_fd < 0)
    {
        return -(int32_t)ENOMEM;
    }

    {
        socket_t *ns = &s_sockets[new_fd];
        (void)memset(ns, 0, sizeof(socket_t));
        ns->in_use = true;
        ns->domain = s_sockets[fd].domain;
        ns->type = s_sockets[fd].type;
        ns->connected = true;
        ns->peer_fd = -1; /* 回环模式下暂不配对 */
    }

    return new_fd;
}

int32_t sys_connect(int32_t fd, uint32_t port)
{
    (void)port;
    if (!socket_valid(fd)) { return -(int32_t)EBADF; }
    s_sockets[fd].connected = true;
    return 0;
}

int32_t sys_sendto(int32_t fd, const void *buf, uint32_t len, int32_t peer_fd)
{
    socket_t *s;

    if (!socket_valid(fd) || (buf == NULL))
    {
        return -(int32_t)EINVAL;
    }

    s = &s_sockets[fd];

    /* 如果指定了 peer_fd，将数据写入 peer 的 rx_buf */
    if ((peer_fd >= 0) && socket_valid(peer_fd))
    {
        socket_t *peer = &s_sockets[peer_fd];
        uint32_t space = SOCKET_BUF_SIZE - peer->rx_count;
        uint32_t to_write = (len < space) ? len : space;

        if (to_write > 0U)
        {
            uint32_t i;
            for (i = 0U; i < to_write; i++)
            {
                peer->rx_buf[peer->rx_tail] = ((const uint8_t *)buf)[i];
                peer->rx_tail = (peer->rx_tail + 1U) % SOCKET_BUF_SIZE;
            }
            peer->rx_count += to_write;
        }

        return (int32_t)to_write;
    }

    /* 无 peer：回环写入自己的 rx_buf */
    {
        uint32_t space = SOCKET_BUF_SIZE - s->rx_count;
        uint32_t to_write = (len < space) ? len : space;

        if (to_write > 0U)
        {
            uint32_t i;
            for (i = 0U; i < to_write; i++)
            {
                s->rx_buf[s->rx_tail] = ((const uint8_t *)buf)[i];
                s->rx_tail = (s->rx_tail + 1U) % SOCKET_BUF_SIZE;
            }
            s->rx_count += to_write;
        }

        return (int32_t)to_write;
    }
}

int32_t sys_recvfrom(int32_t fd, void *buf, uint32_t len)
{
    socket_t *s;

    if (!socket_valid(fd) || (buf == NULL))
    {
        return -(int32_t)EINVAL;
    }

    s = &s_sockets[fd];

    if (s->rx_count == 0U)
    {
        return 0; /* 无数据（非阻塞） */
    }

    {
        uint32_t to_read = (len < s->rx_count) ? len : s->rx_count;
        uint32_t i;
        for (i = 0U; i < to_read; i++)
        {
            ((uint8_t *)buf)[i] = s->rx_buf[s->rx_head];
            s->rx_head = (s->rx_head + 1U) % SOCKET_BUF_SIZE;
        }
        s->rx_count -= to_read;

        return (int32_t)to_read;
    }
}

int32_t sys_shutdown(int32_t fd)
{
    if (!socket_valid(fd))
    {
        return -(int32_t)EBADF;
    }

    s_sockets[fd].connected = false;
    return 0;
}

int32_t sys_close_socket(int32_t fd)
{
    if (!socket_valid(fd))
    {
        return -(int32_t)EBADF;
    }

    s_sockets[fd].in_use = false;
    return 0;
}
