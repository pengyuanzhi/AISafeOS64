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
 *          - 初始化中断控制器（HAL）
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
#include <kernel/klog.h>
#include <kernel/barrier.h>
#include <kernel/timer.h>
#include <kernel/virt_phys.h>
#include <kernel/vmspace.h>
#include <kernel/ipc_endpoint.h>
#include <kernel/ipc_types.h>
#include <kernel/hal_irq.h>
#include <kernel/smp.h>
#include <kernel/ipi.h>
#include <kernel/syscall.h>
#include <kernel/mmu.h>
#include <kernel/irq.h>
#include <kernel/capability.h>
#include <kernel/cspace.h>
#include <kernel/driver.h>
#include <kernel/elf.h>
#include <kernel/process.h>
#include <kernel/mm/kmalloc.h>
#include <kernel/mm/slab.h>
#include <kernel/phys_mem.h>
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
 * @brief 无符号十进制输出辅助函数
 * @param value 要输出的无符号整数
 */
static void uart_print_uint(uint64_t value);

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
    uint32_t from_el0;

    elr = *elr_ptr;

    /* 提取 EC（异常类别）和 ISS（指令特定症状） */
    ec = (uint32_t)((esr >> 26U) & 0x3FU);
    iss = (uint32_t)(esr & 0x01FFFFFFU);

    /* 判断异常来源：SPSR_EL1.M[3:0] == 0 表示来自 EL0 */
    from_el0 = (((uint32_t)spsr) & 0xFU) == 0U ? 1U : 0U;

    /* 异常诊断（精简版：单行输出关键信息） */
    desc = get_ec_desc(ec);
    klog_error("\n[ex] EC=");
    klog_hex64((uint64_t)ec);
    klog_error(from_el0 != 0U ? " EL0 " : " EL1 ");
    klog_error(desc);
    klog_error("\n[ex] ESR=");
    klog_hex64(esr);
    klog_error(" FAR=");
    klog_hex64(far);
    klog_error(" ELR=");
    klog_hex64(elr);
    klog_putc('\n');

    /*
     * 异常分类处理：
     * - EL0 异常（缺页/对齐/非法指令等）：终止出错用户线程
     * - EL1 异常（内核 bug）：panic（打印后死循环）
     *
     * EC 分类：
     *   0x20/0x21 = Instruction Abort (lower/higher EL)
     *   0x24/0x25 = Data Abort (lower/higher EL)
     *   0x22      = PC Alignment fault
     *   0x26      = SP Alignment fault
     *   0x0E      = Illegal Execution state
     *   其他      = 未预期异常
     */
    if (from_el0 != 0U)
    {
        /* EL0 缺页处理（实时系统设计）：
         * 安全关键 RTOS 不使用 demand paging（破坏 WCET 可分析性）。
         * 所有用户内存必须在程序启动时预映射（eager mapping）。
         * 缺页 = 程序错误（越界访问/空指针），终止出错线程。
         * VMA 查找仅用于诊断（告知出错区域类型）。 */
        if ((ec == 0x20U) || (ec == 0x21U) ||
            (ec == 0x24U) || (ec == 0x25U))
        {
            vm_space_t *space = vmspace_get_current();
            if (space != NULL)
            {
                vma_t *vma = vmspace_find_vma(space, (vaddr_t)far);
                if (vma != NULL)
                {
                    /* VMA 命中但缺页：权限不符或预映射遗漏，诊断后终止 */
                    klog_error("[exception] EL0 page fault in mapped VMA region\n");
                }
                else
                {
                    /* 地址不在任何 VMA 范围：非法访问 */
                    klog_error("[exception] EL0 illegal address access\n");
                }
            }
        }

        /* EL0 异常：终止用户线程 */
        klog_error("[exception] EL0 fault → terminating user thread\n");
        kthread_exit();
        /* kthread_exit 不返回，但保险起见设置 elr */
        *elr_ptr = elr + 4U;
    }
    else
    {
        /* EL1 异常：内核 panic */
        klog_error("[exception] EL1 fault → KERNEL PANIC\n");
        klog_error("[exception] SPSR=0x");
        klog_hex64(spsr);
        klog_error("\n");

        /* 死循环（panic） */
        for (;;)
        {
            __asm__ volatile("wfe" ::: "memory");
        }
    }
}

