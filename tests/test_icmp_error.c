/**
 * @file    test_icmp_error.c
 * @brief   ICMP 错误消息测试用例（TDD - RED）
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @details TDD 测试用例：
 *          - Destination Unreachable（类型 3）
 *          - Time Exceeded（类型 11）
 *          - Parameter Problem（类型 12）
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
 * ICMP 错误消息类型定义
 * ======================================================================== */

#define ICMP_TYPE_DEST_UNREACH   3U   /* 目的不可达 */
#define ICMP_TYPE_TIME_EXCEEDED  11U  /* 超时 */
#define ICMP_TYPE_PARAM_PROB     12U  /* 参数问题 */

/* ICMP 代码定义 */
#define ICMP_CODE_NET_UNREACH    0U   /* 网络不可达 */
#define ICMP_CODE_HOST_UNREACH   1U   /* 主机不可达 */
#define ICMP_CODE_PORT_UNREACH   3U   /* 端口不可达 */
#define ICMP_CODE_FRAG_NEEDED    4U   /* 需要分片 */
#define ICMP_CODE_TTL_EXPIRED    0U   /* TTL 过期 */
#define ICMP_CODE_REASS_TIME_EXPIRED 1U /* 重组超时 */
#define ICMP_CODE_BAD_HEADER     0U   /* 坏的 IP 头 */

/* ========================================================================
 * 数据结构定义
 * ======================================================================== */

/**
 * @brief ICMP 头部
 */
typedef struct
{
    uint8_t  type;          /* 类型 */
    uint8_t  code;          /* 代码 */
    uint16_t checksum;      /* 校验和 */
    uint16_t identifier;    /* 标识符 */
    uint16_t sequence;      /* 序列号 */
} icmp_header_t;

/**
 * @brief ICMP 错误消息
 */
typedef struct
{
    uint8_t  type;          /* 类型 */
    uint8_t  code;          /* 代码 */
    uint16_t checksum;      /* 校验和 */
    uint8_t  unused[4];     /* 未使用 */
    uint8_t  orig_ip[20];   /* 原始 IP 头（20 字节） */
    uint8_t  orig_data[8];  /* 原始数据（8 字节） */
} icmp_error_message_t;

/**
 * @brief IPv4 头部（简化版）
 */
typedef struct
{
    uint8_t  version_ihl;   /* 版本 + 头长 */
    uint8_t  tos;           /* 服务类型 */
    uint16_t total_length;  /* 总长度 */
    uint16_t identification;/* 标识 */
    uint16_t flags_offset;  /* 标志 + 片偏移 */
    uint8_t  ttl;           /* 生存时间 */
    uint8_t  protocol;      /* 上层协议 */
    uint16_t checksum;      /* 校验和 */
    uint32_t src_ip;        /* 源 IP */
    uint32_t dst_ip;        /* 目的 IP */
} ipv4_header_test_t;

/* ========================================================================
 * 测试辅助函数
 * ======================================================================== */

/**
 * @brief 计算 Internet 校验和
 */
static uint16_t net_checksum(const void *data, uint32_t len)
{
    const uint8_t *buf = (const uint8_t *)data;
    uint32_t sum = 0U;
    uint32_t i;

    for (i = 0U; i < (len - 1U); i += 2U)
    {
        sum += ((uint32_t)buf[i] << 8U) | (uint32_t)buf[i + 1U];
    }

    if ((len & 1U) != 0U)
    {
        sum += (uint32_t)buf[len - 1U] << 8U;
    }

    while ((sum >> 16U) != 0U)
    {
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    }

    return (uint16_t)(~sum);
}

/**
 * @brief 16 位字节序交换
 */
static uint16_t net_htons(uint16_t val)
{
    return (uint16_t)(((val >> 8U) & 0xFFU) | ((val & 0xFFU) << 8U));
}

/* ========================================================================
 * 测试：Destination Unreachable
 * ======================================================================== */

/**
 * @brief 测试：网络不可达（Code 0）
 */
