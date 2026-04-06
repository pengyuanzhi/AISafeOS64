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

    for (cpu_id = 1U; cpu_id < CONFIG_MAX_CPUS; cpu_id++)
    {
        int32_t psci_ret;

        if ((s_cpu_online_mask & (1U << cpu_id)) != 0U)
        {
            continue;
        }

        s_percpu_data[cpu_id].state = CPU_STATE_BOOTING;
        barrier();

        psci_ret = psci_cpu_on(
            (uint64_t)cpu_id,
            (uint64_t)(uintptr_t)&secondary_entry,
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
    __asm__ volatile(
        "mrs x0, cpacr_el1\n"
        "orr x0, x0, #0x300000\n"
        "msr cpacr_el1, x0\n"
        "isb\n"
        ::: "x0", "memory"
    );

    /* 初始化 GIC CPU interface */
    (void)gic_init_secondary();

    /* 初始化从核定时器 */
    {
        uint64_t current_val = 0ULL;
        uint64_t freq = 0ULL;
        uint64_t delta = 0ULL;

        __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"((uint64_t)0U));
        __asm__ volatile("isb" ::: "memory");

        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(current_val));
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
        __asm__ volatile("isb" ::: "memory");

        if (freq == 0ULL)
        {
            freq = 24000000ULL;
        }

        delta = freq / (uint64_t)CONFIG_TICK_RATE_HZ;
        __asm__ volatile("msr cntp_cval_el0, %0" :: "r"(current_val + delta));
        __asm__ volatile("isb" ::: "memory");
        __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"((uint64_t)1U));
        __asm__ volatile("isb" ::: "memory");
    }

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

    /* 使能中断（IRQ） */
    __asm__ volatile("msr daifclr, #2" ::: "memory");

    /* 从核进入调度循环 */
    scheduler_start_secondary();

    /* 永不到达 */
    for (;;)
    {
        __asm__ volatile("wfe" ::: "memory");
    }
}
