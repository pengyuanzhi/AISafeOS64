/**
 * @file tcp_cubic.h
 * @brief CUBIC 拥塞控制算法接口
 *
 * 本文件定义了 CUBIC 拥塞控制算法的接口
 */

#ifndef TCP_CUBIC_H
#define TCP_CUBIC_H

#include <stdint.h>

/* ========================================================================
 * 数据结构定义
 * ======================================================================== */

/** @brief CUBIC 拥塞控制状态 */
typedef struct cubic_state_t cubic_state_t;

/* ========================================================================
 * 公共函数
 * ======================================================================== */

/**
 * @brief 初始化 CUBIC 状态
 *
 * @param state CUBIC 状态
 */
void cubic_init(cubic_state_t *state);

/**
 * @brief CUBIC 慢启动阶段
 *
 * @param state CUBIC 状态
 * @return 更新后的窗口大小
 */
uint32_t cubic_slow_start(cubic_state_t *state);

/**
 * @brief CUBIC 拥塞避免阶段
 *
 * @param state CUBIC 状态
 * @param current_time 当前时间（毫秒）
 * @return 更新后的窗口大小
 */
uint32_t cubic_congestion_avoidance(cubic_state_t *state,
                                     uint32_t current_time);

/**
 * @brief CUBIC 快速重传
 *
 * @param state CUBIC 状态
 * @return 更新后的窗口大小
 */
uint32_t cubic_fast_retransmit(cubic_state_t *state);

/**
 * @brief CUBIC 快速恢复
 *
 * @param state CUBIC 状态
 * @return 更新后的窗口大小
 */
uint32_t cubic_fast_recovery(cubic_state_t *state);

/**
 * @brief CUBIC 超时
 *
 * @param state CUBIC 状态
 * @return 更新后的窗口大小
 */
uint32_t cubic_timeout(cubic_state_t *state);

/**
 * @brief 处理新 ACK
 *
 * @param state CUBIC 状态
 * @param bytes_acked 确认的字节数
 * @return 更新后的窗口大小
 */
uint32_t cubic_handle_ack(cubic_state_t *state, uint32_t bytes_acked);

/**
 * @brief 获取当前窗口大小
 *
 * @param state CUBIC 状态
 * @return 当前窗口大小
 */
uint32_t cubic_get_cwnd(const cubic_state_t *state);

/**
 * @brief 获取当前状态
 *
 * @param state CUBIC 状态
 * @return 当前状态
 */
uint8_t cubic_get_state(const cubic_state_t *state);

/**
 * @brief 获取峰值窗口大小
 *
 * @param state CUBIC 状态
 * @return 峰值窗口大小
 */
uint32_t cubic_get_w_max(const cubic_state_t *state);

#endif /* TCP_CUBIC_H */
