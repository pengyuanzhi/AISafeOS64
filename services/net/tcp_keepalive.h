/**
 * @file tcp_keepalive.h
 * @brief TCP Keepalive 接口
 *
 * 本文件定义了 TCP Keepalive 的接口
 */

#ifndef TCP_KEEPALIVE_H
#define TCP_KEEPALIVE_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 数据结构定义
 * ======================================================================== */

/** @brief TCP Keepalive 配置 */
typedef struct tcp_keepalive_config_t tcp_keepalive_config_t;

/** @brief TCP Keepalive 状态 */
typedef struct tcp_keepalive_state_t tcp_keepalive_state_t;

/** @brief TCP Keepalive 头部 */
typedef struct tcp_keepalive_header_t tcp_keepalive_header_t;

/* ========================================================================
 * Keepalive 配置函数
 * ======================================================================== */

/**
 * @brief 初始化 Keepalive 配置
 *
 * @param config Keepalive 配置
 */
void keepalive_init_config(tcp_keepalive_config_t *config);

/**
 * @brief 处理连接活跃
 *
 * @param state Keepalive 状态
 * @param current_time 当前时间（秒）
 */
void keepalive_handle_activity(tcp_keepalive_state_t *state,
                               uint32_t current_time);

/**
 * @brief 检查 Keepalive 是否应该发送探测
 *
 * @param config Keepalive 配置
 * @param state Keepalive 状态
 * @param current_time 当前时间（秒）
 * @return true=应该发送，false=不应该发送
 */
bool keepalive_should_send_probe(const tcp_keepalive_config_t *config,
                                  tcp_keepalive_state_t *state,
                                  uint32_t current_time);

/**
 * @brief 发送 Keepalive 探测
 *
 * @param state Keepalive 状态
 * @param config Keepalive 配置
 * @param current_time 当前时间（秒）
 * @return true=探测发送成功，false=探测失败
 */
bool keepalive_send_probe(tcp_keepalive_state_t *state,
                           const tcp_keepalive_config_t *config,
                           uint32_t current_time);

/**
 * @brief 处理 Keepalive 超时
 *
 * @param state Keepalive 状态
 * @param config Keepalive 配置
 */
void keepalive_handle_timeout(tcp_keepalive_state_t *state,
                               const tcp_keepalive_config_t *config);

/**
 * @brief 重置 Keepalive 状态
 *
 * @param state Keepalive 状态
 */
void keepalive_reset_state(tcp_keepalive_state_t *state);

/* ========================================================================
 * Keepalive 头部函数
 * ======================================================================== */

/**
 * @brief 构造 Keepalive 数据包头部
 *
 * @param header 输出头部结构
 */
void keepalive_build_header(tcp_keepalive_header_t *header);

/**
 * @brief 获取 Keepalive 头部长度
 *
 * @param header Keepalive 头部
 * @return 头部长度
 */
uint16_t keepalive_get_length(const tcp_keepalive_header_t *header);

/* ========================================================================
 * Keepalive 控制
 * ======================================================================== */

/**
 * @brief 启用 Keepalive
 *
 * @param config Keepalive 配置
 */
void keepalive_enable(tcp_keepalive_config_t *config);

/**
 * @brief 禁用 Keepalive
 *
 * @param config Keepalive 配置
 */
void keepalive_disable(tcp_keepalive_config_t *config);

/**
 * @brief 检查 Keepalive 是否启用
 *
 * @param config Keepalive 配置
 * @return true=已启用，false=已禁用
 */
bool keepalive_is_enabled(const tcp_keepalive_config_t *config);

/* ========================================================================
 * Keepalive 状态查询
 * ======================================================================== */

/**
 * @brief 获取当前探测次数
 *
 * @param state Keepalive 状态
 * @return 当前探测次数
 */
uint32_t keepalive_get_probe_count(const tcp_keepalive_state_t *state);

/**
 * @brief 获取最后活跃时间
 *
 * @param state Keepalive 状态
 * @return 最后活跃时间（秒）
 */
uint32_t keepalive_get_last_active(const tcp_keepalive_state_t *state);

/**
 * @brief 检查是否超时
 *
 * @param state Keepalive 状态
 * @return true=超时，false=未超时
 */
bool keepalive_is_timeout(const tcp_keepalive_state_t *state);

/* ========================================================================
 * Keepalive 参数设置
 * ======================================================================== */

/**
 * @brief 设置空闲超时时间
 *
 * @param config Keepalive 配置
 * @param idle_time 空闲超时时间（秒）
 */
void keepalive_set_idle_time(tcp_keepalive_config_t *config,
                              uint32_t idle_time);

/**
 * @brief 设置探测间隔时间
 *
 * @param config Keepalive 配置
 * @param interval 探测间隔时间（秒）
 */
void keepalive_set_probe_interval(tcp_keepalive_config_t *config,
                                   uint32_t interval);

/**
 * @brief 设置探测次数
 *
 * @param config Keepalive 配置
 * @param count 探测次数
 */
void keepalive_set_probe_count(tcp_keepalive_config_t *config,
                                uint32_t count);

#endif /* TCP_KEEPALIVE_H */