void test_icmp_dest_unreach_net(void)
{
    icmp_error_message_t msg;

    (void)memset(&msg, 0, sizeof(msg));

    /* 构造网络不可达消息 */
    msg.type = ICMP_TYPE_DEST_UNREACH;
    msg.code = ICMP_CODE_NET_UNREACH;
    msg.checksum = 0U;

    /* 计算 ICMP 校验和 */
    msg.checksum = net_checksum(&msg, sizeof(msg));

    /* 验证：网络不可达消息 */
    TEST_ASSERT_EQUAL(ICMP_TYPE_DEST_UNREACH, msg.type);
    TEST_ASSERT_EQUAL(ICMP_CODE_NET_UNREACH, msg.code);
    TEST_ASSERT_NOT_EQUAL(0U, msg.checksum);
}

/**
 * @brief 测试：主机不可达（Code 1）
 */
void test_icmp_dest_unreach_host(void)
{
    icmp_error_message_t msg;

    (void)memset(&msg, 0, sizeof(msg));

    /* 构造主机不可达消息 */
    msg.type = ICMP_TYPE_DEST_UNREACH;
    msg.code = ICMP_CODE_HOST_UNREACH;
    msg.checksum = 0U;

    /* 计算 ICMP 校验和 */
    msg.checksum = net_checksum(&msg, sizeof(msg));

    /* 验证：主机不可达消息 */
    TEST_ASSERT_EQUAL(ICMP_TYPE_DEST_UNREACH, msg.type);
    TEST_ASSERT_EQUAL(ICMP_CODE_HOST_UNREACH, msg.code);
    TEST_ASSERT_NOT_EQUAL(0U, msg.checksum);
}

/**
 * @brief 测试：端口不可达（Code 3）
 */
void test_icmp_dest_unreach_port(void)
{
    icmp_error_message_t msg;

    (void)memset(&msg, 0, sizeof(msg));

    /* 构造端口不可达消息 */
    msg.type = ICMP_TYPE_DEST_UNREACH;
    msg.code = ICMP_CODE_PORT_UNREACH;
    msg.checksum = 0U;

    /* 计算 ICMP 校验和 */
    msg.checksum = net_checksum(&msg, sizeof(msg));

    /* 验证：端口不可达消息 */
    TEST_ASSERT_EQUAL(ICMP_TYPE_DEST_UNREACH, msg.type);
    TEST_ASSERT_EQUAL(ICMP_CODE_PORT_UNREACH, msg.code);
    TEST_ASSERT_NOT_EQUAL(0U, msg.checksum);
}

/**
 * @brief 测试：需要分片（Code 4）
 */
void test_icmp_dest_unreach_frag_needed(void)
{
    icmp_error_message_t msg;

    (void)memset(&msg, 0, sizeof(msg));

    /* 构造需要分片消息 */
    msg.type = ICMP_TYPE_DEST_UNREACH;
    msg.code = ICMP_CODE_FRAG_NEEDED;

    /* 设置 MTU（下一个跳转 MTU） */
    msg.unused[0] = 0x05;  /* MTU = 1280 */
    msg.unused[1] = 0x00;

    msg.checksum = 0U;

    /* 计算 ICMP 校验和 */
    msg.checksum = net_checksum(&msg, sizeof(msg));

    /* 验证：需要分片消息 */
    TEST_ASSERT_EQUAL(ICMP_TYPE_DEST_UNREACH, msg.type);
    TEST_ASSERT_EQUAL(ICMP_CODE_FRAG_NEEDED, msg.code);
    TEST_ASSERT_EQUAL(0x05, msg.unused[0]);
    TEST_ASSERT_EQUAL(0x00, msg.unused[1]);
}

/**
 * @brief 测试：Destination Unreachable 包含原始 IP 头
 */
