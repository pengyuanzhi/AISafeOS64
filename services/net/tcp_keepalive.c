/**
 * @file tcp_keepalive.c
 * @brief TCP Keepalive 实现
 *
 * 本文件实现了 TCP Keepalive 功能
 */

#include <stdint.h>
#include <string.h>

#include "tcp_keepalive.h"

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
struct tcp_keepalive_config_t
{
    uint32_t idle_time;           /**< @brief 空闲超时时间（秒） */
    uint32_t probe_interval;      /**< @brief 探测间隔时间（秒） */
    uint32_t probe_count;         /**< @brief 探测次数 */
    bool     enabled;             /**< @brief 启用标记 */
};

/** @brief TCP Keepalive 状态 */
struct tcp_keepalive_state_t
{
    uint32_t last_active_time;    /**< @brief 最后活跃时间（秒） */
    uint32_t probe_count;         /**< @brief 当前探测次数 */
    uint32_t next_probe_time;     /**< @brief 下一次探测时间（秒） */
    bool     probe_sent;          /**< @brief 探测已发送标记 */
    bool     is_timeout;          /**< @brief 超时标记 */
};

/** @brief TCP Keepalive 头部 */
struct tcp_keepalive_header_t
{
    uint8_t  kind;                /**< @brief 选项类型 */
    uint8_t  length;              /**< @brief 长度 */
    uint8_t  data[TCP_KEEPALIVE_DATA_SIZE];  /**< @brief Keepalive 数据 */
};

/* ========================================================================
 * 公共函数
 * ======================================================================== */

/**
 * @brief 初始化 Keepalive 配置
 *
 * @param config Keepalive 配置
 */
void keepalive_init_config(tcp_keepalive_config_t *config)
{
    if (config == NULL)
    {
        return;
    }

    (void)memset(config, 0, sizeof(tcp_keepalive_config_t));

    /* 设置默认值 */
    config->idle_time = TCP_KEEPALIVE_IDLE_TIME;
    config->probe_interval = TCP_KEEPALIVE_PROBE_INTERVAL;
    config->probe_count = TCP_KEEPALIVE_PROBE_COUNT;
    config->enabled = true;
}

/**
 * @brief 初始化 Keepalive 状态
 *
 * @param state Keepalive 状态
 */
void keepalive_init_state(tcp_keepalive_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    (void)memset(state, 0, sizeof(tcp_keepalive_state_t));

    /* 设置默认值 */
    state->last_active_time = 0;
    state->probe_count = 0;
    state->next_probe_time = 0;
    state->probe_sent = false;
    state->is_timeout = false;
}

/**
 * @brief 处理连接活跃
 *
 * @param state Keepalive 状态
 * @param current_time 当前时间（秒）
 */
void keepalive_handle_activity(tcp_keepalive_state_t *state,
                               uint32_t current_time)
{
    if (state == NULL)
    {
        return;
    }

    /* 更新最后活跃时间 */
    state->last_active_time = current_time;

    /* 重置探测状态 */
    state->probe_count = 0;
    state->next_probe_time = 0;
    state->probe_sent = false;
    state->is_timeout = false;
}

/**
 * @brief 检查 Keepalive 是否应该发送探测
 *
 * @param config Keepalive 配置
 * @param state Keepalive 状态
 * @param current_time 当前时间（秒）
 * @return true=应该发送，false=不应该发送
 */
bool keepalive_should_send_probe(const tcp_keepalive_config_t *config,
                                  tcp_keepalive_state_t *state,
                                  uint32_t current_time)
{
    if ((config == NULL) || (state == NULL))
    {
        return false;
    }

    if (!config->enabled)
    {
        return false;
    }

    /* 检查是否达到空闲超时 */
    if ((current_time - state->last_active_time) < config->idle_time)
    {
        return false;  /* 空闲时间未到 */
    }

    /* 检查是否应该发送探测 */
    if ((current_time >= state->next_probe_time) &&
        (state->probe_count < config->probe_count))
    {
        return true;  /* 应该发送探测 */
    }

    return false;
}

/**
 * @brief 发送 Keepalive 探测
 *
 * @param state Keepalive 状态
 * @param config Keepalive 配置
 * @param current_time 当前时间（秒）
 * @return true=探测发送成功，false=探测失败
 */
bool keepalive_send_probe(tcp_keepalive_state_t *state,
                           const tcp_keepalive_config_t *config,
                           uint32_t current_time)
{
    if ((state == NULL) || (config == NULL))
    {
        return false;
    }

    if (!config->enabled)
    {
        return false;
    }

    /* 增加探测计数 */
    state->probe_count++;

    /* 更新下一次探测时间 */
    state->next_probe_time = current_time + config->probe_interval;

    /* 检查是否超时 */
    if (state->probe_count >= config->probe_count)
    {
        state->is_timeout = true;
    }

    /* 模拟发送探测 */
    state->probe_sent = true;

    return true;
}

