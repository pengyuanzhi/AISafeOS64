/**
 * @file    smp_boot.c
 * @brief   SMP 多核启动实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 实现 SMP 多核启动和每 CPU 数据管理：
 *          - 主核初始化
 *          - PSCI 启动从核
 *          - 每 CPU 数据区管理
 *          - CPU 状态管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: MP-001~005
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/smp.h>
#include <kernel/config.h>
#include <kernel/barrier.h>
#include <kernel/spinlock.h>
#include <kernel/errno.h>
#include <kernel/gic.h>
#include <kernel/config.h>
#include <kernel/interrupt.h>
#include <kernel/virt_phys.h>
#include <hal.h>
#include <stdint.h>
#include <kernel/mmu.h>
#include "../../sched/scheduler.h"
/* boot.S 中定义的从核汇编入口 */
extern void secondary_entry(void);

/* ========================================================================
 * 每 CPU 数据区
 * ======================================================================== */

static percpu_t s_percpu_data[CONFIG_MAX_CPUS]
    __attribute__((aligned(64U)));

static volatile uint32_t s_cpu_online_mask;
static volatile uint32_t s_cpu_online_count;
static TicketLock_t s_smp_lock;
static volatile uint32_t s_secondary_ready[CONFIG_MAX_CPUS];

/* ========================================================================
 * PSCI 调用
 * ======================================================================== */

#define PSCI_FN_CPU_ON     0xC4000003U

static int32_t psci_cpu_on(uint64_t cpu_id, uint64_t entry_point,
                             uint64_t context_id)
{
    int32_t ret;

    __asm__ volatile(
        "mov x0, %[fid]\n"
        "mov x1, %[cpuid]\n"
        "mov x2, %[entry]\n"
        "mov x3, %[ctx]\n"
        "hvc #0\n"
        "mov %[ret], x0\n"
        : [ret] "=r"(ret)
        : [fid] "r"((uint64_t)PSCI_FN_CPU_ON),
          [cpuid] "r"(cpu_id),
          [entry] "r"(entry_point),
          [ctx] "r"(context_id)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
          "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
          "x16", "x17", "x30", "memory"
    );

    return ret;
}

/* ========================================================================
 * 代码段缓存清理（PSCI 唤醒从核前置操作）
 * ======================================================================== */

/**
 * @brief 清理代码段缓存，确保从核取指到最新指令
 *
 * @param start 起始虚拟地址
 * @param size  字节数
 *
 * @details 主核 MMU/缓存已开启时，代码写入可能停留在主核 L1 缓存中。
 *          从核 PSCI 唤醒后 MMU 关闭、缓存冷启，从物理内存取指时可能
 *          取到未更新的数据（全 0），导致 PC 漂移到地址 0 区域崩溃。
 *          通过 dc cvau（数据清理到统一点）+ ic ivau（指令缓存失效）
 *          + dsb ish 保证代码对从核可见。
 */