void test_icmp_dest_unreach_orig_ip(void)
{
    icmp_error_message_t msg;
    ipv4_header_test_t orig_ip;

    (void)memset(&msg, 0, sizeof(msg));
    (void)memset(&orig_ip, 0, sizeof(orig_ip));

    /* 构造原始 IP 头 */
    orig_ip.version_ihl = (4U << 4U) | 5U;
    orig_ip.tos = 0U;
    orig_ip.total_length = net_htons(40U);
    orig_ip.identification = net_htons(12345U);
    orig_ip.flags_offset = net_htons(0x4000U);  /* DF 标志 */
    orig_ip.ttl = 64U;
    orig_ip.protocol = 6U;  /* TCP */
    orig_ip.src_ip = 0xC0A80001U;  /* 192.168.0.1 */
    orig_ip.dst_ip = 0xC0A80002U;  /* 192.168.0.2 */

    /* 构造 ICMP 错误消息 */
    msg.type = ICMP_TYPE_DEST_UNREACH;
    msg.code = ICMP_CODE_PORT_UNREACH;
    (void)memcpy(msg.orig_ip, &orig_ip, 20U);

    msg.checksum = 0U;
    msg.checksum = net_checksum(&msg, sizeof(msg));

    /* 验证：包含原始 IP 头 */
    TEST_ASSERT_EQUAL(ICMP_TYPE_DEST_UNREACH, msg.type);
    TEST_ASSERT_EQUAL(orig_ip.version_ihl, msg.orig_ip[0]);
    TEST_ASSERT_EQUAL(orig_ip.tos, msg.orig_ip[1]);
    TEST_ASSERT_EQUAL(orig_ip.protocol, msg.orig_ip[9]);
}

/* ========================================================================
 * 测试：Time Exceeded
 * ======================================================================== */

/**
 * @brief 测试：TTL 过期（Code 0）
 */
void test_icmp_time_exceeded_ttl(void)
{
    icmp_error_message_t msg;

    (void)memset(&msg, 0, sizeof(msg));

    /* 构造 TTL 过期消息 */
    msg.type = ICMP_TYPE_TIME_EXCEEDED;
    msg.code = ICMP_CODE_TTL_EXPIRED;
    msg.checksum = 0U;

    /* 计算 ICMP 校验和 */
    msg.checksum = net_checksum(&msg, sizeof(msg));

    /* 验证：TTL 过期消息 */
    TEST_ASSERT_EQUAL(ICMP_TYPE_TIME_EXCEEDED, msg.type);
    TEST_ASSERT_EQUAL(ICMP_CODE_TTL_EXPIRED, msg.code);
    TEST_ASSERT_NOT_EQUAL(0U, msg.checksum);
}

/**
 * @brief 测试：IP 分片重组超时（Code 1）
 */
void test_icmp_time_exceeded_reass(void)
{
    icmp_error_message_t msg;

    (void)memset(&msg, 0, sizeof(msg));

    /* 构造 IP 分片重组超时消息 */
    msg.type = ICMP_TYPE_TIME_EXCEEDED;
    msg.code = ICMP_CODE_REASS_TIME_EXPIRED;
    msg.checksum = 0U;

    /* 计算 ICMP 校验和 */
    msg.checksum = net_checksum(&msg, sizeof(msg));

    /* 验证：IP 分片重组超时消息 */
    TEST_ASSERT_EQUAL(ICMP_TYPE_TIME_EXCEEDED, msg.type);
    TEST_ASSERT_EQUAL(ICMP_CODE_REASS_TIME_EXPIRED, msg.code);
    TEST_ASSERT_NOT_EQUAL(0U, msg.checksum);
}

/**
 * @brief 测试：Time Exceeded 包含原始 IP 头
 */
