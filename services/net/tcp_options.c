/**
 * @file tcp_options.c
 * @brief TCP 选项处理实现
 *
 * 本文件实现了 TCP 选项处理（MSS、窗口缩放、SACK）
 */

#include <stdint.h>
#include <string.h>
#include <netinet/in.h>

#include "tcp_options.h"

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

/** @brief 默认 MSS */
#define TCP_DEFAULT_MSS           1460U

/** @brief 窗口缩放因子最大值 */
#define TCP_WINDOW_SCALE_MAX      14U

/** @brief SACK 最大块数 */
#define TCP_SACK_MAX_BLOCKS       4U

/** @brief 选项结束标记 */
#define TCP_OPT_END               0U

/** @brief NOP 选项 */
#define TCP_OPT_NOP               1U

/* ========================================================================
 * 数据结构定义
 * ======================================================================== */

/** @brief TCP MSS 选项 */
struct tcp_opt_mss_t
{
    uint8_t kind;                /**< @brief 选项类型 */
    uint8_t length;              /**< @brief 长度 */
    uint16_t mss;                /**< @brief 最大段大小 */
};

/** @brief TCP 窗口缩放选项 */
struct tcp_opt_window_scale_t
{
    uint8_t kind;                /**< @brief 选项类型 */
    uint8_t length;              /**< @brief 长度 */
    uint8_t scale_factor;        /**< @brief 缩放因子 */
};

/** @brief TCP SACK 选项 */
struct tcp_opt_sack_t
{
    uint8_t kind;                /**< @brief 选项类型 */
    uint8_t length;              /**< @brief 长度 */
    uint8_t num_blocks;          /**< @brief SACK 块数量 */
    uint32_t left_edge[4];       /**< @brief SACK 左边界 */
    uint32_t right_edge[4];      /**< @brief SACK 右边界 */
};

/** @brief TCP 选项处理状态 */
struct tcp_options_state_t
{
    uint16_t mss;                /**< @brief 最大段大小 */
    uint8_t  window_scale;       /**< @brief 窗口缩放因子 */
    uint8_t  sack_permitted;     /**< @brief SACK 允许 */
    uint8_t  sack_count;         /**< @brief SACK 块数量 */
    uint32_t sack_left[4];       /**< @brief SACK 左边界 */
    uint32_t sack_right[4];      /**< @brief SACK 右边界 */
    bool     mss_negotiated;     /**< @brief MSS 已协商 */
    bool     window_scale_negotiated; /**< @brief 窗口缩放已协商 */
    bool     sack_permitted_received;  /**< @brief 收到 SACK 允许 */
};

/* ========================================================================
 * 内部函数
 * ======================================================================== */

/**
 * @brief 序列化 MSS 选项
 *
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @param mss MSS 值
 * @return 写入的字节数
 */
static uint16_t serialize_mss(uint8_t *buffer, uint16_t size, uint16_t mss)
{
    tcp_opt_mss_t *opt;

    if ((buffer == NULL) || (size < 4))
    {
        return 0;
    }

    opt = (tcp_opt_mss_t *)buffer;

    /* 构造 MSS 选项 */
    opt->kind = TCP_OPT_MSS;
    opt->length = 4;
    opt->mss = mss;

    return 4;
}

/**
 * @brief 序列化窗口缩放选项
 *
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @param scale_factor 缩放因子
 * @return 写入的字节数
 */
static uint16_t serialize_window_scale(uint8_t *buffer, uint16_t size,
                                        uint8_t scale_factor)
{
    tcp_opt_window_scale_t *opt;

    if ((buffer == NULL) || (size < 3))
    {
        return 0;
    }

    opt = (tcp_opt_window_scale_t *)buffer;

    /* 构造窗口缩放选项 */
    opt->kind = TCP_OPT_WINDOW_SCALE;
    opt->length = 3;
    opt->scale_factor = scale_factor;

    return 3;
}

/**
 * @brief 序列化 SACK 选项
 *
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @param num_blocks SACK 块数量
 * @param left_edges 左边界数组
 * @param right_edges 右边界数组
 * @return 写入的字节数
 */
static uint16_t serialize_sack(uint8_t *buffer, uint16_t size,
                                uint8_t num_blocks,
                                const uint32_t *left_edges,
                                const uint32_t *right_edges)
{
    tcp_opt_sack_t *opt;
    uint8_t i;

    if ((buffer == NULL) || (size < (2 + num_blocks * 8)))
    {
        return 0;
    }

    if ((num_blocks == 0) || (num_blocks > TCP_SACK_MAX_BLOCKS))
    {
        return 0;
    }

    opt = (tcp_opt_sack_t *)buffer;

    /* 构造 SACK 选项 */
    opt->kind = TCP_OPT_SACK;
    opt->length = 2 + num_blocks * 8;
    opt->num_blocks = num_blocks;

    /* 填充 SACK 块 */
    for (i = 0; i < num_blocks; i++)
    {
        opt->left_edge[i] = left_edges[i];
        opt->right_edge[i] = right_edges[i];
    }

    return opt->length;
}