static void flush_code_cache(uint64_t start, uint64_t size)
{
    uint64_t addr;
    uint64_t end = start + size;

    /* 64 字节对齐（典型 ARM64 cacheline） */
    start &= ~0x3FULL;
    end = (end + 0x3Full) & ~0x3Full;

    /* 逐 cacheline 清理数据缓存到统一点 */
    for (addr = start; addr < end; addr += 64ULL)
    {
        __asm__ volatile("dc cvau, %0" :: "r"(addr) : "memory");
    }

    /* 数据屏障：等待清理完成（Inner Shareable） */
    __asm__ volatile("dsb ish" ::: "memory");

    /* 失效指令缓存 */
    for (addr = start; addr < end; addr += 64ULL)
    {
        __asm__ volatile("ic ivau, %0" :: "r"(addr) : "memory");
    }

    /* 指令屏障 */
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

/* smp_secondary_entry 在 smp.h 中声明，此处仅需前向引用其地址 */
/* （函数名即地址，用于确定从核代码段终点） */

/* ========================================================================
 * SMP 初始化
 * ======================================================================== */

kernel_status_t smp_init(void)
{
    uint32_t i;

    /* 逐字段清零（避免编译器生成 memset 调用） */
    for (i = 0U; i < (uint32_t)CONFIG_MAX_CPUS; i++)
    {
        volatile uint64_t *ptr = (volatile uint64_t *)&s_percpu_data[i];
        uint32_t j;
        for (j = 0U; j < sizeof(percpu_t) / sizeof(uint64_t); j++)
        {
            ptr[j] = 0ULL;
        }
    }

    s_percpu_data[0U].cpu_id = 0U;
    s_percpu_data[0U].state = CPU_STATE_RUNNING;
    s_percpu_data[0U].irq_depth = 0U;
    s_percpu_data[0U].preempt_count = 0U;
    s_percpu_data[0U].current_thread = NULL;
    s_percpu_data[0U].idle_thread = NULL;
    s_percpu_data[0U].stack_base = NULL;

    s_cpu_online_mask = 1U;
    s_cpu_online_count = 1U;

    for (i = 0U; i < CONFIG_MAX_CPUS; i++)
    {
        s_secondary_ready[i] = 0U;
    }

    ticket_lock_init(&s_smp_lock);

    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 启动从核
 * ======================================================================== */

kernel_status_t smp_boot_secondary(void)
{
    uint32_t cpu_id;
    kernel_status_t ret = KERNEL_OK;

    ticket_lock_acquire(&s_smp_lock);

    /*
     * 唤醒从核前，清理整个内核代码段的缓存。
     *
     * 从核被 PSCI 唤醒时 MMU 关闭，必须从物理内存取到最新指令。
     * 从核执行路径不仅包括 secondary_entry，还包括调度器代码、
     * cpu_switch_to_first_task、thread_entry_trampoline、idle_task_entry 等，
     * 因此需要覆盖整个 .text 段（secondary_entry → __text_end）。
     */
    {
        extern char __text_end[];  /* 链接脚本定义的 .text 段结束符号 */
        uint64_t code_start = (uint64_t)(uintptr_t)&secondary_entry;
        uint64_t code_end   = (uint64_t)(uintptr_t)__text_end;
        flush_code_cache(code_start, code_end - code_start);
    }

    for (cpu_id = 1U; cpu_id < CONFIG_MAX_CPUS; cpu_id++)
    {
        int32_t psci_ret;

        if ((s_cpu_online_mask & (1U << cpu_id)) != 0U)
        {
            continue;
        }

        s_percpu_data[cpu_id].state = CPU_STATE_BOOTING;
        barrier();

        /* PSCI CPU_ON 的 entry_point 必须是物理地址（从核启动时 MMU 关闭） */
        psci_ret = psci_cpu_on(
            (uint64_t)cpu_id,
            (uint64_t)virt_to_phys(&secondary_entry),
            (uint64_t)cpu_id
        );

        if (psci_ret == 0)
        {
            uint32_t timeout;
            for (timeout = 0U; timeout < 1000000U; timeout++)
            {
                barrier();
                if (s_secondary_ready[cpu_id] != 0U)
                {
                    break;
                }
            }

            if (s_secondary_ready[cpu_id] != 0U)
            {
                s_cpu_online_mask |= (1U << cpu_id);
                s_cpu_online_count++;
            }
            else
            {
                s_percpu_data[cpu_id].state = CPU_STATE_OFFLINE;
                ret = -(int32_t)ETIMEDOUT;
            }
        }
        else
        {
            s_percpu_data[cpu_id].state = CPU_STATE_OFFLINE;
        }
    }

    ticket_lock_release(&s_smp_lock);

    return ret;
}

/* ========================================================================
 * 停止 CPU
 * ======================================================================== */

kernel_status_t smp_cpu_stop(uint32_t cpu_id)
{
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return -(int32_t)EINVAL;
    }

    if (cpu_id == 0U)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&s_smp_lock);

    s_percpu_data[cpu_id].state = CPU_STATE_STOPPING;
    barrier();

    s_cpu_online_mask &= ~(1U << cpu_id);
    if (s_cpu_online_count > 0U)
    {
        s_cpu_online_count--;
    }

    ticket_lock_release(&s_smp_lock);

    return KERNEL_OK;
}