/**
 * @brief 处理 Keepalive 超时
 *
 * @param state Keepalive 状态
 * @param config Keepalive 配置
 */
void keepalive_handle_timeout(tcp_keepalive_state_t *state,
                               const tcp_keepalive_config_t *config)
{
    if (state == NULL)
    {
        return;
    }

    /* 标记超时 */
    state->is_timeout = true;

    if (config != NULL)
    {
        /* 重置探测状态 */
        state->probe_count = config->probe_count;
    }

    state->probe_sent = true;
}

/**
 * @brief 重置 Keepalive 状态
 *
 * @param state Keepalive 状态
 */
void keepalive_reset_state(tcp_keepalive_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    /* 重置所有字段 */
    (void)memset(state, 0, sizeof(tcp_keepalive_state_t));

    /* 保留默认值 */
    state->last_active_time = 0;
    state->probe_count = 0;
    state->next_probe_time = 0;
    state->probe_sent = false;
    state->is_timeout = false;
}

/**
 * @brief 构造 Keepalive 数据包头部
 *
 * @param header 输出头部结构
 */
void keepalive_build_header(tcp_keepalive_header_t *header)
{
    if (header == NULL)
    {
        return;
    }

    /* 构造头部 */
    header->kind = 0x1;  /* Keepalive 选项类型 */
    header->length = 4;  /* 选项长度 */
    header->data[0] = 0; /* Keepalive 数据（空） */
}

/**
 * @brief 获取 Keepalive 头部长度
 *
 * @param header Keepalive 头部
 * @return 头部长度
 */
uint16_t keepalive_get_length(const tcp_keepalive_header_t *header)
{
    if (header == NULL)
    {
        return 0;
    }

    return header->length;
}

/**
 * @brief 启用 Keepalive
 *
 * @param config Keepalive 配置
 */
void keepalive_enable(tcp_keepalive_config_t *config)
{
    if (config == NULL)
    {
        return;
    }

    config->enabled = true;
}

/**
 * @brief 禁用 Keepalive
 *
 * @param config Keepalive 配置
 */
void keepalive_disable(tcp_keepalive_config_t *config)
{
    if (config == NULL)
    {
        return;
    }

    config->enabled = false;
}

/**
 * @brief 检查 Keepalive 是否启用
 *
 * @param config Keepalive 配置
 * @return true=已启用，false=已禁用
 */
bool keepalive_is_enabled(const tcp_keepalive_config_t *config)
{
    if (config == NULL)
    {
        return false;
    }

    return config->enabled;
}

/**
 * @brief 获取当前探测次数
 *
 * @param state Keepalive 状态
 * @return 当前探测次数
 */
uint32_t keepalive_get_probe_count(const tcp_keepalive_state_t *state)
{
    if (state == NULL)
    {
        return 0;
    }

    return state->probe_count;
}

/**
 * @brief 获取最后活跃时间
 *
 * @param state Keepalive 状态
 * @return 最后活跃时间（秒）
 */
uint32_t keepalive_get_last_active(const tcp_keepalive_state_t *state)
{
    if (state == NULL)
    {
        return 0;
    }

    return state->last_active_time;
}

/**
 * @brief 检查是否超时
 *
 * @param state Keepalive 状态
 * @return true=超时，false=未超时
 */
bool keepalive_is_timeout(const tcp_keepalive_state_t *state)
{
    if (state == NULL)
    {
        return false;
    }

    return state->is_timeout;
}

/**
 * @brief 设置空闲超时时间
 *
 * @param config Keepalive 配置
 * @param idle_time 空闲超时时间（秒）
 */
void keepalive_set_idle_time(tcp_keepalive_config_t *config,
                              uint32_t idle_time)
{
    if (config == NULL)
    {
        return;
    }

    config->idle_time = idle_time;
}

/**
 * @brief 设置探测间隔时间
 *
 * @param config Keepalive 配置
 * @param interval 探测间隔时间（秒）
 */
void keepalive_set_probe_interval(tcp_keepalive_config_t *config,
                                   uint32_t interval)
{
    if (config == NULL)
    {
        return;
    }

    config->probe_interval = interval;
}

/**
 * @brief 设置探测次数
 *
 * @param config Keepalive 配置
 * @param count 探测次数
 */
void keepalive_set_probe_count(tcp_keepalive_config_t *config,
                                uint32_t count)
{
    if (config == NULL)
    {
        return;
    }

    config->probe_count = count;
}
