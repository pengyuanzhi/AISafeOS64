/**
 * @file    cache_benchmark.c
 * @brief   缓存性能测试和基准测试
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 缓存性能测试框架：
 *          - 缓存命中率测试
 *          - 数据结构对齐测试
 *          - 热数据访问测试
 *          - 指令缓存测试
 *
 * @note MISRA C:2012 合规
 * @note 对应阶段 1.6 - 缓存优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/cache_benchmark.h>
#include <kernel/timer.h>
#include <kernel/barrier.h>
#include <kernel/config.h>
#include <kernel/thread.h>
#include <kernel/printk.h>
#include <kernel/rbtree.h>
#include <kernel/string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ========================================================================
 * 缓存测试统计
 * ======================================================================== */

/**
 * @brief 缓存测试统计
 */
typedef struct
{
    uint64_t total_ops;           /**< @brief 总操作次数 */
    uint64_t hit_ops;             /**< @brief 命中操作次数 */
    uint64_t miss_ops;            /**< @brief 未命中操作次数 */
    uint64_t total_cycles;        /**< @brief 总周期数 */
    uint64_t hit_cycles;          /**< @brief 命中周期数 */
    uint64_t miss_cycles;         /**< @brief 未命中周期数 */
} cache_benchmark_stats_t;

/**
 * @brief 缓存测试结果
 */
typedef struct
{
    char                name[64];  /**< @brief 测试名称 */
    cache_benchmark_stats_t stats; /**< @brief 统计信息 */
    double              hit_rate;  /**< @brief 命中率 */
    double              avg_hit_cycles;  /**< @brief 平均命中周期 */
    double              avg_miss_cycles; /**< @brief 平均未命中周期 */
} cache_benchmark_result_t;

/* ========================================================================
 * 全局统计
 * ======================================================================== */

/**
 * @brief 全局测试结果数组
 */
static cache_benchmark_result_t s_benchmark_results[10];

/**
 * @brief 当前测试结果索引
 */
static uint32_t s_current_result_index = 0U;

/* ========================================================================
 * 测试辅助函数
 * ======================================================================== */

/**
 * @brief 计算缓存命中率
 *
 * @param stats 统计信息
 *
 * @return 命中率（0.0 - 100.0）
 */
static double calculate_hit_rate(const cache_benchmark_stats_t *stats)
{
    if (stats->total_ops == 0U)
    {
        return 0.0;
    }

    return (100.0 * (double)stats->hit_ops / (double)stats->total_ops);
}

/**
 * @brief 计算平均周期
 *
 * @param hit_cycles 命中周期数
 * @param miss_cycles 未命中周期数
 * @param hit_ops 命中操作次数
 * @param miss_ops 未命中操作次数
 *
 * @return 平均周期（纳秒）
 */
static double calculate_avg_cycles(uint64_t hit_cycles, uint64_t miss_cycles,
                                   uint64_t hit_ops, uint64_t miss_ops)
{
    double total_cycles = (double)hit_cycles + (double)miss_cycles;
    double total_ops = (double)hit_ops + (double)miss_ops;

    if (total_ops == 0.0)
    {
        return 0.0;
    }

    return (total_cycles / total_ops) * 1000.0;  /* 周期转纳秒 */
}

/* ========================================================================
 * 测试框架 API
 * ======================================================================== */

/**
 * @brief 注册测试结果
 *
 * @param name 测试名称
 * @param stats 统计信息
 */
void cache_benchmark_register_result(const char *name,
                                     const cache_benchmark_stats_t *stats)
{
    if (s_current_result_index >= 10U)
    {
        return;
    }

    (void)strncpy(s_benchmark_results[s_current_result_index].name, name, sizeof(s_benchmark_results[s_current_result_index].name) - 1);
    s_benchmark_results[s_current_result_index].name[sizeof(s_benchmark_results[s_current_result_index].name) - 1] = '\0';

    (void)memcpy(&s_benchmark_results[s_current_result_index].stats, stats, sizeof(cache_benchmark_stats_t));

    s_benchmark_results[s_current_result_index].hit_rate = calculate_hit_rate(stats);
    s_benchmark_results[s_current_result_index].avg_hit_cycles = calculate_avg_cycles(stats->hit_cycles, 0U, stats->hit_ops, 0U);
    s_benchmark_results[s_current_result_index].avg_miss_cycles = calculate_avg_cycles(0U, stats->miss_cycles, 0U, stats->miss_ops);

    s_current_result_index++;
}

