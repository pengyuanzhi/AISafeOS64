/**
 * @file test_tcp_timestamp.c
 * @brief TCP 时间戳选项测试
 *
 * 本测试文件测试 TCP 时间戳选项（RFC 7323）的正确性
 */

#include "unity.h"
#include <stdint.h>
#include <string.h>
#include <time.h>

/* ========================================================================
 * TCP 时间戳选项定义
 * ======================================================================== */

/** @brief TCP 选项类型：时间戳 */
#define TCP_OPT_TIMESTAMP       8U

/** @brief TCP 时间戳选项长度 */
#define TCP_OPT_TIMESTAMP_LEN   10U

/** @brief 时间戳精度（毫秒） */
#define TCP_TIMESTAMP_PRECISION_MS   1U

/* ========================================================================
 * 数据结构定义
 * ======================================================================== */

/** @brief TCP 时间戳选项结构 */
typedef struct
{
    uint8_t  kind;              /**< @brief 选项类型 */
    uint8_t  length;            /**< @brief 长度 */
    uint32_t ts_val;            /**< @brief 时间戳值 */
    uint32_t ts_echo_rpl;       /**< @brief 时间戳回显 */
} tcp_timestamp_option_t;

/** @brief TCP 时间戳状态 */
typedef struct
{
    uint32_t ts_val;            /**< @brief 最后发送时间戳 */
    uint32_t ts_echo_rpl;       /**< @brief 最后回显时间戳 */
    uint32_t recent_ts;         /**< @brief 最近接收时间戳 */
    bool     enabled;           /**< @brief 启用标记 */
    uint32_t clock_offset;      /**< @brief 时钟偏移（用于测试） */
} tcp_timestamp_state_t;

/** @brief 序列号验证状态 */
typedef struct
{
    uint32_t last_seq;          /**< @brief 最后序列号 */
    uint32_t last_ts;           /**< @brief 最后时间戳 */
    uint32_t expected_ts;       /**< @brief 预期时间戳 */
} tcp_seq_verify_state_t;

/* ========================================================================
 * TCP 时间戳函数声明
 * ======================================================================== */

/**
 * @brief 初始化 TCP 时间戳状态
 *
 * @param state 时间戳状态
 */
static void tcp_timestamp_init(tcp_timestamp_state_t *state);

/**
 * @brief 获取当前时间戳（毫秒）
 *
 * @return 时间戳（毫秒）
 */
static uint32_t tcp_timestamp_get(void);

/**
 * @brief 构造 TCP 时间戳选项
 *
 * @param state 时间戳状态
 * @param opt 输出选项结构
 */
static void tcp_timestamp_build(tcp_timestamp_state_t *state,
                                tcp_timestamp_option_t *opt);

/**
 * @brief 解析 TCP 时间戳选项
 *
 * @param opt 输入选项结构
 * @param state 时间戳状态
 */
static void tcp_timestamp_parse(const tcp_timestamp_option_t *opt,
                                 tcp_timestamp_state_t *state);

/**
 * @brief 序列号验证（防止序列号预测攻击）
 *
 * @param verify_state 验证状态
 * @param seq_num 序列号
 * @param ts_val 时间戳值
 * @return true=通过验证，false=验证失败
 */
static bool tcp_seq_verify(tcp_seq_verify_state_t *verify_state,
                            uint32_t seq_num, uint32_t ts_val);

/**
 * @brief 检查时间戳有效性
 *
 * @param ts_val 时间戳值
 * @param expected_ts 预期时间戳
 * @return true=有效，false=无效
 */
static bool tcp_timestamp_is_valid(uint32_t ts_val, uint32_t expected_ts);

/**
 * @brief 更新回显时间戳
 *
 * @param state 时间戳状态
 * @param ts_val 时间戳值
 */
static void tcp_timestamp_update_echo(tcp_timestamp_state_t *state,
                                       uint32_t ts_val);

/**
 * @brief 计算往返时间（RTT）
 *
 * @param state 时间戳状态
 * @param ts_val 时间戳值
 * @param current_time 当前时间
 * @return RTT（毫秒）
 */
static uint32_t tcp_timestamp_calc_rtt(tcp_timestamp_state_t *state,
                                        uint32_t ts_val,
                                        uint32_t current_time);

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
 * @brief 测试时间戳获取
 */
void test_timestamp_get(void)
{
    uint32_t ts1, ts2;

    /* 测试时间戳单调递增 */
    ts1 = tcp_timestamp_get();
    TEST_ASSERT_GREATER_THAN(0, ts1);

    ts2 = tcp_timestamp_get();
    TEST_ASSERT_GREATER_OR_EQUAL(ts1, ts2);

    /* 测试时间戳精度 */
    TEST_ASSERT_LESS_THAN(1000, ts2 - ts1);  /* 小于 1 秒 */
}

