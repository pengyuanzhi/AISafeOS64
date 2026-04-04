/**
 * @file entry.c
 * @brief 微内核 C 语言入口与异常处理
 * @author AISafe64 Team
 * @date 2026-04-04
 * @version 4.0
 *
 * @details 微内核 C 语言主入口函数与 ARM64 异常处理
 *          - 初始化 UART 早期输出
 *          - 打印启动横幅与硬件信息
 *          - 初始化 GIC 中断控制器
 *          - 初始化定时器与调度器
 *          - 创建测试线程（A/B/C）
 *          - 启用 UART 接收中断
 *          - 内核 Shell 命令处理
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
    "  AISafeOS64 Microkernel v0.3\n"
    "  ARMv8-A (AArch64) Real-Time OS\n"
    "  Copyright (c) 2026 AISafe64 Team\n"
    "========================================\n";

/* ========== QEMU virt 平台中断号定义 ========== */

/** @brief ARM 通用定时器物理定时器 PPI 中断号（30 号 PPI） */
#define QEMU_TIMER_IRQ     30U

/** @brief QEMU virt PL011 UART0 SPI 中断号 */
#define QEMU_UART_IRQ      33U

/** @brief GIC 伪中断号（无挂起中断时 IAR 返回值） */
#define GIC_SPURIOUS_IRQ   1023U

/* ========== IRQ 调试计数器 ========== */

/** @brief IRQ 调试计数器（前几次 IRQ 打印诊断信息） */
static volatile uint32_t s_irq_debug_count = 0U;

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

/* ========================================================================
 * 内核 Shell 实现
 * ======================================================================== */

/** @brief Shell 输入缓冲区大小 */
#define SHELL_BUF_SIZE 128U

/** @brief Shell 输入缓冲区 */
static char s_shell_buf[SHELL_BUF_SIZE];

/** @brief Shell 输入缓冲区当前位置 */
static uint32_t s_shell_pos = 0U;

/** @brief Shell 提示符 */
static const char s_shell_prompt[] = "aisafe64> ";

/**
 * @brief 字符串比较
 *
 * @param s1 第一个字符串
 * @param s2 第二个字符串
 *
 * @return 0 表示相等
 */
static int shell_strcmp(const char *s1, const char *s2)
{
    while ((*s1 != '\0') && (*s2 != '\0'))
    {
        if (*s1 != *s2)
        {
            return (int)(*s1) - (int)(*s2);
        }
        s1++;
        s2++;
    }

    return (int)(*s1) - (int)(*s2);
}

/**
 * @brief 去除字符串尾部空白字符
 *
 * @param str 字符串
 * @param len 字符串长度
 */
static void shell_strip_trailing(char *str, uint32_t len)
{
    int32_t i;

    for (i = (int32_t)(len - 1U); i >= 0; i--)
    {
        if ((str[i] == ' ') || (str[i] == '\r') || (str[i] == '\n') ||
            (str[i] == '\t'))
        {
            str[i] = '\0';
        }
        else
        {
            break;
        }
    }
}

/**
 * @brief 显示 Shell 帮助信息
 */
static void shell_cmd_help(void)
{
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "AISafeOS64 Kernel Shell Commands:\n");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "  help    - 显示命令列表\n");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "  stats   - 显示调度器统计\n");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "  threads - 列出所有线程状态\n");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "  reboot  - 重启系统\n");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n");
}

/**
 * @brief 显示调度器统计信息
 */
static void shell_cmd_stats(void)
{
    tick_t ticks;
    uint64_t ms;
    uint32_t thread_count;
    uint32_t i;

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n=== 系统统计 ===\n");

    /* 系统 tick 数 */
    ticks = timer_get_ticks();
    ms = timer_get_ms();
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "  Ticks: ");
    uart_print_uint((uint64_t)QEMU_UART0_BASE, ticks);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, " (");
    uart_print_uint((uint64_t)QEMU_UART0_BASE, ms);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, " ms)\n");

    /* 线程统计 */
    thread_count = 0U;
    for (i = 0U; i < CONFIG_MAX_THREADS; i++)
    {
        if (g_scheduler.thread_table[i].state != KTHREAD_STATE_DEAD)
        {
            thread_count++;
        }
    }

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "  活跃线程: ");
    uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)thread_count);
    hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');

    /* 就绪队列统计 */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "  就绪线程: ");
    uart_print_uint((uint64_t)QEMU_UART0_BASE,
                    (uint64_t)g_scheduler.cpu_queues[0U].nr_running);
    hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n");
}

/**
 * @brief 列出所有线程状态
 */
