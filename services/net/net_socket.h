/**
 * @file    net_socket.h
 * @brief   网络套接字 API 头文件
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details 网络套接字 API 接口：
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
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef NET_SOCKET_H
#define NET_SOCKET_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 套接字标志
 * ======================================================================== */

/** @brief 套接字标志 */
#define NET_SOCK_NONBLOCK      0x1000U  /**< @brief 非阻塞 */
#define NET_SOCK_REUSEADDR     0x0200U  /**< @brief 地址重用 */

/* ========================================================================
 * 地址族
 * ======================================================================== */

/** @brief 地址族 */
#define NET_AF_INET            2U       /**< @brief IPv4 */
#define NET_AF_INET6           10U      /**< @brief IPv6 */

/* ========================================================================
 * 套接字地址结构
 * ======================================================================== */

/** @brief IPv4 套接字地址结构 */
typedef struct
{
    uint16_t sin_family;        /**< @brief 地址族 */
    uint16_t sin_port;          /**< @brief 端口（网络字节序） */
    uint32_t sin_addr;          /**< @brief IP 地址（网络字节序） */
    char     sin_zero[8];       /**< @brief 填充 */
} net_sockaddr_in_t;

/** @brief 最大套接字数量 */
#define NET_MAX_SOCKETS        64U

/* ========================================================================
 * 套接字 API
 * ======================================================================== */

/**
 * @brief 初始化套接字模块
 *
 * @return 0 成功，<0 失败
 */
int32_t net_socket_init(void);

/**
 * @brief 创建套接字
 *
 * @param domain    地址族（AF_INET）
 * @param type      套接字类型（SOCK_STREAM/SOCK_DGRAM）
 * @param protocol  协议（IPPROTO_TCP/IPPROTO_UDP）
 *
 * @return 套接字描述符（>=0 成功），<0 失败
 */
int32_t net_socket(int32_t domain, int32_t type, int32_t protocol);

/**
 * @brief 绑定地址
 *
 * @param fd        套接字描述符
 * @param addr      套接字地址
 * @param addr_len  地址长度
 *
 * @return 0 成功，<0 失败
 */
int32_t net_bind(int32_t fd, const net_sockaddr_in_t *addr, uint32_t addr_len);

/**
 * @brief 监听连接
 *
 * @param fd        套接字描述符
 * @param backlog   等待连接队列长度
 *
 * @return 0 成功，<0 失败
 */
int32_t net_listen(int32_t fd, int32_t backlog);

/**
 * @brief 接受连接
 *
 * @param fd        套接字描述符
 * @param addr      输出客户端地址
 * @param addr_len  输入/输出地址长度
 *
 * @return 新套接字描述符（>=0 成功），<0 失败
 */
int32_t net_accept(int32_t fd, net_sockaddr_in_t *addr, uint32_t *addr_len);

/**
 * @brief 建立连接
 *
 * @param fd        套接字描述符
 * @param addr      服务器地址
 * @param addr_len  地址长度
 *
 * @return 0 成功，<0 失败
 */
int32_t net_connect(int32_t fd, const net_sockaddr_in_t *addr, uint32_t addr_len);

/**
 * @brief 接收数据
 *
 * @param fd        套接字描述符
 * @param buf       接收缓冲区
 * @param len       接收长度
 *
 * @return 实际接收字节数（>=0 成功），<0 失败
 */
int32_t net_recv(int32_t fd, void *buf, uint32_t len);

/**
 * @brief 发送数据
 *
 * @param fd        套接字描述符
 * @param buf       发送缓冲区
 * @param len       发送长度
 *
 * @return 实际发送字节数（>=0 成功），<0 失败
 */
int32_t net_send(int32_t fd, const void *buf, uint32_t len);

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
                     net_sockaddr_in_t *addr, uint32_t *addr_len);

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
                   const net_sockaddr_in_t *addr, uint32_t addr_len);

/**
 * @brief 关闭套接字
 *
 * @param fd        套接字描述符
 *
 * @return 0 成功，<0 失败
 */
int32_t net_close(int32_t fd);

/**
 * @brief 关闭连接（部分关闭）
 *
 * @param fd        套接字描述符
 * @param how       关闭方式（0：关闭读，1：关闭写，2：全部关闭）
 *
 * @return 0 成功，<0 失败
 */
int32_t net_shutdown(int32_t fd, int32_t how);

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
                       void *optval, uint32_t *optlen);

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
                       const void *optval, uint32_t optlen);

#endif /* NET_SOCKET_H */