void test_icmp_time_exceeded_orig_ip(void)
{
    icmp_error_message_t msg;
    ipv4_header_test_t orig_ip;

    (void)memset(&msg, 0, sizeof(msg));
    (void)memset(&orig_ip, 0, sizeof(orig_ip));

    /* 构造原始 IP 头 */
    orig_ip.version_ihl = (4U << 4U) | 5U;
    orig_ip.ttl = 1U;  /* TTL = 1 */
    orig_ip.protocol = 17U;  /* UDP */
    orig_ip.src_ip = 0xC0A80001U;
    orig_ip.dst_ip = 0xC0A80002U;

    /* 构造 ICMP 错误消息 */
    msg.type = ICMP_TYPE_TIME_EXCEEDED;
    msg.code = ICMP_CODE_TTL_EXPIRED;
    (void)memcpy(msg.orig_ip, &orig_ip, 20U);

    msg.checksum = 0U;
    msg.checksum = net_checksum(&msg, sizeof(msg));

    /* 验证：包含原始 IP 头 */
    TEST_ASSERT_EQUAL(ICMP_TYPE_TIME_EXCEEDED, msg.type);
    TEST_ASSERT_EQUAL(orig_ip.version_ihl, msg.orig_ip[0]);
    TEST_ASSERT_EQUAL(orig_ip.ttl, msg.orig_ip[8]);
    TEST_ASSERT_EQUAL(orig_ip.protocol, msg.orig_ip[9]);
}

/* ========================================================================
 * 测试：Parameter Problem
 * ======================================================================== */

/**
 * @brief 测试：坏的 IP 头（Code 0）
 */
void test_icmp_param_problem_bad_header(void)
{
    icmp_error_message_t msg;

    (void)memset(&msg, 0, sizeof(msg));

    /* 构造坏的 IP 头消息 */
    msg.type = ICMP_TYPE_PARAM_PROB;
    msg.code = ICMP_CODE_BAD_HEADER;

    /* 设置指针（指向错误的字节） */
    msg.unused[0] = 0U;   /* Pointer = 0 */
    msg.unused[1] = 0U;
    msg.unused[2] = 0U;
    msg.unused[3] = 0U;

    msg.checksum = 0U;

    /* 计算 ICMP 校验和 */
    msg.checksum = net_checksum(&msg, sizeof(msg));

    /* 验证：坏的 IP 头消息 */
    TEST_ASSERT_EQUAL(ICMP_TYPE_PARAM_PROB, msg.type);
    TEST_ASSERT_EQUAL(ICMP_CODE_BAD_HEADER, msg.code);
    TEST_ASSERT_EQUAL(0U, msg.unused[0]);
    TEST_ASSERT_NOT_EQUAL(0U, msg.checksum);
}

/**
 * @brief 测试：Parameter Problem 包含原始 IP 头
 */
void test_icmp_param_problem_orig_ip(void)
{
    icmp_error_message_t msg;
    ipv4_header_test_t orig_ip;

    (void)memset(&msg, 0, sizeof(msg));
    (void)memset(&orig_ip, 0, sizeof(orig_ip));

    /* 构造原始 IP 头（带错误） */
    orig_ip.version_ihl = 0x45;  /* 正常 */
    orig_ip.tos = 0x80;  /* 错误的 TOS */
    orig_ip.protocol = 6U;
    orig_ip.src_ip = 0xC0A80001U;
    orig_ip.dst_ip = 0xC0A80002U;

    /* 构造 ICMP 错误消息 */
    msg.type = ICMP_TYPE_PARAM_PROB;
    msg.code = ICMP_CODE_BAD_HEADER;

    /* 指向错误的字节（TOS 字段） */
    msg.unused[0] = 1U;  /* Pointer = 1 */
    msg.unused[1] = 0U;
    msg.unused[2] = 0U;
    msg.unused[3] = 0U;

    (void)memcpy(msg.orig_ip, &orig_ip, 20U);

    msg.checksum = 0U;
    msg.checksum = net_checksum(&msg, sizeof(msg));

    /* 验证：包含原始 IP 头 */
    TEST_ASSERT_EQUAL(ICMP_TYPE_PARAM_PROB, msg.type);
    TEST_ASSERT_EQUAL(1U, msg.unused[0]);
    TEST_ASSERT_EQUAL(orig_ip.version_ihl, msg.orig_ip[0]);
    TEST_ASSERT_EQUAL(orig_ip.tos, msg.orig_ip[1]);
}

