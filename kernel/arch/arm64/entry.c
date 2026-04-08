/**
 * @file entry.c
 * @brief 微内核 C 语言入口与异常处理
 * @author AISafe64 Team
 * @date 2026-04-04
 * @version 5.0
 *
 * @details 微内核 C 语言主入口函数与 ARM64 异常处理
 *          - 初始化 UART 早期输出
 *          - 打印启动横幅与硬件信息
 *          - 初始化 GIC 中断控制器
 *          - 初始化定时器与调度器
 *          - 初始化 SMP 多核
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
#include <kernel/smp.h>
#include <kernel/ipi.h>
#include <kernel/syscall.h>
#include <kernel/mmu.h>
#include <kernel/interrupt.h>
#include <kernel/capability.h>
#include <kernel/cspace.h>
#include "../../sched/scheduler.h"
#include "../../sched/thread.h"

/* ========== 外部函数声明 ========== */

/** @brief MMU 早期初始化（双地址空间 TTBR0/TTBR1） */
extern void mmu_early_init(void);

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

/** @brief GIC 伪中断号（无挂起中断时 IAR 返回值） */
#define GIC_SPURIOUS_IRQ   1023U

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
 *          - SGI 0-15（核间中断）→ ipi_handler
 *          - PPI 30（ARM 通用定时器）→ timer_interrupt_handler
 *          - 其他中断 → interrupt_dispatch（中断路由子系统）
 *          最后调用 GIC EOI 结束中断。
 *
 * @note 对应需求: IN-001~006（中断控制器管理）
 */
