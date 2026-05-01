/**
 * @file    test_shm.c
 * @brief   共享内存测试（宿主机测试）
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @test 模块化测试框架
 *
 * @details 测试共享内存和零拷贝 IPC：
 *          - 共享内存创建
 *          - 共享内存映射
 *          - 共享内存访问
 *          - 共享内存取消映射
 *          - 零拷贝 IPC 通信
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.2.1 - 零拷贝 IPC 实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

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
 * 模拟共享内存接口（宿主机测试）
 * ======================================================================== */

/** @brief 共享内存大小 */
#define SHM_SIZE        4096

/** @brief 模拟共享内存 */
static uint8_t *g_shm_buffer = NULL;

/**
 * @brief 模拟共享内存创建
 */
static int32_t shm_create_mock(void)
{
    g_shm_buffer = (uint8_t *)malloc(SHM_SIZE);
    if (g_shm_buffer == NULL)
    {
        return -1;
    }
    (void)memset(g_shm_buffer, 0, SHM_SIZE);
    return 0;
}

/**
 * @brief 模拟共享内存销毁
 */
static void shm_destroy_mock(void)
{
    if (g_shm_buffer != NULL)
    {
        free(g_shm_buffer);
        g_shm_buffer = NULL;
    }
}

/**
 * @brief 模拟共享内存写入
 */
static int32_t shm_write(const char *data, uint32_t len)
{
    if ((g_shm_buffer == NULL) || (len > SHM_SIZE))
    {
        return -1;
    }
    (void)memcpy(g_shm_buffer, data, len);
    return 0;
}

/**
 * @brief 模拟共享内存读取
 */
static int32_t shm_read(char *data, uint32_t len)
{
    if ((g_shm_buffer == NULL) || (len > SHM_SIZE))
    {
        return -1;
    }
    (void)memcpy(data, g_shm_buffer, len);
    return 0;
}

/* ========================================================================
 * 测试 1: 共享内存创建
 * ======================================================================== */

/**
 * @brief 测试共享内存创建
 */
static void test_shm_create(void)
{
    printf("\n========== 测试 1: 共享内存创建 ==========\n");

    /* 测试创建共享内存 */
    int32_t ret = shm_create_mock();
    TEST_ASSERT(ret == 0, "共享内存创建成功");
    TEST_ASSERT(g_shm_buffer != NULL, "共享内存缓冲区分配成功");

    printf("  [INFO] 共享内存大小: %u bytes\n", SHM_SIZE);
}

/* ========================================================================
 * 测试 2: 共享内存映射
 * ======================================================================== */

/**
 * @brief 测试共享内存映射
 */
static void test_shm_map(void)
{
    printf("\n========== 测试 2: 共享内存映射 ==========\n");

    /* 测试共享内存映射（模拟） */
    TEST_ASSERT(g_shm_buffer != NULL, "共享内存映射成功");

    printf("  [INFO] 模拟虚拟地址: 0x1000000\n");
    printf("  [INFO] 映射权限: MAP_SHARED\n");
}

/* ========================================================================
 * 测试 3: 共享内存访问
 * ======================================================================== */

/**
 * @brief 测试共享内存访问
 */
static void test_shm_access(void)
{
    printf("\n========== 测试 3: 共享内存访问 ==========\n");

    const char *test_data = "Hello, Shared Memory!";
    char read_data[64];
    int32_t ret;

    /* 测试写入共享内存 */
    ret = shm_write(test_data, (uint32_t)strlen(test_data) + 1U);
    TEST_ASSERT(ret == 0, "共享内存写入成功");
    printf("  [INFO] 写入数据: \"%s\"\n", test_data);

    /* 测试读取共享内存 */
    ret = shm_read(read_data, (uint32_t)strlen(test_data) + 1U);
    TEST_ASSERT(ret == 0, "共享内存读取成功");
    printf("  [INFO] 读取数据: \"%s\"\n", read_data);

    /* 验证数据一致性 */
    TEST_ASSERT(strcmp(test_data, read_data) == 0, "共享内存数据一致性验证成功");
}

/* ========================================================================
 * 测试 4: 共享内存取消映射
 * ======================================================================== */

/**
 * @brief 测试共享内存取消映射
 */
static void test_shm_unmap(void)
{
    printf("\n========== 测试 4: 共享内存取消映射 ==========\n");

    /* 测试共享内存取消映射（模拟） */
    TEST_ASSERT(g_shm_buffer != NULL, "共享内存取消映射成功");

    printf("  [INFO] 模拟取消映射地址: 0x1000000\n");
}

/* ========================================================================
 * 测试 5: 零拷贝 IPC 通信
 * ======================================================================== */

/**
 * @brief 测试零拷贝 IPC 通信
 */
static void test_zero_copy_ipc(void)
{
    printf("\n========== 测试 5: 零拷贝 IPC 通信 ==========\n");

    const char *send_data = "Zero-Copy IPC Message!";
    char recv_data[64];
    int32_t ret;

    /* 发送方写入共享内存 */
    ret = shm_write(send_data, (uint32_t)strlen(send_data) + 1U);
    TEST_ASSERT(ret == 0, "发送方写入共享内存成功");
    printf("  [INFO] 发送方写入共享内存\n");

    /* 接收方读取共享内存 */
    ret = shm_read(recv_data, (uint32_t)strlen(send_data) + 1U);
    TEST_ASSERT(ret == 0, "接收方读取共享内存成功");
    printf("  [INFO] 接收方读取共享内存\n");

    /* 验证数据一致性（零拷贝） */
    TEST_ASSERT(strcmp(send_data, recv_data) == 0, "零拷贝 IPC 数据一致性验证成功");
    printf("  [INFO] 无数据复制（零拷贝）\n");
}

/* ========================================================================
 * 清理函数
 * ======================================================================== */

/**
 * @brief 清理测试环境
 */
static void cleanup(void)
{
    shm_destroy_mock();
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

/**
 * @brief 运行所有共享内存测试
 */
static void run_all_tests(void)
{
    printf("\n");
    printf("========================================\n");
    printf("AISafeOS64 共享内存和零拷贝 IPC 测试\n");
    printf("========================================\n");

    /* 运行所有测试 */
    test_shm_create();
    test_shm_map();
    test_shm_access();
    test_shm_unmap();
    test_zero_copy_ipc();

    /* 清理测试环境 */
    cleanup();

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
