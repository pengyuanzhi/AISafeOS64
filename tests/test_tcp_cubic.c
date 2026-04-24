/**
 * @file test_tcp_cubic.c
 * @brief CUBIC 拥塞控制算法测试
 *
 * 本测试文件测试 CUBIC 拥塞控制算法的正确性
 */

#include "unity.h"
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * CUBIC 算法定义
 * ======================================================================== */

/** @brief CUBIC 算法参数 */
#define CUBIC_ALPHA              0.7f   /**< @brief 慢启动阈值调节因子 */
#define CUBIC_BETA               0.7f   /**< @brief 减速因子 */
#define CUBIC_C                  0.4f   /**< @brief 缩放因子 */

/** @brief TCP 最大段大小 */
#define TCP_MSS                  1460U

/** @brief 时间单位（毫秒） */
#define CUBIC_TIME_UNIT_MS       1U

/* ========================================================================
 * 数据结构定义
 * ======================================================================== */

/** @brief CUBIC 拥塞控制状态 */
typedef struct
{
    uint32_t cwnd;               /**< @brief 当前拥塞窗口 */
    uint32_t ssthresh;           /**< @brief 慢启动阈值 */
    uint32_t w_max;              /**< @brief 峰值窗口 */
    uint32_t w_last_max;         /**< @brief 最后峰值窗口 */
    uint32_t epoch_start;        /**< @brief 时代开始时间 */
    uint32_t last_cwnd;          /**< @brief 最后拥塞窗口 */
    uint32_t origin_point;       /**< @brief 原点 */
    uint32_t t;                  /**< @brief 时间距离 */
    float     k;                 /**< @brief 参数 K */
    uint8_t  state;              /**< @brief 状态 */
} cubic_state_t;

/* 状态定义 */
#define CUBIC_STATE_SLOW_START   0U      /**< @brief 慢启动 */
#define CUBIC_STATE_CONGESTION_AVOIDANCE  1U /**< @brief 拥塞避免 */
#define CUBIC_STATE_FAST_RECOVERY 2U      /**< @brief 快速恢复 */

/* ========================================================================
 * CUBIC 算法函数声明
 * ======================================================================== */

/**
 * @brief 初始化 CUBIC 状态
 *
 * @param state CUBIC 状态
 */
static void cubic_init(cubic_state_t *state);

/**
 * @brief CUBIC 窗口计算
 *
 * @param state CUBIC 状态
 * @param current_time 当前时间（毫秒）
 * @return 计算后的窗口大小
 */
static uint32_t cubic_calc_window(cubic_state_t *state, uint32_t current_time);

/**
 * @brief CUBIC 慢启动阶段
 *
 * @param state CUBIC 状态
 * @return 更新后的窗口大小
 */
static uint32_t cubic_slow_start(cubic_state_t *state);

/**
 * @brief CUBIC 拥塞避免阶段
 *
 * @param state CUBIC 状态
 * @param current_time 当前时间（毫秒）
 * @return 更新后的窗口大小
 */
static uint32_t cubic_congestion_avoidance(cubic_state_t *state,
                                            uint32_t current_time);

/**
 * @brief CUBIC 快速重传
 *
 * @param state CUBIC 状态
 * @return 更新后的窗口大小
 */
static uint32_t cubic_fast_retransmit(cubic_state_t *state);

/**
 * @brief CUBIC 快速恢复
 *
 * @param state CUBIC 状态
 * @return 更新后的窗口大小
 */
static uint32_t cubic_fast_recovery(cubic_state_t *state);

/**
 * @brief CUBIC 超时
 *
 * @param state CUBIC 状态
 * @return 更新后的窗口大小
 */
static uint32_t cubic_timeout(cubic_state_t *state);

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
 * @brief 测试 CUBIC 窗口计算
 */
void test_cubic_window_calc(void)
{
    cubic_state_t state;
    uint32_t window;

    /* 初始化 CUBIC 状态 */
    cubic_init(&state);

    /* 测试初始窗口 */
    window = cubic_calc_window(&state, 1000);
    TEST_ASSERT_EQUAL_UINT32(TCP_MSS, window);

    /* 测试窗口增长 */
    state.w_max = 10 * TCP_MSS;
    window = cubic_calc_window(&state, 2000);
    TEST_ASSERT_GREATER_THAN(TCP_MSS, window);

    /* 测试峰值窗口恢复 */
    state.w_max = 10 * TCP_MSS;
    state.ssthresh = 5 * TCP_MSS;
    window = cubic_calc_window(&state, 3000);
    TEST_ASSERT_LESS_OR_EQUAL(10 * TCP_MSS, window);
}

/**
 * @brief 测试 CUBIC 慢启动
 */
