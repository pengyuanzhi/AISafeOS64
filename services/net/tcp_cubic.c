/**
 * @file tcp_cubic.c
 * @brief CUBIC 拥塞控制算法实现
 *
 * 本文件实现了 CUBIC 拥塞控制算法（RFC 3448）
 */

#include <stdint.h>
#include <string.h>
#include <math.h>

#include "tcp_cubic.h"
#include <kernel/syscall.h>

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

/** @brief 最小窗口 */
#define CUBIC_MIN_WINDOW         TCP_MSS

/** @brief 最大窗口 */
#define CUBIC_MAX_WINDOW         (100U * TCP_MSS)

/* ========================================================================
 * 数据结构定义
 * ======================================================================== */

/** @brief CUBIC 拥塞控制状态 */
struct cubic_state_t
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
};

/* 状态定义 */
#define CUBIC_STATE_SLOW_START   0U      /**< @brief 慢启动 */
#define CUBIC_STATE_CONGESTION_AVOIDANCE  1U /**< @brief 拥塞避免 */
#define CUBIC_STATE_FAST_RECOVERY 2U      /**< @brief 快速恢复 */

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

/**
 * @brief 计算 CUBIC 窗口
 *
 * @param state CUBIC 状态
 * @param current_time 当前时间（毫秒）
 * @return 计算后的窗口大小
 */
static uint32_t cubic_calc_window(cubic_state_t *state, uint32_t current_time)
{
    float     t;
    float     window;
    uint32_t  window_int;

    if ((state == NULL) || (current_time == 0U))
    {
        return CUBIC_MIN_WINDOW;
    }

    /* 计算时间距离 */
    t = (float)(current_time - state->epoch_start) / (float)CUBIC_TIME_UNIT_MS;

    /* CUBIC 公式：W(t) = C * (t - K)^3 + w_max */
    window = CUBIC_C * (float)pow(t - state->k, 3.0f) + (float)state->w_max;

    /* 限制窗口范围 */
    if (window < (float)CUBIC_MIN_WINDOW)
    {
        window_int = CUBIC_MIN_WINDOW;
    }
    else if (window > (float)CUBIC_MAX_WINDOW)
    {
        window_int = CUBIC_MAX_WINDOW;
    }
    else
    {
        window_int = (uint32_t)window;
    }

    /* 对齐到 MSS */
    window_int = (window_int / TCP_MSS) * TCP_MSS;

    return window_int;
}

/**
 * @brief 计算参数 K
 *
 * @param state CUBIC 状态
 */
static void cubic_calc_k(cubic_state_t *state)
{
    float     w_ratio;
    float     log_ratio;

    if (state == NULL)
    {
        return;
    }

    /* 计算 w_max / w_last_max */
    if (state->w_last_max == 0U)
    {
        w_ratio = 0.0f;
    }
    else
    {
        w_ratio = (float)state->w_max / (float)state->w_last_max;
    }

    /* 计算 K = cbrt(w_max * (1 - beta) / C) */
    if (w_ratio > 0.0f)
    {
        log_ratio = (float)log(w_ratio) / (float)log(2.71828f);
        state->k = (float)pow((float)(state->w_max * (1.0f - CUBIC_BETA)) /
                              CUBIC_C, 1.0f / 3.0f);
    }
    else
    {
        state->k = 0.0f;
    }
}

/* ========================================================================
 * 公共函数
 * ======================================================================== */

/**
 * @brief 初始化 CUBIC 状态
 *
 * @param state CUBIC 状态
 */
void cubic_init(cubic_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    (void)memset(state, 0, sizeof(cubic_state_t));

    /* 初始化窗口 */
    state->cwnd = CUBIC_MIN_WINDOW;
    state->ssthresh = CUBIC_MAX_WINDOW;
    state->w_max = CUBIC_MIN_WINDOW;
    state->w_last_max = CUBIC_MIN_WINDOW;
    state->epoch_start = 0U;
    state->last_cwnd = CUBIC_MIN_WINDOW;
    state->origin_point = 0U;
    state->t = 0U;
    state->k = 0.0f;
    state->state = CUBIC_STATE_SLOW_START;
}

/**
 * @brief CUBIC 慢启动阶段
 *
 * @param state CUBIC 状态
 * @return 更新后的窗口大小
 */
uint32_t cubic_slow_start(cubic_state_t *state)
{
    if (state == NULL)
    {
        return CUBIC_MIN_WINDOW;
    }

    /* 窗口翻倍 */
    state->cwnd = state->cwnd + TCP_MSS;

    /* 限制窗口范围 */
    if (state->cwnd > CUBIC_MAX_WINDOW)
    {
        state->cwnd = CUBIC_MAX_WINDOW;
    }

    /* 检查是否需要转换到拥塞避免 */
    if (state->cwnd >= state->ssthresh)
    {
        state->state = CUBIC_STATE_CONGESTION_AVOIDANCE;
        state->epoch_start = get_current_time_ms();
        state->w_max = state->cwnd;
        cubic_calc_k(state);
    }

    return state->cwnd;
}

/**
 * @brief CUBIC 拥塞避免阶段
 *
 * @param state CUBIC 状态
 * @param current_time 当前时间（毫秒）
 * @return 更新后的窗口大小
 */
uint32_t cubic_congestion_avoidance(cubic_state_t *state,
                                     uint32_t current_time)
{
    uint32_t window;

    if ((state == NULL) || (current_time == 0U))
    {
        return CUBIC_MIN_WINDOW;
    }

    /* 计算 CUBIC 窗口 */
    window = cubic_calc_window(state, current_time);

    /* 更新窗口 */
    state->cwnd = window;

    /* 限制窗口范围 */
    if (state->cwnd > CUBIC_MAX_WINDOW)
    {
        state->cwnd = CUBIC_MAX_WINDOW;
    }

    return state->cwnd;
}

