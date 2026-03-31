/**
 * @file    printk.h
 * @brief   内核打印与日志系统
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 微内核 RTOS 内核打印与日志级别控制
 *          - printk()：标准内核格式化输出（类似 printf）
 *          - early_printk()：早期启动阶段 UART 直接输出
 *          - 日志级别控制：ERROR / WARN / INFO / DEBUG 四级
 *          - pr_err / pr_warn / pr_info / pr_debug 便捷宏
 *          - hex_dump()：内存区域十六进制转储
 *          - panic()：内核致命错误处理（格式化输出后永久停机）
 *
 * @note    MISRA-C:2012 合规
 * @warning printk 系列函数可在中断上下文中调用，但不可在早期启动阶段
 *          的页表初始化之前使用格式化功能（请使用 early_printk）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_PRINTK_H
#define KERNEL_PRINTK_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * 日志级别定义
 * ============================================================================ */

/**
 * @def LOG_LEVEL_ERROR
 * @brief 错误日志级别（最高优先级，数值 0）
 *
 * @details 仅输出错误信息，适用于生产环境
 *          表示系统检测到不可恢复或需要关注的错误
 */
#define LOG_LEVEL_ERROR    0U

/**
 * @def LOG_LEVEL_WARN
 * @brief 警告日志级别（数值 1）
 *
 * @details 输出错误和警告信息
 *          表示系统检测到潜在问题但可继续运行
 */
#define LOG_LEVEL_WARN     1U

/**
 * @def LOG_LEVEL_INFO
 * @brief 信息日志级别（数值 2）
 *
 * @details 输出错误、警告和一般信息
 *          适用于日常运行监控
 */
#define LOG_LEVEL_INFO     2U

/**
 * @def LOG_LEVEL_DEBUG
 * @brief 调试日志级别（最低优先级，数值 3）
 *
 * @details 输出所有级别日志，包括详细调试信息
 *          仅在开发和调试阶段使用
 */
#define LOG_LEVEL_DEBUG    3U

/* ============================================================================
 * 内核打印函数声明
 * ============================================================================ */

/**
 * @brief 内核格式化打印
 *
 * @details 类似标准 printf 的内核格式化输出函数
 *          输出目标取决于底层实现（UART、帧缓冲等）
 *          支持标准格式说明符：%%d, %%u, %%x, %%s, %%p 等
 *          可在中断上下文和调度器启动后使用
 *
 * @param fmt 格式化字符串（不得为 NULL）
 * @param ... 格式化参数
 *
 * @return 实际输出的字符数
 *
 * @note    此函数是线程安全的（内部使用自旋锁保护）
 * @warning 不得在早期启动阶段（UART 驱动初始化之前）调用
 *
 * @par 示例
 * @code
 * printk("任务 %u 创建成功，优先级 = %u\n", task_id, priority);
 * @endcode
 */
int printk(const char *fmt, ...);

/**
 * @brief 早期启动阶段打印
 *
 * @details 在内核启动早期阶段直接通过 UART 输出字符
 *          不依赖任何驱动框架、内存分配或中断子系统
 *          仅支持简单的字符串输出，不支持格式化参数
 *
 * @param msg 要输出的字符串（不得为 NULL，必须以 '\0' 结尾）
 *
 * @note    此函数可在系统启动的最早阶段调用
 * @note    实现应尽可能简单，通常直接写 UART 寄存器
 *
 * @par 示例
 * @code
 * early_printk("AISafeOS64: 内核启动中...\n");
 * @endcode
 */
void early_printk(const char *msg);

/* ============================================================================
 * 日志级别控制函数声明
 * ============================================================================ */

/**
 * @brief 设置系统日志级别
 *
 * @details 设置全局日志输出阈值
 *          仅日志级别数值小于等于当前阈值的消息才会输出
 *          默认日志级别由编译配置 CONFIG_LOG_LEVEL 决定
 *
 * @param level 目标日志级别（LOG_LEVEL_ERROR ~ LOG_LEVEL_DEBUG）
 *
 * @note 在生产环境中建议设置为 LOG_LEVEL_ERROR 或 LOG_LEVEL_WARN
 */
