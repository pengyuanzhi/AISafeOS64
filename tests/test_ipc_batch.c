/**
 * @file    test_ipc_batch.c
 * @brief   IPC 批量处理测试
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @test 模块化测试框架
 *
 * @details 测试 IPC 批量处理：
 *          - 批量发送接口
 *          - 批量接收接口
 *          - 批量处理性能
 *          - 边界条件处理
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.2.3 - IPC 批量处理
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

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
 * IPC 消息结构定义
 * ======================================================================== */

/** @brief 批量消息结构 */
typedef struct
{
    void    *buffer;    /**< @brief 消息缓冲区指针 */
    uint64_t len;       /**< @brief 消息长度 */
    uint64_t tag;       /**< @brief 消息标签 */
} ipc_msg_batch_t;

/* ========================================================================
 * 模拟批量发送接口
 * ======================================================================== */

/**
 * @brief 批量发送多个消息
 */
static int32_t ipc_batch_send(ipc_msg_batch_t *msgs, uint32_t count)
{
    uint32_t i;

    if ((msgs == NULL) || (count == 0U))
    {
        return -1;
    }

    /* 模拟批量发送 */
    for (i = 0U; i < count; i++)
    {
        if ((msgs[i].buffer == NULL) || (msgs[i].len == 0U))
        {
            return -2;
        }
    }

    return 0;
}

/* ========================================================================
 * 模拟批量接收接口
 * ======================================================================== */

/**
 * @brief 批量接收多个消息
 */
static int32_t ipc_batch_recv(ipc_msg_batch_t *msgs, uint32_t *count)
{
    uint32_t i;

    if ((msgs == NULL) || (count == NULL) || (*count == 0U))
    {
        return -1;
    }

    /* 模拟批量接收 */
    for (i = 0U; i < *count; i++)
    {
        if (msgs[i].buffer == NULL)
        {
            return -2;
        }
    }

    return 0;
}

/* ========================================================================
 * 测试 1: 批量发送接口测试
 * ======================================================================== */

/**
 * @brief 测试批量发送接口
 */
static void test_batch_send(void)
{
    printf("\n========== 测试 1: 批量发送接口 ==========\n");

    /* 测试参数验证 */
    int32_t ret = ipc_batch_send(NULL, 10);
    TEST_ASSERT(ret == -1, "空指针测试失败");

    /* 测试空计数 */
    ipc_msg_batch_t msgs[10];
    ret = ipc_batch_send(msgs, 0);
    TEST_ASSERT(ret == -1, "空计数测试失败");

    /* 测试正常发送 */
    char data[5][64];
    uint32_t i;
    for (i = 0U; i < 5U; i++)
    {
        msgs[i].buffer = data[i];
        msgs[i].len = (uint64_t)strlen("Hello") + 1;
        msgs[i].tag = i;
        snprintf(data[i], sizeof(data[i]), "Message %u", i);
    }
    ret = ipc_batch_send(msgs, 5);
    TEST_ASSERT(ret == 0, "批量发送失败");

    printf("  [INFO] 批量发送 5 个消息\n");
}

/* ========================================================================
 * 测试 2: 批量接收接口测试
 * ======================================================================== */

/**
 * @brief 测试批量接收接口
 */
static void test_batch_recv(void)
{
    printf("\n========== 测试 2: 批量接收接口 ==========\n");

    /* 测试参数验证 */
    int32_t ret;
    ipc_msg_batch_t msgs[10];
    uint32_t count = 10U;

    ret = ipc_batch_recv(NULL, &count);
    TEST_ASSERT(ret == -1, "空指针测试失败");

    ret = ipc_batch_recv(msgs, NULL);
    TEST_ASSERT(ret == -1, "空计数指针测试失败");

    ret = ipc_batch_recv(msgs, &count);
    count = 0U;
    ret = ipc_batch_recv(msgs, &count);
    TEST_ASSERT(ret == -1, "空计数测试失败");

    /* 测试正常接收 */
    char data[5][64];
    count = 5U;
    for (uint32_t i = 0U; i < count; i++)
    {
        msgs[i].buffer = data[i];
        msgs[i].len = sizeof(data[i]);
        msgs[i].tag = 0;
    }
    ret = ipc_batch_recv(msgs, &count);
    TEST_ASSERT(ret == 0, "批量接收失败");

    printf("  [INFO] 批量接收 5 个消息\n");
}

