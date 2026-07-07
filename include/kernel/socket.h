/**
 * @file    socket.h
 * @brief   内核 socket 子系统接口（回环模式）
 * @author  AISafe64 Team
 * @date    2026-07-08
 * @version 1.0
 *
 * @details 最小 socket 实现，支持基本 POSIX socket 操作。
 *          当前为内核内回环模式，后续迁移到用户态 net 服务。
 *
 * @revision history
 * v1.0 2026-07-08 初始版本
 */

#ifndef KERNEL_SOCKET_H
#define KERNEL_SOCKET_H

#include <stdint.h>

/**
 * @brief 初始化 socket 子系统
 */
void socket_subsys_init(void);

/**
 * @brief 创建 socket
 * @param domain 协议族（AF_INET=2）
 * @param type socket 类型（SOCK_STREAM=1）
 * @param protocol 协议（0=自动）
 * @return >= 0 socket fd，< 0 错误码
 */
int32_t sys_socket(uint32_t domain, uint32_t type, uint32_t protocol);

/**
 * @brief 绑定端口
 */
int32_t sys_bind(int32_t fd, uint32_t port);

/**
 * @brief 监听
 */
int32_t sys_listen(int32_t fd, uint32_t backlog);

/**
 * @brief 接受连接
 */
int32_t sys_accept(int32_t fd);

/**
 * @brief 连接
 */
int32_t sys_connect(int32_t fd, uint32_t port);

/**
 * @brief 发送数据
 * @param fd socket fd
 * @param buf 数据
 * @param len 长度
 * @param peer_fd 配对 fd（-1=回环写入自己）
 */
int32_t sys_sendto(int32_t fd, const void *buf, uint32_t len, int32_t peer_fd);

/**
 * @brief 接收数据
 */
int32_t sys_recvfrom(int32_t fd, void *buf, uint32_t len);

/**
 * @brief 关闭连接
 */
int32_t sys_shutdown(int32_t fd);

/**
 * @brief 关闭 socket
 */
int32_t sys_close_socket(int32_t fd);

#endif /* KERNEL_SOCKET_H */