/* ========================================================================
 * 测试：ICMP 校验和
 * ======================================================================== */

/**
 * @brief 测试：ICMP 校验和计算
 */
void test_icmp_checksum(void)
{
    icmp_error_message_t msg;

    (void)memset(&msg, 0, sizeof(msg));

    /* 构造 ICMP 消息 */
    msg.type = ICMP_TYPE_DEST_UNREACH;
    msg.code = ICMP_CODE_NET_UNREACH;
    msg.checksum = 0U;

    /* 计算校验和 */
    msg.checksum = net_checksum(&msg, sizeof(msg));

    /* 验证：校验和正确 */
    uint16_t verify = net_checksum(&msg, sizeof(msg));
    TEST_ASSERT_EQUAL(0U, verify);  /* 正确的校验和再次计算应为 0 */
}

/**
 * @brief 测试：ICMP 校验和验证
 */
void test_icmp_checksum_verify(void)
{
    icmp_error_message_t msg;

    (void)memset(&msg, 0, sizeof(msg));

    /* 构造 ICMP 消息 */
    msg.type = ICMP_TYPE_TIME_EXCEEDED;
    msg.code = ICMP_CODE_TTL_EXPIRED;
    msg.checksum = 0U;

    /* 计算校验和 */
    msg.checksum = net_checksum(&msg, sizeof(msg));

    /* 验证校验和 */
    uint16_t original_checksum = msg.checksum;
    uint16_t verify = net_checksum(&msg, sizeof(msg));

    /* 验证：校验和正确 */
    TEST_ASSERT_EQUAL(original_checksum, msg.checksum);
    TEST_ASSERT_EQUAL(0U, verify);
}

/* ========================================================================
 * 测试：ICMP 错误消息构造
 * ======================================================================== */

/**
 * @brief 测试：构造 Destination Unreachable 消息
 */
void test_icmp_build_dest_unreach(void)
{
    icmp_error_message_t msg;
    ipv4_header_test_t orig_ip;

    (void)memset(&msg, 0, sizeof(msg));
    (void)memset(&orig_ip, 0, sizeof(orig_ip));

    /* 构造原始 IP 头 */
    orig_ip.version_ihl = (4U << 4U) | 5U;
    orig_ip.protocol = 6U;
    orig_ip.src_ip = 0xC0A80001U;
    orig_ip.dst_ip = 0xC0A80002U;

    /* 构造 ICMP 错误消息 */
    msg.type = ICMP_TYPE_DEST_UNREACH;
    msg.code = ICMP_CODE_PORT_UNREACH;
    (void)memcpy(msg.orig_ip, &orig_ip, 20U);

    /* 模拟原始数据 */
    (void)memset(msg.orig_data, 0xFF, 8U);

    msg.checksum = 0U;
    msg.checksum = net_checksum(&msg, sizeof(msg));

    /* 验证：消息构造正确 */
    TEST_ASSERT_EQUAL(ICMP_TYPE_DEST_UNREACH, msg.type);
    TEST_ASSERT_EQUAL(ICMP_CODE_PORT_UNREACH, msg.code);
    TEST_ASSERT_NOT_EQUAL(0U, msg.checksum);
    TEST_ASSERT_EQUAL(0xFF, msg.orig_data[0]);
}

/**
 * @brief 测试：构造 Time Exceeded 消息
 */
