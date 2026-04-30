/**
 * @file    net_if_auto.h
 * @brief   网络接口自动发现和注册接口
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @details 网络接口自动发现和注册接口：
 *          - 提供驱动自动注册接口的机制
 *          - 网络协议栈自动发现已注册的接口
 *
 * @note 这是一个简化的实现，假设网络接口层是全局可访问的
 * @note 在完整实现中，应该使用 IPC 机制
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_NET_NET_IF_NET_IF_AUTO_H
#define SERVICES_NET_NET_IF_NET_IF_AUTO_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 网络接口操作接口（简化版）
 * ======================================================================== */

/** @brief 网络接口操作接口 */
typedef struct net_if_ops_auto
{
    int32_t (*init)(void);                      /**< @brief 初始化网络接口 */
    int64_t (*send_frame)(const void *buf, uint64_t size); /**< @brief 发送以太网帧 */
    int64_t (*recv_frame)(void *buf, uint64_t size);      /**< @brief 接收以太网帧 */
    int32_t (*close)(void);                     /**< @brief 关闭网络接口 */
    bool (*is_running)(void);                    /**< @brief 获取接口状态 */
} net_if_ops_auto_t;

/* ========================================================================
 * 内部类型定义
 * ======================================================================== */

/** @brief 内部网络接口条目 */
typedef struct net_if_auto_internal
{
    net_if_ops_auto_t ops;                       /**< @brief 操作接口 */
} net_if_auto_internal_t;

/* ========================================================================
 * 公共接口
 * ======================================================================== */

/**
 * @brief 注册网络接口（自动发现机制）
 *
 * @param name      接口名称（如 "eth0"）
 * @param driver    驱动名称（如 "virtio-net"）
 * @param ops       操作接口指针
 * @param mac_addr  MAC 地址
 *
 * @return 0 成功，负数错误码
 *
 * @note 这是一个简化的实现，在完整版本中应该使用 IPC
 */
int32_t net_if_auto_register(const char *name, const char *driver,
                                const net_if_ops_auto_t *ops,
                                const uint8_t mac_addr[6]);

/**
 * @brief 注销网络接口
 *
 * @param name  接口名称
 *
 * @return 0 成功，负数错误码
 */
int32_t net_if_auto_unregister(const char *name);

/**
 * @brief 获取网络接口数量
 *
 * @return 网络接口数量
 */
uint32_t net_if_auto_get_count(void);

/**
 * @brief 通过索引获取接口名称
 *
 * @param index 接口索引
 * @param name  输出接口名称
 * @param len   名称缓冲区大小
 *
 * @return 0 成功，负数错误码
 */
int32_t net_if_auto_get_name(uint32_t index, char *name, uint32_t len);

/**
 * @brief 通过接口名称获取操作接口
 *
 * @param name  接口名称
 *
 * @return 操作接口指针，NULL 表示未找到
 */
const net_if_ops_auto_t *net_if_auto_get_ops(const char *name);

/**
 * @brief 通过索引获取 MAC 地址
 *
 * @param index    接口索引
 * @param mac_addr  输出 MAC 地址
 *
 * @return 0 成功，负数错误码
 */
int32_t net_if_auto_get_mac_addr(uint32_t index, uint8_t mac_addr[6]);

#endif /* SERVICES_NET_NET_IF_NET_IF_AUTO_H */
