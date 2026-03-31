/**
 * @file entry.c
 * @brief 微内核 C 语言入口
 * @author AISafe64 Team
 * @date 2026-03-31
 * @version 2.0
 *
 * @details 微内核 C 语言主入口函数
 *          - 初始化 UART 早期输出
 *          - 打印启动横幅
 *          - 初始化调度器
 *          - 启动调度（永不返回）
 *
 * @note 对应需求: KR-001（内核启动与初始化）
 */

/* 内部头文件 */
#include "hal.h"
#include <kernel/types.h>
#include <kernel/config.h>

/* ========== 外部全局变量（boot.S 定义） ========== */

/** @brief 设备树指针（boot.S 中保存） */
extern uint64_t __dtb_ptr;

/* ========== 内核启动横幅 ========== */

/** @brief AISafeOS64 启动横幅 */
static const char g_banner[] =
    "\n"
    "========================================\n"
    "  AISafeOS64 Microkernel v0.1\n"
    "  ARMv8-A (AArch64) Real-Time OS\n"
    "  Copyright (c) 2026 AISafe64 Team\n"
    "========================================\n";

/* ========== 内核主入口 ========== */

/**
 * @brief 微内核 C 语言主入口
 *
 * @details 由 boot.S 中的 kernel_entry 调用
 *          此时已完成：
 *          - EL 降到 EL1
 *          - 异常向量表已设置
 *          - BSS 段已清零
 *          - CPU 0 栈已设置
 *
 * @note 此函数不应返回
 */
void kernel_main(void)
{
    /* ---- 第一步：初始化 UART 早期输出 ---- */
    hal_uart_init((uint64_t)QEMU_UART0_BASE);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, g_banner);

    /* ---- 第二步：打印硬件信息 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] CPU ID: ");
    /* 简单打印 CPU ID（一位数字） */
    uint32_t cpu_id = hal_get_cpu_id();
    hal_uart_putc((uint64_t)QEMU_UART0_BASE, '0' + (char)(cpu_id & 0xFU));
    hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Exception Level: EL");
    uint32_t el = hal_get_current_el();
    hal_uart_putc((uint64_t)QEMU_UART0_BASE, '0' + (char)el);
    hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');

    /* ---- 第三步：打印内存布局 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Kernel initialized\n");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Waiting for scheduler...\n");

    /* ---- 第四步：后续阶段将在这里初始化调度器 ---- */
    /* TODO: scheduler_init() */
    /* TODO: timer_init() */
    /* TODO: scheduler_start() */

    /* 当前阶段：只输出启动信息后永停 */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Halting (scheduler not yet available)\n");

    for (;;)
    {
        __asm__ volatile("wfe");
    }
}
