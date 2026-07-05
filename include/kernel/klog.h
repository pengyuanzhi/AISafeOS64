/**
 * @file    klog.h
 * @brief   内核标准日志接口
 * @author  AISafe64 Team
 * @date    2026-07-04
 * @version 1.0
 *
 * @details 提供分级日志输出接口（klog_error/klog_warn/klog_info/klog_debug）。
 *          - 内部固定使用 QEMU UART0 基地址，不暴露给调用者
 *          - 运行时级别过滤（klog_set_level）
 *          - 编译期默认级别由 CONFIG_DEBUG 决定
 *
 * @note MISRA-C:2012 合规
 */

#ifndef KERNEL_KLOG_H
#define KERNEL_KLOG_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/config.h>

/**
 * @brief 日志级别
 */
typedef enum {
    KLOG_LEVEL_ERROR = 0U,  /**< @brief 错误（始终输出） */
    KLOG_LEVEL_WARN  = 1U,  /**< @brief 警告 */
    KLOG_LEVEL_INFO  = 2U,  /**< @brief 信息 */
    KLOG_LEVEL_DEBUG = 3U   /**< @brief 调试 */
} klog_level_t;

/**
 * @brief 初始化 klog 子系统（初始化 UART 并设置默认级别）
 */
void klog_init(void);

/**
 * @brief 设置运行时日志级别（低于此级别的消息被丢弃）
 * @param level 允许输出的最高级别
 */
void klog_set_level(klog_level_t level);

/* ========== 级别过滤的字符串输出（最常用） ========== */

/**
 * @brief 输出错误级别消息（前缀 "E: "）
 * @param str 消息字符串
 */
void klog_error(const char *str);

/**
 * @brief 输出警告级别消息（前缀 "W: "）
 * @param str 消息字符串
 */
void klog_warn(const char *str);

/**
 * @brief 输出信息级别消息（前缀 "I: "）
 * @param str 消息字符串
 * @note 仅 CONFIG_DEBUG=1 时编译进二进制
 */
#if CONFIG_DEBUG
void klog_info(const char *str);
#else
static inline void klog_info(const char *str)
{
    (void)str;
}
#endif

/**
 * @brief 输出调试级别消息（前缀 "D: "）
 * @param str 消息字符串
 * @note 仅 CONFIG_DEBUG=1 时编译进二进制
 */
#if CONFIG_DEBUG
void klog_debug(const char *str);
#else
static inline void klog_debug(const char *str)
{
    (void)str;
}
#endif

/* ========== 单字符输出 ========== */

/**
 * @brief 输出单个字符（不受级别过滤，用于组装诊断行）
 * @param c 要输出的字符
 */
void klog_putc(char c);

/* ========== 十六进制输出辅助 ========== */

/**
 * @brief 输出 32 位十六进制值（用于诊断）
 * @param val 要输出的值
 */
void klog_hex32(uint32_t val);

/**
 * @brief 输出 64 位十六进制值（用于诊断）
 * @param val 要输出的值
 */
void klog_hex64(uint64_t val);

/* ========== 编译期默认级别控制 ========== */

#if CONFIG_DEBUG
/**
 * @brief 默认日志级别（调试构建：DEBUG）
 */
#define KLOG_DEFAULT_LEVEL KLOG_LEVEL_DEBUG
#else
/**
 * @brief 默认日志级别（生产构建：WARN，ERROR+WARN 生效）
 */
#define KLOG_DEFAULT_LEVEL KLOG_LEVEL_WARN
#endif

#endif /* KERNEL_KLOG_H */
