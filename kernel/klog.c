/**
 * @file    klog.c
 * @brief   内核标准日志接口实现
 * @author  AISafe64 Team
 * @date    2026-07-04
 * @version 1.0
 *
 * @details 提供 klog_error/klog_warn/klog_info/klog_debug 分级日志接口。
 *          内部固定使用 QEMU UART0 基地址（不暴露给调用者）。
 *          运行时级别过滤由 current_level 控制：低于该级别的消息被丢弃。
 *
 *          底层复用 HAL 的 hal_uart_* 接口（kernel/arch/arm64/hal.c）。
 *
 * @note MISRA-C:2012 合规
 */

#include <kernel/klog.h>
#include <kernel/config.h>
#include "hal.h"

/* ========================================================================
 * 内部状态（不暴露给调用者）
 * ======================================================================== */

/**
 * @brief 当前日志级别（运行时可调）
 *
 * @details 级别数值越大表示越详细。低于此级别的消息被丢弃。
 *          初始化前为 KLOG_DEFAULT_LEVEL。
 */
static klog_level_t s_current_level = KLOG_DEFAULT_LEVEL;

/* ========================================================================
 * 公共接口实现
 * ======================================================================== */

void klog_init(void)
{
    hal_uart_init((uint64_t)QEMU_UART0_BASE);
    s_current_level = KLOG_DEFAULT_LEVEL;
}

void klog_set_level(klog_level_t level)
{
    s_current_level = level;
}

void klog_putc(char c)
{
    hal_uart_putc((uint64_t)QEMU_UART0_BASE, c);
}

void klog_error(const char *str)
{
    if (((uint32_t)s_current_level) >= ((uint32_t)KLOG_LEVEL_ERROR))
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "E: ");
        if (str != NULL)
        {
            hal_uart_puts((uint64_t)QEMU_UART0_BASE, str);
        }
    }
}

void klog_warn(const char *str)
{
    if (((uint32_t)s_current_level) >= ((uint32_t)KLOG_LEVEL_WARN))
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "W: ");
        if (str != NULL)
        {
            hal_uart_puts((uint64_t)QEMU_UART0_BASE, str);
        }
    }
}

#if CONFIG_DEBUG
void klog_info(const char *str)
{
    if (((uint32_t)s_current_level) >= ((uint32_t)KLOG_LEVEL_INFO))
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "I: ");
        if (str != NULL)
        {
            hal_uart_puts((uint64_t)QEMU_UART0_BASE, str);
        }
    }
}

void klog_debug(const char *str)
{
    if (((uint32_t)s_current_level) >= ((uint32_t)KLOG_LEVEL_DEBUG))
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "D: ");
        if (str != NULL)
        {
            hal_uart_puts((uint64_t)QEMU_UART0_BASE, str);
        }
    }
}
#endif /* CONFIG_DEBUG */

void klog_hex32(uint32_t val)
{
    static const char s_hex[] = "0123456789ABCDEF";
    int32_t i;

    for (i = 28; i >= 0; i -= 4)
    {
        uint8_t nibble = (uint8_t)((val >> (uint32_t)i) & 0xFU);
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, s_hex[nibble]);
    }
}

void klog_hex64(uint64_t val)
{
    static const char s_hex[] = "0123456789ABCDEF";
    int32_t i;

    for (i = 60; i >= 0; i -= 4)
    {
        uint8_t nibble = (uint8_t)((val >> (uint32_t)i) & 0xFU);
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, s_hex[nibble]);
    }
}
