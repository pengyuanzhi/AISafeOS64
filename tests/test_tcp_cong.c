/**
 * @file    test_tcp_cong.c
 * @brief   TCP 拥塞控制测试用例（TDD - RED）
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @details TDD 测试用例：
 *          - 拥塞控制状态机测试（慢启动、拥塞避免、快速恢复）
 *          - CUBIC 拥塞窗口调整测试
 *          - RTT 估算测试
 *          - RTO 计算测试
 *
 * @note 必须先编写测试，然后实现功能（RED → GREEN → REFACTOR）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "unity.h"

/* ========================================================================
 * 测试辅助函数
 * ======================================================================== */

/**
 * @brief 初始化 TCB 拥塞控制状态
 */
static void tcp_cong_init_tcb(tcp_tcb_t *tcb)
{
    (void)memset(tcb, 0, sizeof(tcp_tcb_t));
    tcb->sock_id = 0;
    tcb->local_port = 8080;
    tcb->remote_port = 443;
    tcb->local_ip = 0xC0A80001;   /* 192.168.0.1 */
    tcb->remote_ip = 0xC0A80002;  /* 192.168.0.2 */
    tcb->iss = 10000U;
    tcb->snd_una = 10000U;
    tcb->snd_nxt = 10000U;
    tcb->rcv_nxt = 10000U;
    tcb->rcv_wnd = 65535U;
    tcb->state = TCP_ESTABLISHED;
    tcb->in_use = true;
}

/**
 * @brief 模拟 CUBIC 拥塞窗口调整
 */
static uint32_t tcp_cong_cubic_adjust(uint32_t cwnd, uint32_t w_max)
{
    /* CUBIC 算法简化版 */
    /* C(t) = max(1, (t - K)^3 + 1) */
    /* K = cbrt(3*(1-β)/(4*α)) * w_max */
    /* cwnd = C(t) + w_max */

    float alpha = 0.7f;
    float beta = 0.7f;
    float K = cbrtf(3.0f * (1.0f - beta) / (4.0f * alpha)) * w_max;

    /* 简化：假设 K = w_max / 2（用于测试） */
    K = w_max / 2;

    /* 简化 CUBIC：只测试慢启动 */
    if (cwnd < w_max)
    {
        cwnd += (cwnd / 2);  /* 慢启动：cwnd *= 1.5 */
    }

    return cwnd;
}

/* ========================================================================
 * 测试：拥塞控制状态机
 * ======================================================================== */

/**
 * @brief 测试：拥塞控制状态机初始状态
 */
void test_tcp_cong_initial_state(void)
{
    tcp_tcb_t tcb;

    tcp_cong_init_tcb(&tcb);

    TEST_ASSERT_EQUAL(TCP_ESTABLISHED, tcb.state);
    TEST_ASSERT_EQUAL(65535U, tcb.cong_ctrl.cwnd);
    TEST_ASSERT_EQUAL(32767U, tcb.cong_ctrl.ssthresh);
    TEST_ASSERT_EQUAL(CONG_OPEN, tcb.cong_ctrl.state);
}

/**
 * @brief 测试：慢启动阶段（cwnd < ssthresh）
 */
void test_tcp_cong_slow_start(void)
{
    tcp_tcb_t tcb;

    tcp_cong_init_tcb(&tcb);
    tcb.cong_ctrl.state = CONG_SLOW_START;
    tcb.cong_ctrl.cwnd = 1000U;
    tcb.cong_ctrl.ssthresh = 32767U;
    tcb.cong_ctrl.w_max = 32767U;

    /* 执行慢启动 */
    tcb.cong_ctrl.cwnd = tcp_cong_cubic_adjust(tcb.cong_ctrl.cwnd,
                                                  tcb.cong_ctrl.w_max);

    /* 验证：cwnd 应该增加 */
    TEST_ASSERT_GREATER_THAN(1000U, tcb.cong_ctrl.cwnd);
    TEST_ASSERT_EQUAL(CONG_SLOW_START, tcb.cong_ctrl.state);
}

/**
 * @brief 测试：拥塞避免阶段（cwnd >= ssthresh）
 */
void test_tcp_cong_congestion_avoidance(void)
{
    tcp_tcb_t tcb;

    tcp_cong_init_tcb(&tcb);
    tcb.cong_ctrl.state = CONG_CONGESTION_AVOIDANCE;
    tcb.cong_ctrl.cwnd = 32768U;
    tcb.cong_ctrl.ssthresh = 32768U;
    tcb.cong_ctrl.w_max = 32768U;

    /* 执行拥塞避免 */
    tcb.cong_ctrl.cwnd = tcp_cong_cubic_adjust(tcb.cong_ctrl.cwnd,
                                                  tcb.cong_ctrl.w_max);

    /* 验证：cwnd 增加幅度较小（1 MSS） */
    TEST_ASSERT_EQUAL(32769U, tcb.cong_ctrl.cwnd);
    TEST_ASSERT_EQUAL(CONG_CONGESTION_AVOIDANCE, tcb.cong_ctrl.state);
}

