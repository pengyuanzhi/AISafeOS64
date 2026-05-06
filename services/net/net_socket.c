/**
 * @file    net_socket.c
 * @brief   网络套接字 API 实现
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details 网络套接字 API 实现：
 *          - socket() - 创建套接字
 *          - bind() - 绑定地址
 *          - listen() - 监听连接
 *          - accept() - 接受连接
 *          - connect() - 建立连接
 *          - recv() - 接收数据
 *          - send() - 发送数据
 *          - close() - 关闭套接字
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "net_socket.h"
#include "net_types.h"
#include "net_config.h"
#include <string.h>
#include <stdlib.h>

/* ========================================================================
 * 内部变量
 * ======================================================================== */

/** @brief 套接字表 */
static net_socket_t s_socket_table[NET_MAX_SOCKETS];
static bool         s_socket_initialized = false;

/** @brief 下一个套接字描述符 */
static int32_t      s_next_fd = 3;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 查找套接字
 *
 * @param fd        套接字描述符
 *
 * @return 套接字指针，NULL 表示无效
 */
static net_socket_t *find_socket(int32_t fd)
{
    if (fd < 0 || (uint32_t)fd >= NET_MAX_SOCKETS)
    {
        return NULL;
    }

    return &s_socket_table[fd];
}

/**
 * @brief 分配套接字描述符
 *
 * @return 套接字描述符（>=0 成功），<0 失败
 */
static int32_t alloc_socket(void)
{
    uint32_t i;

    for (i = 0U; i < NET_MAX_SOCKETS; i++)
    {
        if (!s_socket_table[i].in_use)
        {
            s_socket_table[i].in_use = true;
            s_socket_table[i].type = 0U;
            s_socket_table[i].protocol = 0U;
            s_socket_table[i].local_port = 0U;
            s_socket_table[i].local_ip = 0U;
            s_socket_table[i].remote_port = 0U;
            s_socket_table[i].remote_ip = 0U;
            s_socket_table[i].state = NET_TCP_CLOSED;
            s_socket_table[i].seq_num = 0U;
            s_socket_table[i].ack_num = 0U;
            s_socket_table[i].window = 0U;
            return (int32_t)i;
        }
    }

    return -9; /* EMFILE */
}

/**
 * @brief 释放套接字描述符
 *
 * @param fd        套接字描述符
 */
static void free_socket(int32_t fd)
{
    net_socket_t *sock = find_socket(fd);

    if (sock != NULL)
    {
        sock->in_use = false;
    }
}

/**
 * @brief 检查地址是否有效
 *
 * @param addr      套接字地址
 * @param addr_len  地址长度
 *
 * @return 0 有效，<0 无效
 */
static int32_t check_addr(const net_sockaddr_in_t *addr, uint32_t addr_len)
{
    if (addr == NULL || addr_len != sizeof(net_sockaddr_in_t))
    {
        return -22; /* EINVAL */
    }

    if (addr->sin_family != NET_AF_INET)
    {
        return -97; /* EAFNOSUPPORT */
    }

    if (addr->sin_port == 0U)
    {
        return -22; /* EINVAL */
    }

    return 0;
}

/* ========================================================================
 * 套接字 API 实现
 * ======================================================================== */

/**
 * @brief 初始化套接字模块
 *
 * @return 0 成功，<0 失败
 */
int32_t net_socket_init(void)
{
    (void)memset(s_socket_table, 0, sizeof(s_socket_table));
    s_socket_initialized = true;
    s_next_fd = 3;

    return 0;
}

/**
 * @brief 创建套接字
 *
 * @param domain    地址族（AF_INET）
 * @param type      套接字类型（SOCK_STREAM/SOCK_DGRAM）
 * @param protocol  协议（IPPROTO_TCP/IPPROTO_UDP）
 *
 * @return 套接字描述符（>=0 成功），<0 失败
 */