/**
 * @brief 测试时间戳选项构造
 */
void test_timestamp_build(void)
{
    tcp_timestamp_state_t state;
    tcp_timestamp_option_t opt;

    /* 初始化时间戳状态 */
    tcp_timestamp_init(&state);

    /* 构造时间戳选项 */
    tcp_timestamp_build(&state, &opt);

    /* 测试选项类型 */
    TEST_ASSERT_EQUAL_UINT8(TCP_OPT_TIMESTAMP, opt.kind);

    /* 测试选项长度 */
    TEST_ASSERT_EQUAL_UINT8(TCP_OPT_TIMESTAMP_LEN, opt.length);

    /* 测试时间戳值 */
    TEST_ASSERT_GREATER_THAN(0, opt.ts_val);

    /* 测试回显时间戳 */
    TEST_ASSERT_GREATER_OR_EQUAL(0, opt.ts_echo_rpl);
}

/**
 * @brief 测试时间戳选项解析
 */
void test_timestamp_parse(void)
{
    tcp_timestamp_state_t state;
    tcp_timestamp_option_t opt;

    /* 初始化时间戳状态 */
    tcp_timestamp_init(&state);

    /* 构造时间戳选项 */
    opt.kind = TCP_OPT_TIMESTAMP;
    opt.length = TCP_OPT_TIMESTAMP_LEN;
    opt.ts_val = 1234567;
    opt.ts_echo_rpl = 7654321;

    /* 解析时间戳选项 */
    tcp_timestamp_parse(&opt, &state);

    /* 测试时间戳值更新 */
    TEST_ASSERT_EQUAL_UINT32(1234567, state.recent_ts);

    /* 测试回显时间戳更新 */
    TEST_ASSERT_EQUAL_UINT32(7654321, state.ts_echo_rpl);
}

/**
 * @brief 测试时间戳回显
 */
void test_timestamp_echo(void)
{
    tcp_timestamp_state_t state;
    tcp_timestamp_option_t opt1, opt2;

    /* 初始化时间戳状态 */
    tcp_timestamp_init(&state);

    /* 构造第一个时间戳选项 */
    tcp_timestamp_build(&state, &opt1);

    /* 更新回显时间戳 */
    tcp_timestamp_update_echo(&state, opt1.ts_val);

    /* 构造第二个时间戳选项 */
    tcp_timestamp_build(&state, &opt2);

    /* 测试回显时间戳正确 */
    TEST_ASSERT_EQUAL_UINT32(opt1.ts_val, opt2.ts_echo_rpl);
}

/**
 * @brief 测试序列号验证
 */
void test_seq_verify(void)
{
    tcp_seq_verify_state_t verify_state;
    uint32_t seq_num, ts_val;

    /* 初始化验证状态 */
    (void)memset(&verify_state, 0, sizeof(verify_state));

    /* 测试第一次序列号验证 */
    seq_num = 1000;
    ts_val = 1234567;
    TEST_ASSERT_TRUE(tcp_seq_verify(&verify_state, seq_num, ts_val));

    /* 测试序列号递增 */
    seq_num = 2000;
    ts_val = 1234568;
    TEST_ASSERT_TRUE(tcp_seq_verify(&verify_state, seq_num, ts_val));

    /* 测试序列号重复（回绕） */
    seq_num = 1000;
    ts_val = 1234569;
    TEST_ASSERT_TRUE(tcp_seq_verify(&verify_state, seq_num, ts_val));

    /* 测试序列号回绕检测 */
    verify_state.last_seq = 0xFFFFFFFF;
    seq_num = 0;
    ts_val = 1234570;
    TEST_ASSERT_TRUE(tcp_seq_verify(&verify_state, seq_num, ts_val));

    /* 测试序列号预测攻击检测 */
    verify_state.last_seq = 1000;
    seq_num = 5000;
    ts_val = 1234567;  /* 旧时间戳 */
    TEST_ASSERT_FALSE(tcp_seq_verify(&verify_state, seq_num, ts_val));
}

/**
 * @brief 测试时间戳有效性检查
 */
