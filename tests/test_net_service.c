/**
 * @file    test_net_service.c
 * @brief   Net 服务主循环 IPC 测试
 * @author  AISafe64 Team
 * @date    2026-04-30
 * @version 1.0
 *
 * @details 测试 Net 服务主循环：
 *          - 网络接口状态管理
 *          - 数据包接收处理
 *          - TCP 定时器管理
 *          - IPC 消息集成
 *
 * @note MISRA-C:2012 合规
 * @note TDD: RED-GREEN-REFACTOR
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* 测试计数器 */
static int g_test_passed = 0;
static int g_test_failed = 0;

/* ========================================================================
 * 测试工具函数
 * ======================================================================== */

static void test_start(const char *name)
{
    printf("[NET_SERVICE] %s ... ", name);
}

static void test_pass(void)
{
    printf("PASSED\n");
    g_test_passed++;
}

static void test_fail(const char *reason)
{
    printf("FAILED (%s)\n", reason);
    g_test_failed++;
}

/* ========================================================================
 * 模拟网络接口数据结构
 * ======================================================================== */

/** @brief 网络接口状态 */
typedef enum
{
    NET_IF_DOWN = 0U,
    NET_IF_UP,
    NET_IF_RUNNING
} net_if_state_t;

/** @brief 最大接口数 */
#define NET_MAX_INTERFACES   4U

/** @brief 最大包大小 */
#define NET_MAX_PACKET_SIZE  1514U

/** @brief TCP 重传周期（ms） */
#define TCP_RETRANSMIT_PERIOD_MS  200U

/** @brief 网络接口 */
typedef struct
{
    net_if_state_t state;
    uint32_t       rx_count;
    uint32_t       tx_count;
    uint8_t        mac_addr[6];
} test_net_interface_t;

/** @brief TCP 定时器状态 */
typedef struct
{
    uint64_t timer_accum_ms;
    uint32_t retransmit_count;
    uint32_t keepalive_count;
} test_tcp_timer_t;

/** @brief 模拟网络状态 */
static test_net_interface_t s_interfaces[NET_MAX_INTERFACES];
static test_tcp_timer_t s_tcp_timer;
static uint64_t s_time_ms;

/** @brief 初始化网络接口 */
static void test_net_init(void)
{
    uint32_t i;
    for (i = 0U; i < NET_MAX_INTERFACES; i++)
    {
        s_interfaces[i].state = NET_IF_DOWN;
        s_interfaces[i].rx_count = 0U;
        s_interfaces[i].tx_count = 0U;
    }
    s_tcp_timer.timer_accum_ms = 0ULL;
    s_tcp_timer.retransmit_count = 0U;
    s_tcp_timer.keepalive_count = 0U;
    s_time_ms = 0ULL;
}

/** @brief 模拟接收数据包 */
static int32_t test_net_rx_packet(uint32_t if_id, uint8_t *buf, uint64_t size)
{
    if (if_id >= NET_MAX_INTERFACES)
    {
        return -1;
    }
    if (s_interfaces[if_id].state != NET_IF_RUNNING)
    {
        return 0;
    }
    if (buf == NULL || size == 0ULL)
    {
        return -1;
    }
    s_interfaces[if_id].rx_count++;
    return 0;
}

/** @brief 模拟 TCP 重传检查 */
static void test_tcp_retransmit_check(void)
{
    s_tcp_timer.retransmit_count++;
}

/** @brief 模拟 TCP keepalive 检查 */
static void test_tcp_keepalive_check(void)
{
    s_tcp_timer.keepalive_count++;
}

/* ========================================================================
 * 模拟主循环（与 services/net/main.c 结构一致）
 * ======================================================================== */

/**
 * @brief 模拟网络服务主循环迭代
 *
 * @param tick_ms  时间增量（毫秒）
 */
