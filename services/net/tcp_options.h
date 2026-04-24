/**
 * @file tcp_options.h
 * @brief TCP 选项处理接口
 *
 * 本文件定义了 TCP 选项处理（MSS、窗口缩放、SACK）的接口
 */

#ifndef TCP_OPTIONS_H
#define TCP_OPTIONS_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 数据结构定义
 * ======================================================================== */

/** @brief TCP MSS 选项 */
typedef struct tcp_opt_mss_t tcp_opt_mss_t;

/** @brief TCP 窗口缩放选项 */
typedef struct tcp_opt_window_scale_t tcp_opt_window_scale_t;

/** @brief TCP SACK 选项 */
typedef struct tcp_opt_sack_t tcp_opt_sack_t;

/** @brief TCP 选项处理状态 */
typedef struct tcp_options_state_t tcp_options_state_t;

/* ========================================================================
 * TCP 选项初始化
 * ======================================================================== */

/**
 * @brief 初始化 TCP 选项状态
 *
 * @param state 选项状态
 */
void tcp_options_init(tcp_options_state_t *state);

/* ========================================================================
 * TCP 选项处理
 * ======================================================================== */

/**
 * @brief 处理 MSS 选项
 *
 * @param state 选项状态
 * @param opt 输入选项
 */
void tcp_options_process_mss(tcp_options_state_t *state,
                                const tcp_opt_mss_t *opt);

/**
 * @brief 处理窗口缩放选项
 *
 * @param state 选项状态
 * @param opt 输入选项
 */
void tcp_options_process_window_scale(tcp_options_state_t *state,
                                         const tcp_opt_window_scale_t *opt);

/**
 * @brief 处理 SACK 选项
 *
 * @param state 选项状态
 * @param opt 输入选项
 */
void tcp_options_process_sack(tcp_options_state_t *state,
                                  const tcp_opt_sack_t *opt);

/* ========================================================================
 * TCP 选项构造
 * ======================================================================== */

/**
 * @brief 构造 MSS 选项
 *
 * @param mss 最大段大小
 * @return MSS 选项结构
 */
tcp_opt_mss_t tcp_options_build_mss(uint16_t mss);

/**
 * @brief 构造窗口缩放选项
 *
 * @param scale_factor 缩放因子
 * @return 窗口缩放选项结构
 */
tcp_opt_window_scale_t tcp_options_build_window_scale(uint8_t scale_factor);

/**
 * @brief 构造 SACK 选项
 *
 * @param num_blocks SACK 块数量
 * @param left_edges SACK 左边界数组
 * @param right_edges SACK 右边界数组
 * @return SACK 选项结构
 */
tcp_opt_sack_t tcp_options_build_sack(uint8_t num_blocks,
                                       const uint32_t *left_edges,
                                       const uint32_t *right_edges);

/* ========================================================================
 * TCP 选项检查
 * ======================================================================== */

/**
 * @brief 检查选项是否存在
 *
 * @param buffer 输入缓冲区
 * @param length 缓冲区长度
 * @param kind 选项类型
 * @return true=存在，false=不存在
 */
bool tcp_options_exists(const uint8_t *buffer, uint16_t length, uint8_t kind);

/* ========================================================================
 * TCP 选项序列化
 * ======================================================================== */

/**
 * @brief 序列化 MSS 选项
 *
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @param mss MSS 值
 * @return 写入的字节数
 */
uint16_t tcp_options_serialize_mss(uint8_t *buffer, uint16_t size,
                                    uint16_t mss);

/**
 * @brief 序列化窗口缩放选项
 *
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @param scale_factor 缩放因子
 * @return 写入的字节数
 */
uint16_t tcp_options_serialize_window_scale(uint8_t *buffer, uint16_t size,
                                             uint8_t scale_factor);

/**
 * @brief 序列化 SACK 选项
 *
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @param num_blocks SACK 块数量
 * @param left_edges 左边界数组
 * @param right_edges 右边界数组
 * @return 写入的字节数
 */
uint16_t tcp_options_serialize_sack(uint8_t *buffer, uint16_t size,
                                     uint8_t num_blocks,
                                     const uint32_t *left_edges,
                                     const uint32_t *right_edges);

/* ========================================================================
 * TCP 选项查询
 * ======================================================================== */

/**
 * @brief 获取 MSS
 *
 * @param state 选项状态
 * @return MSS 值
 */
uint16_t tcp_options_get_mss(const tcp_options_state_t *state);

/**
 * @brief 获取窗口缩放因子
 *
 * @param state 选项状态
 * @return 窗口缩放因子
 */
uint8_t tcp_options_get_window_scale(const tcp_options_state_t *state);

/**
 * @brief 检查 SACK 是否允许
 *
 * @param state 选项状态
 * @return true=允许，false=不允许
 */
bool tcp_options_is_sack_permitted(const tcp_options_state_t *state);

/**
 * @brief 获取 SACK 块
 *
 * @param state 选项状态
 * @param index 块索引
 * @param left_edge 左边界（输出）
 * @param right_edge 右边界（输出）
 * @return true=成功，false=失败
 */
bool tcp_options_get_sack_block(const tcp_options_state_t *state,
                                  uint8_t index,
                                  uint32_t *left_edge,
                                  uint32_t *right_edge);

/* ========================================================================
 * TCP 选项状态检查
 * ======================================================================== */

/**
 * @brief 检查 MSS 是否已协商
 *
 * @param state 选项状态
 * @return true=已协商，false=未协商
 */
bool tcp_options_is_mss_negotiated(const tcp_options_state_t *state);

/**
 * @brief 检查窗口缩放是否已协商
 *
 * @param state 选项状态
 * @return true=已协商，false=未协商
 */
bool tcp_options_is_window_scale_negotiated(const tcp_options_state_t *state);

#endif /* TCP_OPTIONS_H */
