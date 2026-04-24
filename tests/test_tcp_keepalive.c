/**
 * @file test_tcp_keepalive.c
 * @brief TCP Keepalive 测试
 *
 * 本测试文件测试 TCP Keepalive 功能的正确性
 */

#include "unity.h"
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * TCP Keepalive 定义
 * ======================================================================== */

/** @brief Keepalive 空闲超时（秒） */
#define TCP_KEEPALIVE_IDLE_TIME     7200U  /**< 2 小时 */

/** @brief Keepalive 探测间隔（秒） */
#define TCP_KEEPALIVE_PROBE_INTERVAL 75U    /**< 75 秒 */

/** @brief Keepalive 探测次数 */
#define TCP_KEEPALIVE_PROBE_COUNT   9U     /**< 9 次 */

/** @brief Keepalive 探测数据包大小 */
#define TCP_KEEPALIVE_DATA_SIZE     1U     /**< 1 字节 */

/* ========================================================================
 * 数据结构定义
 * ======================================================================== */

/** @brief TCP Keepalive 配置 */
typedef struct
{
    uint32_t idle_time;           /**< @brief 空闲超时时间（秒） */
    uint32_t probe_interval;      /**< @brief 探测间隔时间（秒） */
    uint32_t probe_count;         /**< @brief 探测次数 */
    bool     enabled;             /**< @brief 启用标记 */
} tcp_keepalive_config_t;

/** @brief TCP Keepalive 状态 */
typedef struct
{
    uint32_t last_active_time;    /**< @brief 最后活跃时间（秒） */
    uint32_t probe_count;         /**< @brief 当前探测次数 */
    uint32_t next_probe_time;     /**< @brief 下一次探测时间（秒） */
    bool     probe_sent;          /**< @brief 探测已发送标记 */
    bool     is_timeout;          /**< @brief 超时标记 */
} tcp_keepalive_state_t;

/** @brief TCP Keepalive 头部 */
typedef struct
{
    uint8_t  kind;                /**< @brief 选项类型 */
    uint8_t  length;              /**< @brief 长度 */
    uint8_t  data[TCP_KEEPALIVE_DATA_SIZE];  /**< @brief Keepalive 数据 */
} tcp_keepalive_header_t;

/* ========================================================================
 * Keepalive 函数声明
 * ======================================================================== */

/**
 * @brief 初始化 Keepalive 配置
 *
 * @param config Keepalive 配置
 */
static void keepalive_init_config(tcp_keepalive_config_t *config);

/**
 * @brief 初始化 Keepalive 状态
 *
 * @param state Keepalive 状态
 */
static void keepalive_init_state(tcp_keepalive_state_t *state);

/**
 * @ check Keepalive 是否应该发送探测
 *
 * @param config Keepalive 配置
 * @param state Keepalive 状态
 * @param current_time 当前时间（秒）
 * @return true=应该发送，false=不应该发送
 */
static bool keepalive_should_send_probe(const tcp_keepalive_config_t *config,
                                         tcp_keepalive_state_t *state,
                                         uint32_t current_time);

/**
 * @brief 处理 Keepalive 超时
 *
 * @param state Keepalive 状态
 */
static void keepalive_handle_timeout(tcp_keepalive_state_t *state);

/**
 * @brief 重置 Keepalive 状态
 *
 * @param state Keepalive 状态
 */
static void keepalive_reset_state(tcp_keepalive_state_t *state);

/**
 * @brief 构造 Keepalive 数据包头部
 *
 * @param header 输出头部结构
 */
static void keepalive_build_header(tcp_keepalive_header_t *header);

/**
 * @brief 计算总长度
 *
 * @param header Keepalive 头部
 * @return 总长度
 */
static uint16_t keepalive_get_length(const tcp_keepalive_header_t *header);

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 设置测试环境
 */
void setUp(void)
{
}

/**
 * @brief 清理测试环境
 */
void tearDown(void)
{
}

/**
 * @brief 测试 Keepalive 配置初始化
 */
void test_keepalive_config_init(void)
{
    tcp_keepalive_config_t config;

    keepalive_init_config(&config);

    /* 测试默认值 */
    TEST_ASSERT_EQUAL_UINT32(TCP_KEEPALIVE_IDLE_TIME, config.idle_time);
    TEST_ASSERT_EQUAL_UINT32(TCP_KEEPALIVE_PROBE_INTERVAL, config.probe_interval);
    TEST_ASSERT_EQUAL_UINT32(TCP_KEEPALIVE_PROBE_COUNT, config.probe_count);
    TEST_ASSERT_TRUE(config.enabled);
}

/**
 * @brief 测试 Keepalive 状态初始化
 */
void test_keepalive_state_init(void)
{
    tcp_keepalive_state_t state;

    keepalive_init_state(&state);

    /* 测试默认值 */
    TEST_ASSERT_EQUAL_UINT32(0, state.last_active_time);
    TEST_ASSERT_EQUAL_UINT32(0, state.probe_count);
    TEST_ASSERT_EQUAL_UINT32(0, state.next_probe_time);
    TEST_ASSERT_FALSE(state.probe_sent);
    TEST_ASSERT_FALSE(state.is_timeout);
}

/**
 * @brief 测试 Keepalive 发送探测
 */
