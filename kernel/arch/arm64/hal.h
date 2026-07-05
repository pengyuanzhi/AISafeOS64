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
 * @param reg 系统寄存器名称（如 CurrentEL）
 * @return 寄存器值（uint64_t）
 *
 * @note 使用 GCC 语句表达式，将寄存器名字符串化为汇编操作数
 * @note 这在标准 C 中无法实现，是 ARM64 内核必需的编译器扩展
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
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
static inline uint32_t hal_get_current_el(void)
{
    uint64_t currentel = sysreg_read(CurrentEL);
    return (uint32_t)((currentel >> 2U) & 0x3U);
}
#pragma GCC diagnostic pop

/**
 * @brief 获取当前 CPU ID
 * @return CPU ID（0 ~ CONFIG_MAX_CPUS-1）
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
static inline uint32_t hal_get_cpu_id(void)
{
    uint64_t mpidr = sysreg_read(mpidr_el1);
    return (uint32_t)(mpidr & 0xFFU);
}
#pragma GCC diagnostic pop

/* ========== 中断控制 ========== */

/**
 * @brief 禁用所有中断（IRQ/FIQ/SError/Debug）
 */
static inline void hal_local_irq_disable_all(void)
{
    __asm__ volatile("msr daifset, #0xF" ::: "memory");
}

/**
 * @brief 启用 IRQ 中断
 */
static inline void hal_local_irq_enable(void)
{
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

/**
 * @brief 禁用 IRQ 中断
 */
static inline void hal_local_irq_disable(void)
{
    __asm__ volatile("msr daifset, #2" ::: "memory");
}

/**
 * @brief 获取当前中断状态
 * @return 非零表示中断已禁用
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
static inline uint32_t hal_local_irq_saved_state(void)
{
    uint64_t daif = sysreg_read(DAIF);
    return (uint32_t)((daif >> 9U) & 0xFU);
}
#pragma GCC diagnostic pop

/**
 * @brief 恢复中断状态
 * @param state 之前保存的中断状态
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
static inline void hal_local_irq_restore(uint32_t state)
{
    uint64_t daif = sysreg_read(DAIF);
    daif &= ~(0xFULL << 9U);
    daif |= ((uint64_t)state << 9U);
    sysreg_write(daif, daif);
}
#pragma GCC diagnostic pop

/* ========== 栈对齐检查 ========== */

/**
 * @brief 启用栈对齐检查
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
static inline void hal_enable_stack_alignment_check(void)
{
    uint64_t sctlr = sysreg_read(sctlr_el1);
    sctlr |= (1ULL << 3U);  /* SA 位 */
    sysreg_write(sctlr_el1, sctlr);
}
#pragma GCC diagnostic pop

/* ========== UART 底层接口（供 HAL 内部使用，不暴露 base 给内核核心） ========== */

/**
 * @brief 初始化指定 UART 端口
 * @param base UART 基地址（由 HAL 层传入，内核核心不接触）
 */
void hal_uart_init(uint64_t base);

/**
 * @brief 向指定 UART 端口发送单个字符
 * @param base UART 基地址
 * @param ch  要发送的字符
 */
void hal_uart_putc(uint64_t base, char ch);

/**
 * @brief 向指定 UART 端口发送字符串
 * @param base UART 基地址
 * @param str  要发送的字符串
 */
void hal_uart_puts(uint64_t base, const char *str);

/* ========== 控制台接口（内核核心使用，不感知硬件细节） ========== */

/**
 * @brief 初始化系统控制台
 *
 * @details HAL 层内部绑定具体的 UART 端口（base 地址、寄存器布局），
 *          内核核心代码不感知控制台硬件细节。
 *          更换目标板只需修改 hal.c 中的实现。
 */
void hal_console_init(void);

/**
 * @brief 向控制台输出单个字符
 * @param ch 要输出的字符
 */
void hal_console_putc(char ch);

/**
 * @brief 向控制台输出字符串
 * @param str 以 NULL 结尾的字符串
 */
void hal_console_puts(const char *str);

/**
 * @brief 使能 UART 接收中断
 * @param base UART 基地址
 */