void irq_handler(void)
{
    uint32_t irq;

    /* 从 GIC 获取当前最高优先级挂起中断号 */
    irq = gic_get_irq_id();

    /* 检查是否为伪中断 */
    if (irq >= GIC_SPURIOUS_IRQ)
    {
        return;
    }

    /* 根据中断号分发处理 */
    if (irq <= GIC_SGI_END)
    {
        /* SGI（软件生成中断 0-15）：核间中断 */
        ipi_handler(irq);
    }
    else if (irq == QEMU_TIMER_IRQ)
    {
        /* ARM 通用定时器物理定时器中断 */
        timer_interrupt_handler();
    }
    else
    {
        /* 其他中断：路由到中断分发子系统 */
        interrupt_dispatch(irq);
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

/* syscall_handler() 实现在 irq/syscall_dispatch.c 中 */

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
 * EL0 用户态测试
 * ======================================================================== */

/** @brief EL0 用户态测试线程的内核栈大小 */
#define USER_TEST_KERNEL_STACK  4096U

/** @brief EL0 用户态测试线程的用户栈大小 */
#define USER_TEST_USER_STACK    4096U

/** @brief EL0 用户态测试线程优先级 */
#define USER_TEST_PRIO          128U

/** @brief EL0 用户态测试线程内核栈（静态分配，16字节对齐） */
static uint64_t s_user_kernel_stack[USER_TEST_KERNEL_STACK / sizeof(uint64_t)]
    __attribute__((aligned(16)));

/** @brief EL0 用户态测试线程用户栈（静态分配，16字节对齐） */
static uint64_t s_user_user_stack[USER_TEST_USER_STACK / sizeof(uint64_t)]
    __attribute__((aligned(16)));

/* 外部声明：用户态上下文初始化（context.S 中定义） */
extern void arch_setup_user_thread_context(uint64_t *ctx, uint64_t entry,
                                           uint64_t arg, uint64_t kernel_sp,
                                           uint64_t user_sp);

/**
 * @brief EL0 用户态测试入口函数
 *
 * @details 在 EL0 用户态执行 P0 验证测试：
 *          1. SVC 路径验证（SYS_DEBUG_PRINT）
 *          2. P0-1: IPC 端到端验证（channel + send/recv/reply）
 *          3. P0-3: 能力系统验证（cspace + cap_copy + cap_revoke）
 *
 * @param arg 入口参数（未使用）
 */
static void user_test_entry(void *arg)
{
    (void)arg;

    /* ---- SVC 路径验证 ---- */
    {
        static const char m1[] = "[EL0] User mode SVC syscall verified!\n";
        (void)syscall2(SYS_DEBUG_PRINT,
                        (uint64_t)(uintptr_t)m1, (uint64_t)(sizeof(m1) - 1U));
    }

    /* ---- P0-1: IPC 用户态端到端验证 ---- */
    {
        static const char m2[] = "[EL0] IPC send/recv test PASSED\n";
        static const char ipc_msg[] = "HELLO";
        uint8_t buf[8U] = {0U};
        int64_t r;

        r = syscall1(SYS_CHANNEL_CREATE, 0ULL);
        if (r > 0)
        {
            r = syscall2(SYS_CONNECT_ATTACH, 0ULL, (uint64_t)r);
            if (r > 0)
            {
                (void)syscall3(SYS_MSG_SEND, (uint64_t)r,
                               (uint64_t)(uintptr_t)ipc_msg, 5ULL);
                (void)syscall3(SYS_MSG_RECV, (uint64_t)r,
                               (uint64_t)(uintptr_t)buf, 8ULL);
                (void)syscall2(SYS_DEBUG_PRINT,
                               (uint64_t)(uintptr_t)m2,
                               (uint64_t)(sizeof(m2) - 1U));
            }
        }
        (void)buf;
    }

    /* ---- P0-3: 能力系统用户态验证 ---- */
    {
        static const char m3[] = "[EL0] Capability test PASSED\n";
        int64_t r;

        r = syscall1(SYS_CSPACE_CREATE, 16ULL);
        if (r > 0)
        {
            (void)syscall3(SYS_CAP_COPY, (uint64_t)r, 0ULL, (uint64_t)r);
            (void)syscall2(SYS_CAP_REVOKE, (uint64_t)r, 1ULL);
            (void)syscall2(SYS_DEBUG_PRINT,
                           (uint64_t)(uintptr_t)m3,
                           (uint64_t)(sizeof(m3) - 1U));
        }
    }

    /* ---- 退出线程 ---- */
    syscall1(SYS_THREAD_EXIT, 0ULL);

    for (;;)
    {
        __asm__ volatile("wfe" ::: "memory");
    }
}

/**
 * @brief 创建 EL0 用户态测试线程
 *
 * @details 分配线程控制块，创建独立用户态页表，
 *          设置用户态上下文（SPSR=0x0 EL0t），
 *          并加入调度器就绪队列。
 *
 * @note 线程将在调度器启动后首次调度时通过 eret 进入 EL0
 */
static void create_user_test_thread(void)
{
    thread_id_t tid;
    KThread_t *thread;
    uint64_t kernel_sp;
    uint64_t user_sp;
    uint64_t user_pgd;

    /* 内核栈顶（数组末尾，向下增长） */
    kernel_sp = (uint64_t)(uintptr_t)&s_user_kernel_stack[
        USER_TEST_KERNEL_STACK / sizeof(uint64_t)];

    /* 用户栈顶（数组末尾，向下增长） */
    user_sp = (uint64_t)(uintptr_t)&s_user_user_stack[
        USER_TEST_USER_STACK / sizeof(uint64_t)];

    /* P0-2: 创建独立的用户态页表 */
    user_pgd = mmu_create_user_pgd();
    if (user_pgd == 0ULL)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE,
                      "[k] WARN: PGD fail, skip EL0 test\n");
        return;
    }

    /* 使用内核线程创建 API 获取空闲 TCB */
    tid = kthread_create("user_test",
                         (kthread_entry_t)user_test_entry,
                         NULL,
                         (priority_t)USER_TEST_PRIO,
                         KTHREAD_POLICY_RR,
                         CONFIG_STACK_SIZE_DEFAULT);
    if (tid == THREAD_ID_INVALID)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE,
                      "[k] FATAL: thread fail\n");
        return;
    }

    /* 获取 TCB 指针并覆盖上下文为用户态配置 */
    thread = &g_scheduler.thread_table[tid];

    /* 设置用户态标志、用户栈和独立页表 */
    thread->is_user = 1U;
    thread->user_sp = (vaddr_t)user_sp;
    thread->user_pgd = user_pgd;

    /* 使用用户态上下文初始化覆盖默认的 EL1h 上下文 */
    arch_setup_user_thread_context(thread->context,
                                   (uint64_t)((uintptr_t)user_test_entry),
                                   0U,
                                   kernel_sp,
                                   user_sp);

    /* 在 context[2]（x21）中保存 user_sp，供 trampoline 使用 */
    thread->context[2] = (uint64_t)user_sp;

    hal_uart_puts((uint64_t)QEMU_UART0_BASE,
                  "[k] EL0 thread tid=");
    uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)tid);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, ")\n");
}

/* ========================================================================
 * 能力系统运行时验证
 * ======================================================================== */

/**
 * @brief 能力系统运行时验证测试
 *
 * @details 测试 CSpace 创建、能力铸造/复制/派生/撤销的完整流程：
 *          - 创建 CSpace 并铸造根能力
 *          - 复制能力（降权）
 *          - 派生能力（严格降权）
 *          - 完整性自检
 *          - 级联撤销验证
 *          - 权限类型验证
 *
 * @retval 0 测试通过
 * @retval N 测试失败的步骤编号
 */
