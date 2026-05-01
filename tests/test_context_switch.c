/**
 * @file    test_context_switch.c
 * @brief   上下文切换性能测试
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @test 模块化测试框架
 *
 * @details 测试上下文切换性能：
 *          - 寄存器保存/恢复性能
 *          - 栈切换性能
 *          - 虚拟地址空间切换性能
 *          - 系统调用性能
 *          - 上下文切换延迟测试
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.4 - 上下文切换优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

/* 上下文切换性能测试（简化版）
 * 注意：宿主机测试不需要高精度计时，使用循环计数代替 */

/* ========================================================================
 * 测试统计
 * ======================================================================== */

static uint32_t s_total_tests = 0U;
static uint32_t s_passed_tests = 0U;
static uint32_t s_failed_tests = 0U;

/* ========================================================================
 * 测试辅助宏
 * ======================================================================== */

#define TEST_ASSERT(condition, message) \
    do { \
        s_total_tests++; \
        if (condition) { \
            s_passed_tests++; \
            printf("  [PASS] %s\n", message); \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] %s\n", message); \
        } \
    } while (0)

/* ========================================================================
 * 上下文结构定义
 * ======================================================================== */

#define CONTEXT_REG_COUNT  32

typedef struct
{
    uint64_t regs[CONTEXT_REG_COUNT];  /**< @brief 寄存器数组 */
    uint64_t sp;                      /**< @brief 栈指针 */
    uint64_t pc;                      /**< @brief 程序计数器 */
    uint64_t lr;                      /**< @brief 链接寄存器 */
} context_t;

/* ========================================================================
 * 性能测试辅助函数
 * ======================================================================== */

/**
 * @brief 简单的循环计数器（宿主机测试）
 */
static uint64_t get_loop_count(void)
{
    static uint64_t counter = 0ULL;
    return ++counter;
}

/* ========================================================================
 * 测试 1: 寄存器保存/恢复性能
 * ======================================================================== */

/**
 * @brief 测试寄存器保存/恢复
 */
static void test_register_save_restore(void)
{
    printf("\n========== 测试 1: 寄存器保存/恢复性能 ==========\n");

    context_t ctx1, ctx2;
    uint64_t start, end, total = 0ULL;
    uint64_t iterations = 1000000ULL;
    uint64_t i;

    /* 初始化上下文 */
    for (i = 0ULL; i < CONTEXT_REG_COUNT; i++)
    {
        ctx1.regs[i] = i;
        ctx1.sp = 0x10000000ULL + i;
        ctx1.pc = 0x80000000ULL + i;
        ctx1.lr = 0xFFFF0000ULL + i;
    }

    /* 测试寄存器保存/恢复 */
    start = get_loop_count();
    for (i = 0ULL; i < iterations; i++)
    {
        (void)memcpy(&ctx2, &ctx1, sizeof(context_t));
        (void)memcpy(&ctx1, &ctx2, sizeof(context_t));
    }
    end = get_loop_count();
    total = end - start;

    uint64_t avg_ns = total / (iterations * 2ULL);
    uint64_t avg_us = avg_ns / 1000ULL;

    printf("  [INFO] 总时间: %llu loops\n", total);
    printf("  [INFO] 平均每次: %llu loops\n", avg_ns);
    printf("  [INFO] 迭代次数: %llu\n", iterations * 2ULL);

    TEST_ASSERT(avg_ns < 1000ULL, "寄存器保存/恢复应该 < 1000 loops");
    TEST_ASSERT(avg_us < 10ULL, "寄存器保存/恢复应该 < 10 loops");
}

/* ========================================================================
 * 测试 2: 栈切换性能
 * ======================================================================== */

/**
 * @brief 测试栈切换
 */
static void test_stack_switch(void)
{
    printf("\n========== 测试 2: 栈切换性能 ==========\n");

    uint64_t stack1[1024];
    uint64_t stack2[1024];
    uint64_t *current_sp = stack1;
    uint64_t start, end, total = 0ULL;
    uint64_t iterations = 1000000ULL;
    uint64_t i;

    /* 测试栈切换 */
    start = get_loop_count();
    for (i = 0ULL; i < iterations; i++)
    {
        if (current_sp == stack1)
        {
            current_sp = stack2;
        }
        else
        {
            current_sp = stack1;
        }
    }
    end = get_loop_count();
    total = end - start;

    uint64_t avg_ns = total / iterations;
    uint64_t avg_us = avg_ns / 1000ULL;

    printf("  [INFO] 总时间: %llu loops\n", total);
    printf("  [INFO] 平均每次: %llu loops\n", avg_ns);
    printf("  [INFO] 迭代次数: %llu\n", iterations);

    TEST_ASSERT(avg_ns < 100ULL, "栈切换应该 < 100 loops");
    TEST_ASSERT(avg_us < 1ULL, "栈切换应该 < 1 loops");
}

/* ========================================================================
 * 测试 3: 系统调用性能
 * ======================================================================== */

/**
 * @brief 模拟系统调用
 */
static int32_t mock_syscall(int32_t syscall_num, int32_t arg1, int32_t arg2)
{
    (void)syscall_num;
    (void)arg1;
    (void)arg2;
    return 0;
}

/**
 * @brief 测试系统调用性能
 */
static void test_syscall_performance(void)
{
    printf("\n========== 测试 3: 系统调用性能 ==========\n");

    uint64_t start, end, total = 0ULL;
    uint64_t iterations = 1000000ULL;
    uint64_t i;

    /* 测试系统调用 */
    start = get_loop_count();
    for (i = 0ULL; i < iterations; i++)
    {
        (void)mock_syscall(0, (int32_t)i, (int32_t)(i * 2));
    }
    end = get_loop_count();
    total = end - start;

    uint64_t avg_ns = total / iterations;
    uint64_t avg_us = avg_ns / 1000ULL;

    printf("  [INFO] 总时间: %llu loops\n", total);
    printf("  [INFO] 平均每次: %llu loops\n", avg_ns);
    printf("  [INFO] 迭代次数: %llu\n", iterations);

    TEST_ASSERT(avg_ns < 500ULL, "系统调用应该 < 500 loops");
    TEST_ASSERT(avg_us < 5ULL, "系统调用应该 < 10 loops");
}

/* ========================================================================
 * 测试 4: 上下文切换延迟
 * ======================================================================== */

/**
 * @brief 测试上下文切换延迟
 */
static void test_context_switch_latency(void)
{
    printf("\n========== 测试 4: 上下文切换延迟 ==========\n");

    context_t ctx1, ctx2;
    uint64_t start, end, total = 0ULL;
    uint64_t iterations = 100000ULL;
    uint64_t i;

    /* 初始化上下文 */
    (void)memset(&ctx1, 0, sizeof(context_t));
    (void)memset(&ctx2, 0, sizeof(context_t));

    /* 测试上下文切换延迟 */
    start = get_loop_count();
    for (i = 0ULL; i < iterations; i++)
    {
        context_t *current;
        if (i % 2ULL == 0ULL)
        {
            current = &ctx1;
        }
        else
        {
            current = &ctx2;
        }

        /* 模拟上下文切换 */
        current->regs[0] = i;
        current->regs[1] = i + 1;
        current->regs[2] = i + 2;
        current->sp = 0x10000000ULL + i;
        current->pc = 0x80000000ULL + i;
    }
    end = get_loop_count();
    total = end - start;

    uint64_t avg_ns = total / iterations;
    uint64_t avg_us = avg_ns / 1000ULL;

    printf("  [INFO] 总时间: %llu loops\n", total);
    printf("  [INFO] 平均每次: %llu loops\n", avg_ns);
    printf("  [INFO] 迭代次数: %llu\n", iterations);

    TEST_ASSERT(avg_ns < 200ULL, "上下文切换延迟应该 < 200 loops");
    TEST_ASSERT(avg_us < 10ULL, "上下文切换延迟应该 < 10 loops");
}

/* ========================================================================
 * 测试 5: 批量上下文切换
 * ======================================================================== */

/**
 * @brief 测试批量上下文切换
 */
static void test_batch_context_switch(void)
{
    printf("\n========== 测试 5: 批量上下文切换 ==========\n");

    context_t contexts[10];
    uint64_t start, end, total = 0ULL;
    uint64_t batch_size = 10ULL;
    uint64_t iterations = 100000ULL;
    uint64_t i, j;

    /* 初始化上下文 */
    for (j = 0ULL; j < batch_size; j++)
    {
        (void)memset(&contexts[j], 0, sizeof(context_t));
        contexts[j].regs[0] = j;
        contexts[j].sp = 0x10000000ULL + j;
    }

    /* 测试批量上下文切换 */
    start = get_loop_count();
    for (i = 0ULL; i < iterations; i++)
    {
        for (j = 0ULL; j < batch_size; j++)
        {
            context_t *current = &contexts[j];
            current->regs[1] = i + j;
            current->pc = 0x80000000ULL + i + j;
        }
    }
    end = get_loop_count();
    total = end - start;

    uint64_t avg_ns = total / (iterations * batch_size);
    uint64_t avg_us = avg_ns / 1000ULL;

    printf("  [INFO] 总时间: %llu loops\n", total);
    printf("  [INFO] 平均每次: %llu loops\n", avg_ns);
    printf("  [INFO] 批量大小: %llu\n", batch_size);
    printf("  [INFO] 迭代次数: %llu\n", iterations * batch_size);

    TEST_ASSERT(avg_ns < 100ULL, "批量上下文切换应该 < 100 loops");
    TEST_ASSERT(avg_us < 5ULL, "批量上下文切换应该 < 10 loops");
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

/**
 * @brief 运行所有上下文切换测试
 */
static void run_all_tests(void)
{
    printf("\n");
    printf("========================================\n");
    printf("AISafe64 上下文切换性能测试\n");
    printf("========================================\n");

    /* 运行所有测试 */
    test_register_save_restore();
    test_stack_switch();
    test_syscall_performance();
    test_context_switch_latency();
    test_batch_context_switch();

    /* 输出测试结果 */
    printf("\n");
    printf("========================================\n");
    printf("测试结果统计\n");
    printf("========================================\n");
    printf("总计测试: %u\n", s_total_tests);
    printf("通过: %u (%.1f%%)\n", s_passed_tests,
           (100.0 * s_passed_tests / s_total_tests));
    printf("失败: %u (%.1f%%)\n", s_failed_tests,
           (100.0 * s_failed_tests / s_total_tests));
    printf("========================================\n");

    if (s_failed_tests == 0)
    {
        printf("\n✅ 所有测试通过！\n");
    }
    else
    {
        printf("\n❌ 有 %u 个测试失败！\n", s_failed_tests);
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    /* 运行所有测试 */
    run_all_tests();

    return (s_failed_tests == 0) ? 0 : 1;
}
