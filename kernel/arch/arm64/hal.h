/**
 * @file hal.h
 * @brief ARMv8-A 硬件抽象层接口
 * @author AISafe64 Team
 * @date 2026-03-31
 * @version 2.0
 *
 * @details ARMv8-A 硬件抽象层头文件
 *          - 系统寄存器访问封装
 *          - UART 早期输出接口
 *          - CPU 信息查询
 *          - 缓存维护操作
 *
 * @note MISRA-C:2012 合规
 * @note 仅适用于 ARMv8-A AArch64 架构
 */

#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <kernel/config.h>
#include <kernel/compiler.h>

/* ========== 系统寄存器访问 ========== */

/**
 * @brief 读取系统寄存器
 * @param reg 系统寄存器名称
 * @return 寄存器值（uint64_t）
 */
#define sysreg_read(reg) ({                            \
    uint64_t _val;                                     \
    __asm__ volatile("mrs %0, " #reg : "=r"(_val));    \
    _val;                                              \
})

/**
 * @brief 写入系统寄存器
 * @param reg 系统寄存器名称
 * @param val 要写入的值
 */
#define sysreg_write(reg, val)                            \
    do {                                                  \
        __asm__ volatile("msr " #reg ", %0" :: "r"(val)); \
        __asm__ volatile("isb");                          \
    } while (0)

/* ========== 异常级别查询 ========== */

/**
 * @brief 获取当前异常级别
 * @return 异常级别（0-3）
 *
 * @details EL0=用户态, EL1=内核态, EL2=虚拟机监控, EL3=安全监控
 */
static inline uint32_t hal_get_current_el(void)
{
    uint64_t currentel = sysreg_read(CurrentEL);
    return (uint32_t)((currentel >> 2U) & 0x3U);
}

/**
 * @brief 获取当前 CPU ID
 * @return CPU ID（0 ~ CONFIG_MAX_CPUS-1）
 *
 * @details 读取 MPIDR_EL1 寄存器的 Aff0 字段
 */
static inline uint32_t hal_get_cpu_id(void)
{
    uint64_t mpidr = sysreg_read(mpidr_el1);
    return (uint32_t)(mpidr & 0xFFU);
}

/* ========== 中断控制 ========== */

/**
 * @brief 禁用所有中断（IRQ/FIQ/SError/Debug）
 */
static inline void hal_irq_disable_all(void)
{
    __asm__ volatile("msr daifset, #0xF" ::: "memory");
}

/**
 * @brief 启用 IRQ 中断
 */
static inline void hal_irq_enable(void)
{
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

/**
 * @brief 禁用 IRQ 中断
 */
static inline void hal_irq_disable(void)
{
    __asm__ volatile("msr daifset, #2" ::: "memory");
}

/**
 * @brief 获取当前中断状态
 * @return 非零表示中断已禁用
 */
static inline uint32_t hal_irq_saved_state(void)
{
    uint64_t daif = sysreg_read(DAIF);
    return (uint32_t)((daif >> 9U) & 0xFU);
}

/**
 * @brief 恢复中断状态
 * @param state 之前保存的中断状态
 */
static inline void hal_irq_restore(uint32_t state)
{
    uint64_t daif = sysreg_read(DAIF);
    daif &= ~(0xFULL << 9U);
    daif |= ((uint64_t)state << 9U);
    sysreg_write(daif, daif);
}

/* ========== 栈对齐检查 ========== */

/**
 * @brief 启用栈对齐检查
 */
static inline void hal_enable_stack_alignment_check(void)
{
    uint64_t sctlr = sysreg_read(sctlr_el1);
    sctlr |= (1ULL << 3U);  /* SA 位 */
    sysreg_write(sctlr_el1, sctlr);
}

/* ========== UART 早期输出接口 ========== */

/**
 * @brief 初始化 UART（QEMU PL011）
 * @param base UART 基地址
 */
void hal_uart_init(uint64_t base);

/**
 * @brief UART 发送单个字符
 * @param base UART 基地址
 * @param ch 要发送的字符
 */
void hal_uart_putc(uint64_t base, char ch);

/**
 * @brief UART 发送字符串
 * @param base UART 基地址
 * @param str 要发送的字符串（以 NULL 结尾）
 */
void hal_uart_puts(uint64_t base, const char *str);

/* ========== 缓存维护操作 ========== */

/**
 * @brief 清理数据缓存到内存（按缓存行）
 * @param start 起始虚拟地址
 * @param size 字节数
 */
void hal_dcache_clean(uint64_t start, uint64_t size);

/**
 * @brief 使数据缓存无效（按缓存行）
 * @param start 起始虚拟地址
 * @param size 字节数
 */
void hal_dcache_invalidate(uint64_t start, uint64_t size);

/**
 * @brief 清理并使数据缓存无效
 * @param start 起始虚拟地址
 * @param size 字节数
 */
void hal_dcache_clean_and_invalidate(uint64_t start, uint64_t size);

/**
 * @brief 使整个 TLB 无效
 */
static inline void hal_tlb_invalidate_all(void)
{
    __asm__ volatile("tlbi vmalle1is" ::: "memory");
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb");
}

/* ========== QEMU UART 基地址 ========== */

/** @brief QEMU virt 平台 PL011 UART0 基地址 */
#define QEMU_UART0_BASE 0x09000000UL

#endif /* HAL_H */