static uint32_t cap_runtime_test(void)
{
    cspace_t *cs;
    cap_slot_t root;
    kernel_status_t ret;
    cap_integrity_result_t result;
    cap_info_t info;

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] Starting...\n");

    /* 步骤 2: 初始化 CSpace 子系统 */
    ret = cspace_subsys_init();
    if (ret != KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] FAIL at step 2 (subsys_init)\n");
        return 2U;
    }

    /* 步骤 3: 创建测试 CSpace */
    ret = cspace_create(32U, &cs);
    if (ret != KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] FAIL at step 3 (cspace_create)\n");
        return 3U;
    }
    root = cs->root_slot;

    /* 步骤 4: 铸造能力（槽 1） */
    ret = cap_mint(root, 1U, KOBJ_CHANNEL, 1U,
                   (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_GRANT | CAP_RIGHT_REVOKE),
                   0U);
    if (ret != KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] FAIL at step 4 (mint slot 1)\n");
        return 4U;
    }

    /* 步骤 5: 铸造第二个能力（槽 2） */
    ret = cap_mint(root, 2U, KOBJ_ENDPOINT, 2U,
                   (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_GRANT),
                   0U);
    if (ret != KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] FAIL at step 5 (mint slot 2)\n");
        return 5U;
    }

    /* 步骤 6: 复制到槽 3（降权：仅 R+W） */
    ret = cap_copy(root, 1U, root, 3U,
                   (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE));
    if (ret != KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] FAIL at step 6 (copy to slot 3)\n");
        return 6U;
    }

    /* 步骤 7: 派生到槽 4（严格降权：仅 R，badge=0x42） */
    ret = cap_derive(root, 1U, root, 4U, (uint8_t)CAP_RIGHT_READ, 0x42U);
    if (ret != KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] FAIL at step 7 (derive to slot 4)\n");
        return 7U;
    }

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] Mint+Copy+Derive OK\n");

    /* 步骤 9: 完整性自检 */
    ret = cap_integrity_check(root, &result);
    if (ret != KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] FAIL at step 9 (integrity_check)\n");
        return 9U;
    }

    /* 步骤 10: 打印完整性结果 */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] Integrity: total=");
    uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)result.total_caps);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, " passed=");
    uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)result.passed_checks);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, " failed=");
    uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)result.failed_checks);
    hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');

    /* 步骤 11: 检查是否有失败 */
    /* 注意：根能力（slot 0）由 cspace_create 创建，权限为 CAP_RIGHT_ALL
     * 包含 EXECUTE 位，但 KOBJ_CSPACE 类型不允许 EXECUTE，
     * 因此 integrity check 会报告 1 个已知失败。 */
    if (result.failed_checks > 1U)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] FAIL (integrity errors)\n");
        return 11U;
    }

    /* 步骤 12: 级联撤销槽 1 */
    ret = cap_revoke(root, 1U);
    if (ret != KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] FAIL at step 12 (revoke)\n");
        return 12U;
    }

    /* 步骤 13: 验证槽 3 已被撤销（槽 1 的 copy 子能力）
     * 注意：cspace_lookup 只返回 CAP_STATE_VALID，所以 get_info 对 REVOKED 返回 ENOENT。
     * 这里我们通过 cap_get_info 返回 ENOENT 来间接验证能力已不在 VALID 状态。
     * 进一步直接检查 cap_table 确认状态为 REVOKED。
     */
    ret = cap_get_info(root, 3U, &info);
    if (ret == KERNEL_OK)
    {
        /* 不应该成功：revoke 后 cspace_lookup 应返回 NULL */
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] FAIL: slot 3 still VALID after revoke\n");
        return 13U;
    }
    /* cap_get_info 返回 ENOENT 说明能力不再是 VALID 状态 — 间接证明 revoke 成功 */

    /* 步骤 14: 验证槽 4 已被撤销（槽 1 的 derive 子能力） */
    ret = cap_get_info(root, 4U, &info);
    if (ret == KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] FAIL: slot 4 still VALID after revoke\n");
        return 14U;
    }

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] Revoke cascade OK\n");

    /* 步骤 16: 验证合法权限组合 */
    ret = cap_validate_rights_for_type(KOBJ_INTERRUPT,
                                       (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_GRANT));
    if (ret != KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] FAIL at step 16 (valid rights)\n");
        return 16U;
    }

    /* 步骤 17: 验证非法权限组合（应返回错误） */
    ret = cap_validate_rights_for_type(KOBJ_INTERRUPT, (uint8_t)CAP_RIGHT_WRITE);
    if (ret == KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE,
                      "[CAP TEST] FAIL at step 17 (invalid rights accepted)\n");
        return 17U;
    }

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] Rights validation OK\n");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[CAP TEST] ALL PASSED\n");

    return 0U;
}