void hal_uart_enable_rx_irq(uint64_t base);

/**
 * @brief UART 接收单个字符（非阻塞）
 * @param base UART 基地址
 * @param ch   输出字符指针
 * @return 非0表示成功读取，0表示无数据
 */
int hal_uart_getc(uint64_t base, char *ch);

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
 * @brief 数据同步屏障 (Inner Shareable)
 * @details 确保所有之前的缓存操作和数据访问在所有 inner shareable 域的观察者可见
 */
void hal_dsb_ish(void);

/**
 * @brief 系统同步屏障 (Full System)
 * @details 确保所有之前的缓存操作和数据访问在整个系统可见
 */
void hal_dsb_sy(void);

/**
 * @brief 使整个 TLB 无效
 */
static inline void hal_tlb_invalidate_all(void)
{
    __asm__ volatile("tlbi vmalle1is" ::: "memory");
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb");
}

/* ========== 定时器接口 ========== */

/**
 * @brief 读取物理定时器计数值
 * @return CNTPCT_EL0 当前值
 */
uint64_t hal_timer_get_count(void);

/**
 * @brief 读取物理定时器频率
 * @return CNTFRQ_EL0 频率值（Hz）
 */
uint64_t hal_timer_get_freq(void);

/**
 * @brief 读取物理定时器控制寄存器
 * @return CNTP_CTL_EL0 当前值
 */
uint64_t hal_timer_get_control(void);

/**
 * @brief 设置物理定时器比较值
 * @param val 要写入 CNTP_CVAL_EL0 的值
 */
void hal_timer_set_compare(uint64_t val);

/**
 * @brief 设置物理定时器控制寄存器
 * @param val 要写入 CNTP_CTL_EL0 的值
 */
void hal_timer_set_control(uint64_t val);

/* ========== 内存屏障接口 ========== */

/**
 * @brief 数据内存屏障 (Inner Shareable)
 * @details 确保所有之前的内存访问在所有 inner shareable 域的观察者可见后才执行后续访问
 */
void hal_dmb_ish(void);

/**
 * @brief 数据内存屏障 (Inner Shareable, Store)
 * @details 仅确保之前的 store 操作在后续操作之前可见
 */
void hal_dmb_ishst(void);

/**
 * @brief 数据内存屏障 (Inner Shareable, Load)
 * @details 仅确保之前的 load 操作在后续操作之前可见
 */
void hal_dmb_ishld(void);

/* ========== 页表寄存器接口 ========== */

/**
 * @brief 读取 TTBR0_EL1 寄存器
 * @return TTBR0_EL1 当前值
 */
uint64_t hal_read_ttbr0(void);

/**
 * @brief 读取 TTBR1_EL1 寄存器
 * @return TTBR1_EL1 当前值
 */
uint64_t hal_read_ttbr1(void);

/**
 * @brief 写入 TTBR0_EL1 寄存器
 * @param val 要写入的值
 */
void hal_write_ttbr0(uint64_t val);

/**
 * @brief 写入 TTBR1_EL1 寄存器
 * @param val 要写入的值
 */
void hal_write_ttbr1(uint64_t val);

/**
 * @brief 刷新指定 ASID 的 TLB
 * @param asid 地址空间标识
 */
void hal_tlb_invalidate_asid(uint64_t asid);

/* ========== 低功耗等待接口 ========== */

/**
 * @brief 等待事件（低功耗 WFE）
 * @details 暂停 CPU 执行直到事件信号到达或中断发生
 */
void hal_wfe(void);

/**
 * @brief 等待中断（低功耗 WFI）
 * @details 暂停 CPU 执行直到中断发生
 */
void hal_wfi(void);

/**
 * @brief 发送事件
 * @details 向所有核心广播事件，用于唤醒处于 WFE 的等待者
 */
void hal_sev(void);

/* ========== QEMU UART 基地址 ========== */

/** @brief QEMU virt 平台 PL011 UART0 基地址（TTBR1 高地址线性映射）
 *
 * @details 物理地址 0x09000000 经线性映射偏移 KERNEL_VA_OFFSET
 *          (0xFFFF000000000000) 映射到高地址 0xFFFF000009000000。
 */
