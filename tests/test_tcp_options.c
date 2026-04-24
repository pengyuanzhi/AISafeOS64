/**
 * @file test_tcp_options.c
 * @brief TCP 选项处理测试
 *
 * 本测试文件测试 TCP 选项的正确性
 */

#include "unity.h"
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * TCP 选项定义
 * ======================================================================== */

/** @brief TCP 选项类型：MSS */
#define TCP_OPT_MSS              2U

/** @brief TCP 选项类型：窗口缩放 */
#define TCP_OPT_WINDOW_SCALE     3U

/** @brief TCP 选项类型：SACK */
#define TCP_OPT_SACK              4U

/** @brief TCP 选项类型：Keepalive */
#define TCP_OPT_KEEPALIVE         1U

/* ========================================================================
 * 数据结构定义
 * ======================================================================== */

/** @brief TCP MSS 选项 */
typedef struct
{
    uint8_t kind;                /**< @brief 选项类型 */
    uint8_t length;              /**< @brief 长度 */
    uint16_t mss;                /**< @brief 最大段大小 */
} tcp_opt_mss_t;

/** @brief TCP 窗口缩放选项 */
typedef struct
{
    uint8_t kind;                /**< @brief 选项类型 */
    uint8_t length;              /**< @brief 长度 */
    uint8_t scale_factor;        /**< @brief 缩放因子 */
} tcp_opt_window_scale_t;

/** @brief TCP SACK 选项 */
typedef struct
{
    uint8_t kind;                /**< @brief 选项类型 */
    uint8_t length;              /**< @brief 长度 */
    uint8_t num_blocks;          /**< @brief SACK 块数量 */
    uint32_t left_edge[4];       /**< @brief SACK 左边界 */
    uint32_t right_edge[4];      /**< @brief SACK 右边界 */
} tcp_opt_sack_t;

/** @brief TCP 选项处理状态 */
typedef struct
{
    uint16_t mss;                /**< @brief 最大段大小 */
    uint8_t  window_scale;       /**< @brief 窗口缩放因子 */
    uint8_t  sack_permitted;     /**< @brief SACK 允许 */
    uint8_t  sack_blocks[4];     /**< @brief SACK 块数量 */
    uint32_t sack_left[4];       /**< @brief SACK 左边界 */
    uint32_t sack_right[4];      /**< @brief SACK 右边界 */
} tcp_options_state_t;

/* ========================================================================
 * TCP 选项函数声明
 * ======================================================================== */

/**
 * @brief 初始化 TCP 选项状态
 *
 * @param state 选项状态
 */
static void tcp_options_init(tcp_options_state_t *state);

/**
 * @brief 处理 MSS 选项
 *
 * @param state 选项状态
 * @param opt 输入选项
 */
static void tcp_options_process_mss(tcp_options_state_t *state,
                                     const tcp_opt_mss_t *opt);

/**
 * @brief 处理窗口缩放选项
 *
 * @param state 选项状态
 * @param opt 输入选项
 */
static void tcp_options_process_window_scale(tcp_options_state_t *state,
                                             const tcp_opt_window_scale_t *opt);

/**
 * @brief 处理 SACK 选项
 *
 * @param state 选项状态
 * @param opt 输入选项
 */
static void tcp_options_process_sack(tcp_options_state_t *state,
                                      const tcp_opt_sack_t *opt);

/**
 * @brief 构造 MSS 选项
 *
 * @param mss 最大段大小
 * @return MSS 选项结构
 */
static tcp_opt_mss_t tcp_options_build_mss(uint16_t mss);

/**
 * @brief 构造窗口缩放选项
 *
 * @param scale_factor 缩放因子
 * @return 窗口缩放选项结构
 */
static tcp_opt_window_scale_t tcp_options_build_window_scale(uint8_t scale_factor);

/**
 * @brief 构造 SACK 选项
 *
 * @param num_blocks SACK 块数量
 * @param left_edges SATCH 左边界数组
 * @param right_edges SACK 右边界数组
 * @return SACK 选项结构
 */
static tcp_opt_sack_t tcp_options_build_sack(uint8_t num_blocks,
                                              const uint32_t *left_edges,
                                              const uint32_t *right_edges);

/**
 * @brief 检查选项是否存在
 *
 * @param buffer 输入缓冲区
 * @param length 缓冲区长度
 * @param kind 选项类型
 * @return true=存在，false=不存在
 */
static bool tcp_options_exists(const uint8_t *buffer, uint16_t length, uint8_t kind);

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
 * @brief 测试 TCP 选项状态初始化
 */
void test_tcp_options_init(void)
{
    tcp_options_state_t state;

    tcp_options_init(&state);

    /* 测试默认值 */
    TEST_ASSERT_EQUAL_UINT16(1460, state.mss);  /* 默认 MSS */
    TEST_ASSERT_EQUAL_UINT8(0, state.window_scale);
    TEST_ASSERT_EQUAL_UINT8(0, state.sack_permitted);
    TEST_ASSERT_EQUAL_UINT8(0, state.sack_blocks[0]);
    TEST_ASSERT_EQUAL_UINT32(0, state.sack_left[0]);
    TEST_ASSERT_EQUAL_UINT32(0, state.sack_right[0]);
}