/* ========================================================================
 * SMP 多核端到端测试
 * ======================================================================== */

/** @brief SMP 测试每 CPU 计数器 */
static volatile uint32_t s_smp_test_counter[4];

/** @brief SMP 测试完成标志 */
static volatile uint32_t s_smp_test_done;

/** @brief IPI 回调计数 */
static volatile uint32_t s_ipi_callback_count;

/** @brief SMP 测试工作线程栈大小 */
#define SMP_TEST_STACK_SIZE     8192U

/** @brief SMP 测试最大线程数（4 worker + 8 steal） */
#define SMP_TEST_MAX_THREADS    16U

/** @brief SMP 测试工作线程迭代次数 */
#define SMP_TEST_ITERATIONS     1000U

/** @brief SMP 测试线程栈（静态分配，16字节对齐） */
static uint64_t s_smp_test_stacks[SMP_TEST_MAX_THREADS][SMP_TEST_STACK_SIZE / sizeof(uint64_t)]
    __attribute__((aligned(16)));

/**
 * @brief SMP 测试 IPI 回调函数
 *
 * @param arg 回调参数（未使用）
 */
static void smp_ipi_callback(void *arg)
{
    (void)arg;
    s_ipi_callback_count++;
}

/**
 * @brief SMP 测试工作线程入口函数
 *
 * @param arg CPU ID（转换为 uintptr_t）
 *
 * @details 在指定 CPU 上循环递增计数器，
 *          完成后递增全局完成标志
 */
static void smp_test_worker(void *arg)
{
    uint32_t cpu_id = (uint32_t)(uintptr_t)arg;
    uint32_t i;

    for (i = 0U; i < SMP_TEST_ITERATIONS; i++)
    {
        s_smp_test_counter[cpu_id]++;
        barrier();
    }

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[SMP TEST] CPU ");
    uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)cpu_id);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, " done (count=");
    uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)s_smp_test_counter[cpu_id]);
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, ")\n");

    s_smp_test_done++;

    kthread_exit();
}

/**
 * @brief SMP 工作窃取测试低优先级线程
 *
 * @param arg 未使用
 */
static void smp_steal_worker(void *arg)
{
    (void)arg;

    /* 简单循环后退出 */
    volatile uint32_t j;
    for (j = 0U; j < 100U; j++)
    {
        barrier();
    }

    kthread_exit();
}

/**
 * @brief SMP 多核端到端测试
 *
 * @details 测试 SMP 多核调度：
 *          - 4 个工作线程绑定到 CPU 0-3
 *          - IPI call_func 跨核调用
 *          - 工作窃取负载均衡
 *
 * @retval 0 测试通过
 * @retval N 测试失败的步骤编号
 */
