/**
 * @file entry.c
 * @brief 微内核 C 语言入口与异常处理
 * @author AISafe64 Team
 * @date 2026-03-31
 * @version 3.0
 *
 * @details 微内核 C 语言主入口函数与 ARM64 异常处理
 *          - 初始化 UART 早期输出
 *          - 打印启动横幅与硬件信息
 *          - 初始化 GIC 中断控制器
 *          - 初始化定时器与调度器
 *          - 异常处理函数（同步异常、IRQ、SError）
 *          - 启动调度（永不返回）
 *
 * @note 对应需求: KR-001（内核启动与初始化）, KR-003（异常处理）
 */

/* 内部头文件 */
#include "hal.h"
#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/timer.h>
#include <kernel/gic.h>
#include "../../sched/scheduler.h"

/* ========== 外部全局变量（boot.S 定义） ========== */

/** @brief 设备树指针（boot.S 中保存） */
extern uint64_t __dtb_ptr;

/** @brief BSS 段起始地址（链接脚本定义） */
extern char __bss_start[];

/** @brief BSS 段结束地址（链接脚本定义） */
extern char __bss_end[];

/** @brief 栈区域起始地址（链接脚本定义） */
extern char __stacks_start[];

/** @brief 栈区域结束地址（链接脚本定义） */
extern char __stacks_end[];

/* ========== 辅助函数声明 ========== */

/**
 * @brief 十六进制输出辅助函数
 * @param base UART 基地址
 * @param value 要输出的无符号整数
 */
static void uart_print_hex(uint64_t base, uint64_t value);

/**
 * @brief 无符号十进制输出辅助函数
 * @param base UART 基地址
 * @param value 要输出的无符号整数
 */
static void uart_print_uint(uint64_t base, uint64_t value);

/* ========== 内核启动横幅 ========== */

/** @brief AISafeOS64 启动横幅 */
static const char g_banner[] =
    "\n"
    "========================================\n"
    "  AISafeOS64 Microkernel v0.2\n"
    "  ARMv8-A (AArch64) Real-Time OS\n"
    "  Copyright (c) 2026 AISafe64 Team\n"
    "========================================\n";

/* ========== ESR 异常类别描述 ========== */

/**
 * @brief 根据 EC（异常类别）获取描述字符串
 *
 * @param ec ESR_EL1 中的 EC 字段（bits [31:26]）
 * @return 异常类别描述字符串
 */
static const char *get_ec_desc(uint32_t ec)
{
    const char *desc;

    switch (ec)
    {
        case 0x00U: desc = "Unknown"; break;
        case 0x01U: desc = "Trapped WFI/WFE"; break;
        case 0x03U: desc = "Trapped MCR/MRC"; break;
        case 0x04U: desc = "Trapped MCRR/MRRC"; break;
        case 0x05U: desc = "Trapped MCR/MRC2"; break;
        case 0x06U: desc = "Trapped LDC/STC"; break;
        case 0x07U: desc = "SVE/SIMD access"; break;
        case 0x08U: desc = "Trapped VMRS"; break;
        case 0x0EU: desc = "Illegal Execution"; break;
        case 0x11U: desc = "SVC32"; break;
        case 0x15U: desc = "SVC64"; break;
        case 0x20U: desc = "Instruction Abort (LL)"; break;
        case 0x21U: desc = "Instruction Abort (EL)"; break;
        case 0x22U: desc = "PC Alignment"; break;
        case 0x24U: desc = "Data Abort (LL)"; break;
        case 0x25U: desc = "Data Abort (EL)"; break;
        case 0x26U: desc = "SP Alignment"; break;
        case 0x2CU: desc = "Trapped FP"; break;
        case 0x2FU: desc = "SError"; break;
        case 0x30U: desc = "Breakpoint (LL)"; break;
        case 0x31U: desc = "Breakpoint (EL)"; break;
        case 0x32U: desc = "Software Step"; break;
        case 0x34U: desc = "Watchpoint (LL)"; break;
        case 0x35U: desc = "Watchpoint (EL)"; break;
        case 0x3CU: desc = "BRK"; break;
        default:    desc = "Reserved"; break;
    }

    return desc;
}

/* ========== QEMU virt 平台定时器中断号 ========== */

/** @brief ARM 通用定时器物理定时器 PPI 中断号（30 号 PPI） */
#define QEMU_TIMER_IRQ     30U

/** @brief GIC 伪中断号（无挂起中断时 IAR 返回值） */
#define GIC_SPURIOUS_IRQ   1023U

/* ========================================================================
 * 异常处理函数实现
 * ======================================================================== */

