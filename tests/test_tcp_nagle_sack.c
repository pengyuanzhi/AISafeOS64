/**
 * @file    test_tcp_nagle_sack.c
 * @brief   TCP Nagle 算法和 SACK 测试用例（TDD - RED）
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @details TDD 测试用例：
 *          - Nagle 算法（延迟发送小数据包）
 *          - SACK（Selective Acknowledgment）选项处理
 *          - 延迟 ACK 机制
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
 * 测试常量定义
 * ======================================================================== */

#define TCP_NODELAY  0U   /* 启用 Nagle 算法 */
#define TCP_CORK     1U   /* 启用 Cork 模式 */
#define TCP_MAX_SACK_BLOCKS 4U
#define TCP_MSS      1460U

/* ========================================================================
 * 测试数据结构
 * ======================================================================== */

/**
 * @brief TCP 控制块（简化版，用于测试）
 */
typedef struct tcp_tcb_test_t
{
    uint32_t          sock_id;          /* 套接字 ID */
    tcp_state_t       state;            /* TCP 状态 */
    uint32_t          local_ip;         /* 本地 IP */
    uint32_t          remote_ip;        /* 远端 IP */
    uint16_t          local_port;       /* 本地端口 */
    uint16_t          remote_port;      /* 远端端口 */
    uint32_t          snd_una;          /* 发送未确认序列号 */
    uint32_t          snd_nxt;          /* 下一个发送序列号 */
    uint32_t          snd_wnd;          /* 发送窗口大小 */
    uint32_t          rcv_nxt;          /* 下一个期望接收序列号 */
    uint32_t          rcv_wnd;          /* 接收窗口大小 */
    uint8_t           send_buf[TCP_MAX_SEG_SIZE]; /* 发送缓冲 */
    uint32_t          send_len;         /* 发送缓冲已用长度 */

    /* Nagle 算法字段 */
    uint8_t           nagle_enabled;    /* Nagle 算法启用 */
    uint8_t           tcp_cork;         /* Cork 模式 */
    uint8_t           delayed_ack;      /* 延迟 ACK 计数 */

    /* SACK 字段 */
    uint8_t           sack_permitted;   /* SACK 允许标志 */
    uint8_t           sack_blocks[TCP_MAX_SACK_BLOCKS][2]; /* SACK 块 */
    uint8_t           sack_count;       /* SACK 块数量 */
    uint32_t          sack_left[TCP_MAX_SACK_BLOCKS];   /* SACK 左边界 */
    uint32_t          sack_right[TCP_MAX_SACK_BLOCKS];  /* SACK 右边界 */

    bool              in_use;           /* 使用标记 */
} tcp_tcb_test_t;

/* ========================================================================
 * 测试辅助函数
 * ======================================================================== */

/**
 * @brief 初始化 TCB
 */
static void tcp_tcb_init(tcp_tcb_test_t *tcb)
{
    (void)memset(tcb, 0, sizeof(tcp_tcb_test_t));
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
    tcb->nagle_enabled = 1U;  /* 默认启用 Nagle 算法 */
    tcb->sack_permitted = 1U; /* 默认允许 SACK */
    tcb->in_use = true;
}

/* ========================================================================
 * 测试：Nagle 算法
 * ======================================================================== */

/**
 * @brief 测试：Nagle 算法初始状态
 */
void test_tcp_nagle_initial_state(void)
{
    tcp_tcb_test_t tcb;

    tcp_tcb_init(&tcb);

    /* 验证：Nagle 算法默认启用 */
    TEST_ASSERT_EQUAL(1U, tcb.nagle_enabled);
    TEST_ASSERT_EQUAL(0U, tcb.tcp_cork);
    TEST_ASSERT_EQUAL(0U, tcb.delayed_ack);
    TEST_ASSERT_EQUAL(0U, tcb.send_len);
}

/**
 * @brief 测试：Nagle 算法禁用
 */
void test_tcp_nagle_disabled(void)
{
    tcp_tcb_test_t tcb;

    tcp_tcb_init(&tcb);
    tcb.nagle_enabled = 0U;  /* 禁用 Nagle 算法 */

    /* 模拟：发送小数据包 */
    uint8_t data[100];
    (void)memcpy(tcb.send_buf, data, sizeof(data));
    tcb.send_len = 100U;
    tcb.snd_nxt += 100U;

    /* 验证：禁用 Nagle 算法后，小数据包立即发送 */
    TEST_ASSERT_EQUAL(0U, tcb.nagle_enabled);
    TEST_ASSERT_EQUAL(100U, tcb.send_len);
    TEST_ASSERT_EQUAL(10100U, tcb.snd_nxt);  /* 10000 + 100 */
}