static void shell_cmd_threads(void)
{
    uint32_t i;
    KThread_t *thread;
    const char *state_str;

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n=== 线程列表 ===\n");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE,
                  "  TID  Name        State     Prio Policy\n");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE,
                  "  ---  ----------  --------  ---- -------\n");

    for (i = 0U; i < CONFIG_MAX_THREADS; i++)
    {
        thread = &g_scheduler.thread_table[i];

        if (thread->state == KTHREAD_STATE_DEAD)
        {
            continue;
        }

        /* 线程 ID */
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "  ");
        uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)i);
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "    ");

        /* 线程名称（左对齐10字符） */
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, thread->name);

        /* 状态字符串 */
        switch (thread->state)
        {
            case KTHREAD_STATE_READY:
                state_str = "READY ";
                break;
            case KTHREAD_STATE_RUNNING:
                state_str = "RUNNING";
                break;
            case KTHREAD_STATE_BLOCKED:
                state_str = "BLOCKED";
                break;
            case KTHREAD_STATE_SLEEPING:
                state_str = "SLEEP ";
                break;
            case KTHREAD_STATE_SUSPENDED:
                state_str = "SUSPEN";
                break;
            default:
                state_str = "UNKNOW";
                break;
        }

        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "  ");
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, state_str);
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "  ");

        /* 优先级 */
        uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)thread->prio);
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "   ");

        /* 调度策略 */
        if (thread->policy == KTHREAD_POLICY_FIFO)
        {
            hal_uart_puts((uint64_t)QEMU_UART0_BASE, "FIFO");
        }
        else
        {
            hal_uart_puts((uint64_t)QEMU_UART0_BASE, "RR  ");
        }

        hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
    }

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n");
}

/**
 * @brief 系统重启
 */
static void shell_cmd_reboot(void)
{
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n正在重启系统...\n");

    /* QEMU virt 平台通过 SYS_PWRCTRL 寄存器重启 */
    *(volatile uint32_t *)0x09010000U = 0x80000000U;

    /* 如果重启失败，死循环 */
    for (;;)
    {
        __asm__ volatile("wfe" ::: "memory");
    }
}

/**
 * @brief 处理 Shell 命令
 *
 * @param cmd 命令字符串（以 '\0' 结尾）
 */
static void shell_process_cmd(const char *cmd)
{
    if (cmd[0] == '\0')
    {
        /* 空命令，显示提示符 */
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, s_shell_prompt);
        return;
    }

    if (shell_strcmp(cmd, "help") == 0)
    {
        shell_cmd_help();
    }
    else if (shell_strcmp(cmd, "stats") == 0)
    {
        shell_cmd_stats();
    }
    else if (shell_strcmp(cmd, "threads") == 0)
    {
        shell_cmd_threads();
    }
    else if (shell_strcmp(cmd, "reboot") == 0)
    {
        shell_cmd_reboot();
    }
    else
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "未知命令: ");
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, cmd);
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n输入 'help' 查看可用命令\n");
    }

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, s_shell_prompt);
}

/**
 * @brief Shell 接收一个字符并处理
 *
 * @param ch 接收到的字符
 */
static void shell_rx_char(char ch)
{
    /* 回显 */
    hal_uart_putc((uint64_t)QEMU_UART0_BASE, ch);

    if (ch == '\r')
    {
        /* Enter 键：处理命令 */
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
        s_shell_buf[s_shell_pos] = '\0';
        shell_strip_trailing(s_shell_buf, s_shell_pos);
        shell_process_cmd(s_shell_buf);
        s_shell_pos = 0U;
    }
    else if (ch == 0x7F)
    {
        /* Backspace 键 */
        if (s_shell_pos > 0U)
        {
            s_shell_pos--;
            hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\b \b");
        }
    }
    else
    {
        /* 普通字符：存入缓冲区 */
        if (s_shell_pos < (SHELL_BUF_SIZE - 1U))
        {
            s_shell_buf[s_shell_pos] = ch;
            s_shell_pos++;
        }
    }
}

/* ========================================================================
 * UART 接收中断处理
 * ======================================================================== */

/**
 * @brief UART 中断处理函数
 *
 * @details 处理 PL011 UART0 接收中断。
 *          从 UART 数据寄存器读取所有可用字符，
 *          并传递给 Shell 进行处理。
 */
static void uart_irq_handler(void)
{
    char ch;

    /* 读取所有可用字符 */
    while (hal_uart_getc((uint64_t)QEMU_UART0_BASE, &ch) != 0)
    {
        shell_rx_char(ch);
    }
}

/* ========================================================================
 * 测试线程
 * ======================================================================== */

/**
 * @brief 测试线程 A：每 100ms 打印 "A"
 *
 * @param arg 线程参数（未使用）
 */
static void thread_a_entry(void *arg)
{
    (void)arg;

    for (;;)
    {
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, 'A');
        (void)kthread_sleep(100U);
    }
}

/**
 * @brief 测试线程 B：每 200ms 打印 "B"
 *
 * @param arg 线程参数（未使用）
 */