/* ========================================================================
 * 公共函数
 * ======================================================================== */

/**
 * @brief 初始化 TCP 选项状态
 *
 * @param state 选项状态
 */
void tcp_options_init(tcp_options_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    (void)memset(state, 0, sizeof(tcp_options_state_t));

    /* 设置默认值 */
    state->mss = TCP_DEFAULT_MSS;
    state->window_scale = 0;
    state->sack_permitted = 0;
    state->sack_count = 0;
    state->mss_negotiated = false;
    state->window_scale_negotiated = false;
    state->sack_permitted_received = false;
}

/**
 * @brief 处理 MSS 选项
 *
 * @param state 选项状态
 * @param opt 输入选项
 */
void tcp_options_process_mss(tcp_options_state_t *state,
                                const tcp_opt_mss_t *opt)
{
    if ((state == NULL) || (opt == NULL))
    {
        return;
    }

    if (opt->kind != TCP_OPT_MSS)
    {
        return;
    }

    if (opt->length != 4)
    {
        return;
    }

    /* 更新 MSS */
    state->mss = opt->mss;
    state->mss_negotiated = true;
}

/**
 * @brief 处理窗口缩放选项
 *
 * @param state 选项状态
 * @param opt 输入选项
 */
void tcp_options_process_window_scale(tcp_options_state_t *state,
                                         const tcp_opt_window_scale_t *opt)
{
    if ((state == NULL) || (opt == NULL))
    {
        return;
    }

    if (opt->kind != TCP_OPT_WINDOW_SCALE)
    {
        return;
    }

    if (opt->length != 3)
    {
        return;
    }

    if (opt->scale_factor > TCP_WINDOW_SCALE_MAX)
    {
        return;
    }

    /* 更新窗口缩放因子 */
    state->window_scale = opt->scale_factor;
    state->window_scale_negotiated = true;
}

/**
 * @brief 处理 SACK 选项
 *
 * @param state 选项状态
 * @param opt 输入选项
 */
void tcp_options_process_sack(tcp_options_state_t *state,
                                  const tcp_opt_sack_t *opt)
{
    uint8_t i;

    if ((state == NULL) || (opt == NULL))
    {
        return;
    }

    if (opt->kind != TCP_OPT_SACK)
    {
        return;
    }

    if (opt->num_blocks == 0)
    {
        /* SACK 允许选项 */
        state->sack_permitted = 1;
        state->sack_permitted_received = true;
        return;
    }

    if (opt->num_blocks > TCP_SACK_MAX_BLOCKS)
    {
        return;
    }

    /* 更新 SACK 块 */
    state->sack_count = opt->num_blocks;
    for (i = 0; i < opt->num_blocks; i++)
    {
        state->sack_left[i] = opt->left_edge[i];
        state->sack_right[i] = opt->right_edge[i];
    }
}

/**
 * @brief 构造 MSS 选项
 *
 * @param mss 最大段大小
 * @return MSS 选项结构
 */
tcp_opt_mss_t tcp_options_build_mss(uint16_t mss)
{
    tcp_opt_mss_t opt;

    opt.kind = TCP_OPT_MSS;
    opt.length = 4;
    opt.mss = mss;

    return opt;
}

/**
 * @brief 构造窗口缩放选项
 *
 * @param scale_factor 缩放因子
 * @return 窗口缩放选项结构
 */
tcp_opt_window_scale_t tcp_options_build_window_scale(uint8_t scale_factor)
{
    tcp_opt_window_scale_t opt;

    opt.kind = TCP_OPT_WINDOW_SCALE;
    opt.length = 3;
    opt.scale_factor = scale_factor;

    return opt;
}

/**
 * @brief 构造 SACK 选项
 *
 * @param num_blocks SACK 块数量
 * @param left_edges SACK 左边界数组
 * @param right_edges SACK 右边界数组
 * @return SACK 选项结构
 */
tcp_opt_sack_t tcp_options_build_sack(uint8_t num_blocks,
                                       const uint32_t *left_edges,
                                       const uint32_t *right_edges)
{
    tcp_opt_sack_t opt;
    uint8_t i;

    opt.kind = TCP_OPT_SACK;
    opt.length = 2 + num_blocks * 8;
    opt.num_blocks = num_blocks;

    /* 填充 SACK 块 */
    for (i = 0; i < num_blocks; i++)
    {
        opt.left_edge[i] = left_edges[i];
        opt.right_edge[i] = right_edges[i];
    }

    return opt;
}