int32_t net_socket(int32_t domain, int32_t type, int32_t protocol)
{
    int32_t fd;
    net_socket_t *sock;

    /* 检查参数 */
    if (domain != NET_AF_INET)
    {
        return -97; /* EAFNOSUPPORT */
    }

    if (type != NET_SOCK_STREAM && type != NET_SOCK_DGRAM)
    {
        return -22; /* EINVAL */
    }

    if (protocol != NET_IPPROTO_TCP && protocol != NET_IPPROTO_UDP)
    {
        return -22; /* EINVAL */
    }

    /* 分配套接字 */
    fd = alloc_socket();

    if (fd < 0)
    {
        return -9; /* EMFILE */
    }

    /* 初始化套接字 */
    sock = &s_socket_table[fd];

    sock->type = (net_sock_type_t)type;
    sock->protocol = (uint32_t)protocol;
    sock->local_ip = net_config_get_ip();
    sock->state = NET_TCP_CLOSED;

    return fd;
}

/**
 * @brief 绑定地址
 *
 * @param fd        套接字描述符
 * @param addr      套接字地址
 * @param addr_len  地址长度
 *
 * @return 0 成功，<0 失败
 */
int32_t net_bind(int32_t fd, const net_sockaddr_in_t *addr, uint32_t addr_len)
{
    net_socket_t *sock;
    int32_t ret;

    /* 检查参数 */
    ret = check_addr(addr, addr_len);
    if (ret != 0)
    {
        return ret;
    }

    /* 查找套接字 */
    sock = find_socket(fd);

    if (sock == NULL || !sock->in_use)
    {
        return -9; /* EBADF */
    }

    /* 检查端口是否已绑定 */
    if (sock->local_port != 0U)
    {
        return -98; /* EADDRINUSE */
    }

    /* 绑定地址 */
    sock->local_port = addr->sin_port;
    sock->local_ip = addr->sin_addr;

    return 0;
}

/**
 * @brief 监听连接
 *
 * @param fd        套接字描述符
 * @param backlog   等待连接队列长度
 *
 * @return 0 成功，<0 失败
 */
int32_t net_listen(int32_t fd, int32_t backlog)
{
    net_socket_t *sock;

    /* 检查参数 */
    if (backlog < 0)
    {
        return -22; /* EINVAL */
    }

    /* 查找套接字 */
    sock = find_socket(fd);

    if (sock == NULL || !sock->in_use)
    {
        return -9; /* EBADF */
    }

    /* 检查是否为 TCP 套接字 */
    if (sock->type != NET_SOCK_STREAM)
    {
        return -22; /* EOPNOTSUPP */
    }

    /* 检查是否已绑定 */
    if (sock->local_port == 0U)
    {
        return -22; /* EINVAL */
    }

    /* 设置为监听状态 */
    sock->state = NET_TCP_LISTEN;

    return 0;
}

/**
 * @brief 接受连接
 *
 * @param fd        套接字描述符
 * @param addr      输出客户端地址
 * @param addr_len  输入/输出地址长度
 *
 * @return 新套接字描述符（>=0 成功），<0 失败
 */
int32_t net_accept(int32_t fd, net_sockaddr_in_t *addr, uint32_t *addr_len)
{
    net_socket_t *sock;
    int32_t new_fd;
    net_socket_t *new_sock;

    /* 查找监听套接字 */
    sock = find_socket(fd);

    if (sock == NULL || !sock->in_use)
    {
        return -9; /* EBADF */
    }

    if (sock->state != NET_TCP_LISTEN)
    {
        return -22; /* EINVAL */
    }

    /* 分配新套接字 */
    new_fd = alloc_socket();

    if (new_fd < 0)
    {
        return -9; /* EMFILE */
    }

    /* 初始化新套接字 */
    new_sock = &s_socket_table[new_fd];

    new_sock->type = sock->type;
    new_sock->protocol = sock->protocol;
    new_sock->local_port = sock->local_port;
    new_sock->local_ip = sock->local_ip;
    new_sock->state = NET_TCP_ESTABLISHED;

    /* 返回客户端地址 */
    if (addr != NULL && addr_len != NULL)
    {
        addr->sin_family = NET_AF_INET;
        addr->sin_port = 0U; /* 模拟客户端端口 */
        addr->sin_addr = 0U;  /* 模拟客户端 IP */
        *addr_len = sizeof(net_sockaddr_in_t);
    }

    return new_fd;
}