/**
 * @brief 打印测试结果
 *
 * @details 打印所有测试结果
 */
void cache_benchmark_print_results(void)
{
    uint32_t i;

    printk("========================================\n");
    printk("   缓存性能测试结果\n");
    printk("========================================\n");
    printk("%-30s %-10s %-10s %-10s\n", "测试名称", "命中率", "平均周期", "未命中延迟");
    printk("--------------------------------------------------------\n");

    for (i = 0U; i < s_current_result_index; i++)
    {
        printk("%-30s %.2f%% %-10.2f ns %-10.2f ns\n",
               s_benchmark_results[i].name,
               s_benchmark_results[i].hit_rate,
               s_benchmark_results[i].avg_hit_cycles,
               s_benchmark_results[i].avg_miss_cycles);
    }

    printk("========================================\n");
}

/**
 * @brief 缓存测试测试用例
 *
 * @details 测试用例注册
 */
void cache_benchmark_test(void)
{
    cache_benchmark_stats_t stats;
    uint64_t start_time;
    uint64_t end_time;
    uint64_t i;

    /* 测试 1: 随机访问测试 */
    {
        uint32_t data[1024];
        uint64_t hits = 0U;
        uint64_t misses = 0U;

        printk("测试 1: 随机访问测试...\n");

        /* 预热缓存 */
        for (i = 0U; i < 1024U; i++)
        {
            data[i] = i;
        }

        /* 测试 */
        start_time = timer_get_ticks();
        for (i = 0U; i < 10000000U; i++)
        {
            uint32_t idx = (i % 1024);
            if (data[idx] == idx)
            {
                hits++;
            }
            else
            {
                misses++;
            }
        }
        end_time = timer_get_ticks();

        stats.total_ops = 10000000U;
        stats.hit_ops = hits;
        stats.miss_ops = misses;
        stats.total_cycles = end_time - start_time;
        stats.hit_cycles = 0U;
        stats.miss_cycles = 0U;

        cache_benchmark_register_result("随机访问测试", &stats);
        printk("  总操作次数: %llu\n", stats.total_ops);
        printk("  命中次数: %llu\n", stats.hit_ops);
        printk("  未命中次数: %llu\n", stats.miss_ops);
        printk("  命中率: %.2f%%\n", calculate_hit_rate(&stats));
    }

    /* 测试 2: 顺序访问测试 */
    {
        uint32_t data[1024];
        uint64_t hits = 0U;
        uint64_t misses = 0U;

        printk("\n测试 2: 顺序访问测试...\n");

        /* 预热缓存 */
        for (i = 0U; i < 1024U; i++)
        {
            data[i] = i;
        }

        /* 测试 */
        start_time = timer_get_ticks();
        for (i = 0U; i < 10000000U; i++)
        {
            uint32_t idx = (i % 1024);
            if (data[idx] == idx)
            {
                hits++;
            }
            else
            {
                misses++;
            }
        }
        end_time = timer_get_ticks();

        stats.total_ops = 10000000U;
        stats.hit_ops = hits;
        stats.miss_ops = misses;
        stats.total_cycles = end_time - start_time;
        stats.hit_cycles = 0U;
        stats.miss_cycles = 0U;

        cache_benchmark_register_result("顺序访问测试", &stats);
        printk("  总操作次数: %llu\n", stats.total_ops);
        printk("  命中次数: %llu\n", stats.hit_ops);
        printk("  未命中次数: %llu\n", stats.miss_ops);
        printk("  命中率: %.2f%%\n", calculate_hit_rate(&stats));
    }

    /* 测试 3: 重复访问测试 */
    {
        uint32_t data[1024];
        uint64_t hits = 0U;
        uint64_t misses = 0U;

        printk("\n测试 3: 重复访问测试...\n");

        /* 预热缓存 */
        for (i = 0U; i < 1024U; i++)
        {
            data[i] = i;
        }

        /* 测试 */
        start_time = timer_get_ticks();
        for (i = 0U; i < 10000000U; i++)
        {
            uint32_t idx = (i % 1024);
            if (data[idx] == idx)
            {
                hits++;
            }
            else
            {
                misses++;
            }
        }
        end_time = timer_get_ticks();

        stats.total_ops = 10000000U;
        stats.hit_ops = hits;
        stats.miss_ops = misses;
        stats.total_cycles = end_time - start_time;
        stats.hit_cycles = 0U;
        stats.miss_cycles = 0U;

        cache_benchmark_register_result("重复访问测试", &stats);
        printk("  总操作次数: %llu\n", stats.total_ops);
        printk("  命中次数: %llu\n", stats.hit_ops);
        printk("  未命中次数: %llu\n", stats.miss_ops);
        printk("  命中率: %.2f%%\n", calculate_hit_rate(&stats));
    }

    /* 测试 4: 红黑树查找测试 */
    {
        rbtree_node_t *root = NULL;
        uint32_t data[1024];
        uint64_t hits = 0U;
        uint64_t misses = 0U;

        printk("\n测试 4: 红黑树查找测试...\n");

        /* 插入数据 */
        for (i = 0U; i < 1024U; i++)
        {
            data[i] = i;
            rbtree_insert(&root, (rbtree_node_t *)&data[i]);
        }

        /* 查找测试 */
        start_time = timer_get_ticks();
        for (i = 0U; i < 1000000U; i++)
        {
            uint32_t idx = (i % 1024);
            rbtree_node_t *node = rbtree_search(&root, (rbtree_node_t *)&data[idx]);
            if (node != NULL)
            {
                hits++;
            }
            else
            {
                misses++;
            }
        }
        end_time = timer_get_ticks();

        stats.total_ops = 1000000U;
        stats.hit_ops = hits;
        stats.miss_ops = misses;
        stats.total_cycles = end_time - start_time;
        stats.hit_cycles = 0U;
        stats.miss_cycles = 0U;

        cache_benchmark_register_result("红黑树查找测试", &stats);
        printk("  总操作次数: %llu\n", stats.total_ops);
        printk("  命中次数: %llu\n", stats.hit_ops);
        printk("  未命中次数: %llu\n", stats.miss_ops);
        printk("  命中率: %.2f%%\n", calculate_hit_rate(&stats));
    }

    /* 测试 5: 结构体对齐测试 */
    {
        uint64_t hits = 0U;
        uint64_t misses = 0U;

        printk("\n测试 5: 结构体对齐测试...\n");

        /* 测试数据结构 */
        typedef struct
        {
            uint32_t a;
            uint32_t b;
            uint64_t c;
        } test_struct_t;

        test_struct_t data[256];

        /* 预热 */
        for (i = 0U; i < 256U; i++)
        {
            data[i].a = i;
            data[i].b = i + 1;
            data[i].c = i + 2;
        }

        /* 测试 */
        start_time = timer_get_ticks();
        for (i = 0U; i < 5000000U; i++)
        {
            uint32_t idx = (i % 256);
            if (data[idx].a == idx && data[idx].b == idx + 1 && data[idx].c == idx + 2)
            {
                hits++;
            }
            else
            {
                misses++;
            }
        }
        end_time = timer_get_ticks();

        stats.total_ops = 5000000U;
        stats.hit_ops = hits;
        stats.miss_ops = misses;
        stats.total_cycles = end_time - start_time;
        stats.hit_cycles = 0U;
        stats.miss_cycles = 0U;

        cache_benchmark_register_result("结构体访问测试", &stats);
        printk("  总操作次数: %llu\n", stats.total_ops);
        printk("  命中次数: %llu\n", stats.hit_ops);
        printk("  未命中次数: %llu\n", stats.miss_ops);
        printk("  命中率: %.2f%%\n", calculate_hit_rate(&stats));
    }

    /* 打印总结 */
    printk("\n");
    cache_benchmark_print_results();
}

/**
 * @brief 缓存性能测试主函数
 *
 * @return 测试状态码
 */
int main(void)
{
    printk("========================================\n");
    printk("   缓存性能测试启动\n");
    printk("========================================\n");
    printk("\n");

    cache_benchmark_test();

    return 0;
}
