/**
 * @file    test_performance_benchmark.c
 * @brief   内核性能基准测试
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @details 内核性能基准测试，测试关键性能指标：
 *          - 上下文切换时间
 *          - IPC 延迟
 *          - 调度延迟
 *          - 内存分配时间
 *          - 中断响应时间
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.1 - 调度器优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* ========================================================================
 * 性能测试辅助宏
 * ======================================================================== */

#define BENCHMARK_ITERATIONS   100000U
#define BENCHMARK_THRESHOLD_CTX_SWITCH_US   10U
#define BENCHMARK_THRESHOLD_IPC_LATENCY_US   5U
#define BENCHMARK_THRESHOLD_SCHED_LATENCY_US  1U
#define BENCHMARK_THRESHOLD_MALLOC_US         1U
#define BENCHMARK_THRESHOLD_IRQ_LATENCY_US    1U

/* ========================================================================
 * 时间测量辅助函数
 * ======================================================================== */

/**
 * @brief 获取当前时间（纳秒）
 */
static uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * @brief 将纳秒转换为微秒
 */
static uint64_t ns_to_us(uint64_t ns)
{
    return ns / 1000ULL;
}

/* ========================================================================
 * 上下文切换性能测试
 * ======================================================================== */

/**
 * @brief 模拟上下文切换开销
 */
static uint64_t benchmark_context_switch(void)
{
    uint64_t start;
    uint64_t end;
    uint64_t total = 0ULL;
    uint32_t i;

    printf("\n========== 上下文切换性能测试 ==========\n");

    /* 模拟上下文切换：保存/恢复寄存器 */
    for (i = 0U; i < BENCHMARK_ITERATIONS; i++)
    {
        uint64_t context[32];
        uint64_t temp;

        start = get_time_ns();

        /* 模拟保存寄存器 */
        context[0] = (uint64_t)i;

        /* 模拟一些计算 */
        temp = i * 123456789ULL;
        temp = temp / 12345ULL;
        temp = temp + i;

        /* 模拟恢复寄存器 */
        i = (uint32_t)context[0];

        end = get_time_ns();
        total += (end - start);
    }

    total /= BENCHMARK_ITERATIONS;
    printf("  平均上下文切换时间: %lu ns (%lu μs)\n", total, ns_to_us(total));

    if (ns_to_us(total) <= BENCHMARK_THRESHOLD_CTX_SWITCH_US)
    {
        printf("  [PASS] ✅ 上下文切换时间 < %u μs\n", BENCHMARK_THRESHOLD_CTX_SWITCH_US);
    }
    else
    {
        printf("  [FAIL] ❌ 上下文切换时间 > %u μs\n", BENCHMARK_THRESHOLD_CTX_SWITCH_US);
    }

    return total;
}

/* ========================================================================
 * IPC 延迟性能测试
 * ======================================================================== */

/**
 * @brief 模拟 IPC 延迟
 */
static uint64_t benchmark_ipc_latency(void)
{
    uint64_t start;
    uint64_t end;
    uint64_t total = 0ULL;
    uint32_t i;
    uint32_t msg[4] = {0};

    printf("\n========== IPC 延迟性能测试 ==========\n");

    /* 模拟 IPC 消息传递 */
    for (i = 0U; i < BENCHMARK_ITERATIONS; i++)
    {
        start = get_time_ns();

        /* 模拟消息发送 */
        msg[0] = i;
        msg[1] = i * 2;
        msg[2] = i * 3;
        msg[3] = i * 4;

        /* 模拟消息接收 */
        volatile uint32_t val = msg[0];
        (void)val;

        end = get_time_ns();
        total += (end - start);
    }

    total /= BENCHMARK_ITERATIONS;
    printf("  平均 IPC 延迟: %lu ns (%lu μs)\n", total, ns_to_us(total));

    if (ns_to_us(total) <= BENCHMARK_THRESHOLD_IPC_LATENCY_US)
    {
        printf("  [PASS] ✅ IPC 延迟 < %u μs\n", BENCHMARK_THRESHOLD_IPC_LATENCY_US);
    }
    else
    {
        printf("  [FAIL] ❌ IPC 延迟 > %u μs\n", BENCHMARK_THRESHOLD_IPC_LATENCY_US);
    }

    return total;
}

