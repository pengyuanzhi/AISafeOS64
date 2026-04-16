/**
 * @file    ethernet.h
 * @brief   以太网帧处理
 * @author  AISafe64 Team
 * @date    2026-04-16
 * @version 1.0
 *
 * @details 以太网帧处理：
 *          - 以太网帧头解析和构造
 *          - 以太网类型识别（IPv4、ARP、IPX）
 *          - CRC 校验（可选）
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: NW-001
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_NET_NET_IF_ETHERNET_H
#define SERVICES_NET_NET_IF_ETHERNET_H

#include <stdint.h>

/* ========================================================================
 * 以太网帧头定义
 * ======================================================================== */

/**
 * @brief 以太网帧头大小
 */
#define ETHERNET_HDR_SIZE     14U

/**
 * @brief 以太网类型：IPv4
 */
#define ETHER_TYPE_IPV4       0x0800U

/**
 * @brief 以太网类型：ARP
 */
#define ETHER_TYPE_ARP        0x0806U

/**
 * @brief 以太网类型：IPX
 */
#define ETHER_TYPE_IPX        0x8137U

/**
 * @brief 以太网类型：IPv6
 */
#define ETHER_TYPE_IPV6       0x86DDU

/**
 * @brief 以太网帧头
 */
typedef struct __attribute__((packed))
{
    uint8_t  dst_mac[6];    /**< @brief 目标 MAC 地址 */
    uint8_t  src_mac[6];    /**< @brief 源 MAC 地址 */
    uint16_t eth_type;      /**< @brief 以太网类型（IPv4/ARP/...） */
} ethernet_hdr_t;

/* ========================================================================
 * 函数声明
 * ======================================================================== */

/**
 * @brief 解析以太网帧类型
 *
 * @param hdr   以太网帧头
 *
 * @return 以太网类型
 */
uint16_t ethernet_get_type(const ethernet_hdr_t *hdr);

/**
 * @brief 检查是否为 IPv4 帧
 *
 * @param hdr   以太网帧头
 *
 * @return true = 是 IPv4，false = 否
 */
int ethernet_is_ipv4(const ethernet_hdr_t *hdr);

/**
 * @brief 检查是否为 ARP 帧
 *
 * @param hdr   以太网帧头
 *
 * @return true = 是 ARP，false = 否
 */
int ethernet_is_arp(const ethernet_hdr_t *hdr);

/**
 * @brief 检查是否为 IPv6 帧
 *
 * @param hdr   以太网帧头
 *
 * @return true = 是 IPv6，false = 否
 */
int ethernet_is_ipv6(const ethernet_hdr_t *hdr);

/**
 * @brief 解析以太网帧（提取 payload）
 *
 * @param buf   以太网帧缓冲区
 * @param size  帧大小
 * @param hdr   输出以太网帧头
 * @param payload_size  输出 payload 大小
 *
 * @return 0 成功，负数错误码
 */
int32_t ethernet_parse(const void *buf, uint64_t size,
                       ethernet_hdr_t *hdr, uint64_t *payload_size);

#endif /* SERVICES_NET_NET_IF_ETHERNET_H */