#define QEMU_UART0_BASE 0xFFFF000009000000UL

/* ========== 定时器接口 ========== */

/**
 * @brief 读取物理定时器计数值
 * @return CNTPCT_EL0 当前值
 */
uint64_t hal_timer_get_count(void);

/**
 * @brief 读取物理定时器频率
 * @return CNTFRQ_EL0 频率值（Hz）
 */
uint64_t hal_timer_get_freq(void);

/**
 * @brief 读取物理定时器控制寄存器
 * @return CNTP_CTL_EL0 当前值
 */
uint64_t hal_timer_get_control(void);

/**
 * @brief 设置物理定时器比较值
 * @param val 要写入 CNTP_CVAL_EL0 的值
 */
void hal_timer_set_compare(uint64_t val);

/**
 * @brief 设置物理定时器控制寄存器
 * @param val 要写入 CNTP_CTL_EL0 的值
 */
void hal_timer_set_control(uint64_t val);

/* ========== 内存屏障接口 ========== */

/**
 * @brief 数据内存屏障 (Inner Shareable)
 * @details 确保所有之前的内存访问在所有 inner shareable 域可见
 */
void hal_dmb_ish(void);

/**
 * @brief 数据内存屏障 (Inner Shareable, Store)
 * @details 仅确保之前的 store 操作在后续操作之前可见
 */
void hal_dmb_ishst(void);

/**
 * @brief 数据内存屏障 (Inner Shareable, Load)
 * @details 仅确保之前的 load 操作在后续操作之前可见
 */
void hal_dmb_ishld(void);

/* ========== 页表寄存器接口 ========== */

/**
 * @brief 读取 TTBR0_EL1 寄存器
 * @return TTBR0_EL1 当前值
 */
uint64_t hal_read_ttbr0(void);

/**
 * @brief 读取 TTBR1_EL1 寄存器
 * @return TTBR1_EL1 当前值
 */
uint64_t hal_read_ttbr1(void);

/**
 * @brief 写入 TTBR0_EL1 寄存器
 * @param val 要写入的值
 */
void hal_write_ttbr0(uint64_t val);

/**
 * @brief 写入 TTBR1_EL1 寄存器
 * @param val 要写入的值
 */
void hal_write_ttbr1(uint64_t val);

/**
 * @brief 刷新指定 ASID 的 TLB
 * @param asid 地址空间标识
 */
void hal_tlb_invalidate_asid(uint64_t asid);

/* ========== 低功耗等待接口 ========== */

/**
 * @brief 等待事件（低功耗 WFE）
 * @details 暂停 CPU 执行直到事件信号到达或中断发生
 */
void hal_wfe(void);

/* ========== 异常返回寄存器接口 ========== */

/**
 * @brief 读取 ELR_EL1（异常链接寄存器）
 * @details 保存异常返回地址，eret 时恢复到 PC
 * @return ELR_EL1 当前值
 */
static inline uint64_t hal_read_elr(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, elr_el1" : "=r"(val));
    return val;
}

/**
 * @brief 写入 ELR_EL1（异常链接寄存器）
 * @param val 要写入的值
 */
static inline void hal_write_elr(uint64_t val)
{
    __asm__ volatile("msr elr_el1, %0" :: "r"(val));
}

/**
 * @brief 读取 SPSR_EL1（保存的程序状态寄存器）
 * @details 保存异常发生时的 PSTATE，eret 时恢复
 * @return SPSR_EL1 当前值
 */
static inline uint64_t hal_read_spsr(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, spsr_el1" : "=r"(val));
    return val;
}

/**
 * @brief 写入 SPSR_EL1（保存的程序状态寄存器）
 * @param val 要写入的值
 */
static inline void hal_write_spsr(uint64_t val)
{
    __asm__ volatile("msr spsr_el1, %0" :: "r"(val));
}

/**
 * @brief 指令同步屏障 (ISB)
 * @details 刷新流水线，确保之前所有上下文改变操作完成
 */
static inline void hal_isb(void)
{
    __asm__ volatile("isb");
}

#endif /* HAL_H */