/* ========================================================================
 * 每 CPU 数据访问
 * ======================================================================== */

uint32_t smp_get_cpu_id(void)
{
    uint64_t mpidr;

    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));

    return (uint32_t)(mpidr & 0xFFU);
}

percpu_t *smp_get_percpu(void)
{
    uint32_t cpu_id = smp_get_cpu_id();

    if (cpu_id < CONFIG_MAX_CPUS)
    {
        return &s_percpu_data[cpu_id];
    }

    return &s_percpu_data[0U];
}

percpu_t *smp_get_percpu_by_id(uint32_t cpu_id)
{
    if (cpu_id < CONFIG_MAX_CPUS)
    {
        return &s_percpu_data[cpu_id];
    }

    return NULL;
}

uint32_t smp_get_online_count(void)
{
    return s_cpu_online_count;
}

bool smp_cpu_online(uint32_t cpu_id)
{
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return false;
    }

    return ((s_cpu_online_mask & (1U << cpu_id)) != 0U);
}

/* ========================================================================
 * 从核入口
 * ======================================================================== */

void smp_secondary_entry(uint32_t cpu_id)
{
    percpu_t *percpu;

    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return;
    }

    /* 初始化 MMU（加载与主核相同的页表） */
    mmu_init_secondary();

    /* 使能 FP/SIMD（CPACR_EL1_EL1FPEN = 0b11） */
    /* 注意：HAL 无 FP 接口，保留内联汇编 */
    __asm__ volatile(
        "mrs x0, cpacr_el1\n"
        "orr x0, x0, #0x300000\n"
        "msr cpacr_el1, x0\n"
        "isb\n"
        ::: "x0", "memory"
    );

    /* 初始化 GIC CPU interface */
    (void)gic_init_secondary();

    /* 使能定时器 PPI 中断（IRQ 30） */
    (void)gic_set_priority(30U, (uint8_t)0xA0U);
    (void)gic_enable_irq(30U);

    /* 初始化从核定时器（使用 HAL 接口） */
    {
        uint64_t current_val = 0ULL;
        uint64_t freq = 0ULL;
        uint64_t delta = 0ULL;

        /* 禁用物理定时器（清除 ISTATUS） */
        hal_timer_set_control(0U);

        /* 读取当前计数器和频率 */
        current_val = hal_timer_get_count();
        freq = hal_timer_get_freq();

        if (freq == 0ULL)
        {
            freq = 24000000ULL;
        }

        /* 设置比较值 */
        delta = freq / (uint64_t)CONFIG_TICK_RATE_HZ;
        hal_timer_set_compare(current_val + delta);

        /* 使能定时器（ENABLE=1, IMASK=0） */
        hal_timer_set_control(1U);
    }

    /* 初始化中断路由子系统（从核也需要） */
    (void)interrupt_subsys_init();

    /* 初始化 percpu 数据 */
    percpu = &s_percpu_data[cpu_id];
    percpu->cpu_id = cpu_id;
    percpu->state = CPU_STATE_RUNNING;
    percpu->irq_depth = 0U;
    percpu->preempt_count = 0U;
    percpu->current_thread = NULL;
    percpu->idle_thread = NULL;

    /* 标记就绪 */
    s_secondary_ready[cpu_id] = 1U;
    barrier();

    /* 从核进入调度循环。
     * 注意：不在此处使能中断（hal_irq_enable）。
     * cpu_switch_to_first_task 的 eret 会从 SPSR=0x5（EL1h, IRQ 使能）恢复，
     * 自动使能中断。这确保中断在切换到 idle 线程栈后才到达，
     * 避免 8KB .stacks 段栈上的中断处理溢出。 */
    scheduler_start_secondary();

    /* 永不到达 */
    for (;;)
    {
        hal_wfe();
    }
}
