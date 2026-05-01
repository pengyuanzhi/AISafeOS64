/**
 * @file    test_perf_mem_simple.c
 * @brief   内存管理优化性能测试（简化版）
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
#include <stdbool.h>

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
 * 内存分配性能测试
 * ======================================================================== */

static void benchmark_malloc(void)
{
    void *ptrs[10000];
    uint64_t total = 0ULL;
    uint32_t i;

    printf("\n========== 内存分配性能测试 ==========\n");

    /* 测试连续分配 */
    for (i = 0U; i < 10000U; i++)
    {
        uint64_t start = get_time_ns();

        ptrs[i] = malloc(256);

        uint64_t end = get_time_ns();
        total += (end - start);
    }

    uint64_t alloc_avg_us = ns_to_us(total / 10000U);

    printf("  分配次数: 10000\n");
    printf("  平均分配时间: %llu us\n", alloc_avg_us);
    printf("  吞吐量: %.2f 百万次/秒\n",
           10000.0 * 1000000.0 / (double)(total / 1000ULL));

    /* 测试释放性能 */
    total = 0ULL;
    for (i = 0U; i < 10000U; i++)
    {
        uint64_t start = get_time_ns();

        free(ptrs[i]);

        uint64_t end = get_time_ns();
        total += (end - start);
    }

    uint64_t free_avg_us = ns_to_us(total / 10000U);

    printf("  平均释放时间: %llu us\n", free_avg_us);
    printf("  吞吐量: %.2f 百万次/秒\n",
           10000.0 * 1000000.0 / (double)(total / 1000ULL));
}

/* ========================================================================
 * Ring Buffer 性能测试
 * ======================================================================== */

#define RING_BUFFER_SIZE 4096

typedef struct
{
    uint8_t buffer[RING_BUFFER_SIZE];
    atomic_uint head;
    atomic_uint tail;
} RingBuffer_t;

static void ring_buffer_init(RingBuffer_t *rb)
{
    atomic_init(&rb->head, 0U);
    atomic_init(&rb->tail, 0U);
    memset(rb->buffer, 0, RING_BUFFER_SIZE);
}

static bool ring_buffer_enqueue(RingBuffer_t *rb, const uint8_t *data, size_t len)
{
    size_t available = RING_BUFFER_SIZE - atomic_load_explicit(&rb->tail, memory_order_relaxed);
    if (available < len)
    {
        return false;
    }

    size_t head = atomic_fetch_add_explicit(&rb->head, len, memory_order_relaxed) % RING_BUFFER_SIZE;

    if (head + len <= RING_BUFFER_SIZE)
    {
        memcpy(&rb->buffer[head], data, len);
    }
    else
    {
        size_t first_part = RING_BUFFER_SIZE - head;
        memcpy(&rb->buffer[head], data, first_part);
        memcpy(&rb->buffer[0], data + first_part, len - first_part);
    }

    atomic_fetch_add_explicit(&rb->tail, len, memory_order_relaxed);
    return true;
}

static bool ring_buffer_dequeue(RingBuffer_t *rb, uint8_t *data, size_t len)
{
    size_t available = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    if (available < len)
    {
        return false;
    }

    size_t tail = atomic_fetch_add_explicit(&rb->tail, len, memory_order_relaxed) % RING_BUFFER_SIZE;

    if (tail + len <= RING_BUFFER_SIZE)
    {
        memcpy(data, &rb->buffer[tail], len);
    }
    else
    {
        size_t first_part = RING_BUFFER_SIZE - tail;
        memcpy(data, &rb->buffer[tail], first_part);
        memcpy(data + first_part, &rb->buffer[0], len - first_part);
    }

    atomic_fetch_add_explicit(&rb->tail, -len, memory_order_relaxed);
    return true;
}

static void benchmark_ring_buffer(void)
{
    RingBuffer_t rb;
    ring_buffer_init(&rb);

    uint8_t data[64];
    memset(data, 0xAB, sizeof(data));

    uint64_t total = 0ULL;
    uint32_t i;

    printf("\n========== Ring Buffer 性能测试 ==========\n");

    /* 测试入队性能 */
    for (i = 0U; i < 1000000U; i++)
    {
        uint64_t start = get_time_ns();

        ring_buffer_enqueue(&rb, data, sizeof(data));

        uint64_t end = get_time_ns();
        total += (end - start);
    }

    uint64_t enqueue_avg_us = ns_to_us(total / 1000000U);

    printf("  入队次数: 1000000\n");
    printf("  平均入队时间: %llu us\n", enqueue_avg_us);
    printf("  吞吐量: %.2f 百万次/秒\n",
           1000000.0 * 1000000.0 / (double)(total / 1000ULL));

    /* 测试出队性能 */
    total = 0ULL;
    for (i = 0U; i < 1000000U; i++)
    {
        uint64_t start = get_time_ns();

        ring_buffer_dequeue(&rb, data, sizeof(data));

        uint64_t end = get_time_ns();
        total += (end - start);
    }

    uint64_t dequeue_avg_us = ns_to_us(total / 1000000U);

    printf("  平均出队时间: %llu us\n", dequeue_avg_us);
    printf("  吞吐量: %.2f 百万次/秒\n",
           1000000.0 * 1000000.0 / (double)(total / 1000ULL));
}

/* ========================================================================
 * 内存拷贝性能测试
 * ======================================================================== */

static void benchmark_memcpy(void)
{
    uint8_t src[4096];
    uint8_t dst[4096];
    memset(src, 0xAB, sizeof(src));

    uint64_t total = 0ULL;
    uint32_t i;

    printf("\n========== 内存拷贝性能测试 ==========\n");

    for (i = 0U; i < 1000000U; i++)
    {
        uint64_t start = get_time_ns();

        memcpy(dst, src, sizeof(src));

        uint64_t end = get_time_ns();
        total += (end - start);
    }

    uint64_t avg_us = ns_to_us(total / 1000000U);

    printf("  拷贝次数: 1000000\n");
    printf("  拷贝大小: 4096 字节\n");
    printf("  平均拷贝时间: %llu us\n", avg_us);
    printf("  吞吐量: %.2f 百万次/秒\n",
           1000000.0 * 1000000.0 / (double)(total / 1000ULL));
    printf("  吞吐量: %.2f GB/秒\n",
           (1000000.0 * 4096.0) / (double)(total / 1000ULL) / 1024.0 / 1024.0 / 1024.0);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("========================================\n");
    printf("AISafe64 - 内存管理优化性能测试（简化版）\n");
    printf("========================================\n");

    benchmark_malloc();
    benchmark_ring_buffer();
    benchmark_memcpy();

    printf("\n========================================\n");
    printf("性能测试完成\n");
    printf("========================================\n");

    return 0;
}
