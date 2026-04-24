/**
 * @file    net_if_ipc.h
 * @brief   网络接口 IPC 协议定义
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @details 网络接口 IPC 协议：
 *          - 定义网络接口注册的 IPC 消息格式
 *          - 允许驱动通过网络协议栈注册接口
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: NW-001, NW-002
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_NET_NET_IF_NET_IF_IPC_H
#define SERVICES_NET_NET_IF_NET_IF_IPC_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * IPC 消息类型
 * ======================================================================== */

/** @brief 网络接口 IPC 消息类型 */
typedef enum
{
    NET_IF_IPC_REGISTER = 1U,       /**< @brief 注册网络接口 */
    NET_IF_IPC_UNREGISTER = 2U,     /**< @brief 注销网络接口 */
    NET_IF_IPC_SEND_FRAME = 3U,     /**< @brief 发送以太网帧 */
    NET_IF_IPC_RECV_FRAME = 4U,     /**< @brief 接收以太网帧 */
    NET_IF_IPC_GET_STATUS = 5U      /**< @brief 获取接口状态 */
} net_if_ipc_type_t;

/* ========================================================================
 * 网络接口注册消息
 * ======================================================================== */

/** @brief 网络接口注册请求 */
typedef struct
{
    uint8_t name[16];           /**< @brief 接口名称（如 "eth0"） */
    uint8_t driver[16];         /**< @brief 驱动名称（如 "virtio-net"） */
    uint8_t mac_addr[6];        /**< @brief MAC 地址 */
    uint32_t mtu;              /**< @brief MTU */
    uint64_t driver_ep;        /**< @brief 驱动端点地址 */
} net_if_register_req_t;

/** @brief 网络接口注册响应 */
typedef struct
{
    int32_t result;             /**< @brief 结果（0 成功，负数错误码） */
    uint32_t if_id;             /**< @brief 接口 ID */
} net_if_register_resp_t;

/* ========================================================================
 * 以太网帧消息
 * ======================================================================== */

/** @brief 以太网帧发送/接收请求 */
typedef struct
{
    uint32_t if_id;             /**< @brief 接口 ID */
    uint32_t size;              /**< @brief 帧大小 */
    uint8_t data[1500];         /**< @brief 帧数据（最大 1500 字节） */
} net_if_frame_msg_t;

/** @brief 以太网帧发送/接收响应 */
typedef struct
{
    int64_t result;             /**< @brief 结果（实际大小，负数错误码） */
} net_if_frame_resp_t;

/* ========================================================================
 * IPC 消息
 * ======================================================================== */

/** @brief 网络接口 IPC 消息 */
typedef struct
{
    net_if_ipc_type_t type;     /**< @brief 消息类型 */
    union
    {
        net_if_register_req_t reg_req;      /**< @brief 注册请求 */
        net_if_register_resp_t reg_resp;    /**< @brief 注册响应 */
        net_if_frame_msg_t frame_req;       /**< @brief 帧请求 */
        net_if_frame_resp_t frame_resp;     /**< @brief 帧响应 */
    } data;
} net_if_ipc_msg_t;

/* ========================================================================
 * 公共接口
 * ======================================================================== */

/**
 * @brief 网络接口 IPC 服务地址
 */
#define NET_IF_IPC_SERVICE_NAME  "net_if"

/**
 * @brief 网络接口 IPC 端点名称
 */
#define NET_IF_IPC_ENDPOINT_NAME  "net_if_ep"

#endif /* SERVICES_NET_NET_IF_NET_IF_IPC_H */
