/**
 * @file tcp_timestamp.h
 * @brief TCP 时间戳选项接口
 *
 * 本文件定义了 TCP 时间戳选项（RFC 7323）的接口
 */

#ifndef TCP_TIMESTAMP_H
#define TCP_TIMESTAMP_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 数据结构定义
 * ======================================================================== */

/** @brief TCP 时间戳选项结构 */
typedef struct tcp_timestamp_option_t tcp_timestamp_option_t;

/** @brief TCP 时间戳状态 */
typedef struct tcp_timestamp_state_t tcp_timestamp_state_t;

/** @brief 序列号验证状态 */
typedef struct tcp_seq_verify_state_t tcp_seq_verify_state_t;

/* ========================================================================
 * 公共函数
 * ======================================================================== */

/**
 * @brief 初始化 TCP 时间戳状态
 *
 * @param state 时间戳状态
 */
void tcp_timestamp_init(tcp_timestamp_state_t *state);

/**
 * @brief 获取当前时间戳（毫秒）
 *
 * @return 时间戳（毫秒）
 */
uint32_t tcp_timestamp_get(void);

/**
 * @brief 构造 TCP 时间戳选项
 *
 * @param state 时间戳状态
 * @param opt 输出选项结构
 */
void tcp_timestamp_build(tcp_timestamp_state_t *state,
                         tcp_timestamp_option_t *opt);

/**
 * @brief 解析 TCP 时间戳选项
 *
 * @param opt 输入选项结构
 * @param state 时间戳状态
 */
void tcp_timestamp_parse(const tcp_timestamp_option_t *opt,
                         tcp_timestamp_state_t *state);

/**
 * @brief 序列号验证（防止序列号预测攻击）
 *
 * @param verify_state 验证状态
 * @param seq_num 序列号
 * @param ts_val 时间戳值
 * @return true=通过验证，false=验证失败
 */
bool tcp_seq_verify(tcp_seq_verify_state_t *verify_state,
                     uint32_t seq_num, uint32_t ts_val);

/**
 * @brief 检查时间戳有效性
 *
 * @param ts_val 时间戳值
 * @param expected_ts 预期时间戳
 * @return true=有效，false=无效
 */
bool tcp_timestamp_is_valid(uint32_t ts_val, uint32_t expected_ts);

/**
 * @brief 更新回显时间戳
 *
 * @param state 时间戳状态
 * @param ts_val 时间戳值
 */
void tcp_timestamp_update_echo(tcp_timestamp_state_t *state,
                                uint32_t ts_val);

/**
 * @brief 计算 RTT
 *
 * @param state 时间戳状态
 * @param ts_val 时间戳值
 * @param current_time 当前时间
 * @return RTT（毫秒）
 */
uint32_t tcp_timestamp_calc_rtt(tcp_timestamp_state_t *state,
                                uint32_t ts_val,
                                uint32_t current_time);

/**
 * @brief 启用时间戳
 *
 * @param state 时间戳状态
 */
void tcp_timestamp_enable(tcp_timestamp_state_t *state);

/**
 * @brief 禁用时间戳
 *
 * @param state 时间戳状态
 */
void tcp_timestamp_disable(tcp_timestamp_state_t *state);

/**
 * @brief 检查时间戳是否启用
 *
 * @param state 时间戳状态
 * @return true=已启用，false=已禁用
 */
bool tcp_timestamp_is_enabled(const tcp_timestamp_state_t *state);

/**
 * @brief 获取最后发送时间戳
 *
 * @param state 时间戳状态
 * @return 最后发送时间戳
 */
uint32_t tcp_timestamp_get_last(const tcp_timestamp_state_t *state);

/**
 * @brief 获取最近接收时间戳
 *
 * @param state 时间戳状态
 * @return 最近接收时间戳
 */
uint32_t tcp_timestamp_get_recent(const tcp_timestamp_state_t *state);

/**
 * @brief 设置时钟偏移（用于测试）
 *
 * @param state 时间戳状态
 * @param offset 时钟偏移（毫秒）
 */
void tcp_timestamp_set_clock_offset(tcp_timestamp_state_t *state,
                                     uint32_t offset);

#endif /* TCP_TIMESTAMP_H */