/**
 * @brief 测试 MSS 选项处理
 */
void test_tcp_options_process_mss(void)
{
    tcp_options_state_t state;
    tcp_opt_mss_t opt;

    /* 初始化状态 */
    tcp_options_init(&state);

    /* 构造 MSS 选项 */
    opt.kind = TCP_OPT_MSS;
    opt.length = 4;
    opt.mss = 1024;

    /* 处理 MSS 选项 */
    tcp_options_process_mss(&state, &opt);

    /* 测试 MSS 更新 */
    TEST_ASSERT_EQUAL_UINT16(1024, state.mss);
}

/**
 * @brief 测试窗口缩放选项处理
 */
void test_tcp_options_process_window_scale(void)
{
    tcp_options_state_t state;
    tcp_opt_window_scale_t opt;

    /* 初始化状态 */
    tcp_options_init(&state);

    /* 构造窗口缩放选项 */
    opt.kind = TCP_OPT_WINDOW_SCALE;
    opt.length = 3;
    opt.scale_factor = 2;

    /* 处理窗口缩放选项 */
    tcp_options_process_window_scale(&state, &opt);

    /* 测试窗口缩放因子更新 */
    TEST_ASSERT_EQUAL_UINT8(2, state.window_scale);
}

/**
 * @brief 测试 SACK 选项处理
 */
void test_tcp_options_process_sack(void)
{
    tcp_options_state_t state;
    tcp_opt_sack_t opt;

    /* 初始化状态 */
    tcp_options_init(&state);

    /* 构造 SACK 选项 */
    opt.kind = TCP_OPT_SACK;
    opt.length = 10;
    opt.num_blocks = 2;
    opt.left_edge[0] = 100;
    opt.right_edge[0] = 200;
    opt.left_edge[1] = 300;
    opt.right_edge[1] = 400;

    /* 处理 SACK 选项 */
    tcp_options_process_sack(&state, &opt);

    /* 测试 SACK 块数量更新 */
    TEST_ASSERT_EQUAL_UINT8(2, state.sack_blocks[0]);

    /* 测试 SACK 左边界更新 */
    TEST_ASSERT_EQUAL_UINT32(100, state.sack_left[0]);
    TEST_ASSERT_EQUAL_UINT32(300, state.sack_left[1]);
}

/**
 * @brief 测试 MSS 选项构造
 */
void test_tcp_options_build_mss(void)
{
    tcp_opt_mss_t opt;

    /* 构造 MSS 选项 */
    opt = tcp_options_build_mss(1024);

    /* 测试选项类型 */
    TEST_ASSERT_EQUAL_UINT8(TCP_OPT_MSS, opt.kind);

    /* 测试选项长度 */
    TEST_ASSERT_EQUAL_UINT8(4, opt.length);

    /* 测试 MSS 值 */
    TEST_ASSERT_EQUAL_UINT16(1024, opt.mss);
}

/**
 * @brief 测试窗口缩放选项构造
 */
void test_tcp_options_build_window_scale(void)
{
    tcp_opt_window_scale_t opt;

    /* 构造窗口缩放选项 */
    opt = tcp_options_build_window_scale(3);

    /* 测试选项类型 */
    TEST_ASSERT_EQUAL_UINT8(TCP_OPT_WINDOW_SCALE, opt.kind);

    /* 测试选项长度 */
    TEST_ASSERT_EQUAL_UINT8(3, opt.length);

    /* 测试缩放因子 */
    TEST_ASSERT_EQUAL_UINT8(3, opt.scale_factor);
}

/**
 * @brief 测试 SACK 选项构造
 */
void test_tcp_options_build_sack(void)
{
    tcp_opt_sack_t opt;

    /* 构造 SACK 选项 */
    opt = tcp_options_build_sack(3,
                                 (uint32_t[]){100, 200, 300},
                                 (uint32_t[]){150, 250, 350});

    /* 测试选项类型 */
    TEST_ASSERT_EQUAL_UINT8(TCP_OPT_SACK, opt.kind);

    /* 测试选项长度 */
    TEST_ASSERT_EQUAL_UINT8(10, opt.length);

    /* 测试 SACK 块数量 */
    TEST_ASSERT_EQUAL_UINT8(3, opt.num_blocks);

    /* 测试 SACK 左边界 */
    TEST_ASSERT_EQUAL_UINT32(100, opt.left_edge[0]);
    TEST_ASSERT_EQUAL_UINT32(200, opt.left_edge[1]);
    TEST_ASSERT_EQUAL_UINT32(300, opt.left_edge[2]);

    /* 测试 SACK 右边界 */
    TEST_ASSERT_EQUAL_UINT32(150, opt.right_edge[0]);
    TEST_ASSERT_EQUAL_UINT32(250, opt.right_edge[1]);
    TEST_ASSERT_EQUAL_UINT32(350, opt.right_edge[2]);
}

/**
 * @brief 测试选项存在检查
 */