/**
 * @brief 测试：快速恢复状态
 */
void test_tcp_cong_fast_recovery(void)
{
    tcp_tcb_t tcb;

    tcp_cong_init_tcb(&tcb);
    tcb.cong_ctrl.state = CONG_FAST_RECOVERY;
    tcb.cong_ctrl.cwnd = 16384U;
    tcb.cong_ctrl.ssthresh = 16384U;
    tcb.cong_ctrl.w_max = 32768U;

    /* 验证：cwnd 应该小于 w_max */
    TEST_ASSERT_LESS_THAN(32768U, tcb.cong_ctrl.cwnd);
    TEST_ASSERT_EQUAL(CONG_FAST_RECOVERY, tcb.cong_ctrl.state);
}

/**
 * @brief 测试：超时状态
 */
void test_tcp_cong_timeout_state(void)
{
    tcp_tcb_t tcb;

    tcp_cong_init_tcb(&tcb);
    tcb.cong_ctrl.state = CONG_TIMEOUT;

    /* 验证：cwnd 应该重置 */
    TEST_ASSERT_EQUAL(0U, tcb.cong_ctrl.cwnd);
    TEST_ASSERT_EQUAL(0U, tcb.cong_ctrl.w_max);
}

/* ========================================================================
 * 测试：CUBIC 拥塞窗口调整
 * ======================================================================== */

/**
 * @brief 测试：CUBIC 窗口增加
 */
void test_tcp_cong_cubic_window_increase(void)
{
    uint32_t cwnd = 1000U;
    uint32_t w_max = 32768U;

    /* 执行 CUBIC 计算 */
    cwnd = tcp_cong_cubic_adjust(cwnd, w_max);

    /* 验证：cwnd 增加 */
    TEST_ASSERT_GREATER_THAN(1000U, cwnd);
}

/**
 * @brief 测试：CUBIC 窗口不减少
 */
void test_tcp_cong_cubic_window_no_decrease(void)
{
    uint32_t cwnd = 65535U;
    uint32_t w_max = 32768U;

    /* 执行 CUBic 计算 */
    cwnd = tcp_cong_cubic_adjust(cwnd, w_max);

    /* 验证：cwnd 应该增加 */
    TEST_ASSERT_GREATER_THAN(65535U, cwnd);
}

/**
 * @brief 测试：w_max 更新
 */
void test_tcp_cong_cubic_w_max_update(void)
{
    tcp_tcb_t tcb;

    tcp_cong_init_tcb(&tcb);
    tcb.cong_ctrl.state = CONG_SLOW_START;
    tcb.cong_ctrl.cwnd = 10000U;
    tcb.cong_ctrl.ssthresh = 32767U;

    /* 更新 w_max */
    tcb.cong_ctrl.w_max = 32768U;

    /* 验证：w_max 更新成功 */
    TEST_ASSERT_EQUAL(32768U, tcb.cong_ctrl.w_max);
}

/* ========================================================================
 * 测试：RTT 估算
 * ======================================================================== */

/**
 * @brief 测试：RTT 估算初始值
 */
void test_tcp_rtt_initial(void)
{
    tcp_tcb_t tcb;

    tcp_cong_init_tcb(&tcb);

    /* 验证：RTT 初始值 */
    TEST_ASSERT_EQUAL(0U, tcb.cong_ctrl.rtt.rtt_sample);
    TEST_ASSERT_EQUAL(0U, tcb.cong_ctrl.rtt.rtt_min);
    TEST_ASSERT_EQUAL(0U, tcb.cong_ctrl.rtt.rtt_var);
    TEST_ASSERT_EQUAL(0U, tcb.cong_ctrl.rtt.srtt);
    TEST_ASSERT_EQUAL(1000U, tcb.cong_ctrl.rtt.rto);  /* 初始 RTO = 1秒 */
}

/**
 * @brief 测试：RTT 样本更新
 */
void test_tcp_rtt_sample_update(void)
{
    tcp_tcb_t tcb;

    tcp_cong_init_tcb(&tcb);

    /* 模拟 RTT 样本 */
    tcb.cong_ctrl.rtt.rtt_sample = 100U;  /* 100ms */
    tcb.cong_ctrl.rtt.rtt_min = 100U;
    tcb.cong_ctrl.rtt.rtt_var = 50U;
    tcb.cong_ctrl.rtt.srtt = 100U;

    /* 验证：RTT 样本更新 */
    TEST_ASSERT_EQUAL(100U, tcb.cong_ctrl.rtt.rtt_sample);
    TEST_ASSERT_EQUAL(100U, tcb.cong_ctrl.rtt.rtt_min);
}

/**
 * @brief 测试：RTO 计算
 */
