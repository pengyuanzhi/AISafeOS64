/**
 * @file    benchmark.c
 * @brief   内核性能基准测试
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 1.0
 *
 * @details 简单的内核性能基准测试：
 *          - IPC 延迟测量（通过定时器计数器）
 *          - 调度器切换延迟
 *          - 中断响应延迟
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/timer.h>
#include <kernel/config.h>
#include <stdint.h>
#include "scheduler.h"
#include "thread.h"
#include "hal.h"

/* ========================================================================
 * ARM64 计数器访问
 * ======================================================================== */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

/** @brief 读取物理计数器 */
static inline uint64_t bench_get_cycle(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(val));
    return val;
}

/** @brief 读取计数器频率 */
static inline uint64_t bench_get_freq(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

#pragma GCC diagnostic pop

/* ========================================================================
 * UART 输出辅助
 * ======================================================================== */

extern void hal_uart_puts(uint64_t base, const char *str);
extern void hal_uart_putc(uint64_t base, char ch);

/** @brief QEMU UART0 基地址 */
#define BENCH_UART_BASE  ((uint64_t)0x09000000UL)

/**
 * @brief 输出无符号十进制数
 */
static void bench_print_uint(uint64_t value)
{
    if (value == 0ULL)
    {
        hal_uart_putc(BENCH_UART_BASE, '0');
        return;
    }

    uint64_t divisor = 1ULL;
    while ((value / divisor) >= 10ULL)
    {
        divisor *= 10ULL;
    }

    while (divisor > 0ULL)
    {
        uint8_t digit = (uint8_t)((value / divisor) % 10ULL);
        hal_uart_putc(BENCH_UART_BASE, (char)('0' + (int32_t)digit));
        divisor /= 10ULL;
    }
}

/* ========================================================================
 * 基准测试 1：调度器上下文切换延迟
 * ======================================================================== */

/**
 * @brief 上下文切换延迟测试线程
 *
 * @details 两个线程交替执行，测量每次切换的周期数
 */
static volatile uint64_t s_switch_cycles;
static volatile uint32_t s_switch_count;
static volatile int32_t  s_bench_done;

static void bench_thread_x(void *arg)
{
    uint64_t start;
    (void)arg;

    while (s_bench_done == 0)
    {
        start = bench_get_cycle();
        schedule();
        s_switch_cycles += (bench_get_cycle() - start);
        s_switch_count++;
    }
}

static void bench_thread_y(void *arg)
{
    (void)arg;

    while (s_bench_done == 0)
    {
        schedule();
    }
}

/* ========================================================================
 * 基准测试 2：定时器中断延迟
 * ======================================================================== */

/**
 * @brief 测量从定时器到期到 IRQ handler 入口的延迟
 *
 * @details 设置一个即将触发的比较值，
 *          然后在 timer_interrupt_handler 中测量差值
 */
static volatile uint64_t s_irq_start_cycle;
static volatile uint64_t s_irq_latency_sum;
static volatile uint32_t s_irq_latency_count;

/**
 * @brief 由 timer_interrupt_handler 调用以测量延迟
 */
void bench_record_irq_latency(uint64_t handler_entry_cycle)
{
    if (s_irq_start_cycle != 0ULL)
    {
        s_irq_latency_sum += (handler_entry_cycle - s_irq_start_cycle);
        s_irq_latency_count++;
        s_irq_start_cycle = 0ULL;
    }
}

/* ========================================================================
 * 基准测试入口
 * ======================================================================== */

/**
 * @brief 运行所有性能基准测试
 *
 * @details 执行以下测试并输出结果：
 *          1. 定时器精度（读取频率，计算 tick 周期）
 *          2. 调度器切换延迟（双线程交替切换）
 *          3. 中断响应延迟（定时器中断往返时间）
 *
 * @param arg 未使用
 */
void benchmark_run(void *arg)
{
    uint64_t freq;
    uint64_t ns_per_cycle;
    uint64_t avg;

    (void)arg;

    hal_uart_puts(BENCH_UART_BASE, "\n=== 性能基准测试 ===\n");

    freq = bench_get_freq();
    hal_uart_puts(BENCH_UART_BASE, "  计数器频率: ");
    bench_print_uint(freq / 1000000ULL);
    hal_uart_puts(BENCH_UART_BASE, " MHz\n");

    if (freq > 0ULL)
    {
        ns_per_cycle = 1000000000ULL / freq;
    }
    else
    {
        ns_per_cycle = 1ULL;
    }

    /* ---- 测试 1: 定时器精度 ---- */
    {
        uint64_t t1 = bench_get_cycle();
        uint64_t t2 = bench_get_cycle();
        uint64_t overhead = t2 - t1;

        hal_uart_puts(BENCH_UART_BASE, "  计数器读取开销: ");
        bench_print_uint(overhead * ns_per_cycle);
        hal_uart_puts(BENCH_UART_BASE, " ns\n");
    }

    /* ---- 测试 2: 中断延迟 ---- */
    {
        uint64_t saved_start = s_irq_start_cycle;
        s_irq_latency_sum = 0ULL;
        s_irq_latency_count = 0U;

        /* 设置即将触发的定时器，测量中断延迟 */
        uint64_t now = bench_get_cycle();
        s_irq_start_cycle = now;
        __asm__ volatile("msr cntp_cval_el0, %0" :: "r"(now + (freq / 100ULL)));
        __asm__ volatile("isb");

        /* 等待中断触发 */
        (void)kthread_sleep(2U);

        if (s_irq_latency_count > 0U)
        {
            avg = s_irq_latency_sum / (uint64_t)s_irq_latency_count;
            hal_uart_puts(BENCH_UART_BASE, "  IRQ 平均延迟: ");
            bench_print_uint(avg * ns_per_cycle);
            hal_uart_puts(BENCH_UART_BASE, " ns (");
            bench_print_uint(avg);
            hal_uart_puts(BENCH_UART_BASE, " cycles)\n");
        }
        else
        {
            hal_uart_puts(BENCH_UART_BASE, "  IRQ 延迟: 未测量到\n");
        }

        s_irq_start_cycle = saved_start;
    }

    /* ---- 测试 3: 调度器切换延迟 ---- */
    {
        thread_id_t tid_x;
        thread_id_t tid_y;

        s_switch_cycles = 0ULL;
        s_switch_count = 0U;
        s_bench_done = 0;

        tid_x = kthread_create("bench_x", bench_thread_x, NULL,
                               (priority_t)200U, KTHREAD_POLICY_RR,
                               CONFIG_STACK_SIZE_DEFAULT);
        tid_y = kthread_create("bench_y", bench_thread_y, NULL,
                               (priority_t)200U, KTHREAD_POLICY_RR,
                               CONFIG_STACK_SIZE_DEFAULT);

        if ((tid_x != THREAD_ID_INVALID) && (tid_y != THREAD_ID_INVALID))
        {
            /* 让测试线程运行一段时间 */
            (void)kthread_sleep(100U);

            s_bench_done = 1;

            if (s_switch_count > 0U)
            {
                avg = s_switch_cycles / (uint64_t)s_switch_count;
                hal_uart_puts(BENCH_UART_BASE, "  上下文切换平均: ");
                bench_print_uint(avg * ns_per_cycle);
                hal_uart_puts(BENCH_UART_BASE, " ns (");
                bench_print_uint(avg);
                hal_uart_puts(BENCH_UART_BASE, " cycles, ");
                bench_print_uint((uint64_t)s_switch_count);
                hal_uart_puts(BENCH_UART_BASE, " samples)\n");
            }
        }
        else
        {
            hal_uart_puts(BENCH_UART_BASE, "  上下文切换: 线程创建失败\n");
        }
    }

    hal_uart_puts(BENCH_UART_BASE, "=== 基准测试完成 ===\n\n");

    /* 测试完成，线程退出 */
    kthread_exit();
}
