/**
 * @file tcp_timestamp.c
 * @brief TCP 时间戳选项实现
 *
 * 本文件实现了 TCP 时间戳选项（RFC 7323）
 */

#include <stdint.h>
#include <string.h>

#include "tcp_timestamp.h"
#include <kernel/syscall.h>

/* ========================================================================
 * TCP 时间戳选项定义
 * ======================================================================== */

/** @brief TCP 选项类型：时间戳 */
#define TCP_OPT_TIMESTAMP       8U

/** @brief TCP 时间戳选项长度 */
#define TCP_OPT_TIMESTAMP_LEN   10U

/** @brief 时间戳精度（毫秒） */
#define TCP_TIMESTAMP_PRECISION_MS   1U

/** @brief 时间戳最大偏差（10 秒） */
#define TCP_TIMESTAMP_MAX_DEVIATION_MS  10000U

/** @brief 时钟频率（Hz） */
#define CLOCK_FREQUENCY_HZ      1000U

/* ========================================================================
 * 数据结构定义
 * ======================================================================== */

/** @brief TCP 时间戳选项结构 */
struct tcp_timestamp_option_t
{
    uint8_t  kind;              /**< @brief 选项类型 */
    uint8_t  length;            /**< @brief 长度 */
    uint32_t ts_val;            /**< @brief 时间戳值 */
    uint32_t ts_echo_rpl;       /**< @brief 时间戳回显 */
};

/** @brief TCP 时间戳状态 */
struct tcp_timestamp_state_t
{
    uint32_t ts_val;            /**< @brief 最后发送时间戳 */
    uint32_t ts_echo_rpl;       /**< @brief 最后回显时间戳 */
    uint32_t recent_ts;         /**< @brief 最近接收时间戳 */
    bool     enabled;           /**< @brief 启用标记 */
    uint32_t clock_offset;      /**< @brief 时钟偏移（用于测试） */
};

/** @brief 序列号验证状态 */
struct tcp_seq_verify_state_t
{
    uint32_t last_seq;          /**< @brief 最后序列号 */
    uint32_t last_ts;           /**< @brief 最后时间戳 */
    uint32_t expected_ts;       /**< @brief 预期时间戳 */
};

/* ========================================================================
 * 内部函数
 * ======================================================================== */

/**
 * @brief 获取当前时间（毫秒）
 *
 * @return 当前时间（毫秒）
 */
static uint32_t get_current_time_ms(void)
{
    /* 使用内核系统调用获取时间 */
    /* 这里简化实现，返回模拟时间 */
    /* TODO: 集成真实的内核时间接口 */

    static uint32_t s_time_counter = 0U;

    /* 简单的模拟时间（每调用一次增加 1 毫秒） */
    s_time_counter++;

    return s_time_counter;
}

/* ========================================================================
 * 公共函数
 * ======================================================================== */

/**
 * @brief 初始化 TCP 时间戳状态
 *
 * @param state 时间戳状态
 */
void tcp_timestamp_init(tcp_timestamp_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    (void)memset(state, 0, sizeof(tcp_timestamp_state_t));

    state->enabled = true;
    state->clock_offset = 0U;
}

/**
 * @brief 获取当前时间戳（毫秒）
 *
 * @return 时间戳（毫秒）
 */
uint32_t tcp_timestamp_get(void)
{
    return get_current_time_ms();
}

/**
 * @brief 构造 TCP 时间戳选项
 *
 * @param state 时间戳状态
 * @param opt 输出选项结构
 */
void tcp_timestamp_build(tcp_timestamp_state_t *state,
                         tcp_timestamp_option_t *opt)
{
    uint32_t ts_val;

    if ((state == NULL) || (opt == NULL))
    {
        return;
    }

    if (!state->enabled)
    {
        return;
    }

    /* 获取时间戳 */
    ts_val = tcp_timestamp_get() + state->clock_offset;
    state->ts_val = ts_val;

    /* 构造时间戳选项 */
    opt->kind = TCP_OPT_TIMESTAMP;
    opt->length = TCP_OPT_TIMESTAMP_LEN;
    opt->ts_val = ts_val;
    opt->ts_echo_rpl = state->ts_echo_rpl;
}

/**
 * @brief 解析 TCP 时间戳选项
 *
 * @param opt 输入选项结构
 * @param state 时间戳状态
 */
void tcp_timestamp_parse(const tcp_timestamp_option_t *opt,
                         tcp_timestamp_state_t *state)
{
    if ((opt == NULL) || (state == NULL))
    {
        return;
    }

    if (!state->enabled)
    {
        return;
    }

    /* 检查选项类型和长度 */
    if ((opt->kind != TCP_OPT_TIMESTAMP) ||
        (opt->length != TCP_OPT_TIMESTAMP_LEN))
    {
        return;
    }

    /* 更新时间戳状态 */
    state->recent_ts = opt->ts_val;
    state->ts_echo_rpl = opt->ts_echo_rpl;
}

/**
 * @brief 序列号验证（防止序列号预测攻击）
 *
 * @param verify_state 验证状态
 * @param seq_num 序列号
 * @param ts_val 时间戳值
 * @return true=通过验证，false=验证失败
 */