void test_cubic_slow_start(void)
{
    cubic_state_t state;
    uint32_t window;

    /* 初始化 CUBIC 状态 */
    cubic_init(&state);
    state.state = CUBIC_STATE_SLOW_START;

    /* 测试慢启动阶段窗口增长 */
    window = cubic_slow_start(&state);
    TEST_ASSERT_EQUAL_UINT32(2 * TCP_MSS, window);

    window = cubic_slow_start(&state);
    TEST_ASSERT_EQUAL_UINT32(4 * TCP_MSS, window);

    /* 测试慢启动到拥塞避免的转换 */
    while (state.cwnd < state.ssthresh)
    {
        window = cubic_slow_start(&state);
    }
    TEST_ASSERT_EQUAL_UINT32(CUBIC_STATE_CONGESTION_AVOIDANCE, state.state);
}

/**
 * @brief 测试 CUBIC 拥塞避免
 */
void test_cubic_congestion_avoidance(void)
{
    cubic_state_t state;
    uint32_t window;
    uint32_t prev_window;

    /* 初始化 CUBIC 状态 */
    cubic_init(&state);
    state.state = CUBIC_STATE_CONGESTION_AVOIDANCE;
    state.cwnd = state.ssthresh;
    state.epoch_start = 1000;

    /* 测试拥塞避免阶段窗口增长 */
    prev_window = state.cwnd;
    window = cubic_congestion_avoidance(&state, 2000);
    TEST_ASSERT_GREATER_THAN(prev_window, window);

    /* 测试 CUBIC 立方函数计算 */
    prev_window = window;
    window = cubic_congestion_avoidance(&state, 3000);
    TEST_ASSERT_GREATER_THAN(prev_window, window);

    /* 测试窗口平滑增长 */
    prev_window = window;
    window = cubic_congestion_avoidance(&state, 4000);
    TEST_ASSERT_GREATER_THAN(prev_window, window);
}

/**
 * @brief 测试 CUBIC 快速重传
 */
void test_cubic_fast_retransmit(void)
{
    cubic_state_t state;
    uint32_t window;

    /* 初始化 CUBIC 状态 */
    cubic_init(&state);
    state.cwnd = 10 * TCP_MSS;
    state.w_max = 8 * TCP_MSS;

    /* 测试收到 3 个重复 ACK 的处理 */
    window = cubic_fast_retransmit(&state);
    TEST_ASSERT_EQUAL_UINT32(CUBIC_STATE_FAST_RECOVERY, state.state);

    /* 测试窗口减半 */
    TEST_ASSERT_LESS_THAN(10 * TCP_MSS, window);

    /* 测试慢启动阈值更新 */
    TEST_ASSERT_LESS_THAN(state.cwnd, state.ssthresh);
}

/**
 * @brief 测试 CUBIC 快速恢复
 */
void test_cubic_fast_recovery(void)
{
    cubic_state_t state;
    uint32_t window;

    /* 初始化 CUBIC 状态 */
    cubic_init(&state);
    state.state = CUBIC_STATE_FAST_RECOVERY;
    state.cwnd = state.ssthresh + 3 * TCP_MSS;
    state.epoch_start = 1000;

    /* 测试快速恢复阶段窗口增长 */
    window = cubic_fast_recovery(&state);
    TEST_ASSERT_GREATER_THAN(state.ssthresh, window);

    /* 测试从快速恢复返回拥塞避免 */
    while (state.state == CUBIC_STATE_FAST_RECOVERY)
    {
        window = cubic_fast_recovery(&state);
    }
    TEST_ASSERT_EQUAL_UINT32(CUBIC_STATE_CONGESTION_AVOIDANCE, state.state);

    /* 测试完整恢复过程 */
    TEST_ASSERT_GREATER_OR_EQUAL(state.ssthresh, state.cwnd);
}

/**
 * @brief 测试 CUBIC 超时
 */
void test_cubic_timeout(void)
{
    cubic_state_t state;
    uint32_t window;

    /* 初始化 CUBIC 状态 */
    cubic_init(&state);
    state.cwnd = 10 * TCP_MSS;
    state.w_max = 8 * TCP_MSS;

    /* 测试超时处理 */
    window = cubic_timeout(&state);
    TEST_ASSERT_EQUAL_UINT32(CUBIC_STATE_SLOW_START, state.state);

    /* 测试窗口重置为 1 MSS */
    TEST_ASSERT_EQUAL_UINT32(TCP_MSS, window);

    /* 测试重新进入慢启动 */
    window = cubic_slow_start(&state);
    TEST_ASSERT_EQUAL_UINT32(2 * TCP_MSS, window);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_cubic_window_calc);
    RUN_TEST(test_cubic_slow_start);
    RUN_TEST(test_cubic_congestion_avoidance);
    RUN_TEST(test_cubic_fast_retransmit);
    RUN_TEST(test_cubic_fast_recovery);
    RUN_TEST(test_cubic_timeout);

    return UNITY_END();
}