/**
 * @brief 检查选项是否存在
 *
 * @param buffer 输入缓冲区
 * @param length 缓冲区长度
 * @param kind 选项类型
 * @return true=存在，false=不存在
 */
bool tcp_options_exists(const uint8_t *buffer, uint16_t length, uint8_t kind)
{
    uint16_t offset;

    if ((buffer == NULL) || (length == 0))
    {
        return false;
    }

    /* 遍历所有选项 */
    offset = 0;
    while (offset < length)
    {
        if (buffer[offset] == TCP_OPT_END)
        {
            break;
        }

        if (buffer[offset] == TCP_OPT_NOP)
        {
            offset++;
            continue;
        }

        /* 检查选项类型 */
        if (buffer[offset] == kind)
        {
            return true;
        }

        /* 跳过选项 */
        if (offset + 1 >= length)
        {
            break;
        }

        offset += buffer[offset + 1];
    }

    return false;
}

/**
 * @brief 序列化 MSS 选项
 *
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @param mss MSS 值
 * @return 写入的字节数
 */
uint16_t tcp_options_serialize_mss(uint8_t *buffer, uint16_t size,
                                    uint16_t mss)
{
    return serialize_mss(buffer, size, mss);
}

/**
 * @brief 序列化窗口缩放选项
 *
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @param scale_factor 缩放因子
 * @return 写入的字节数
 */
uint16_t tcp_options_serialize_window_scale(uint8_t *buffer, uint16_t size,
                                             uint8_t scale_factor)
{
    return serialize_window_scale(buffer, size, scale_factor);
}

/**
 * @brief 序列化 SACK 选项
 *
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @param num_blocks SACK 块数量
 * @param left_edges 左边界数组
 * @param right_edges 右边界数组
 * @return 写入的字节数
 */
uint16_t tcp_options_serialize_sack(uint8_t *buffer, uint16_t size,
                                     uint8_t num_blocks,
                                     const uint32_t *left_edges,
                                     const uint32_t *right_edges)
{
    return serialize_sack(buffer, size, num_blocks, left_edges, right_edges);
}

/**
 * @brief 获取 MSS
 *
 * @param state 选项状态
 * @return MSS 值
 */
uint16_t tcp_options_get_mss(const tcp_options_state_t *state)
{
    if (state == NULL)
    {
        return TCP_DEFAULT_MSS;
    }

    return state->mss;
}

/**
 * @brief 获取窗口缩放因子
 *
 * @param state 选项状态
 * @return 窗口缩放因子
 */
uint8_t tcp_options_get_window_scale(const tcp_options_state_t *state)
{
    if (state == NULL)
    {
        return 0;
    }

    return state->window_scale;
}

/**
 * @brief 检查 SACK 是否允许
 *
 * @param state 选项状态
 * @return true=允许，false=不允许
 */
bool tcp_options_is_sack_permitted(const tcp_options_state_t *state)
{
    if (state == NULL)
    {
        return false;
    }

    return (state->sack_permitted != 0);
}

/**
 * @brief 获取 SACK 块
 *
 * @param state 选项状态
 * @param index 块索引
 * @param left_edge 左边界（输出）
 * @param right_edge 右边界（输出）
 * @return true=成功，false=失败
 */
bool tcp_options_get_sack_block(const tcp_options_state_t *state,
                                  uint8_t index,
                                  uint32_t *left_edge,
                                  uint32_t *right_edge)
{
    if (state == NULL)
    {
        return false;
    }

    if (index >= state->sack_count)
    {
        return false;
    }

    if ((left_edge == NULL) || (right_edge == NULL))
    {
        return false;
    }

    *left_edge = state->sack_left[index];
    *right_edge = state->sack_right[index];

    return true;
}

/**
 * @brief 检查 MSS 是否已协商
 *
 * @param state 选项状态
 * @return true=已协商，false=未协商
 */
bool tcp_options_is_mss_negotiated(const tcp_options_state_t *state)
{
    if (state == NULL)
    {
        return false;
    }

    return state->mss_negotiated;
}

/**
 * @brief 检查窗口缩放是否已协商
 *
 * @param state 选项状态
 * @return true=已协商，false=未协商
 */
bool tcp_options_is_window_scale_negotiated(const tcp_options_state_t *state)
{
    if (state == NULL)
    {
        return false;
    }

    return state->window_scale_negotiated;
}