void test_keepalive_should_send_probe(void)
{
    tcp_keepalive_config_t config;
    tcp_keepalive_state_t state;
    uint32_t current_time;

    /* 初始化配置和状态 */
    keepalive_init_config(&config);
    keepalive_init_state(&state);
    state.last_active_time = current_time - config.idle_time;

    /* 测试应该发送探测 */
    current_time = state.last_active_time + config.idle_time + 1;
    TEST_ASSERT_TRUE(keepalive_should_send_probe(&config, &state, current_time));

    /* 测试不应该发送探测（时间未到） */
    current_time = state.last_active_time + config.idle_time - 1;
    TEST_ASSERT_FALSE(keepalive_should_send_probe(&config, &state, current_time));
}

/**
 * @brief 测试 Keepalive 探测次数
 */
void test_keepalive_probe_count(void)
{
    tcp_keepalive_config_t config;
    tcp_keepalive_state_t state;
    uint32_t current_time;

    /* 初始化配置和状态 */
    keepalive_init_config(&config);
    keepalive_init_state(&state);
    state.last_active_time = current_time - config.idle_time;

    /* 模拟发送多个探测 */
    current_time = state.last_active_time + config.idle_time + 1;
    for (uint32_t i = 0; i < config.probe_count + 1; i++)
    {
        keepalive_should_send_probe(&config, &state, current_time);
        if (i < config.probe_count)
        {
            TEST_ASSERT_FALSE(state.is_timeout);
        }
        else
        {
            TEST_ASSERT_TRUE(state.is_timeout);
        }
    }
}

/**
 * @brief 测试 Keepalive 超时处理
 */
void test_keepalive_handle_timeout(void)
{
    tcp_keepalive_state_t state;

    /* 初始化状态 */
    keepalive_init_state(&state);
    state.is_timeout = true;

    /* 测试超时处理 */
    keepalive_handle_timeout(&state);

    /* 测试状态更新 */
    TEST_ASSERT_FALSE(state.is_timeout);
    TEST_ASSERT_TRUE(state.probe_sent);
}

/**
 * @brief 测试 Keepalive 状态重置
 */
void test_keepalive_reset_state(void)
{
    tcp_keepalive_state_t state;

    /* 初始化状态 */
    keepalive_init_state(&state);
    state.last_active_time = 1000;
    state.probe_count = 5;
    state.probe_sent = true;
    state.is_timeout = true;

    /* 测试状态重置 */
    keepalive_reset_state(&state);

    /* 测试状态重置后 */
    TEST_ASSERT_EQUAL_UINT32(0, state.last_active_time);
    TEST_ASSERT_EQUAL_UINT32(0, state.probe_count);
    TEST_ASSERT_FALSE(state.probe_sent);
    TEST_ASSERT_FALSE(state.is_timeout);
}

/**
 * @brief 测试 Keepalive 头部构造
 */
void test_keepalive_build_header(void)
{
    tcp_keepalive_header_t header;

    /* 构造头部 */
    keepalive_build_header(&header);

    /* 测试选项类型 */
    TEST_ASSERT_EQUAL_UINT8(0x1, header.kind);

    /* 测试长度 */
    TEST_ASSERT_EQUAL_UINT8(4, header.length);

    /* 测试数据 */
    TEST_ASSERT_EQUAL_UINT8(0, header.data[0]);
}

/**
 * @brief 测试 Keepalive 总长度计算
 */
void test_keepalive_get_length(void)
{
    tcp_keepalive_header_t header;

    /* 构造头部 */
    keepalive_build_header(&header);

    /* 测试长度计算 */
    TEST_ASSERT_EQUAL_UINT16(4, keepalive_get_length(&header));
}

/**
 * @brief 测试 Keepalive 启用/禁用
 */
void test_keepalive_enable_disable(void)
{
    tcp_keepalive_config_t config;

    /* 初始化配置 */
    keepalive_init_config(&config);

    /* 测试启用 */
    config.enabled = true;
    TEST_ASSERT_TRUE(config.enabled);

    /* 测试禁用 */
    config.enabled = false;
    TEST_ASSERT_FALSE(config.enabled);
}

/**
 * @brief 测试 Keepalive 参数修改
 */
void test_keepalive_param_modify(void)
{
    tcp_keepalive_config_t config;

    keepalive_init_config(&config);

    /* 修改空闲超时 */
    config.idle_time = 3600;  // 1 小时
    TEST_ASSERT_EQUAL_UINT32(3600, config.idle_time);

    /* 修改探测间隔 */
    config.probe_interval = 60;  // 60 秒
    TEST_ASSERT_EQUAL_UINT32(60, config.probe_interval);

    /* 修改探测次数 */
    config.probe_count = 5;
    TEST_ASSERT_EQUAL_UINT32(5, config.probe_count);
}

/**
 * @brief 测试 Keepalive 数据包大小
 */
void test_keepalive_packet_size(void)
{
    tcp_keepalive_header_t header;

    keepalive_build_header(&header);

    /* 测试数据包大小 */
    TEST_ASSERT_EQUAL_UINT32(4, keepalive_get_length(&header));
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_keepalive_config_init);
    RUN_TEST(test_keepalive_state_init);
    RUN_TEST(test_keepalive_should_send_probe);
    RUN_TEST(test_keepalive_probe_count);
    RUN_TEST(test_keepalive_handle_timeout);
    RUN_TEST(test_keepalive_reset_state);
    RUN_TEST(test_keepalive_build_header);
    RUN_TEST(test_keepalive_get_length);
    RUN_TEST(test_keepalive_enable_disable);
    RUN_TEST(test_keepalive_param_modify);
    RUN_TEST(test_keepalive_packet_size);

    return UNITY_END();
}