void test_timestamp_valid(void)
{
    uint32_t ts_val, expected_ts;

    /* 测试有效时间戳 */
    ts_val = 1234567;
    expected_ts = 1234567;
    TEST_ASSERT_TRUE(tcp_timestamp_is_valid(ts_val, expected_ts));

    /* 测试时间戳稍有偏差 */
    ts_val = 1234568;
    expected_ts = 1234567;
    TEST_ASSERT_TRUE(tcp_timestamp_is_valid(ts_val, expected_ts));

    /* 测试时间戳偏差较大（1 秒） */
    ts_val = 1235567;
    expected_ts = 1234567;
    TEST_ASSERT_TRUE(tcp_timestamp_is_valid(ts_val, expected_ts));

    /* 测试时间戳偏差过大（10 秒） */
    ts_val = 1244567;
    expected_ts = 1234567;
    TEST_ASSERT_FALSE(tcp_timestamp_is_valid(ts_val, expected_ts));

    /* 测试旧时间戳 */
    ts_val = 1234567;
    expected_ts = 1235567;
    TEST_ASSERT_FALSE(tcp_timestamp_is_valid(ts_val, expected_ts));
}

/**
 * @brief 测试 RTT 计算
 */
void test_timestamp_calc_rtt(void)
{
    tcp_timestamp_state_t state;
    uint32_t rtt, ts_val, current_time;

    /* 初始化时间戳状态 */
    tcp_timestamp_init(&state);

    /* 发送时间戳 */
    ts_val = tcp_timestamp_get();
    state.ts_val = ts_val;

    /* 模拟网络延迟（100ms） */
    current_time = ts_val + 100;

    /* 计算 RTT */
    rtt = tcp_timestamp_calc_rtt(&state, ts_val, current_time);

    /* 测试 RTT 正确性 */
    TEST_ASSERT_EQUAL_UINT32(100, rtt);

    /* 测试零延迟 */
    current_time = ts_val;
    rtt = tcp_timestamp_calc_rtt(&state, ts_val, current_time);
    TEST_ASSERT_EQUAL_UINT32(0, rtt);

    /* 测试大延迟（1 秒） */
    current_time = ts_val + 1000;
    rtt = tcp_timestamp_calc_rtt(&state, ts_val, current_time);
    TEST_ASSERT_EQUAL_UINT32(1000, rtt);
}

/**
 * @brief 测试时间戳回绕
 */
void test_timestamp_wraparound(void)
{
    tcp_timestamp_state_t state;
    tcp_timestamp_option_t opt;

    /* 初始化时间戳状态 */
    tcp_timestamp_init(&state);

    /* 设置时间戳接近最大值 */
    state.clock_offset = 0xFFFFFF00;

    /* 构造时间戳选项 */
    tcp_timestamp_build(&state, &opt);

    /* 测试时间戳回绕 */
    TEST_ASSERT_LESS_THAN(0xFF, opt.ts_val & 0xFF);

    /* 再次构造时间戳选项 */
    tcp_timestamp_build(&state, &opt);

    /* 测试时间戳继续递增 */
    TEST_ASSERT_GREATER_THAN(0, opt.ts_val & 0xFF);
}

/**
 * @brief 测试时间戳状态管理
 */
void test_timestamp_state(void)
{
    tcp_timestamp_state_t state;

    /* 初始化时间戳状态 */
    tcp_timestamp_init(&state);

    /* 测试初始状态 */
    TEST_ASSERT_TRUE(state.enabled);
    TEST_ASSERT_EQUAL_UINT32(0, state.ts_val);
    TEST_ASSERT_EQUAL_UINT32(0, state.ts_echo_rpl);
    TEST_ASSERT_EQUAL_UINT32(0, state.recent_ts);

    /* 更新时间戳状态 */
    state.ts_val = 1234567;
    state.ts_echo_rpl = 7654321;
    state.recent_ts = 9999999;

    /* 测试状态更新 */
    TEST_ASSERT_EQUAL_UINT32(1234567, state.ts_val);
    TEST_ASSERT_EQUAL_UINT32(7654321, state.ts_echo_rpl);
    TEST_ASSERT_EQUAL_UINT32(9999999, state.recent_ts);

    /* 禁用时间戳 */
    state.enabled = false;
    TEST_ASSERT_FALSE(state.enabled);

    /* 重新启用时间戳 */
    state.enabled = true;
    TEST_ASSERT_TRUE(state.enabled);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_timestamp_get);
    RUN_TEST(test_timestamp_build);
    RUN_TEST(test_timestamp_parse);
    RUN_TEST(test_timestamp_echo);
    RUN_TEST(test_seq_verify);
    RUN_TEST(test_timestamp_valid);
    RUN_TEST(test_timestamp_calc_rtt);
    RUN_TEST(test_timestamp_wraparound);
    RUN_TEST(test_timestamp_state);

    return UNITY_END();
}
