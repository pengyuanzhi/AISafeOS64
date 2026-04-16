/**
 * @file    net_if.h
 * @brief   网络接口抽象层接口
 * @author  AISafe64 Team
 * @date    2026-04-16
 * @version 1.0
 *
 * @details 网络接口抽象层：
 *          - 定义统一的网络接口操作接口
 *          - 支持多种网卡驱动（VirtIO、Ethernet、WiFi、PPP）
 *          - 驱动注册和管理机制
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: NW-001, NW-002
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_NET_NET_IF_NET_IF_H
#define SERVICES_NET_NET_IF_NET_IF_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 网络接口操作接口
 * ======================================================================== */

/**
 * @brief 网络接口操作接口
 */
typedef struct net_if_ops
{
    /**
     * @brief 初始化网络接口
     *
     * @return 0 成功，负数错误码
     */
    int32_t (*init)(void);

    /**
     * @brief 发送以太网帧
     *
     * @param buf   以太网帧缓冲区
     * @param size  帧大小（必须包含以太网头）
     *
     * @return 实际发送大小，负数表示错误
     */
    int64_t (*send_frame)(const void *buf, uint64_t size);

    /**
     * @brief 接收以太网帧
     *
     * @param buf   接收缓冲区
     * @param size  缓冲区大小
     *
     * @return 实际接收大小，负数表示错误
     */
    int64_t (*recv_frame)(void *buf, uint64_t size);

    /**
     * @brief 关闭网络接口
     *
     * @return 0 成功，负数错误码
     */
    int32_t (*close)(void);

    /**
     * @brief 获取接口状态
     *
     * @return 接口状态（true = 运行中，false = 关闭）
     */
    bool (*is_running)(void);
} net_if_ops_t;

/* ========================================================================
 * 网络接口管理
 * ======================================================================== */

/**
 * @brief 注册网络接口驱动
 *
 * @param name      接口名称（如 "eth0"）
 * @param driver    驱动名称（如 "virtio-net"）
 * @param ops       操作接口指针
 * @param mac_addr  MAC 地址
 *
 * @return 0 成功，负数错误码
 */
int32_t net_if_register(const char *name, const char *driver,
                        const net_if_ops_t *ops, const uint8_t mac_addr[6]);

/**
 * @brief 获取网络接口操作接口
 *
 * @param name      接口名称
 *
 * @return 操作接口指针，NULL 表示未找到
 */
const net_if_ops_t *net_if_get_ops(const char *name);

/**
 * @brief 发送以太网帧（通过接口层）
 *
 * @param name      接口名称
 * @param buf       以太网帧缓冲区
 * @param size      帧大小
 *
 * @return 实际发送大小，负数表示错误
 */
int64_t net_if_send_frame(const char *name, const void *buf, uint64_t size);

/**
 * @brief 接收以太网帧（通过接口层）
 *
 * @param name      接口名称
 * @param buf       接收缓冲区
 * @param size      缓冲区大小
 *
 * @return 实际接收大小，负数表示错误
 */
int64_t net_if_recv_frame(const char *name, void *buf, uint64_t size);

/**
 * @brief 获取网络接口列表
 *
 * @return 接口数量
 */
uint32_t net_if_get_count(void);

#endif /* SERVICES_NET_NET_IF_NET_IF_H */