void log_level_set(uint32_t level);

/**
 * @brief 获取当前系统日志级别
 *
 * @details 返回当前全局日志输出阈值
 *
 * @return 当前日志级别（LOG_LEVEL_ERROR ~ LOG_LEVEL_DEBUG）
 */
uint32_t log_level_get(void);

/* ============================================================================
 * 带级别前缀的打印宏
 * ============================================================================ */

/**
 * @brief 错误级别打印宏
 *
 * @details 输出前缀 "[ERROR] " 后跟格式化内容
 *          当日志级别 >= LOG_LEVEL_ERROR 时输出（始终输出）
 *
 * @param fmt 格式化字符串
 * @param ... 格式化参数
 */
#define pr_err(fmt, ...) \
    printk("[ERROR] " fmt, ##__VA_ARGS__)

/**
 * @brief 警告级别打印宏
 *
 * @details 输出前缀 "[WARN]  " 后跟格式化内容
 *          当日志级别 >= LOG_LEVEL_WARN 时输出
 *
 * @param fmt 格式化字符串
 * @param ... 格式化参数
 */
#define pr_warn(fmt, ...) \
    printk("[WARN]  " fmt, ##__VA_ARGS__)

/**
 * @brief 信息级别打印宏
 *
 * @details 输出前缀 "[INFO]  " 后跟格式化内容
 *          当日志级别 >= LOG_LEVEL_INFO 时输出
 *
 * @param fmt 格式化字符串
 * @param ... 格式化参数
 */
#define pr_info(fmt, ...) \
    printk("[INFO]  " fmt, ##__VA_ARGS__)

/**
 * @brief 调试级别打印宏
 *
 * @details 输出前缀 "[DEBUG] " 后跟格式化内容
 *          当日志级别 >= LOG_LEVEL_DEBUG 时输出
 *          默认在编译配置中可能被禁用
 *
 * @param fmt 格式化字符串
 * @param ... 格式化参数
 */
#define pr_debug(fmt, ...) \
    printk("[DEBUG] " fmt, ##__VA_ARGS__)

/* ============================================================================
 * 辅助函数声明
 * ============================================================================ */

/**
 * @brief 内存区域十六进制转储
 *
 * @details 将指定内存区域以经典 hex dump 格式输出
 *          每行显示 16 字节，包含地址、十六进制值和 ASCII 字符
 *          用于调试内存内容、协议数据包等
 *
 * @param addr   起始内存地址（不得为 NULL）
 * @param size   要转储的字节数
 * @param prefix 每行输出的前缀字符串（可为 NULL）
 *
 * @note    此函数会在 printk 输出中产生多行内容
 * @warning 仅用于调试目的，不要在中断上下文中转储大量数据
 *
 * @par 输出格式示例
 * @code
 * [prefix] 0xFFFF000000100000: 48 65 6C 6C 6F 20 57 6F 72 6C 64 00 FF FF FF FF  Hello World.....
 * [prefix] 0xFFFF000000100010: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
 * @endcode
 */
void hex_dump(const void *addr, size_t size, const char *prefix);

/**
 * @brief 内核致命错误处理
 *
 * @details 格式化输出错误信息后使内核进入永久停机状态
 *          调用后内核不会返回，将执行以下操作：
 *          1. 输出 "[PANIC] " 前缀和格式化错误信息
 *          2. 输出当前 CPU 寄存器快照（如果架构支持）
 *          3. 输出调用栈回溯（如果调试信息可用）
 *          4. 禁用所有中断并进入无限循环
 *
 * @param fmt 格式化字符串，描述错误原因（不得为 NULL）
 * @param ... 格式化参数
 *
 * @note    此函数不会返回（等效于 _Noreturn）
 * @warning 仅在不可恢复的致命错误时调用
 *
 * @par 示例
 * @code
 * if (page_table == NULL)
 * {
 *     panic("页表分配失败: 内存不足，系统无法继续运行\n");
 * }
 * @endcode
 */
void panic(const char *fmt, ...);

#endif /* KERNEL_PRINTK_H */