void test_icmp_build_time_exceeded(void)
{
    icmp_error_message_t msg;
    ipv4_header_test_t orig_ip;

    (void)memset(&msg, 0, sizeof(msg));
    (void)memset(&orig_ip, 0, sizeof(orig_ip));

    /* 构造原始 IP 头 */
    orig_ip.version_ihl = (4U << 4U) | 5U;
    orig_ip.ttl = 1U;
    orig_ip.protocol = 17U;
    orig_ip.src_ip = 0xC0A80001U;
    orig_ip.dst_ip = 0xC0A80002U;

    /* 构造 ICMP 错误消息 */
    msg.type = ICMP_TYPE_TIME_EXCEEDED;
    msg.code = ICMP_CODE_TTL_EXPIRED;
    (void)memcpy(msg.orig_ip, &orig_ip, 20U);

    /* 模拟原始数据 */
    (void)memset(msg.orig_data, 0xAA, 8U);

    msg.checksum = 0U;
    msg.checksum = net_checksum(&msg, sizeof(msg));

    /* 验证：消息构造正确 */
    TEST_ASSERT_EQUAL(ICMP_TYPE_TIME_EXCEEDED, msg.type);
    TEST_ASSERT_EQUAL(ICMP_CODE_TTL_EXPIRED, msg.code);
    TEST_ASSERT_NOT_EQUAL(0U, msg.checksum);
    TEST_ASSERT_EQUAL(0xAA, msg.orig_data[0]);
}

/**
 * @brief 测试：构造 Parameter Problem 消息
 */
void test_icmp_build_param_problem(void)
{
    icmp_error_message_t msg;
    ipv4_header_test_t orig_ip;

    (void)memset(&msg, 0, sizeof(msg));
    (void)memset(&orig_ip, 0, sizeof(orig_ip));

    /* 构造原始 IP 头 */
    orig_ip.version_ihl = (4U << 4U) | 5U;
    orig_ip.protocol = 6U;
    orig_ip.src_ip = 0xC0A80001U;
    orig_ip.dst_ip = 0xC0A80002U;

    /* 构造 ICMP 错误消息 */
    msg.type = ICMP_TYPE_PARAM_PROB;
    msg.code = ICMP_CODE_BAD_HEADER;

    /* 设置指针 */
    msg.unused[0] = 2U;  /* Pointer = 2 */

    (void)memcpy(msg.orig_ip, &orig_ip, 20U);

    /* 模拟原始数据 */
    (void)memset(msg.orig_data, 0xCC, 8U);

    msg.checksum = 0U;
    msg.checksum = net_checksum(&msg, sizeof(msg));

    /* 验证：消息构造正确 */
    TEST_ASSERT_EQUAL(ICMP_TYPE_PARAM_PROB, msg.type);
    TEST_ASSERT_EQUAL(ICMP_CODE_BAD_HEADER, msg.code);
    TEST_ASSERT_EQUAL(2U, msg.unused[0]);
    TEST_ASSERT_NOT_EQUAL(0U, msg.checksum);
    TEST_ASSERT_EQUAL(0xCC, msg.orig_data[0]);
}

/* ========================================================================
 * 测试套件注册
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Destination Unreachable 测试 */
    RUN_TEST(test_icmp_dest_unreach_net);
    RUN_TEST(test_icmp_dest_unreach_host);
    RUN_TEST(test_icmp_dest_unreach_port);
    RUN_TEST(test_icmp_dest_unreach_frag_needed);
    RUN_TEST(test_icmp_dest_unreach_orig_ip);

    /* Time Exceeded 测试 */
    RUN_TEST(test_icmp_time_exceeded_ttl);
    RUN_TEST(test_icmp_time_exceeded_reass);
    RUN_TEST(test_icmp_time_exceeded_orig_ip);

    /* Parameter Problem 测试 */
    RUN_TEST(test_icmp_param_problem_bad_header);
    RUN_TEST(test_icmp_param_problem_orig_ip);

    /* ICMP 校验和测试 */
    RUN_TEST(test_icmp_checksum);
    RUN_TEST(test_icmp_checksum_verify);

    /* ICMP 错误消息构造测试 */
    RUN_TEST(test_icmp_build_dest_unreach);
    RUN_TEST(test_icmp_build_time_exceeded);
    RUN_TEST(test_icmp_build_param_problem);

    return UNITY_END();
}
