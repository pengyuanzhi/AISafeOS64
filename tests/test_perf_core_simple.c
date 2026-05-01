/**
 * @file    test_perf_core_simple.c
 * @brief   核心模块综合性能测试（简化版）
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

/* ========================================================================
 * 时间测量辅助函数
 * ======================================================================== */

static uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t ns_to_us(uint64_t ns)
{
    return ns / 1000ULL;
}

/* ========================================================================
 * 上下文切换性能测试
 * ======================================================================== */

static void benchmark_context_switch(void)
{
    uint64_t total = 0ULL;
    uint32_t i;

    printf("\n========== 上下文切换性能测试 ==========\n");

    for (i = 0U; i < 1000000U; i++)
    {
        uint64_t start = get_time_ns();

        /* 模拟保存/恢复 callee-saved 寄存器 */
        uint64_t x19 = (uint64_t)i;
        uint64_t x20 = i + 1;
        uint64_t x21 = i + 2;
        uint64_t x22 = i + 3;
        uint64_t x23 = i + 4;
        uint64_t x24 = i + 5;
        uint64_t x25 = i + 6;
        uint64_t x26 = i + 7;
        uint64_t x27 = i + 8;
        uint64_t x28 = i + 9;

        /* 模拟计算 */
        volatile uint64_t tmp = i * 123456789ULL;
        tmp = tmp / 12345ULL;
        tmp = tmp + i * 987654321ULL;
        tmp = tmp / 98765ULL;
        (void)tmp;

        /* 模拟恢复 callee-saved 寄存器 */
        i = (uint32_t)x19;
        x19 = (i + 1) % 10000;
        x20 = (i + 2) % 10000;
        x21 = (i + 3) % 10000;
        x22 = (i + 4) % 10000;
        x23 = (i + 5) % 10000;
        x24 = (i + 6) % 10000;
        x25 = (i + 7) % 10000;
        x26 = (i + 8) % 10000;
        x27 = (i + 9) % 10000;
        x28 = (i + 10) % 10000;
        (void)x19;
        (void)x20;
        (void)x21;
        (void)x22;
        (void)x23;
        (void)x24;
        (void)x25;
        (void)x26;
        (void)x27;
        (void)x28;

        uint64_t end = get_time_ns();
        total += (end - start);
    }

    uint64_t avg_us = ns_to_us(total / 1000000U);

    printf("  迭代次数: 1000000\n");
    printf("  平均上下文切换时间: %llu us\n", avg_us);
    printf("  吞吐量: %.2f 百万次/秒\n",
           1000000.0 * 1000000.0 / (double)(total / 1000ULL));
}

/* ========================================================================
 * IPC 性能测试
 * ======================================================================== */

typedef struct
{
    uint64_t src_id;
    uint64_t dst_id;
    uint64_t msg_id;
    uint8_t  data[64];
    uint32_t data_len;
} ipc_msg_t;

typedef struct
{
    atomic_uint next_msg_id;
    ipc_msg_t messages[256];
    atomic_uint msg_count;
} ipc_channel_t;

static void ipc_channel_init(ipc_channel_t *ch)
{
    atomic_init(&ch->next_msg_id, 0U);
    atomic_init(&ch->msg_count, 0U);
    memset(ch->messages, 0, sizeof(ch->messages));
}

static void ipc_send(ipc_channel_t *ch, uint64_t src_id, uint64_t dst_id, const uint8_t *data, uint32_t len)
{
    uint32_t msg_id = atomic_fetch_add_explicit(&ch->next_msg_id, 1, memory_order_relaxed);
    ipc_msg_t *msg = &ch->messages[msg_id % 256];

    msg->src_id = src_id;
    msg->dst_id = dst_id;
    msg->msg_id = msg_id;
    msg->data_len = len;
    memcpy(msg->data, data, len);
}

static ipc_msg_t *ipc_recv(ipc_channel_t *ch, uint32_t timeout_ms)
{
    uint32_t count = atomic_load_explicit(&ch->msg_count, memory_order_relaxed);
    if (count == 0U)
    {
        return NULL;
    }

    uint32_t msg_id = atomic_fetch_sub_explicit(&ch->msg_count, 1, memory_order_relaxed) - 1;
    return &ch->messages[msg_id % 256];
}