/* ========================================================================
 * 测试 3: 批量处理性能测试
 * ======================================================================== */

/**
 * @brief 测试批量处理性能
 */
static void test_batch_performance(void)
{
    printf("\n========== 测试 3: 批量处理性能 ==========\n");

    uint64_t iterations = 1000000U;
    ipc_msg_batch_t msgs[10];
    char data[10][64];
    uint64_t i;

    /* 初始化消息 */
    for (i = 0U; i < 10U; i++)
    {
        msgs[i].buffer = data[i];
        msgs[i].len = (uint64_t)strlen("Hello") + 1;
        msgs[i].tag = i;
    }

    /* 测试批量发送性能 */
    clock_t start = clock();
    for (i = 0ULL; i < iterations; i++)
    {
        (void)ipc_batch_send(msgs, 10);
    }
    clock_t end = clock();
    double batch_send_time = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("  [INFO] 批量发送 1M 次: %.3f seconds\n", batch_send_time);
    printf("  [INFO] 平均每次: %.3f microseconds\n", batch_send_time * 1000000.0 / iterations);
    printf("  [INFO] 平均每个消息: %.3f microseconds\n", batch_send_time * 1000000.0 / (iterations * 10));

    /* 测试批量接收性能 */
    uint32_t count = 10U;
    start = clock();
    for (i = 0ULL; i < iterations; i++)
    {
        (void)ipc_batch_recv(msgs, &count);
    }
    end = clock();
    double batch_recv_time = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("  [INFO] 批量接收 1M 次: %.3f seconds\n", batch_recv_time);
    printf("  [INFO] 平均每次: %.3f microseconds\n", batch_recv_time * 1000000.0 / iterations);
    printf("  [INFO] 平均每个消息: %.3f microseconds\n", batch_recv_time * 1000000.0 / (iterations * 10));
}

/* ========================================================================
 * 测试 4: 边界条件测试
 * ======================================================================== */

/**
 * @brief 测试边界条件
 */
static void test_boundary_conditions(void)
{
    printf("\n========== 测试 4: 边界条件 ==========\n");

    ipc_msg_batch_t msgs[1];
    char data[64];

    /* 测试单消息批量 */
    msgs[0].buffer = data;
    msgs[0].len = sizeof(data);
    msgs[0].tag = 0;
    int32_t ret = ipc_batch_send(msgs, 1);
    TEST_ASSERT(ret == 0, "单消息批量发送失败");

    printf("  [INFO] 单消息批量测试通过\n");

    /* 测试大批量消息 */
    ipc_msg_batch_t big_msgs[100];
    char big_data[100][64];
    for (uint32_t i = 0U; i < 100U; i++)
    {
        big_msgs[i].buffer = big_data[i];
        big_msgs[i].len = sizeof(big_data[i]);
        big_msgs[i].tag = i;
    }
    ret = ipc_batch_send(big_msgs, 100);
    TEST_ASSERT(ret == 0, "大批量消息发送失败");

    printf("  [INFO] 大批量（100 消息）测试通过\n");
}

/* ========================================================================
 * 测试 5: 空消息测试
 * ======================================================================== */

/**
 * @brief 测试空消息处理
 */
static void test_empty_messages(void)
{
    printf("\n========== 测试 5: 空消息处理 ==========\n");

    ipc_msg_batch_t msgs[10];
    char data[10][64];

    /* 初始化消息 */
    for (uint32_t i = 0U; i < 10U; i++)
    {
        msgs[i].buffer = data[i];
        msgs[i].len = 0; /* 空消息 */
        msgs[i].tag = i;
    }

    /* 测试空消息批量发送（应该失败） */
    int32_t ret = ipc_batch_send(msgs, 10);
    TEST_ASSERT(ret == -2, "空消息批量发送应该失败");

    printf("  [INFO] 空消息测试通过\n");
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

/**
 * @brief 运行所有批量处理测试
 */
static void run_all_tests(void)
{
    printf("\n");
    printf("========================================\n");
    printf("AISafeOS64 IPC 批量处理测试\n");
    printf("========================================\n");

    /* 运行所有测试 */
    test_batch_send();
    test_batch_recv();
    test_batch_performance();
    test_boundary_conditions();
    test_empty_messages();

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