bool tcp_seq_verify(tcp_seq_verify_state_t *verify_state,
                     uint32_t seq_num, uint32_t ts_val)
{
    uint32_t time_diff;
    uint32_t seq_diff;

    if ((verify_state == NULL) || (ts_val == 0U))
    {
        return false;
    }

    /* 第一次验证 */
    if ((verify_state->last_seq == 0U) && (verify_state->last_ts == 0U))
    {
        verify_state->last_seq = seq_num;
        verify_state->last_ts = ts_val;
        verify_state->expected_ts = ts_val;
        return true;
    }

    /* 检查时间戳有效性 */
    if (ts_val < verify_state->last_ts)
    {
        /* 时间戳递减（可能是回绕） */
        time_diff = (verify_state->last_ts - ts_val);
        if (time_diff > TCP_TIMESTAMP_MAX_DEVIATION_MS)
        {
            /* 时间戳偏差过大，拒绝 */
            return false;
        }
    }
    else
    {
        /* 时间戳递增 */
        time_diff = (ts_val - verify_state->last_ts);
        if (time_diff > TCP_TIMESTAMP_MAX_DEVIATION_MS)
        {
            /* 时间戳偏差过大，检查序列号 */
            seq_diff = seq_num - verify_state->last_seq;

            /* 如果序列号没有相应增长，可能是攻击 */
            if (seq_diff < 1000)  /* 小于 1000 字节 */
            {
                return false;
            }
        }
    }

    /* 检查序列号回绕 */
    if (seq_num < verify_state->last_seq)
    {
        /* 序列号回绕 */
        seq_diff = (verify_state->last_seq - seq_num);

        /* 检查回绕是否合理（小于 2^31） */
        if (seq_diff > 0x80000000U)
        {
            return false;
        }
    }

    /* 更新验证状态 */
    verify_state->last_seq = seq_num;
    verify_state->last_ts = ts_val;

    return true;
}

/**
 * @brief 检查时间戳有效性
 *
 * @param ts_val 时间戳值
 * @param expected_ts 预期时间戳
 * @return true=有效，false=无效
 */
bool tcp_timestamp_is_valid(uint32_t ts_val, uint32_t expected_ts)
{
    uint32_t time_diff;

    if (ts_val == 0U)
    {
        return false;
    }

    if (expected_ts == 0U)
    {
        /* 没有预期时间戳，认为有效 */
        return true;
    }

    /* 检查时间戳偏差 */
    if (ts_val >= expected_ts)
    {
        time_diff = ts_val - expected_ts;
    }
    else
    {
        /* 时间戳回绕 */
        time_diff = expected_ts - ts_val;
    }

    /* 检查是否在允许范围内 */
    if (time_diff > TCP_TIMESTAMP_MAX_DEVIATION_MS)
    {
        return false;
    }

    return true;
}

/**
 * @brief 更新回显时间戳
 *
 * @param state 时间戳状态
 * @param ts_val 时间戳值
 */
void tcp_timestamp_update_echo(tcp_timestamp_state_t *state,
                                uint32_t ts_val)
{
    if (state == NULL)
    {
        return;
    }

    state->ts_echo_rpl = ts_val;
}

/**
 * @brief 计算 RTT
 *
 * @param state 时间戳状态
 * @param ts_val 时间戳值
 * @param current_time 当前时间
 * @return RTT（毫秒）
 */
uint32_t tcp_timestamp_calc_rtt(tcp_timestamp_state_t *state,
                                uint32_t ts_val,
                                uint32_t current_time)
{
    uint32_t rtt;

    if ((state == NULL) || (ts_val == 0U) || (current_time == 0U))
    {
        return 0U;
    }

    /* 计算 RTT */
    if (current_time >= ts_val)
    {
        rtt = current_time - ts_val;
    }
    else
    {
        /* 时间戳回绕 */
        rtt = 0U;
    }

    return rtt;
}

/**
 * @brief 启用时间戳
 *
 * @param state 时间戳状态
 */
void tcp_timestamp_enable(tcp_timestamp_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    state->enabled = true;
}

/**
 * @brief 禁用时间戳
 *
 * @param state 时间戳状态
 */
void tcp_timestamp_disable(tcp_timestamp_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    state->enabled = false;
}

/**
 * @brief 检查时间戳是否启用
 *
 * @param state 时间戳状态
 * @return true=已启用，false=已禁用
 */
bool tcp_timestamp_is_enabled(const tcp_timestamp_state_t *state)
{
    if (state == NULL)
    {
        return false;
    }

    return state->enabled;
}

/**
 * @brief 获取最后发送时间戳
 *
 * @param state 时间戳状态
 * @return 最后发送时间戳
 */
uint32_t tcp_timestamp_get_last(const tcp_timestamp_state_t *state)
{
    if (state == NULL)
    {
        return 0U;
    }

    return state->ts_val;
}

/**
 * @brief 获取最近接收时间戳
 *
 * @param state 时间戳状态
 * @return 最近接收时间戳
 */
uint32_t tcp_timestamp_get_recent(const tcp_timestamp_state_t *state)
{
    if (state == NULL)
    {
        return 0U;
    }

    return state->recent_ts;
}

/**
 * @brief 设置时钟偏移（用于测试）
 *
 * @param state 时间戳状态
 * @param offset 时钟偏移（毫秒）
 */
void tcp_timestamp_set_clock_offset(tcp_timestamp_state_t *state,
                                     uint32_t offset)
{
    if (state == NULL)
    {
        return;
    }

    state->clock_offset = offset;
}