/**
 * @brief EL1 同步异常处理函数
 *
 * @details 读取 ESR_EL1、FAR_EL1、ELR_EL1、SPSR_EL1，
 *          打印异常诊断信息。对于 SVC 系统调用由 exception.S
 *          单独分发处理，此处仅处理非 SVC 的同步异常。
 *
 *          对于不可恢复的同步异常（如 Data Abort），
 *          跳过出错指令（ELR += 4）以防止无限循环。
 *
 * @param esr      ESR_EL1 寄存器值（异常症状寄存器）
 * @param far      FAR_EL1 寄存器值（错误地址寄存器）
 * @param elr_ptr  指向栈上保存的 ELR_EL1 的指针（用于修改返回地址）
 * @param spsr     SPSR_EL1 寄存器值（保存的程序状态）
 *
 * @note 对应需求: KR-003（异常向量表管理）
 */
void exception_sync_handler(uint64_t esr, uint64_t far,
                            uint64_t *elr_ptr, uint64_t spsr)
{
    uint32_t ec;
    uint32_t iss;
    const char *desc;
    uint64_t elr;

    elr = *elr_ptr;

    /* 提取 EC（异常类别）和 ISS（指令特定症状） */
    ec = (uint32_t)((esr >> 26U) & 0x3FU);
    iss = (uint32_t)(esr & 0x01FFFFFFU);

    /* 获取异常类别描述 */
    desc = get_ec_desc(ec);

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n[exception] === Sync Exception ===\n");

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[exception] EC=0x");
    uart_print_hex((uint64_t)QEMU_UART0_BASE, (uint64_t)ec);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, " (");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, desc);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, ")\n");

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[exception] ESR=0x");
    uart_print_hex((uint64_t)QEMU_UART0_BASE, esr);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, " ISS=0x");
    uart_print_hex((uint64_t)QEMU_UART0_BASE, (uint64_t)iss);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n");

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[exception] FAR=0x");
    uart_print_hex((uint64_t)QEMU_UART0_BASE, far);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, " ELR=0x");
    uart_print_hex((uint64_t)QEMU_UART0_BASE, elr);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n");

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[exception] SPSR=0x");
    uart_print_hex((uint64_t)QEMU_UART0_BASE, spsr);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n");

    (void)spsr; /* 避免 unused 警告（实际已通过 UART 输出使用） */

    /* 跳过出错指令，防止无限循环 */
    *elr_ptr = elr + 4U;
}

/**
 * @brief EL1 IRQ 中断处理函数
 *
 * @details 读取 GIC 中断确认寄存器（IAR）获取中断号，
 *          根据中断号分发到对应处理函数：
 *          - PPI 30（ARM 通用定时器）→ timer_interrupt_handler
 *          - 其他中断 → 打印中断号
 *          最后调用 GIC EOI 结束中断。
 *
 * @note 对应需求: IN-001~006（中断控制器管理）
 */
void irq_handler(void)
{
    uint32_t irq;

    /* 从 GIC 获取当前最高优先级挂起中断号 */
    irq = gic_acknowledge_irq();

    /* 检查是否为伪中断 */
    if (irq >= GIC_SPURIOUS_IRQ)
    {
        return;
    }

    /* 根据中断号分发处理 */
    if (irq == QEMU_TIMER_IRQ)
    {
        /* ARM 通用定时器物理定时器中断 */
        timer_interrupt_handler();
    }
    else
    {
        /* 未知中断：打印诊断信息 */
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[irq] unhandled IRQ: ");
        uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)irq);
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n");
    }

    /* 通知 GIC 中断处理完成 */
    gic_end_of_interrupt(irq);
}

/**
 * @brief EL1 SError 系统错误处理函数
 *
 * @details 处理 ARM64 SError（系统错误）异常。
 *          SError 通常由硬件错误引起（如内存 RAS 错误）。
 *          打印诊断信息后挂起系统。
 *
 * @param esr ESR_EL1 寄存器值
 * @param far FAR_EL1 寄存器值
 * @param elr ELR_EL1 寄存器值
 *
 * @note 对应需求: KR-003（异常向量表管理）
 */
void el1_serror_handler(uint64_t esr, uint64_t far, uint64_t elr)
{
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n[exception] === SError ===\n");

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[exception] ESR=0x");
    uart_print_hex((uint64_t)QEMU_UART0_BASE, esr);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, " FAR=0x");
    uart_print_hex((uint64_t)QEMU_UART0_BASE, far);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, " ELR=0x");
    uart_print_hex((uint64_t)QEMU_UART0_BASE, elr);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n");

    /* SError 为严重错误，挂起系统 */
    for (;;)
    {
        __asm__ volatile("wfe" ::: "memory");
    }
}

/**
 * @brief 系统调用处理函数（SVC）
 *
 * @details SVC 系统调用的 C 语言入口。
 *          x8 = 系统调用号，x0-x7 = 参数。
 *          当前为桩实现。
 */
