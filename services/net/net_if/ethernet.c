/**
 * @file    ethernet.c
 * @brief   以太网帧处理实现
 * @author  AISafe64 Team
 * @date    2026-04-16
 * @version 1.0
 *
 * @details 以太网帧处理：
 *          - 以太网帧头解析和构造
 *          - 以太网类型识别（IPv4、ARP、IPX、IPv6）
 *          - Payload 提取
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: NW-001
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "ethernet.h"
#include <string.h>
#include <stdbool.h>

uint16_t ethernet_get_type(const ethernet_hdr_t *hdr)
{
    if (hdr == NULL)
    {
        return 0U;
    }

    /* 网络字节序（大端序） */
    return (uint16_t)(((hdr->eth_type >> 8U) & 0xFFU) | (hdr->eth_type << 8U));
}

int ethernet_is_ipv4(const ethernet_hdr_t *hdr)
{
    uint16_t eth_type;

    if (hdr == NULL)
    {
        return false;
    }

    eth_type = ethernet_get_type(hdr);

    return (eth_type == ETHER_TYPE_IPV4);
}

int ethernet_is_arp(const ethernet_hdr_t *hdr)
{
    uint16_t eth_type;

    if (hdr == NULL)
    {
        return false;
    }

    eth_type = ethernet_get_type(hdr);

    return (eth_type == ETHER_TYPE_ARP);
}

int ethernet_is_ipv6(const ethernet_hdr_t *hdr)
{
    uint16_t eth_type;

    if (hdr == NULL)
    {
        return false;
    }

    eth_type = ethernet_get_type(hdr);

    return (eth_type == ETHER_TYPE_IPV6);
}

int32_t ethernet_parse(const void *buf, uint64_t size,
                       ethernet_hdr_t *hdr, uint64_t *payload_size)
{
    if ((buf == NULL) || (hdr == NULL) || (payload_size == NULL))
    {
        return -22; /* EINVAL */
    }

    if (size < ETHERNET_HDR_SIZE)
    {
        return -5; /* EIO */
    }

    /* 复制以太网帧头 */
    (void)memcpy(hdr, buf, ETHERNET_HDR_SIZE);

    /* 提取 payload 大小 */
    *payload_size = size - ETHERNET_HDR_SIZE;

    return 0;
}