void test_tcp_options_exists(void)
{
    uint8_t buffer[20];
    uint8_t kind;

    /* 初始化缓冲区 */
    (void)memset(buffer, 0, sizeof(buffer));

    /* 测试选项不存在 */
    kind = TCP_OPT_MSS;
    TEST_ASSERT_FALSE(tcp_options_exists(buffer, 20, kind));

    /* 测试选项存在 */
    buffer[0] = kind;
    buffer[1] = 4;  /* 长度 */
    TEST_ASSERT_TRUE(tcp_options_exists(buffer, 20, kind));
}

/**
 * @brief 测试 MSS 选项序列化
 */
void test_tcp_options_mss_serialize(void)
{
    tcp_options_state_t state;
    uint8_t buffer[100];
    uint16_t written;

    /* 初始化状态 */
    tcp_options_init(&state);
    state.mss = 2048;

    /* 序列化 MSS 选项 */
    written = tcp_options_serialize(buffer, sizeof(buffer), TCP_OPT_MSS);

    /* 测试写入长度 */
    TEST_ASSERT_EQUAL_UINT16(4, written);

    /* 测试数据正确性 */
    TEST_ASSERT_EQUAL_UINT8(TCP_OPT_MSS, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(4, buffer[1]);
    TEST_ASSERT_EQUAL_UINT16(2048, (buffer[2] << 8) | buffer[3]);
}

/**
 * @brief 测试窗口缩放选项序列化
 */
void test_tcp_options_window_scale_serialize(void)
{
    tcp_options_state_t state;
    uint8_t buffer[100];
    uint16_t written;

    /* 初始化状态 */
    tcp_options_init(&state);
    state.window_scale = 4;

    /* 序列化窗口缩放选项 */
    written = tcp_options_serialize(buffer, sizeof(buffer), TCP_OPT_WINDOW_SCALE);

    /* 测试写入长度 */
    TEST_ASSERT_EQUAL_UINT16(3, written);

    /* 测试数据正确性 */
    TEST_ASSERT_EQUAL_UINT8(TCP_OPT_WINDOW_SCALE, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(3, buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(4, buffer[2]);
}

/**
 * @brief 测试 SACK 选项序列化
 */
void test_tcp_options_sack_serialize(void)
{
    tcp_options_state_t state;
    uint8_t buffer[100];
    uint16_t written;

    /* 初始化状态 */
    tcp_options_init(&state);
    state.sack_permitted = 1;
    state.sack_blocks[0] = 2;
    state.sack_left[0] = 100;
    state.sack_right[0] = 200;
    state.sack_left[1] = 300;
    state.sack_right[1] = 400;

    /* 序列化 SACK 选项 */
    written = tcp_options_serialize(buffer, sizeof(buffer), TCP_OPT_SACK);

    /* 测试写入长度 */
    TEST_ASSERT_EQUAL_UINT16(10, written);

    /* 测试数据正确性 */
    TEST_ASSERT_EQUAL_UINT8(TCP_OPT_SACK, buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(10, buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(2, buffer[2]);
    TEST_ASSERT_EQUAL_UINT32(100, (buffer[3] << 24) | (buffer[4] << 16) |
                              (buffer[5] << 8) | buffer[6]);
    TEST_ASSERT_EQUAL_UINT32(200, (buffer[7] << 24) | (buffer[8] << 16) |
                              (buffer[9] << 8) | buffer[10]);
}

/**
 * @brief 测试 TCP 选项完整流程
 */
void test_tcp_options_complete_flow(void)
{
    tcp_options_state_t state;
    uint8_t buffer[100];
    uint16_t written;

    /* 初始化状态 */
    tcp_options_init(&state);

    /* 设置选项值 */
    state.mss = 1024;
    state.window_scale = 1;
    state.sack_permitted = 1;
    state.sack_blocks[0] = 1;
    state.sack_left[0] = 100;
    state.sack_right[0] = 150;

    /* 序列化所有选项 */
    written = tcp_options_serialize(buffer, sizeof(buffer), TCP_OPT_MSS);

    /* 测试数据正确性 */
    TEST_ASSERT_GREATER_THAN(0, written);

    /* 验证选项可以正确解析 */
    tcp_options_parse(buffer, written, &state);

    /* 测试选项解析正确 */
    TEST_ASSERT_EQUAL_UINT16(1024, state.mss);
    TEST_ASSERT_EQUAL_UINT8(1, state.window_scale);
    TEST_ASSERT_EQUAL_UINT8(1, state.sack_permitted);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_tcp_options_init);
    RUN_TEST(test_tcp_options_process_mss);
    RUN_TEST(test_tcp_options_process_window_scale);
    RUN_TEST(test_tcp_options_process_sack);
    RUN_TEST(test_tcp_options_build_mss);
    RUN_TEST(test_tcp_options_build_window_scale);
    RUN_TEST(test_tcp_options_build_sack);
    RUN_TEST(test_tcp_options_exists);
    RUN_TEST(test_tcp_options_mss_serialize);
    RUN_TEST(test_tcp_options_window_scale_serialize);
    RUN_TEST(test_tcp_options_sack_serialize);
    RUN_TEST(test_tcp_options_complete_flow);

    return UNITY_END();
}
