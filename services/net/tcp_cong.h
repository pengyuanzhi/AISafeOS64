/**
 * @file    tcp_cong.h
 * @brief   TCP 拥塞控制接口声明
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_NET_TCP_CONG_H
#define SERVICES_NET_TCP_CONG_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 函数声明
 * ======================================================================== */

/**
 * @brief 处理新 ACK（调用拥塞控制）
 *
 * @param tcb TCP 控制块
 * @param ack ACK 序列号
 */
void tcp_handle_new_ack(tcp_tcb_t *tcb, uint32_t ack);

/**
 * @brief 处理重复 ACK（触发快速重传）
 *
 * @param tcb TCP 控制块
 * @param ack ACK 序列号
 */
void tcp_handle_dup_ack(tcp_tcb_t *tcb, uint32_t ack);

/**
 * @brief 处理超时（调用拥塞控制）
 *
 * @param tcb TCP 控制块
 */
void tcp_handle_timeout(tcp_tcb_t *tcb);

/**
 * @brief 处理 RTT 样本（更新 RTT 和 RTO）
 *
 * @param tcb TCP 控制块
 * @param rtt_sample RTT 样本（毫秒）
 */
void tcp_handle_rtt_sample(tcp_tcb_t *tcb, uint32_t rtt_sample);

#endif /* SERVICES_NET_TCP_CONG_H */