void syscall_handler(void)
{
    /* TODO: 实现系统调用分发 */
}

/* ========================================================================
 * 辅助函数实现
 * ======================================================================== */

/**
 * @brief 十六进制输出辅助函数
 *
 * @details 逐字符输出 64 位无符号整数的十六进制表示。
 *          不使用本地数组，避免触发栈保护器。
 *
 * @param base  UART 基地址
 * @param value 要输出的 64 位无符号整数
 */
static void uart_print_hex(uint64_t base, uint64_t value)
{
    static const char s_hex[] = "0123456789ABCDEF";
    int32_t i;

    for (i = 60; i >= 0; i -= 4)
    {
        uint8_t nibble = (uint8_t)((value >> (uint32_t)i) & 0xFU);
        hal_uart_putc(base, s_hex[nibble]);
    }
}

/**
 * @brief 无符号十进制输出辅助函数
 *
 * @details 使用迭代方式逐位提取并输出，避免本地数组和递归。
 *          先找到最高位的位置，然后从高到低依次输出。
 *
 * @param base  UART 基地址
 * @param value 要输出的 64 位无符号整数
 */
static void uart_print_uint(uint64_t base, uint64_t value)
{
    /* 特殊处理 0 */
    if (value == 0ULL)
    {
        hal_uart_putc(base, '0');
        return;
    }

    /* 找到最高位的 10 的幂次 */
    uint64_t divisor = 1ULL;
    while ((value / divisor) >= 10ULL)
    {
        divisor *= 10ULL;
    }

    /* 从最高位开始输出 */
    while (divisor > 0ULL)
    {
        uint8_t digit = (uint8_t)((value / divisor) % 10ULL);
        hal_uart_putc(base, (char)('0' + (int32_t)digit));
        divisor /= 10ULL;
    }
}

/* ========================================================================
 * 内核主入口
 * ======================================================================== */

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
    uint64_t bss_size;
    uint64_t stack_start;
    uint64_t stack_end;
    kernel_status_t ret;

    /* ---- 第一步：初始化 UART 早期输出 ---- */
    hal_uart_init((uint64_t)QEMU_UART0_BASE);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, g_banner);

    /* ---- 第二步：打印编译信息 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Compiler: ");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, __VERSION__);
    hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Build: ");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, __DATE__);
    hal_uart_putc((uint64_t)QEMU_UART0_BASE, ' ');
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, __TIME__);
    hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');

    /* ---- 第三步：打印硬件信息 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] CPU ID: ");
    {
        uint32_t cpu_id = hal_get_cpu_id();
        uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)cpu_id);
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
    }

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Exception Level: EL");
    {
        uint32_t el = hal_get_current_el();
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, '0' + (char)el);
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
    }

    /* ---- 第四步：打印内存布局 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] --- Memory Layout ---\n");

    bss_size = (uint64_t)((uintptr_t)__bss_end - (uintptr_t)__bss_start);
    stack_start = (uint64_t)(uintptr_t)__stacks_start;
    stack_end = (uint64_t)(uintptr_t)__stacks_end;

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "  .bss:  ");
    uart_print_uint((uint64_t)QEMU_UART0_BASE, bss_size);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, " bytes (");
    uart_print_uint((uint64_t)QEMU_UART0_BASE, bss_size / 1024ULL);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, " KB)\n");

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "  Stack: 0x");
    uart_print_hex((uint64_t)QEMU_UART0_BASE, stack_start);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, " - 0x");
    uart_print_hex((uint64_t)QEMU_UART0_BASE, stack_end);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, " (8KB x ");
    uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)CONFIG_MAX_CPUS);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, " CPUs)\n");

    /* ---- 第五步：初始化 GIC 中断控制器 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Initializing GIC...\n");
    ret = gic_init();
    if (ret != KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] FATAL: gic_init failed\n");
        for (;;)
        {
            __asm__ volatile("wfe" ::: "memory");
        }
    }

    /* ---- 第六步：初始化定时器 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Initializing timer...\n");
    timer_init();

    /* 使能定时器 PPI 中断 */
    (void)gic_enable_irq(QEMU_TIMER_IRQ);

    /* ---- 第七步：初始化调度器 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Initializing scheduler...\n");
    ret = scheduler_init();
    if (ret != KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] FATAL: scheduler_init failed\n");
        for (;;)
        {
            __asm__ volatile("wfe" ::: "memory");
        }
    }

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] All subsystems initialized\n");

    /* ---- 第八步：启用 IRQ 中断 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Enabling IRQ...\n");
    hal_irq_enable();

    /* ---- 第九步：启动调度器（永不返回） ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Scheduler started, entering idle...\n");
    scheduler_start();

    /* 永不到达 */
    for (;;)
    {
        __asm__ volatile("wfe" ::: "memory");
    }
}