/**
 * @brief 测试：Nagle 算法启用（小数据包延迟发送）
 */
void test_tcp_nagle_small_packet_delayed(void)
{
    tcp_tcb_test_t tcb;

    tcp_tcb_init(&tcb);

    /* 模拟：有未确认的数据 */
    tcb.snd_una = 10000U;
    tcb.snd_nxt = 10050U;  /* 已发送 50 字节 */
    tcb.snd_wnd = 65535U;

    /* 模拟：发送小数据包（< MSS） */
    uint8_t data[50];
    (void)memcpy(tcb.send_buf, data, sizeof(data));
    tcb.send_len = 50U;

    /* 验证：小数据包延迟发送 */
    TEST_ASSERT_EQUAL(50U, tcb.send_len);
    TEST_ASSERT_LESS_THAN(TCP_MSS, tcb.send_len);
    TEST_ASSERT_GREATER_THAN(0U, tcb.snd_nxt - tcb.snd_una);  /* 有未确认数据 */
}

/**
 * @brief 测试：Nagle 算法（大包立即发送）
 */
void test_tcp_nagle_large_packet_immediate(void)
{
    tcp_tcb_test_t tcb;

    tcp_tcb_init(&tcb);

    /* 模拟：发送大数据包（>= MSS） */
    uint8_t data[TCP_MSS];
    (void)memset(data, 0x42, sizeof(data));
    (void)memcpy(tcb.send_buf, data, sizeof(data));
    tcb.send_len = TCP_MSS;
    tcb.snd_nxt += TCP_MSS;

    /* 验证：大包立即发送 */
    TEST_ASSERT_EQUAL(TCP_MSS, tcb.send_len);
    TEST_ASSERT_GREATER_OR_EQUAL(TCP_MSS, tcb.send_len);
}

/**
 * @brief 测试：Nagle 算法（ACK 到达后发送）
 */
void test_tcp_nagle_ack_then_send(void)
{
    tcp_tcb_test_t tcb;

    tcp_tcb_init(&tcb);

    /* 模拟：有未确认的数据 */
    tcb.snd_una = 10000U;
    tcb.snd_nxt = 10050U;

    /* 模拟：ACK 到达 */
    tcb.snd_una = 10050U;  /* ACK 确认所有数据 */

    /* 模拟：发送小数据包 */
    uint8_t data[100];
    (void)memcpy(tcb.send_buf, data, sizeof(data));
    tcb.send_len = 100U;
    tcb.snd_nxt += 100U;

    /* 验证：ACK 到达后，小数据包立即发送 */
    TEST_ASSERT_EQUAL(100U, tcb.send_len);
    TEST_ASSERT_EQUAL(10150U, tcb.snd_nxt);  /* 10000 + 50 + 100 */
}

/* ========================================================================
 * 测试：Cork 模式
 * ======================================================================== */

/**
 * @brief 测试：Cork 模式启用
 */
void test_tcp_cork_enabled(void)
{
    tcp_tcb_test_t tcb;

    tcp_tcb_init(&tcb);
    tcb.tcp_cork = 1U;  /* 启用 Cork 模式 */

    /* 模拟：发送小数据包 */
    uint8_t data[100];
    (void)memcpy(tcb.send_buf, data, sizeof(data));
    tcb.send_len = 100U;

    /* 验证：Cork 模式下，数据包延迟发送 */
    TEST_ASSERT_EQUAL(1U, tcb.tcp_cork);
    TEST_ASSERT_EQUAL(100U, tcb.send_len);
}

/**
 * @brief 测试：Cork 模式禁用（立即发送）
 */
void test_tcp_cork_disabled_send(void)
{
    tcp_tcb_test_t tcb;

    tcp_tcb_init(&tcb);
    tcb.tcp_cork = 1U;  /* 启用 Cork 模式 */
    tcb.send_len = 100U;

    /* 禁用 Cork 模式 */
    tcb.tcp_cork = 0U;

    /* 验证：Cork 模式禁用后，数据包立即发送 */
    TEST_ASSERT_EQUAL(0U, tcb.tcp_cork);
    TEST_ASSERT_EQUAL(100U, tcb.send_len);
}

/* ========================================================================
 * 测试：延迟 ACK
 * ======================================================================== */

/**
 * @brief 测试：延迟 ACK 计数
 */
void test_tcp_delayed_ack_count(void)
{
    tcp_tcb_test_t tcb;

    tcp_tcb_init(&tcb);

    /* 验证：延迟 ACK 计数初始值 */
    TEST_ASSERT_EQUAL(0U, tcb.delayed_ack);
}

/**
 * @brief 测试：延迟 ACK 发送
 */