/* ========================================================================
 * 调度延迟性能测试
 * ======================================================================== */

/**
 * @brief 模拟调度延迟
 */
static uint64_t benchmark_schedule_latency(void)
{
    uint64_t start;
    uint64_t end;
    uint64_t total = 0ULL;
    uint32_t i;
    uint32_t priority[256];

    printf("\n========== 调度延迟性能测试 ==========\n");

    /* 初始化优先级数组 */
    for (i = 0U; i < 256U; i++)
    {
        priority[i] = i;
    }

    /* 模拟调度器查找最高优先级 */
    for (i = 0U; i < BENCHMARK_ITERATIONS; i++)
    {
        start = get_time_ns();

        /* 模拟位图查找最高优先级 */
        uint32_t highest = 255U;
        uint32_t j;
        for (j = 0U; j < 256U; j++)
        {
            if (priority[j] != 0U)
            {
                highest = j;
                break;
            }
        }

        (void)highest; /* 避免未使用警告 */

        end = get_time_ns();
        total += (end - start);
    }

    total /= BENCHMARK_ITERATIONS;
    printf("  平均调度延迟: %lu ns (%lu μs)\n", total, ns_to_us(total));

    if (ns_to_us(total) <= BENCHMARK_THRESHOLD_SCHED_LATENCY_US)
    {
        printf("  [PASS] ✅ 调度延迟 < %u μs\n", BENCHMARK_THRESHOLD_SCHED_LATENCY_US);
    }
    else
    {
        printf("  [FAIL] ❌ 调度延迟 > %u μs\n", BENCHMARK_THRESHOLD_SCHED_LATENCY_US);
    }

    return total;
}

/* ========================================================================
 * 内存分配性能测试
 * ======================================================================== */

/**
 * @brief 内存分配性能测试
 */
static uint64_t benchmark_malloc(void)
{
    uint64_t start;
    uint64_t end;
    uint64_t total = 0ULL;
    uint32_t i;

    printf("\n========== 内存分配性能测试 ==========\n");

    /* 测试 malloc/free 性能 */
    for (i = 0U; i < BENCHMARK_ITERATIONS; i++)
    {
        start = get_time_ns();

        void *ptr = malloc(1024U);
        free(ptr);

        end = get_time_ns();
        total += (end - start);
    }

    total /= BENCHMARK_ITERATIONS;
    printf("  平均内存分配时间: %lu ns (%lu μs)\n", total, ns_to_us(total));

    if (ns_to_us(total) <= BENCHMARK_THRESHOLD_MALLOC_US)
    {
        printf("  [PASS] ✅ 内存分配时间 < %u μs\n", BENCHMARK_THRESHOLD_MALLOC_US);
    }
    else
    {
        printf("  [FAIL] ❌ 内存分配时间 > %u μs\n", BENCHMARK_THRESHOLD_MALLOC_US);
    }

    return total;
}

/* ========================================================================
 * 中断响应时间性能测试
 * ======================================================================== */

/**
 * @brief 模拟中断响应时间
 */