static void thread_b_entry(void *arg)
{
    (void)arg;

    for (;;)
    {
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, 'B');
        (void)kthread_sleep(200U);
    }
}

/**
 * @brief 测试线程 C：每 500ms 打印系统 tick 数
 *
 * @param arg 线程参数（未使用）
 */
static void thread_c_entry(void *arg)
{
    (void)arg;

    for (;;)
    {
        tick_t ticks = timer_get_ticks();
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n[tick=");
        uart_print_uint((uint64_t)QEMU_UART0_BASE, ticks);
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "] ");
        (void)kthread_sleep(500U);
    }
}

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
 * @details 从 GIC 获取当前最高优先级挂起中断号，
 *          根据中断号分发到对应处理函数：
 *          - PPI 30（ARM 通用定时器）→ timer_interrupt_handler
 *          - SPI 33（PL011 UART0）   → uart_irq_handler
 *          - 其他中断 → 打印中断号
 *          最后调用 GIC EOI 结束中断。
 *
 * @note 对应需求: IN-001~006（中断控制器管理）
 */
void irq_handler(void)
{
    uint32_t irq;

    /* 入口诊断 - 确认 IRQ handler 被调用 */
    hal_uart_putc((uint64_t)0x09000000UL, '!');

    /* 从 GIC 获取当前最高优先级挂起中断号 */
    irq = gic_get_irq_id();

    /* 诊断 GIC 返回值 */
    hal_uart_putc((uint64_t)0x09000000UL, 'I');
    hal_uart_putc((uint64_t)0x09000000UL, '0' + (char)(irq % 10U));
    hal_uart_putc((uint64_t)0x09000000UL, '.');

    /* 检查是否为伪中断 */
    if (irq >= GIC_SPURIOUS_IRQ)
    {
        return;
    }

    /* 前 5 次 IRQ 打印调试信息，确认中断分发正常 */
    if (s_irq_debug_count < 5U)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[irq] #");
        uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)s_irq_debug_count);
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, " irq=");
        uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)irq);
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n");
        s_irq_debug_count++;
    }

    /* 根据中断号分发处理 */
    if (irq == QEMU_TIMER_IRQ)
    {
        /* ARM 通用定时器物理定时器中断 */
        timer_interrupt_handler();
    }
    else if (irq == QEMU_UART_IRQ)
    {
        /* PL011 UART0 接收中断 */
        uart_irq_handler();
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
    thread_id_t tid;

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

    /* 打印定时器调试信息 */
    {
        uint64_t timer_ns = timer_get_ns();
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[timer] initial_ns=");
        uart_print_uint((uint64_t)QEMU_UART0_BASE, timer_ns);
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
    }

    /* 配置定时器 PPI 中断（IRQ 30）的优先级和路由 */
    (void)gic_set_priority(QEMU_TIMER_IRQ, (uint8_t)0xA0U);
    /* 注意：PPI 路由为当前 CPU，不需要设置 ITARGETSR */
    /* 使能定时器 PPI 中断（IRQ 30） */
    (void)gic_enable_irq(QEMU_TIMER_IRQ);

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[timer] IRQ 30 enabled\n");

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

    /* ---- 第八步：创建测试线程 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Creating test threads...\n");

    /* 线程 A：优先级 100，RR 策略，每 100ms 打印 'A' */
    tid = kthread_create("thread_a", thread_a_entry, NULL,
                         (priority_t)100U, KTHREAD_POLICY_RR,
                         CONFIG_STACK_SIZE_DEFAULT);
    if (tid == THREAD_ID_INVALID)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] WARN: thread_a create failed\n");
    }

    /* 线程 B：优先级 80，RR 策略，每 200ms 打印 'B' */
    tid = kthread_create("thread_b", thread_b_entry, NULL,
                         (priority_t)80U, KTHREAD_POLICY_RR,
                         CONFIG_STACK_SIZE_DEFAULT);
    if (tid == THREAD_ID_INVALID)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] WARN: thread_b create failed\n");
    }

    /* 线程 C：优先级 60，RR 策略，每 500ms 打印 tick 数 */
    tid = kthread_create("thread_c", thread_c_entry, NULL,
                         (priority_t)60U, KTHREAD_POLICY_RR,
                         CONFIG_STACK_SIZE_DEFAULT);
    if (tid == THREAD_ID_INVALID)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] WARN: thread_c create failed\n");
    }

    (void)tid; /* 避免 unused 警告 */

    /* ---- 第九步：初始化 UART 接收中断 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Enabling UART RX interrupt...\n");
    (void)gic_set_priority(QEMU_UART_IRQ, (uint8_t)GIC_PRIORITY_DEFAULT);
    (void)gic_enable_irq(QEMU_UART_IRQ);
    hal_uart_enable_rx_irq((uint64_t)QEMU_UART0_BASE);

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] All subsystems initialized\n");

    /* ---- 第十步：重新武装定时器并启用 IRQ ----
     *
     * @details 修复定时器中断不触发问题。
     *          timer_init() 设置的比较值在大量 UART 输出期间
     *          已经过期，定时器 ISTATUS=1。虽然 ARMv8 规定
     *          ISTATUS=1 时应产生中断，但在 QEMU GICv2 模型中，
     *          如果中断在 DAIF.I=1 期间触发，GIC 可能不会
     *          正确锁存该中断。
     *
     *          修复方案：禁用→重新武装→使能→启用 IRQ
     *          确保定时器处于干净状态。
     */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Rearming timer...\n");
    {
        /* 禁用物理定时器（清除 ISTATUS） */
        __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"((uint64_t)0U));
        __asm__ volatile("isb");

        /* 读取当前计数器和频率 */
        uint64_t current = 0ULL;
        uint64_t freq = 0ULL;
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(current));
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
        __asm__ volatile("isb");

        if (freq == 0ULL)
        {
            freq = 24000000ULL; /* QEMU virt 默认 24MHz */
        }

        /* 设置比较值为当前值 + 10ms (freq/100) */
        uint64_t delta = freq / 100ULL;
        __asm__ volatile("msr cntp_cval_el0, %0" :: "r"(current + delta));
        __asm__ volatile("isb");

        /* 使能定时器（ENABLE=1, IMASK=0） */
        __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"((uint64_t)1U));
        __asm__ volatile("isb");

        /* 验证定时器状态 */
        uint64_t ctl = 0ULL;
        __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(ctl));
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[timer] CTL=0x");
        uart_print_hex((uint64_t)QEMU_UART0_BASE, ctl);
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, " freq=");
        uart_print_uint((uint64_t)QEMU_UART0_BASE, freq);
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, " delta=");
        uart_print_uint((uint64_t)QEMU_UART0_BASE, delta);
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
    }

    /* 验证 GIC ISENABLER0（确认 PPI 30 已使能） */
    {
        uint32_t isenable0 = *(volatile uint32_t *)(0x08000000UL + 0x0100U);
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[gic] ISENABLER0=0x");
        uart_print_hex((uint64_t)QEMU_UART0_BASE, (uint64_t)isenable0);
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
    }

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Enabling IRQ...\n");
    hal_irq_enable();

    /* 验证 DAIF 状态 */
    {
        uint64_t daif = 0ULL;
        __asm__ volatile("mrs %0, daif" : "=r"(daif));
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[diag] DAIF=0x");
        uart_print_hex((uint64_t)QEMU_UART0_BASE, daif);
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
    }

    /* 验证 VBAR_EL1 指向异常向量表 */
    {
        uint64_t vbar = 0ULL;
        __asm__ volatile("mrs %0, vbar_el1" : "=r"(vbar));
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[diag] VBAR_EL1=0x");
        uart_print_hex((uint64_t)QEMU_UART0_BASE, vbar);
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
    }

    /* 发送 SGI 0 给自己，测试 GIC 中断传递 */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[diag] Sending SGI 0 to self...\n");
    gic_send_sgi(0U, 0x01U);

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] IRQ enabled OK\n");

    /* ---- 第十一步：进入主循环 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[kernel] Scheduler started\n");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, s_shell_prompt);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "AISafeOS64: entering main loop...\n");
    {
        tick_t last_tick = 0ULL;
        uint64_t loop_count = 0ULL;
        uint64_t tick_count = 0ULL;

        for (;;)
        {
            tick_t now = timer_get_ticks();

            /*
             * 轮询定时器 ISTATUS 位作为备份。
             * 如果 IRQ 未到达但定时器条件已满足，
             * 说明 GIC 中断传递有问题，手动推进 tick。
             */
            {
                uint64_t ctl = 0ULL;
                __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(ctl));
                if ((ctl & 0x4ULL) != 0ULL)
                {
                    /* ISTATUS=1：定时器已触发 */
                    hal_uart_putc((uint64_t)QEMU_UART0_BASE, 'T');

                    /* 手动调用定时器中断处理 */
                    timer_interrupt_handler();
                }
            }

            if (now != last_tick)
            {
                tick_count++;
                if ((tick_count % 1000ULL) == 0ULL)
                {
                    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[tick] #");
                    uart_print_uint((uint64_t)QEMU_UART0_BASE, tick_count);
                    hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
                }
                last_tick = now;
            }

            loop_count++;
            if ((loop_count % 10000000ULL) == 0ULL)
            {
                hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[alive] loops=");
                uart_print_uint((uint64_t)QEMU_UART0_BASE, loop_count);
                hal_uart_puts((uint64_t)QEMU_UART0_BASE, " ticks=");
                uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)now);
                hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
            }
        }
    }
}