/**
 * @brief EL1 IRQ 中断处理函数
 *
 * @details 从 GIC 获取当前最高优先级挂起中断号，
 *          根据中断号分发到对应处理函数：
 *          - SGI 0-15（核间中断）→ ipi_handler
 *          - PPI 30（ARM 通用定时器）→ timer_interrupt_handler
 *          - 其他中断 → irq_dispatch（中断路由子系统）
 *          最后调用 GIC EOI 结束中断。
 *
 * @note 对应需求: IN-001~006（中断控制器管理）
 */
void irq_handler(void)
{
    uint32_t irq;

    /* 从中断控制器获取当前最高优先级挂起中断号 */
    irq = hal_irq_acknowledge();

    /* 检查是否为伪中断 */
    if (hal_irq_is_spurious(irq))
    {
        return;
    }

    /* 根据中断类型分发处理 */
    if (hal_irq_is_sgi(irq))
    {
        /* SGI（软件生成中断 0-15）：核间中断 */
        ipi_handler(irq);
    }
    else if (irq == QEMU_TIMER_IRQ)
    {
        /* ARM 通用定时器物理定时器中断（PPI 30） */
        timer_interrupt_handler();
    }
    else
    {
        /* 其他中断：路由到中断分发子系统 */
        irq_dispatch(irq);
    }

    /* 通知中断控制器处理完成（EOI） */
    hal_irq_eoi(irq);

    /* 中断返回前检查重调度标志。
     * scheduler_tick（经由 sched_class_tick）在中断中仅置位 need_resched，
     * 此处（中断已处理完成、即将返回）执行实际调度，
     * 避免 context_switch 在 IRQ 处理中途切栈导致栈混淆。 */
    scheduler_irq_exit_check();
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
    klog_error("\n[ex] SError ESR=");
    klog_hex64(esr);
    klog_error(" FAR=");
    klog_hex64(far);
    klog_error(" ELR=");
    klog_hex64(elr);
    klog_putc('\n');

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
 * @brief 无符号十进制输出辅助函数
 *
 * @details 使用迭代方式逐位提取并输出，避免本地数组和递归。
 *          先找到最高位的位置，然后从高到低依次输出。
 *          通过 klog_putc 输出（内部已固定 UART 基地址）。
 *
 * @param value 要输出的 64 位无符号整数
 */
static void uart_print_uint(uint64_t value)
{
    /* 特殊处理 0 */
    if (value == 0ULL)
    {
        klog_putc('0');
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
        klog_putc((char)('0' + (int32_t)digit));
        divisor /= 10ULL;
    }
}

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
    kernel_status_t ret;

    /* ---- 第一步：升级 MMU 页表为精细映射 ----
     * boot.S 已用粗粒度页表（1GB 块）开启 MMU 并重定位到高地址 VA。
     * 此时 kernel_main 已在高地址 VA 运行，可正常访问所有符号。
     * mmu_early_init 重建 4KB 精细权限页表（RX/R--/RW-）并切换 TTBR0 为空。
     */
    mmu_early_init();

    /* ---- 第二步：初始化 UART 早期输出（MMU 已开，高地址可访问）---- */
    klog_init();
    klog_info(g_banner);
    klog_info("[k] MMU ok\n");

    /* ---- 第五步：初始化中断控制器（HAL）---- */
    klog_info("[k] INTC init\n");
    hal_irq_init();

    /* 初始化中断子系统（中断描述符表清零 + 标志置位） */
    {
        extern kernel_status_t irq_subsys_init(void);
        (void)irq_subsys_init();
    }

    /* ---- 第六步：初始化定时器 ---- */
    klog_info("[k] Timer init\n");
    timer_init();

    /* 配置定时器 PPI 中断（IRQ 30）的优先级和路由 */
    hal_irq_set_priority(QEMU_TIMER_IRQ, (uint8_t)0xA0U);
    /* 注意：PPI 路由为当前 CPU，不需要设置 ITARGETSR */
    /* 使能定时器 PPI 中断（IRQ 30） */
    hal_irq_enable(QEMU_TIMER_IRQ);

    /* ---- 初始化内核堆分配器（kmalloc）---- */
    klog_info("[k] kmalloc init\n");
    ret = kmalloc_init();
    if (ret != 0)
    {
        klog_warn("[k] WARN: kmalloc init fail\n");
    }

    /* ---- 初始化物理内存分配器（buddy system，用于用户态页分配）---- */
    {
        extern char __kernel_end[];
        /* 管理内核镜像之后的 32MB 物理内存。
         * 该区域由 phys_mem（buddy）统一管理，既服务用户态页分配，
         * 也作为 slab（对象缓存）的底层页源；kmalloc 仍使用独立的
         * __heap_start 堆服务杂项小对象分配。
         *   层级：phys_mem(buddy) → slab → kmalloc
         * __kernel_end 是高地址 VMA 符号，需减去 KERNEL_VA_OFFSET 得到物理地址。
         * 32MB = 8192 页，与 phys_mem.c 的 MAX_PHYS_PAGES 上限一致。 */
        paddr_t pmem_base = (paddr_t)(((uintptr_t)__kernel_end - KERNEL_VA_OFFSET + 0xFFFU) & ~0xFFFU);
        uint64_t pmem_size = 32U * 1024U * 1024U;  /* 32MB */
        klog_info("[k] phys_mem init\n");
        ret = phys_mem_init(pmem_base, pmem_size);
        klog_info("[k] phys_mem init ret=");
        uart_print_uint((uint64_t)ret);
        klog_info(" base=0x");
        klog_hex64((uint64_t)pmem_base);
        klog_putc('\n');

        /* 测试 alloc */
        {
            extern paddr_t phys_mem_alloc_page(void);
            paddr_t test_pa = phys_mem_alloc_page();
            klog_info("[k] alloc_page=0x");
            klog_hex64((uint64_t)test_pa);
            klog_putc('\n');
        }
    }

    /* ---- 第七步：初始化调度器 ---- */
    klog_info("[k] Sched init\n");
    ret = scheduler_init();
    if (ret != KERNEL_OK)
    {
        klog_error("[k] FATAL: sched fail\n");
        for (;;)
        {
            __asm__ volatile("wfe" ::: "memory");
        }
    }

    /* ---- 初始化 IPC 子系统 ---- */
    klog_info("[k] IPC init\n");
    ret = ipc_endpoint_subsys_init();
    if (ret != KERNEL_OK)
    {
        klog_warn("[k] WARN: IPC subsys fail\n");
    }

    /* ---- 初始化能力系统（CSpace 子系统）---- */
    klog_info("[k] Cap init\n");
    ret = cspace_subsys_init();
    if (ret != KERNEL_OK)
    {
        klog_warn("[k] WARN: cap subsys fail\n");
    }

    /* ---- 初始化 IPC 池化子系统（notification 池 + channel 池）----
     * 注意：必须在 cspace_subsys_init() 之后调用，因为 channel/notification
     * 对象注册依赖已就绪的内核对象表（kobject 全局表由 cspace 子系统初始化时连带建立）。
     * 这里仅完成池预分配，不依赖调度器。 */
    {
        extern kernel_status_t ipc_notification_subsys_init(void);
        extern kernel_status_t ipc_channel_subsys_init(void);

        klog_info("[k] IPC notification pool init\n");
        ret = ipc_notification_subsys_init();
        if (ret != KERNEL_OK)
        {
            klog_warn("[k] WARN: IPC notification subsys fail\n");
        }

        klog_info("[k] IPC channel pool init\n");
        ret = ipc_channel_subsys_init();
        if (ret != KERNEL_OK)
        {
            klog_warn("[k] WARN: IPC channel subsys fail\n");
        }
    }

    /* ---- 初始化虚拟地址空间子系统（VMA 管理）---- */
    {
        extern kernel_status_t vmspace_subsys_init(void);
        ret = vmspace_subsys_init();
        if (ret != KERNEL_OK)
        {
            klog_warn("[k] WARN: vmspace subsys fail\n");
        }
    }

    /* ---- 第八步：初始化 SMP 多核 ---- */
    klog_info("[k] SMP init\n");

    /* ---- 驱动框架初始化 ---- */
    klog_info("[k] Driver framework init\n");
    ret = driver_subsys_init();
    if (ret != KERNEL_OK)
    {
        klog_warn("[k] WARN: driver subsys fail\n");
    }
    /* ---- 注册平台设备 ---- */
    {
        /* PL011 UART @ 0x09000000, IRQ 33 */
        (void)device_register("pl011", DRIVER_TYPE_UART,
                              (paddr_t)QEMU_UART0_BASE, 0x1000ULL, 33U, NULL);

        /* VirtIO Block 由引导读取器管理，不再注册到驱动框架 */
        /* (void)device_register("virtio,blk", ...); */

        /* 执行设备探测 */
        ret = device_probe_all();

        /* 打印驱动统计 */
        {
            driver_stats_t stats;
            driver_get_stats(&stats);
            klog_info("[k] drivers: ");
            uart_print_uint((uint64_t)stats.total_drivers);
            klog_info(", devices: ");
            uart_print_uint((uint64_t)stats.total_devices);
            klog_info(", probed: ");
            uart_print_uint((uint64_t)stats.probe_count);
            klog_putc('\n');
        }
        (void)ret;
    }

    /* ---- VirtIO MMIO 设备扫描（由引导读取器 boot_blk 接管）---- */

    ret = smp_init();
    if (ret != KERNEL_OK)
    {
        klog_warn("[k] WARN: SMP fail\n");
    }

    ret = smp_boot_secondary();

    /* 初始化 SMP 调度器（负载均衡） */
    ret = smp_sched_init();
    if (ret != KERNEL_OK)
    {
        klog_warn("[k] WARN: smp_sched fail\n");
    }

    klog_info("[k] All inited\n");

    /* ---- 加载 init 进程（第一个用户态进程）---- */
    {
        extern kernel_status_t process_subsys_init(void);
        extern void ramfs_init(void);
        extern kernel_status_t elf_load_and_run(const uint8_t *elf_data,
                                                 uint32_t elf_size,
                                                 const char *thread_name);

        /* 初始化内核 RAMFS（用户态文件操作直通） */
        ramfs_init();
        klog_info("[k] RAMFS inited\n");

        /* 嵌入 hello_start ELF（最小用户态冒烟测试）
         * 后续替换为从磁盘加载 init.elf */
        extern const uint8_t _init_elf_start[];
        extern const uint8_t _init_elf_end[];
        uint32_t init_size = (uint32_t)((uintptr_t)_init_elf_end - (uintptr_t)_init_elf_start);

        (void)process_subsys_init();
        klog_info("[k] process subsystem inited\n");

        /* 加载 hello_start ELF 到用户空间并创建 EL0 线程 */
        if (init_size > 0U)
        {
            klog_info("[k] Loading init ELF, size=");
            klog_dec(init_size);
            klog_putc('\n');

            if (elf_load_and_run(_init_elf_start, init_size, "init") != KERNEL_OK)
            {
                klog_error("[k] FATAL: init ELF load failed\n");
            }
        }
        else
        {
            klog_warn("[k] WARN: init ELF not found\n");
        }
    }

    /* ---- 启动调度器（永不返回）----
     * scheduler_start 会从就绪队列取第一个线程切入。
     * init 线程优先级 > idle，会被优先调度执行。
     * scheduler_start 永不返回，启动阶段日志需在此之前 flush。 */
    klog_flush();
    scheduler_start();

    /* 永不到达 */
    for (;;)
    {
        __asm__ volatile("wfe" ::: "memory");
    }
}