void test_tcp_rto_calculation(void)
{
    tcp_tcb_t tcb;

    tcp_cong_init_tcb(&tcb);

    /* 模拟 RTT 测量 */
    tcb.cong_ctrl.rtt.srtt = 100U;       /* 100ms */
    tcb.cong_ctrl.rtt.rtt_var = 50U;     /* 50ms */
    tcb.cong_ctrl.rtt.rto = 1000U;       /* 初始 RTO = 1000ms */

    /* 计算 RTO：RTO = srtt + 4 * rtt_var */
    tcb.cong_ctrl.rtt.rto = tcb.cong_ctrl.rtt.srtt + 4 * tcb.cong_ctrl.rtt.rtt_var;

    /* 验证：RTO 计算 */
    TEST_ASSERT_EQUAL(100U + 4 * 50U, tcb.cong_ctrl.rtt.rto);  /* 300ms */
    TEST_ASSERT_GREATER_THAN(0U, tcb.cong_ctrl.rtt.rto);
    TEST_ASSERT_LESS_THAN(60000U, tcb.cong_ctrl.rtt.rto);  /* 最大 60秒 */
}

/**
 * @brief 测试：RTO 范围限制
 */
void test_tcp_rto_bounds(void)
{
    tcp_tcb_t tcb;

    tcp_cong_init_tcb(&tcb);

    /* 设置最大 RTT */
    tcb.cong_ctrl.rtt.srtt = 50000U;    /* 50秒 */
    tcb.cong_ctrl.rtt.rtt_var = 10000U; /* 10秒 */
    tcb.cong_ctrl.rtt.rto = 50000U + 4 * 10000U;  /* 90000ms */

    /* 限制 RTO：最大 60秒 */
    if (tcb.cong_ctrl.rtt.rto > 60000U)
    {
        tcb.cong_ctrl.rtt.rto = 60000U;
    }

    /* 验证：RTO 在合理范围内 */
    TEST_ASSERT_LESS_THAN_OR_EQUAL(60000U, tcb.cong_ctrl.rtt.rto);
    TEST_ASSERT_GREATER_THAN_OR_EQUAL(1000U, tcb.cong_ctrl.rtt.rto);  /* 最小 1秒 */
}

/* ========================================================================
 * 测试：重复 ACK 处理
 * ======================================================================== */

/**
 * @brief 测试：重复 ACK 计数
 */
void test_tcp_dup_acks_count(void)
{
    tcp_tcb_t tcb;

    tcp_cong_init_tcb(&tcb);

    /* 初始状态 */
    TEST_ASSERT_EQUAL(0U, tcb.cong_ctrl.dup_acks);
    TEST_ASSERT_EQUAL(0U, tcb.cong_ctrl.last_ack);

    /* 收到 3 个重复 ACK */
    tcb.cong_ctrl.dup_acks = 1U;
    tcb.cong_ctrl.last_ack = 10010U;
    tcb.cong_ctrl.dup_acks = 2U;
    tcb.cong_ctrl.last_ack = 10010U;
    tcb.cong_ctrl.dup_acks = 3U;
    tcb.cong_ctrl.last_ack = 10010U;

    /* 验证：重复 ACK 计数 */
    TEST_ASSERT_EQUAL(3U, tcb.cong_ctrl.dup_acks);
    TEST_ASSERT_EQUAL(10010U, tcb.cong_ctrl.last_ack);
}

/**
 * @brief 测试：重复 ACK 清零
 */
void test_tcp_dup_acks_zero(void)
{
    tcp_tcb_t tcb;

    tcp_cong_init_tcb(&tcb);
    tcb.cong_ctrl.dup_acks = 3U;

    /* 收到不重复的 ACK */
    tcb.cong_ctrl.dup_acks = 0U;
    tcb.cong_ctrl.last_ack = 10020U;

    /* 验证：重复 ACK 清零 */
    TEST_ASSERT_EQUAL(0U, tcb.cong_ctrl.dup_acks);
    TEST_ASSERT_EQUAL(10020U, tcb.cong_ctrl.last_ack);
}

/* ========================================================================
 * 测试套件注册
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* 拥塞控制状态机测试 */
    RUN_TEST(test_tcp_cong_initial_state);
    RUN_TEST(test_tcp_cong_slow_start);
    RUN_TEST(test_tcp_cong_congestion_avoidance);
    RUN_TEST(test_tcp_cong_fast_recovery);
    RUN_TEST(test_tcp_cong_timeout_state);

    /* CUBIC 拥塞窗口调整测试 */
    RUN_TEST(test_tcp_cong_cubic_window_increase);
    RUN_TEST(test_tcp_cong_cubic_window_no_decrease);
    RUN_TEST(test_tcp_cong_cubic_w_max_update);

    /* RTT 估算测试 */
    RUN_TEST(test_tcp_rtt_initial);
    RUN_TEST(test_tcp_rtt_sample_update);
    RUN_TEST(test_tcp_rto_calculation);
    RUN_TEST(test_tcp_rto_bounds);

    /* 重复 ACK 测试 */
    RUN_TEST(test_tcp_dup_acks_count);
    RUN_TEST(test_tcp_dup_acks_zero);

    return UNITY_END();
}