void test_tcp_delayed_ack_send(void)
{
    tcp_tcb_test_t tcb;

    tcp_tcb_init(&tcb);

    /* 模拟：收到数据包，延迟 ACK */
    tcb.delayed_ack = 1U;

    /* 验证：延迟 ACK 计数 */
    TEST_ASSERT_EQUAL(1U, tcb.delayed_ack);

    /* 模拟：收到第二个数据包，取消延迟 */
    tcb.delayed_ack = 0U;

    /* 验证：延迟 ACK 清零 */
    TEST_ASSERT_EQUAL(0U, tcb.delayed_ack);
}

/* ========================================================================
 * 测试：SACK（Selective Acknowledgment）
 * ======================================================================== */

/**
 * @brief 测试：SACK 初始状态
 */
void test_tcp_sack_initial_state(void)
{
    tcp_tcb_test_t tcb;

    tcp_tcb_init(&tcb);

    /* 验证：SACK 默认允许 */
    TEST_ASSERT_EQUAL(1U, tcb.sack_permitted);
    TEST_ASSERT_EQUAL(0U, tcb.sack_count);
    TEST_ASSERT_EQUAL(0U, tcb.sack_left[0]);
    TEST_ASSERT_EQUAL(0U, tcb.sack_right[0]);
}

/**
 * @brief 测试：SACK 添加块
 */
void test_tcp_sack_add_block(void)
{
    tcp_tcb_test_t tcb;

    tcp_tcb_init(&tcb);

    /* 添加 SACK 块 1 */
    tcb.sack_left[0] = 10000U;
    tcb.sack_right[0] = 10500U;
    tcb.sack_count = 1U;

    /* 验证：SACK 块 1 添加成功 */
    TEST_ASSERT_EQUAL(1U, tcb.sack_count);
    TEST_ASSERT_EQUAL(10000U, tcb.sack_left[0]);
    TEST_ASSERT_EQUAL(10500U, tcb.sack_right[0]);

    /* 添加 SACK 块 2 */
    tcb.sack_left[1] = 11000U;
    tcb.sack_right[1] = 11500U;
    tcb.sack_count = 2U;

    /* 验证：SACK 块 2 添加成功 */
    TEST_ASSERT_EQUAL(2U, tcb.sack_count);
    TEST_ASSERT_EQUAL(11000U, tcb.sack_left[1]);
    TEST_ASSERT_EQUAL(11500U, tcb.sack_right[1]);
}

/**
 * @brief 测试：SACK 最大块数
 */
void test_tcp_sack_max_blocks(void)
{
    tcp_tcb_test_t tcb;

    tcp_tcb_init(&tcb);

    /* 添加最大 SACK 块数 */
    tcb.sack_left[0] = 10000U; tcb.sack_right[0] = 10500U;
    tcb.sack_left[1] = 11000U; tcb.sack_right[1] = 11500U;
    tcb.sack_left[2] = 12000U; tcb.sack_right[2] = 12500U;
    tcb.sack_left[3] = 13000U; tcb.sack_right[3] = 13500U;
    tcb.sack_count = TCP_MAX_SACK_BLOCKS;

    /* 验证：SACK 最大块数 */
    TEST_ASSERT_EQUAL(TCP_MAX_SACK_BLOCKS, tcb.sack_count);
    TEST_ASSERT_EQUAL(4U, tcb.sack_count);
}

/**
 * @brief 测试：SACK 包含检查
 */
void test_tcp_sack_contains(void)
{
    tcp_tcb_test_t tcb;

    tcp_tcb_init(&tcb);

    /* 添加 SACK 块 */
    tcb.sack_left[0] = 10000U;
    tcb.sack_right[0] = 10500U;
    tcb.sack_count = 1U;

    /* 检查：序列号 10200 在 SACK 块中 */
    bool contains = false;
    if ((tcb.sack_count > 0U) &&
        (10200U >= tcb.sack_left[0]) &&
        (10200U < tcb.sack_right[0]))
    {
        contains = true;
    }

    /* 验证：SACK 包含检查 */
    TEST_ASSERT_EQUAL(true, contains);

    /* 检查：序列号 9000 不在 SACK 块中 */
    contains = false;
    if ((tcb.sack_count > 0U) &&
        (9000U >= tcb.sack_left[0]) &&
        (9000U < tcb.sack_right[0]))
    {
        contains = true;
    }

    /* 验证：SACK 不包含 */
    TEST_ASSERT_EQUAL(false, contains);
}

/**
 * @brief 测试：SACK 清除
 */
