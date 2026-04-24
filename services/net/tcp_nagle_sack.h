/**
 * @file    tcp_nagle_sack.h
 * @brief   TCP Nagle 算法和 SACK 接口声明
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_NET_TCP_NAGLE_SACK_H
#define SERVICES_NET_TCP_NAGLE_SACK_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * Nagle 算法函数声明
 * ======================================================================== */

/**
 * @brief 检查是否可以发送（Nagle 算法）
 *
 * @param tcb TCP 控制块
 * @param unacked 未确认的数据长度
 *
 * @return true 可以发送，false 不能发送
 */
bool tcp_nagle_can_send(tcp_tcb_t *tcb, uint32_t unacked);

/**
 * @brief 设置 Cork 模式
 *
 * @param tcb TCP 控制块
 * @param enable true 启用，false 禁用
 */
void tcp_nagle_set_cork(tcp_tcb_t *tcb, bool enable);

/* ========================================================================
 * SACK 函数声明
 * ======================================================================== */

/**
 * @brief 添加 SACK 块
 *
 * @param tcb TCP 控制块
 * @param left 左边界序列号
 * @param right 右边界序列号
 */
void tcp_sack_add_block(tcp_tcb_t *tcb, uint32_t left, uint32_t right);

/**
 * @brief 检查 SACK 包含
 *
 * @param tcb TCP 控制块
 * @param seq 序列号
 *
 * @return true 包含，false 不包含
 */
bool tcp_sack_contains(tcp_tcb_t *tcb, uint32_t seq);

/**
 * @brief 清除 SACK 块
 *
 * @param tcb TCP 控制块
 */
void tcp_sack_clear(tcp_tcb_t *tcb);

/* ========================================================================
 * 延迟 ACK 函数声明
 * ======================================================================== */

/**
 * @brief 延迟 ACK 处理
 *
 * @param tcb TCP 控制块
 */
void tcp_delayed_ack(tcp_tcb_t *tcb);

/**
 * @brief 立即发送 ACK
 *
 * @param tcb TCP 控制块
 */
void tcp_send_immediate_ack(tcp_tcb_t *tcb);

#endif /* SERVICES_NET_TCP_NAGLE_SACK_H */
