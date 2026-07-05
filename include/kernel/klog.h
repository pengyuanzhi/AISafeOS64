/**
 * @file    klog.h
 * @brief   内核标准日志接口（异步、实时安全）
 * @author  AISafe64 Team
 * @date    2026-07-05
 * @version 2.0
 *
 * @details 提供分级日志接口，设计原则：
 *
 *          1. **实时安全**：klog_error/warn/info/debug 写入 per-CPU 环形缓冲
 *             （极短临界区，无 UART 阻塞）。实际 UART 输出由 klog_flush 在
 *             非实时上下文（idle 线程）中异步执行。
 *             这保证了中断处理和调度路径中调用 klog 不会引入不可控延迟。
 *
 *          2. **平台无关**：klog 通过 hal_console_putc/puts 输出，
 *             不感知 UART 基地址、寄存器布局等硬件细节。
 *             更换目标板只需修改 HAL 层（hal.c），klog 代码不变。
 *
 *          3. **级别过滤**：编译期（CONFIG_DEBUG）+ 运行期（klog_set_level）
 *             双重过滤。生产模式只保留 ERROR+WARN。
 *
 *          4. **缓冲溢出策略**：环形缓冲满时丢弃最旧消息（不阻塞调用者）。
 *
 * @note    对标 QNX slogf/slogibe：QNX 的系统日志也是异步的，
 *          写入共享内存缓冲由 slog 线程异步输出。
 * @note    MISRA-C:2012 合规
 *
 * @revision history
 * v1.0 2026-07-04 初版（同步阻塞，硬编码 UART base）
 * v2.0 2026-07-05 异步环形缓冲 + 平台无关（当前版本）
 */

#ifndef KERNEL_KLOG_H
#define KERNEL_KLOG_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/config.h>

/* ========================================================================
 * 日志级别
 * ======================================================================== */

/**
 * @brief 日志级别枚举
 *
 * @details 数值越大越详细。
 *          - ERROR：系统不可恢复错误，始终输出
 *          - WARN：可恢复异常/资源耗尽警告
 *          - INFO：启动/状态信息（仅 CONFIG_DEBUG=1 时编译）
 *          - DEBUG：开发调试（仅 CONFIG_DEBUG=1 时编译）
 */
typedef enum
{
    KLOG_LEVEL_ERROR = 0U,  /**< @brief 错误（始终输出） */
    KLOG_LEVEL_WARN  = 1U,  /**< @brief 警告 */
    KLOG_LEVEL_INFO  = 2U,  /**< @brief 信息（仅 DEBUG 编译） */
    KLOG_LEVEL_DEBUG = 3U   /**< @brief 调试（仅 DEBUG 编译） */
} klog_level_t;

/* ========================================================================
 * 初始化与配置
 * ======================================================================== */

/**
 * @brief 初始化内核日志子系统
 *
 * @details 初始化 per-CPU 环形缓冲。
 *          内部调用 hal_console_init() 初始化控制台硬件。
 *          必须在内核启动早期（调度器启动前）调用。
 *
 * @note 不接收任何硬件参数——UART 绑定由 HAL 层内部完成
 */
void klog_init(void);

/**
 * @brief 设置运行时日志级别
 *
 * @param level 允许输出的最高级别（低于此级别的消息被丢弃）
 */
void klog_set_level(klog_level_t level);

/* ========================================================================
 * 日志输出接口（写入环形缓冲，异步输出到控制台）
 *
 * @warning 禁止在中断处理（IRQ handler / timer handler）中调用以下任何函数！
 *          中断处理应只做最短路径工作，日志延迟到线程上下文输出。
 *          违反此规则会导致关中断时间不可控，破坏实时性。
 *          唯一例外：klog_panic（panic 路径直接同步输出）。
 * ======================================================================== */

/**
 * @brief 输出错误级别消息
 *
 * @details 写入环形缓冲（不阻塞）。消息前缀 "E: "。
 *          适用于：panic 诊断、硬件故障、安全违规。
 *
 * @param str 消息字符串
 *
 * @note 实时安全：写入 per-CPU 缓冲，关中断时间 < 1μs
 */
void klog_error(const char *str);

/**
 * @brief 输出警告级别消息
 *
 * @details 写入环形缓冲（不阻塞）。消息前缀 "W: "。
 *          适用于：栈溢出检测、资源耗尽、配置异常。
 *
 * @param str 消息字符串
 */
void klog_warn(const char *str);

/**
 * @brief 输出信息级别消息
 *
 * @details 写入环形缓冲（不阻塞）。消息前缀 "I: "。
 *          适用于：启动信息、子系统初始化状态。
 *
 * @param str 消息字符串
 * @note 仅 CONFIG_DEBUG=1 时编译进二进制（生产模式排除）
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
 * @brief 输出调试级别消息
 *
 * @details 写入环形缓冲（不阻塞）。消息前缀 "D: "。
 *          适用于：页表条目、调度决策、IPC 追踪。
 *
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

/* ========================================================================
 * 低级输出接口（不受级别过滤，用于组装诊断行）
 * ======================================================================== */

/**
 * @brief 输出单个字符到日志缓冲
 *
 * @param c 要输出的字符
 * @note 不受级别过滤，用于 hex/dec 辅助函数组装诊断行
 */
void klog_putc(char c);

/**
 * @brief 输出 32 位无符号十进制值
 * @param val 要输出的值
 */
void klog_dec(uint32_t val);

/**
 * @brief 输出 32 位十六进制值
 * @param val 要输出的值（输出 8 位十六进制，无 "0x" 前缀）
 */
void klog_hex32(uint32_t val);

/**
 * @brief 输出 64 位十六进制值
 * @param val 要输出的值（输出 16 位十六进制，无 "0x" 前缀）
 */
void klog_hex64(uint64_t val);

/* ========================================================================
 * 异步刷新与紧急输出
 * ======================================================================== */

/**
 * @brief 将环形缓冲中的待输出消息刷新到控制台
 *
 * @details 从当前 CPU 的环形缓冲取出消息并输出。
 *          此函数会阻塞（等待 UART TX），因此**只能在非实时上下文调用**：
 *          - idle 线程循环中周期调用
 *          - panic 路径（使用 klog_panic 替代）
 *
 * @note 禁止在中断处理或调度路径中调用此函数
 */
void klog_flush(void);

/**
 * @brief 紧急同步输出（绕过环形缓冲，直接写控制台）
 *
 * @details 用于 panic/异常处理路径，此时环形缓冲可能不可靠。
 *          直接调用 HAL 控制台输出，不经过环形缓冲。
 *
 * @param str 要输出的字符串
 */
void klog_panic(const char *str);

/* ========================================================================
 * 编译期默认级别控制
 * ======================================================================== */

#if CONFIG_DEBUG
/** @brief 默认日志级别（调试构建：DEBUG） */
#define KLOG_DEFAULT_LEVEL KLOG_LEVEL_DEBUG
#else
/** @brief 默认日志级别（生产构建：WARN，ERROR+WARN 生效） */
#define KLOG_DEFAULT_LEVEL KLOG_LEVEL_WARN
#endif

/** @brief 每个 CPU 的日志缓冲大小（字节） */
#ifndef KLOG_BUF_SIZE
#define KLOG_BUF_SIZE 2048U
#endif

#endif /* KERNEL_KLOG_H */