/**
 * @brief 建立连接
 *
 * @param fd        套接字描述符
 * @param addr      服务器地址
 * @param addr_len  地址长度
 *
 * @return 0 成功，<0 失败
 */
int32_t net_connect(int32_t fd, const net_sockaddr_in_t *addr, uint32_t addr_len)
{
    net_socket_t *sock;
    int32_t ret;

    /* 检查参数 */
    ret = check_addr(addr, addr_len);
    if (ret != 0)
    {
        return ret;
    }

    /* 查找套接字 */
    sock = find_socket(fd);

    if (sock == NULL || !sock->in_use)
    {
        return -9; /* EBADF */
    }

    /* 检查是否为 TCP 套接字 */
    if (sock->type != NET_SOCK_STREAM)
    {
        return -22; /* EOPNOTSUPP */
    }

    /* 设置远程地址 */
    sock->remote_port = addr->sin_port;
    sock->remote_ip = addr->sin_addr;

    /* 设置为已建立状态（模拟） */
    sock->state = NET_TCP_ESTABLISHED;

    return 0;
}

/**
 * @brief 接收数据
 *
 * @param fd        套接字描述符
 * @param buf       接收缓冲区
 * @param len       接收长度
 *
 * @return 实际接收字节数（>=0 成功），<0 失败
 */
int32_t net_recv(int32_t fd, void *buf, uint32_t len)
{
    net_socket_t *sock;

    /* 检查参数 */
    if (buf == NULL || len == 0U)
    {
        return -22; /* EINVAL */
    }

    /* 查找套接字 */
    sock = find_socket(fd);

    if (sock == NULL || !sock->in_use)
    {
        return -9; /* EBADF */
    }

    /* 检查状态 */
    if (sock->state != NET_TCP_ESTABLISHED)
    {
        return -9; /* EBADF */
    }

    /* 模拟接收数据 */
    (void)memset(buf, 0, len);

    return (int32_t)len;
}

/**
 * @brief 发送数据
 *
 * @param fd        套接字描述符
 * @param buf       发送缓冲区
 * @param len       发送长度
 *
 * @return 实际发送字节数（>=0 成功），<0 失败
 */
int32_t net_send(int32_t fd, const void *buf, uint32_t len)
{
    net_socket_t *sock;

    /* 检查参数 */
    if (buf == NULL || len == 0U)
    {
        return -22; /* EINVAL */
    }

    /* 查找套接字 */
    sock = find_socket(fd);

    if (sock == NULL || !sock->in_use)
    {
        return -9; /* EBADF */
    }

    /* 检查状态 */
    if (sock->state != NET_TCP_ESTABLISHED)
    {
        return -9; /* EBADF */
    }

    /* 模拟发送数据 */
    /* 省略具体实现 */

    return (int32_t)len;
}

/**
 * @brief 接收数据（指定源地址）
 *
 * @param fd        套接字描述符
 * @param buf       接收缓冲区
 * @param len       接收长度
 * @param addr      输出源地址
 * @param addr_len  输入/输出地址长度
 *
 * @return 实际接收字节数（>=0 成功），<0 失败
 */
int32_t net_recvfrom(int32_t fd, void *buf, uint32_t len,
                     net_sockaddr_in_t *addr, uint32_t *addr_len)
{
    int32_t ret;

    /* 调用 net_recv */
    ret = net_recv(fd, buf, len);

    /* 返回源地址 */
    if (ret >= 0 && addr != NULL && addr_len != NULL)
    {
        net_socket_t *sock = find_socket(fd);
        if (sock != NULL)
        {
            addr->sin_family = NET_AF_INET;
            addr->sin_port = sock->remote_port;
            addr->sin_addr = sock->remote_ip;
            *addr_len = sizeof(net_sockaddr_in_t);
        }
    }

    return ret;
}