/**
 * @brief CUBIC 快速重传
 *
 * @param state CUBIC 状态
 * @return 更新后的窗口大小
 */
uint32_t cubic_fast_retransmit(cubic_state_t *state)
{
    if (state == NULL)
    {
        return CUBIC_MIN_WINDOW;
    }

    /* 保存当前窗口 */
    state->last_cwnd = state->cwnd;

    /* 更新峰值窗口 */
    if (state->cwnd > state->w_max)
    {
        state->w_last_max = state->w_max;
        state->w_max = state->cwnd;
    }
    else
    {
        state->w_last_max = state->cwnd;
    }

    /* 窗口乘以 beta（0.7） */
    state->cwnd = (uint32_t)((float)state->cwnd * CUBIC_BETA);

    /* 对齐到 MSS */
    state->cwnd = (state->cwnd / TCP_MSS) * TCP_MSS;

    /* 限制窗口范围 */
    if (state->cwnd < CUBIC_MIN_WINDOW)
    {
        state->cwnd = CUBIC_MIN_WINDOW;
    }

    /* 更新慢启动阈值 */
    state->ssthresh = state->cwnd;

    /* 切换到快速恢复状态 */
    state->state = CUBIC_STATE_FAST_RECOVERY;

    /* 重新计算参数 K */
    cubic_calc_k(state);

    return state->cwnd;
}

/**
 * @brief CUBIC 快速恢复
 *
 * @param state CUBIC 状态
 * @return 更新后的窗口大小
 */
uint32_t cubic_fast_recovery(cubic_state_t *state)
{
    uint32_t window;

    if (state == NULL)
    {
        return CUBIC_MIN_WINDOW;
    }

    /* 计算新的窗口 */
    window = cubic_calc_window(state, get_current_time_ms());

    /* 更新窗口 */
    state->cwnd = window;

    /* 检查是否需要返回拥塞避免 */
    if (state->cwnd >= state->w_max)
    {
        state->state = CUBIC_STATE_CONGESTION_AVOIDANCE;
    }

    /* 限制窗口范围 */
    if (state->cwnd > CUBIC_MAX_WINDOW)
    {
        state->cwnd = CUBIC_MAX_WINDOW;
    }

    return state->cwnd;
}

/**
 * @brief CUBIC 超时
 *
 * @param state CUBIC 状态
 * @return 更新后的窗口大小
 */
uint32_t cubic_timeout(cubic_state_t *state)
{
    if (state == NULL)
    {
        return CUBIC_MIN_WINDOW;
    }

    /* 保存当前窗口 */
    state->last_cwnd = state->cwnd;

    /* 更新峰值窗口 */
    if (state->cwnd > state->w_max)
    {
        state->w_last_max = state->w_max;
        state->w_max = state->cwnd;
    }
    else
    {
        state->w_last_max = state->cwnd;
    }

    /* 窗口重置为 1 MSS */
    state->cwnd = CUBIC_MIN_WINDOW;

    /* 更新慢启动阈值 */
    state->ssthresh = (uint32_t)((float)state->last_cwnd * CUBIC_BETA);

    /* 对齐到 MSS */
    state->ssthresh = (state->ssthresh / TCP_MSS) * TCP_MSS;

    /* 限制慢启动阈值范围 */
    if (state->ssthresh < CUBIC_MIN_WINDOW * 2)
    {
        state->ssthresh = CUBIC_MIN_WINDOW * 2;
    }

    /* 切换到慢启动状态 */
    state->state = CUBIC_STATE_SLOW_START;

    /* 重新计算参数 K */
    cubic_calc_k(state);

    return state->cwnd;
}

/**
 * @brief 处理新 ACK
 *
 * @param state CUBIC 状态
 * @param bytes_acked 确认的字节数
 * @return 更新后的窗口大小
 */
uint32_t cubic_handle_ack(cubic_state_t *state, uint32_t bytes_acked)
{
    uint32_t window;

    if (state == NULL)
    {
        return CUBIC_MIN_WINDOW;
    }

    /* 根据状态处理 ACK */
    switch (state->state)
    {
        case CUBIC_STATE_SLOW_START:
            window = cubic_slow_start(state);
            break;

        case CUBIC_STATE_CONGESTION_AVOIDANCE:
            window = cubic_congestion_avoidance(state, get_current_time_ms());
            break;

        case CUBIC_STATE_FAST_RECOVERY:
            window = cubic_fast_recovery(state);
            break;

        default:
            window = CUBIC_MIN_WINDOW;
            break;
    }

    return window;
}

/**
 * @brief 获取当前窗口大小
 *
 * @param state CUBIC 状态
 * @return 当前窗口大小
 */
uint32_t cubic_get_cwnd(const cubic_state_t *state)
{
    if (state == NULL)
    {
        return CUBIC_MIN_WINDOW;
    }

    return state->cwnd;
}

/**
 * @brief 获取当前状态
 *
 * @param state CUBIC 状态
 * @return 当前状态
 */
uint8_t cubic_get_state(const cubic_state_t *state)
{
    if (state == NULL)
    {
        return CUBIC_STATE_SLOW_START;
    }

    return state->state;
}

/**
 * @brief 获取峰值窗口大小
 *
 * @param state CUBIC 状态
 * @return 峰值窗口大小
 */
uint32_t cubic_get_w_max(const cubic_state_t *state)
{
    if (state == NULL)
    {
        return CUBIC_MIN_WINDOW;
    }

    return state->w_max;
}