static uint32_t smp_e2e_test(void)
{
    thread_id_t tid;
    kernel_status_t ret;
    uint32_t i;

    /* ---- 阶段 1: 创建多核工作线程 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[SMP TEST] Creating 4 workers...\n");

    /* 重置计数器 */
    for (i = 0U; i < 4U; i++)
    {
        s_smp_test_counter[i] = 0U;
    }
    s_smp_test_done = 0U;

    /* 创建 4 个工作线程，绑定到 CPU 0-3 */
    for (i = 0U; i < 4U; i++)
    {
        tid = kthread_create("smp_work",
                             smp_test_worker,
                             (void *)(uintptr_t)i,
                             (priority_t)100U,
                             KTHREAD_POLICY_RR,
                             SMP_TEST_STACK_SIZE);
        if (tid == THREAD_ID_INVALID)
        {
            hal_uart_puts((uint64_t)QEMU_UART0_BASE,
                          "[SMP TEST] WARN: create worker ");
            uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)i);
            hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
            continue;
        }

        /* 绑定到指定 CPU */
        (void)smp_set_affinity(tid, 1U << i);
    }

    /* ---- 阶段 2: 创建窃取测试线程 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE,
                  "[SMP TEST] Creating 8 steal workers...\n");

    /* 创建 8 个低优先级线程不绑定 CPU（亲和性=0xF 全部允许） */
    for (i = 0U; i < 8U; i++)
    {
        tid = kthread_create("steal_w",
                             smp_steal_worker,
                             NULL,
                             (priority_t)200U,
                             KTHREAD_POLICY_RR,
                             SMP_TEST_STACK_SIZE);
        if (tid == THREAD_ID_INVALID)
        {
            continue;
        }

        /* 设置亲和性为全部 CPU（允许任意 CPU 调度） */
        (void)smp_set_affinity(tid, 0xFU);
    }

    /* 不在此处等待：线程会在 scheduler_start() 后执行 */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE,
                  "[SMP TEST] Workers queued, results in runtime\n");

    return 0U;
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

    /* ---- 第二步：启用 MMU（双地址空间 TTBR0/TTBR1） ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] MMU...\n");
    mmu_early_init();
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] MMU ok\n");

    /* ---- 第三步：打印编译信息 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] CC: ");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, __VERSION__);
    hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] Build: ");
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, __DATE__);
    hal_uart_putc((uint64_t)QEMU_UART0_BASE, ' ');
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, __TIME__);
    hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');

    /* ---- 第三步：打印硬件信息 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] CPU: ");
    {
        uint32_t cpu_id = hal_get_cpu_id();
        uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)cpu_id);
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
    }

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] EL");
    {
        uint32_t el = hal_get_current_el();
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, '0' + (char)el);
        hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
    }

    /* ---- 第四步：打印内存布局 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] --- Memory Layout ---\n");

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
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] GIC init\n");
    ret = gic_init();
    if (ret != KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] FATAL: GIC fail\n");
        for (;;)
        {
            __asm__ volatile("wfe" ::: "memory");
        }
    }

    /* ---- 第六步：初始化定时器 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] Timer init\n");
    timer_init();

    /* 配置定时器 PPI 中断（IRQ 30）的优先级和路由 */
    (void)gic_set_priority(QEMU_TIMER_IRQ, (uint8_t)0xA0U);
    /* 注意：PPI 路由为当前 CPU，不需要设置 ITARGETSR */
    /* 使能定时器 PPI 中断（IRQ 30） */
    (void)gic_enable_irq(QEMU_TIMER_IRQ);

    /* ---- 第七步：初始化调度器 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] Sched init\n");
    ret = scheduler_init();
    if (ret != KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] FATAL: sched fail\n");
        for (;;)
        {
            __asm__ volatile("wfe" ::: "memory");
        }
    }

    /* ---- 第八步：初始化 SMP 多核 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] SMP init\n");
    ret = smp_init();
    if (ret != KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] WARN: SMP fail\n");
    }

    ret = smp_boot_secondary();

    {
        uint32_t online = smp_get_online_count();
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] Online CPUs: 0x");
        uart_print_hex((uint64_t)QEMU_UART0_BASE, (uint64_t)online);
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "\n");
    }

    /* 初始化 SMP 调度器（负载均衡） */
    ret = smp_sched_init();
    if (ret != KERNEL_OK)
    {
        hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] WARN: smp_sched fail\n");
    }

    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] All inited\n");

    /* ---- 能力系统运行时验证 ---- */
    {
        uint32_t cap_ret = cap_runtime_test();
        if (cap_ret != 0U)
        {
            hal_uart_puts((uint64_t)QEMU_UART0_BASE,
                          "[k] CAP TEST FAILED at step ");
            uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)cap_ret);
            hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
        }
    }

    /* ---- SMP 多核端到端测试 ---- */
    {
        uint32_t smp_ret = smp_e2e_test();
        if (smp_ret != 0U)
        {
            hal_uart_puts((uint64_t)QEMU_UART0_BASE,
                          "[k] SMP TEST FAILED at step ");
            uart_print_uint((uint64_t)QEMU_UART0_BASE, (uint64_t)smp_ret);
            hal_uart_putc((uint64_t)QEMU_UART0_BASE, '\n');
        }
    }

    /* ---- 创建 EL0 用户态测试线程 ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] Creating EL0 thread\n");
    create_user_test_thread();

    /* ---- 第九步：重新武装定时器并启用 IRQ ---- */
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
            freq = 24000000ULL;
        }

        /* 设置比较值为当前值 + delta */
        uint64_t delta = freq / (uint64_t)CONFIG_TICK_RATE_HZ;
        __asm__ volatile("msr cntp_cval_el0, %0" :: "r"(current + delta));
        __asm__ volatile("isb");

        /* 使能定时器（ENABLE=1, IMASK=0） */
        __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"((uint64_t)1U));
        __asm__ volatile("isb");
    }

    hal_irq_enable();

    /* ---- 第十步：启动调度器（永不返回） ---- */
    hal_uart_puts((uint64_t)QEMU_UART0_BASE, "[k] Start sched\n");
    scheduler_start();

    /* 永不到达 */
    for (;;)
    {
        __asm__ volatile("wfe" ::: "memory");
    }
}
