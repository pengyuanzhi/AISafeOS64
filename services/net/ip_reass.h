/**
 * @file    ip_reass.h
 * @brief   IP 分片重组接口声明
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_NET_IP_REASS_H
#define SERVICES_NET_IP_REASS_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 函数声明
 * ======================================================================== */

/**
 * @brief 查找或创建重组队列
 *
 * @param src_ip 源 IP 地址（网络字节序）
 * @param dst_ip 目的 IP 地址（网络字节序）
 * @param frag_id 分片标识符
 *
 * @return 重组队列指针，失败返回 NULL
 */
ip_reass_queue_t *ip_find_reass_queue(uint32_t src_ip, uint32_t dst_ip, uint16_t frag_id);

/**
 * @brief 添加分片到重组队列
 *
 * @param queue 重组队列
 * @param ip_hdr IP 头部
 * @param payload IP 载荷
 * @param payload_len 载荷长度
 *
 * @return KERNEL_OK 成功，负数错误码
 */
kernel_status_t ip_add_reass_frag(ip_reass_queue_t *queue,
                                   const ipv4_header_t *ip_hdr,
                                   const uint8_t *payload,
                                   uint32_t payload_len);

/**
 * @brief 分片重组
 *
 * @param queue 重组队列
 * @param output 输出缓冲区
 * @param output_len 输出缓冲区长度
 *
 * @return 实际重组长度，负数表示错误
 */
int32_t ip_reassemble(ip_reass_queue_t *queue, uint8_t *output, uint32_t output_len);

/**
 * @brief 分片超时处理
 *
 * @param current_time 当前时间（毫秒）
 */
void ip_reass_timeout(uint64_t current_time);

#endif /* SERVICES_NET_IP_REASS_H */