static void benchmark_ipc(void)
{
    ipc_channel_t ch;
    ipc_channel_init(&ch);

    uint8_t test_data[64];
    memset(test_data, 0xAB, sizeof(test_data));

    uint64_t total = 0ULL;
    uint32_t i;

    printf("\n========== IPC 性能测试 ==========\n");

    /* 测试发送性能 */
    for (i = 0U; i < 1000000U; i++)
    {
        uint64_t start = get_time_ns();

        ipc_send(&ch, i, i + 1, test_data, sizeof(test_data));

        uint64_t end = get_time_ns();
        total += (end - start);
    }

    uint64_t send_avg_us = ns_to_us(total / 1000000U);

    printf("  发送次数: 1000000\n");
    printf("  平均发送时间: %llu us\n", send_avg_us);
    printf("  吞吐量: %.2f 百万次/秒\n",
           1000000.0 * 1000000.0 / (double)(total / 1000ULL));

    /* 测试接收性能 */
    total = 0ULL;
    for (i = 0U; i < 1000000U; i++)
    {
        uint64_t start = get_time_ns();

        volatile ipc_msg_t *msg = ipc_recv(&ch, 1000);
        (void)msg;

        uint64_t end = get_time_ns();
        total += (end - start);
    }

    uint64_t recv_avg_us = ns_to_us(total / 1000000U);

    printf("  平均接收时间: %llu us\n", recv_avg_us);
    printf("  吞吐量: %.2f 百万次/秒\n",
           1000000.0 * 1000000.0 / (double)(total / 1000ULL));
}

/* ========================================================================
 * 调度器性能测试
 * ======================================================================== */

typedef enum
{
    PRIORITY_HIGH = 0,
    PRIORITY_NORMAL = 64,
    PRIORITY_LOW = 128,
    PRIORITY_COUNT
} priority_t;

typedef struct
{
    atomic_uint last_run;
    atomic_uint run_count;
} thread_t;

static void benchmark_scheduler(void)
{
    thread_t threads[PRIORITY_COUNT];
    uint64_t total = 0ULL;
    uint32_t i;
    uint32_t j;

    printf("\n========== 调度器性能测试 ==========\n");

    /* 初始化线程 */
    for (i = 0; i < PRIORITY_COUNT; i++)
    {
        atomic_init(&threads[i].last_run, 0U);
        atomic_init(&threads[i].run_count, 0U);
    }

    /* 模拟调度 */
    for (i = 0U; i < 1000000U; i++)
    {
        uint64_t start = get_time_ns();

        for (j = 0; j < PRIORITY_COUNT; j++)
        {
            thread_t *thread = &threads[j];

            /* 模拟线程运行时间 */
            volatile uint64_t tmp = j * 123456789ULL;
            tmp = tmp / 12345ULL;
            (void)tmp;

            atomic_fetch_add_explicit(&thread->run_count, 1, memory_order_relaxed);
            atomic_store_explicit(&thread->last_run, i, memory_order_relaxed);
        }

        uint64_t end = get_time_ns();
        total += (end - start);
    }

    uint64_t avg_us = ns_to_us(total / 1000000U);

    printf("  调度次数: 1000000\n");
    printf("  平均调度时间: %llu us\n", avg_us);
    printf("  吞吐量: %.2f 百万次/秒\n",
           1000000.0 * 1000000.0 / (double)(total / 1000ULL));

    /* 统计每个线程的运行次数 */
    printf("  各线程运行次数:\n");
    for (i = 0; i < PRIORITY_COUNT; i++)
    {
        uint32_t count = atomic_load_explicit(&threads[i].run_count, memory_order_relaxed);
        printf("    优先级 %d: %u 次\n", i, count);
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("========================================\n");
    printf("AISafe64 - 核心模块综合性能测试（简化版）\n");
    printf("========================================\n");

    benchmark_context_switch();
    benchmark_ipc();
    benchmark_scheduler();

    printf("\n========================================\n");
    printf("性能测试完成\n");
    printf("========================================\n");

    return 0;
}
