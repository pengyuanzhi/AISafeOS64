/**
 * @file    icmp_error.h
 * @brief   ICMP 错误消息接口声明
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_NET_ICMP_ERROR_H
#define SERVICES_NET_ICMP_ERROR_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 函数声明
 * ======================================================================== */

/**
 * @brief 发送目的不可达消息
 *
 * @param if_id 接口 ID
 * @param src_ip 源 IP 地址（主机字节序）
 * @param dst_ip 目的 IP 地址（主机字节序）
 * @param ip_hdr 原始 IP 头部
 * @param code ICMP 代码
 */
void icmp_send_dest_unreachable(uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
                                const ipv4_header_t *ip_hdr, uint8_t code);

/**
 * @brief 发送超时消息
 *
 * @param if_id 接口 ID
 * @param src_ip 源 IP 地址（主机字节序）
 * @param dst_ip 目的 IP 地址（主机字节序）
 * @param ip_hdr 原始 IP 头部
 * @param code ICMP 代码
 */
void icmp_send_time_exceeded(uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
                              const ipv4_header_t *ip_hdr, uint8_t code);

/**
 * @brief 发送参数问题消息
 *
 * @param if_id 接口 ID
 * @param src_ip 源 IP 地址（主机字节序）
 * @param dst_ip 目的 IP 地址（主机字节序）
 * @param ip_hdr 原始 IP 头部
 * @param code ICMP 代码
 * @param ptr 指向错误的字节偏移
 */
void icmp_send_param_problem(uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
                              const ipv4_header_t *ip_hdr, uint8_t code, uint8_t ptr);

/**
 * @brief 处理 IP 分片超时
 *
 * @param if_id 接口 ID
 * @param src_ip 源 IP 地址（主机字节序）
 * @param dst_ip 目的 IP 地址（主机字节序）
 * @param frag_id 分片标识符
 */
void icmp_send_reass_timeout(uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
                             uint16_t frag_id);

#endif /* SERVICES_NET_ICMP_ERROR_H */