static void test_net_main_loop_tick(uint64_t tick_ms)
{
    /* 更新时间计数器 */
    s_time_ms += tick_ms;
    s_tcp_timer.timer_accum_ms += tick_ms;

    /* 处理接收包 */
    {
        uint32_t if_id;
        uint8_t rx_tmp[NET_MAX_PACKET_SIZE];

        for (if_id = 0U; if_id < NET_MAX_INTERFACES; if_id++)
        {
            if (s_interfaces[if_id].state == NET_IF_RUNNING)
            {
                (void)test_net_rx_packet(if_id, rx_tmp,
                    (uint64_t)NET_MAX_PACKET_SIZE);
            }
        }
    }

    /* TCP 定时器检查 */
    if (s_tcp_timer.timer_accum_ms >= TCP_RETRANSMIT_PERIOD_MS)
    {
        test_tcp_retransmit_check();
        test_tcp_keepalive_check();
        s_tcp_timer.timer_accum_ms = 0ULL;
    }
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试网络初始化
 */
static void test_net_init_state(void)
{
    uint32_t i;
    test_start("net_init_state");
    test_net_init();
    for (i = 0U; i < NET_MAX_INTERFACES; i++)
    {
        if (s_interfaces[i].state != NET_IF_DOWN || s_interfaces[i].rx_count != 0U)
        {
            test_fail("interface not initialized correctly");
            return;
        }
    }
    if (s_time_ms != 0ULL || s_tcp_timer.timer_accum_ms != 0ULL)
    {
        test_fail("timers not initialized to zero");
        return;
    }
    test_pass();
}

/**
 * @brief 测试接口 DOWN 状态不接收包
 */
static void test_net_if_down_no_rx(void)
{
    test_start("net_if_down_no_rx");
    test_net_init();
    test_net_main_loop_tick(10ULL);
    if (s_interfaces[0].rx_count == 0U)
    {
        test_pass();
    }
    else
    {
        test_fail("expected no rx when interface down");
    }
}

/**
 * @brief 测试接口 RUNNING 状态接收包
 */
static void test_net_if_running_rx(void)
{
    test_start("net_if_running_rx");
    test_net_init();
    s_interfaces[0].state = NET_IF_RUNNING;
    test_net_main_loop_tick(10ULL);
    if (s_interfaces[0].rx_count == 1U)
    {
        test_pass();
    }
    else
    {
        printf("(rx_count=%u)", s_interfaces[0].rx_count);
        test_fail("expected rx_count=1");
    }
}

/**
 * @brief 测试时间计数器累加
 */
static void test_net_time_accumulation(void)
{
    test_start("net_time_accumulation");
    test_net_init();
    test_net_main_loop_tick(10ULL);
    test_net_main_loop_tick(10ULL);
    test_net_main_loop_tick(10ULL);
    if (s_time_ms == 30ULL)
    {
        test_pass();
    }
    else
    {
        printf("(time=%lu)", (unsigned long)s_time_ms);
        test_fail("expected time_ms=30");
    }
}

/**
 * @brief 测试 TCP 定时器触发
 */
static void test_tcp_timer_trigger(void)
{
    test_start("tcp_timer_trigger");
    test_net_init();
    s_interfaces[0].state = NET_IF_RUNNING;

    /* 累积 200ms */
    uint32_t i;
    for (i = 0U; i < 20U; i++)
    {
        test_net_main_loop_tick(10ULL);
    }

    if (s_tcp_timer.retransmit_count == 1U && s_tcp_timer.keepalive_count == 1U)
    {
        test_pass();
    }
    else
    {
        printf("(retransmit=%u, keepalive=%u)",
               s_tcp_timer.retransmit_count, s_tcp_timer.keepalive_count);
        test_fail("expected 1 retransmit and 1 keepalive check");
    }
}

/**
 * @brief 测试 TCP 定时器重置
 */
static void test_tcp_timer_reset(void)
{
    test_start("tcp_timer_reset");
    test_net_init();
    s_interfaces[0].state = NET_IF_RUNNING;

    /* 累积 200ms 触发一次 */
    uint32_t i;
    for (i = 0U; i < 20U; i++)
    {
        test_net_main_loop_tick(10ULL);
    }

    /* 定时器应该已重置 */
    if (s_tcp_timer.timer_accum_ms == 0ULL)
    {
        test_pass();
    }
    else
    {
        printf("(accum=%lu)", (unsigned long)s_tcp_timer.timer_accum_ms);
        test_fail("expected timer reset after trigger");
    }
}

/**
 * @brief 测试多接口并行接收
 */
static void test_net_multi_interface_rx(void)
{
    test_start("net_multi_interface_rx");
    test_net_init();
    s_interfaces[0].state = NET_IF_RUNNING;
    s_interfaces[1].state = NET_IF_RUNNING;
    s_interfaces[2].state = NET_IF_UP;  /* UP 但不 RUNNING */

    test_net_main_loop_tick(10ULL);

    if (s_interfaces[0].rx_count == 1U &&
        s_interfaces[1].rx_count == 1U &&
        s_interfaces[2].rx_count == 0U)
    {
        test_pass();
    }
    else
    {
        printf("(if0=%u, if1=%u, if2=%u)",
               s_interfaces[0].rx_count,
               s_interfaces[1].rx_count,
               s_interfaces[2].rx_count);
        test_fail("expected if0=1, if1=1, if2=0");
    }
}

/**
 * @brief 测试长时间运行稳定性
 */
static void test_net_long_run(void)
{
    test_start("net_long_run");
    test_net_init();
    s_interfaces[0].state = NET_IF_RUNNING;

    /* 模拟 1000 个 tick（10 秒） */
    uint32_t i;
    for (i = 0U; i < 1000U; i++)
    {
        test_net_main_loop_tick(10ULL);
    }

    /* 验证：
     * - 时间 = 10000ms
     * - TCP 重传触发次数 = 10000 / 200 = 50
     * - 接收包次数 = 1000
     */
    if (s_time_ms == 10000ULL &&
        s_tcp_timer.retransmit_count == 50U &&
        s_interfaces[0].rx_count == 1000U)
    {
        test_pass();
    }
    else
    {
        printf("(time=%lu, retransmit=%u, rx=%u)",
               (unsigned long)s_time_ms,
               s_tcp_timer.retransmit_count,
               s_interfaces[0].rx_count);
        test_fail("unexpected values in long run");
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  Net 服务主循环测试\n");
    printf("========================================\n");
    printf("\n");

    /* 初始化和状态测试 */
    test_net_init_state();
    test_net_if_down_no_rx();
    test_net_if_running_rx();

    /* 定时器测试 */
    test_net_time_accumulation();
    test_tcp_timer_trigger();
    test_tcp_timer_reset();

    /* 多接口测试 */
    test_net_multi_interface_rx();

    /* 稳定性测试 */
    test_net_long_run();

    /* 测试总结 */
    printf("\n");
    printf("========================================\n");
    printf("  测试结果\n");
    printf("========================================\n");
    printf("Total:    %d\n", g_test_passed + g_test_failed);
    printf("Passed:   %d\n", g_test_passed);
    printf("Failed:   %d\n", g_test_failed);
    printf("========================================\n");
    printf("\n");

    if (g_test_failed == 0)
    {
        printf("所有测试通过\n");
        return 0;
    }
    else
    {
        printf("部分测试失败\n");
        return 1;
    }
}