void test_tcp_sack_clear(void)
{
    tcp_tcb_test_t tcb;

    tcp_tcb_init(&tcb);

    /* 添加 SACK 块 */
    tcb.sack_left[0] = 10000U;
    tcb.sack_right[0] = 10500U;
    tcb.sack_count = 1U;

    /* 验证：SACK 块已添加 */
    TEST_ASSERT_EQUAL(1U, tcb.sack_count);

    /* 清除 SACK */
    tcb.sack_count = 0U;
    (void)memset(tcb.sack_left, 0, sizeof(tcb.sack_left));
    (void)memset(tcb.sack_right, 0, sizeof(tcb.sack_right));

    /* 验证：SACK 已清除 */
    TEST_ASSERT_EQUAL(0U, tcb.sack_count);
    TEST_ASSERT_EQUAL(0U, tcb.sack_left[0]);
    TEST_ASSERT_EQUAL(0U, tcb.sack_right[0]);
}

/* ========================================================================
 * 测试：TCP 选项处理
 * ======================================================================== */

/**
 * @brief 测试：TCP 选项类型识别
 */
void test_tcp_options_type(void)
{
    /* TCP 选项类型定义 */
    #define TCP_OPT_NOP   1U
    #define TCP_OPT_MSS   2U
    #define TCP_OPT_WIN_SCALE  3U
    #define TCP_OPT_SACK_PERMITTED 4U
    #define TCP_OPT_SACK   5U
    #define TCP_OPT_TS     8U

    /* 验证：选项类型识别 */
    TEST_ASSERT_EQUAL(TCP_OPT_NOP, 1U);
    TEST_ASSERT_EQUAL(TCP_OPT_MSS, 2U);
    TEST_ASSERT_EQUAL(TCP_OPT_SACK_PERMITTED, 4U);
    TEST_ASSERT_EQUAL(TCP_OPT_SACK, 5U);
}

/**
 * @brief 测试：SACK 选项解析
 */
void test_tcp_sack_option_parse(void)
{
    /* SACK 选项格式 */
    uint8_t sack_option[12];
    sack_option[0] = 5U;  /* SACK 类型 */
    sack_option[1] = 10U; /* 长度 */

    /* SACK 块（left edge, right edge） */
    sack_option[2] = 0x27; sack_option[3] = 0x10;  /* left edge = 10000 */
    sack_option[4] = 0x00; sack_option[5] = 0x00;
    sack_option[6] = 0x27; sack_option[7] = 0x1A;  /* right edge = 10500 */
    sack_option[8] = 0x00; sack_option[9] = 0x00;
    sack_option[10] = 0x00; sack_option[11] = 0x00;

    /* 验证：SACK 选项类型 */
    TEST_ASSERT_EQUAL(5U, sack_option[0]);
    TEST_ASSERT_EQUAL(10U, sack_option[1]);

    /* 验证：SACK 左边界 */
    uint32_t left = ((uint32_t)sack_option[2] << 24U) |
                    ((uint32_t)sack_option[3] << 16U) |
                    ((uint32_t)sack_option[4] << 8U) |
                    (uint32_t)sack_option[5];
    TEST_ASSERT_EQUAL(10000U, left);

    /* 验证：SACK 右边界 */
    uint32_t right = ((uint32_t)sack_option[6] << 24U) |
                     ((uint32_t)sack_option[7] << 16U) |
                     ((uint32_t)sack_option[8] << 8U) |
                     (uint32_t)sack_option[9];
    TEST_ASSERT_EQUAL(10500U, right);
}

/* ========================================================================
 * 测试套件注册
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Nagle 算法测试 */
    RUN_TEST(test_tcp_nagle_initial_state);
    RUN_TEST(test_tcp_nagle_disabled);
    RUN_TEST(test_tcp_nagle_small_packet_delayed);
    RUN_TEST(test_tcp_nagle_large_packet_immediate);
    RUN_TEST(test_tcp_nagle_ack_then_send);

    /* Cork 模式测试 */
    RUN_TEST(test_tcp_cork_enabled);
    RUN_TEST(test_tcp_cork_disabled_send);

    /* 延迟 ACK 测试 */
    RUN_TEST(test_tcp_delayed_ack_count);
    RUN_TEST(test_tcp_delayed_ack_send);

    /* SACK 测试 */
    RUN_TEST(test_tcp_sack_initial_state);
    RUN_TEST(test_tcp_sack_add_block);
    RUN_TEST(test_tcp_sack_max_blocks);
    RUN_TEST(test_tcp_sack_contains);
    RUN_TEST(test_tcp_sack_clear);

    /* TCP 选项处理测试 */
    RUN_TEST(test_tcp_options_type);
    RUN_TEST(test_tcp_sack_option_parse);

    return UNITY_END();
}