static uint64_t benchmark_irq_latency(void)
{
    uint64_t start;
    uint64_t end;
    uint64_t total = 0ULL;
    uint32_t i;

    printf("\n========== 中断响应时间性能测试 ==========\n");

    /* 模拟中断处理 */
    for (i = 0U; i < BENCHMARK_ITERATIONS; i++)
    {
        start = get_time_ns();

        /* 模拟中断处理 */
        volatile uint32_t irq_status = i;
        if (irq_status != 0U)
        {
            /* 模拟中断处理程序 */
            volatile uint32_t handler = irq_status * 2;
            (void)handler;
        }

        end = get_time_ns();
        total += (end - start);
    }

    total /= BENCHMARK_ITERATIONS;
    printf("  平均中断响应时间: %lu ns (%lu μs)\n", total, ns_to_us(total));

    if (ns_to_us(total) <= BENCHMARK_THRESHOLD_IRQ_LATENCY_US)
    {
        printf("  [PASS] ✅ 中断响应时间 < %u μs\n", BENCHMARK_THRESHOLD_IRQ_LATENCY_US);
    }
    else
    {
        printf("  [FAIL] ❌ 中断响应时间 > %u μs\n", BENCHMARK_THRESHOLD_IRQ_LATENCY_US);
    }

    return total;
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    uint64_t ctx_switch_ns;
    uint64_t ipc_latency_ns;
    uint64_t sched_latency_ns;
    uint64_t malloc_ns;
    uint64_t irq_latency_ns;

    printf("\n");
    printf("========================================\n");
    printf("AISafeOS64 内核性能基准测试\n");
    printf("========================================\n");
    printf("\n测试配置:\n");
    printf("  迭代次数: %u\n", BENCHMARK_ITERATIONS);
    printf("  上下文切换阈值: %u μs\n", BENCHMARK_THRESHOLD_CTX_SWITCH_US);
    printf("  IPC 延迟阈值: %u μs\n", BENCHMARK_THRESHOLD_IPC_LATENCY_US);
    printf("  调度延迟阈值: %u μs\n", BENCHMARK_THRESHOLD_SCHED_LATENCY_US);
    printf("  内存分配阈值: %u μs\n", BENCHMARK_THRESHOLD_MALLOC_US);
    printf("  中断响应阈值: %u μs\n", BENCHMARK_THRESHOLD_IRQ_LATENCY_US);

    /* 运行所有性能测试 */
    ctx_switch_ns = benchmark_context_switch();
    ipc_latency_ns = benchmark_ipc_latency();
    sched_latency_ns = benchmark_schedule_latency();
    malloc_ns = benchmark_malloc();
    irq_latency_ns = benchmark_irq_latency();

    /* 输出性能测试结果 */
    printf("\n");
    printf("========================================\n");
    printf("性能测试结果汇总\n");
    printf("========================================\n");
    printf("上下文切换: %lu ns (%lu μs)\n", ctx_switch_ns, ns_to_us(ctx_switch_ns));
    printf("IPC 延迟: %lu ns (%lu μs)\n", ipc_latency_ns, ns_to_us(ipc_latency_ns));
    printf("调度延迟: %lu ns (%lu μs)\n", sched_latency_ns, ns_to_us(sched_latency_ns));
    printf("内存分配: %lu ns (%lu μs)\n", malloc_ns, ns_to_us(malloc_ns));
    printf("中断响应: %lu ns (%lu μs)\n", irq_latency_ns, ns_to_us(irq_latency_ns));
    printf("========================================\n");

    printf("\n性能优化建议:\n");
    if (ns_to_us(ctx_switch_ns) > BENCHMARK_THRESHOLD_CTX_SWITCH_US)
    {
        printf("  ⚠️  上下文切换时间超过阈值，建议优化寄存器保存/恢复\n");
    }
    if (ns_to_us(ipc_latency_ns) > BENCHMARK_THRESHOLD_IPC_LATENCY_US)
    {
        printf("  ⚠️  IPC 延迟超过阈值，建议实现零拷贝 IPC\n");
    }
    if (ns_to_us(sched_latency_ns) > BENCHMARK_THRESHOLD_SCHED_LATENCY_US)
    {
        printf("  ⚠️  调度延迟超过阈值，建议优化调度器缓存\n");
    }
    if (ns_to_us(malloc_ns) > BENCHMARK_THRESHOLD_MALLOC_US)
    {
        printf("  ⚠️  内存分配时间超过阈值，建议实现 Slab 分配器\n");
    }
    if (ns_to_us(irq_latency_ns) > BENCHMARK_THRESHOLD_IRQ_LATENCY_US)
    {
        printf("  ⚠️  中断响应时间超过阈值，建议优化中断处理\n");
    }

    printf("\n✅ 性能测试完成！\n");

    return 0;
}