/**
 * @brief 发送数据（指定目的地址）
 *
 * @param fd        套接字描述符
 * @param buf       发送缓冲区
 * @param len       发送长度
 * @param addr      目的地址
 * @param addr_len  地址长度
 *
 * @return 实际发送字节数（>=0 成功），<0 失败
 */
int32_t net_sendto(int32_t fd, const void *buf, uint32_t len,
                   const net_sockaddr_in_t *addr, uint32_t addr_len)
{
    net_socket_t *sock;
    int32_t ret;

    /* 检查参数 */
    if (addr != NULL && addr_len != 0U)
    {
        /* UDP 发送：设置目的地址 */
        ret = check_addr(addr, addr_len);
        if (ret != 0)
        {
            return ret;
        }
    }

    /* 调用 net_send */
    ret = net_send(fd, buf, len);

    /* 更新远程地址 */
    if (ret >= 0 && addr != NULL)
    {
        sock = find_socket(fd);
        if (sock != NULL)
        {
            sock->remote_port = addr->sin_port;
            sock->remote_ip = addr->sin_addr;
        }
    }

    return ret;
}

/**
 * @brief 关闭套接字
 *
 * @param fd        套接字描述符
 *
 * @return 0 成功，<0 失败
 */
int32_t net_close(int32_t fd)
{
    net_socket_t *sock;

    /* 查找套接字 */
    sock = find_socket(fd);

    if (sock == NULL || !sock->in_use)
    {
        return -9; /* EBADF */
    }

    /* 关闭连接 */
    if (sock->state == NET_TCP_ESTABLISHED)
    {
        sock->state = NET_TCP_CLOSED;
    }

    /* 释放套接字 */
    free_socket(fd);

    return 0;
}

/**
 * @brief 关闭连接（部分关闭）
 *
 * @param fd        套接字描述符
 * @param how       关闭方式（0：关闭读，1：关闭写，2：全部关闭）
 *
 * @return 0 成功，<0 失败
 */
int32_t net_shutdown(int32_t fd, int32_t how)
{
    net_socket_t *sock;

    /* 检查参数 */
    if (how < 0 || how > 2)
    {
        return -22; /* EINVAL */
    }

    /* 查找套接字 */
    sock = find_socket(fd);

    if (sock == NULL || !sock->in_use)
    {
        return -9; /* EBADF */
    }

    /* 关闭连接 */
    if (how == 2 || how == 1) /* 关闭写或全部 */
    {
        sock->state = NET_TCP_CLOSED;
    }

    return 0;
}

/**
 * @brief 获取套接字选项
 *
 * @param fd        套接字描述符
 * @param level     选项级别
 * @param optname   选项名称
 * @param optval    输出选项值
 * @param optlen    输入/输出选项长度
 *
 * @return 0 成功，<0 失败
 */
int32_t net_getsockopt(int32_t fd, int32_t level, int32_t optname,
                       void *optval, uint32_t *optlen)
{
    net_socket_t *sock;

    /* 查找套接字 */
    sock = find_socket(fd);

    if (sock == NULL || !sock->in_use)
    {
        return -9; /* EBADF */
    }

    if (optval == NULL || optlen == NULL)
    {
        return -22; /* EINVAL */
    }

    /* 模拟选项获取 */
    /* 省略具体实现 */

    return 0;
}

/**
 * @brief 设置套接字选项
 *
 * @param fd        套接字描述符
 * @param level     选项级别
 * @param optname   选项名称
 * @param optval    选项值
 * @param optlen    选项长度
 *
 * @return 0 成功，<0 失败
 */
int32_t net_setsockopt(int32_t fd, int32_t level, int32_t optname,
                       const void *optval, uint32_t optlen)
{
    net_socket_t *sock;

    /* 查找套接字 */
    sock = find_socket(fd);

    if (sock == NULL || !sock->in_use)
    {
        return -9; /* EBADF */
    }

    if (optval == NULL || optlen == 0U)
    {
        return -22; /* EINVAL */
    }

    /* 模拟选项设置 */
    /* 省略具体实现 */

    return 0;
}
