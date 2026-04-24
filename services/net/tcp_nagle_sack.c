/**
 * @file    tcp_nagle_sack.c
 * @brief   TCP Nagle 算法和 SACK 实现（GREEN 阶段）
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @details TCP Nagle 算法和 SACK 功能实现：
 *          - Nagle 算法（延迟发送小数据包）
 *          - SACK（Selective Acknowledgment）选项处理
 *          - 延迟 ACK 机制
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: TCP Nagle 算法、SACK
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/netstack.h>
#include <kernel/config.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 数据结构定义
 * ======================================================================== */

/* TCP 最大段大小 */
#define TCP_MSS      1460U

/* TCP SACK 最大块数 */
#define TCP_MAX_SACK_BLOCKS  4U

/* TCP 控制块（简化版，只包含 Nagle/SACK 需要的字段） */
typedef struct
{
    uint32_t cwnd;             /**< @brief 拥塞窗口（用于 Nagle 算法） */
    uint8_t  sack_permitted;   /**< @brief SACK 允许标志 */
    uint32_t sack_left[4];     /**< @brief SACK 左边界 */
    uint32_t sack_right[4];    /**< @brief SACK 右边界 */
    uint8_t  sack_count;       /**< @brief SACK 块数量 */
    uint8_t  nagle_enabled;    /**< @brief Nagle 算法启用 */
    uint8_t  tcp_cork;         /**< @brief Cork 模式 */
    uint8_t  delayed_ack;      /**< @brief 延迟 ACK 计数 */
} tcp_tcb_t;

/* ========================================================================
 * Nagle 算法函数
 * ======================================================================== */

/**
 * @brief 检查是否可以发送（Nagle 算法）
 *
 * @param tcb TCP 控制块
 * @param unacked 未确认的数据长度
 *
 * @return true 可以发送，false 不能发送
 */
bool tcp_nagle_can_send(tcp_tcb_t *tcb, uint32_t unacked)
{
    if (tcb == NULL)
    {
        return true;  /* 允许发送 */
    }

    /* Nagle 算法禁用，立即发送 */
    if (tcb->nagle_enabled == 0U)
    {
        return true;
    }

    /* Cork 模式，不允许发送 */
    if (tcb->tcp_cork != 0U)
    {
        return false;
    }

    /* 没有未确认的数据，立即发送 */
    if (unacked == 0U)
    {
        return true;
    }

    /* 有未确认的数据，但发送缓冲区 >= MSS，立即发送 */
    if (tcb->cwnd >= TCP_MSS)
    {
        return true;
    }

    /* 其他情况，延迟发送 */
    return false;
}

/**
 * @brief 设置 Cork 模式
 *
 * @param tcb TCP 控制块
 * @param enable true 启用，false 禁用
 */
void tcp_nagle_set_cork(tcp_tcb_t *tcb, bool enable)
{
    if (tcb == NULL)
    {
        return;
    }

    if (enable)
    {
        tcb->tcp_cork = 1U;
    }
    else
    {
        tcb->tcp_cork = 0U;
    }
}

/* ========================================================================
 * SACK 函数
 * ======================================================================== */

/**
 * @brief 添加 SACK 块
 *
 * @param tcb TCP 控制块
 * @param left 左边界序列号
 * @param right 右边界序列号
 */
void tcp_sack_add_block(tcp_tcb_t *tcb, uint32_t left, uint32_t right)
{
    uint32_t i;

    if (tcb == NULL)
    {
        return;
    }

    /* SACK 未允许 */
    if (tcb->sack_permitted == 0U)
    {
        return;
    }

    /* 检查是否已存在 */
    for (i = 0U; i < tcb->sack_count; i++)
    {
        if ((tcb->sack_left[i] == left) && (tcb->sack_right[i] == right))
        {
            return;  /* 已存在 */
        }
    }

    /* 添加新块 */
    if (tcb->sack_count < TCP_MAX_SACK_BLOCKS)
    {
        tcb->sack_left[tcb->sack_count] = left;
        tcb->sack_right[tcb->sack_count] = right;
        tcb->sack_count++;
    }
    else
    {
        /* 超过最大块数，删除最老的块 */
        for (i = 0U; i < (TCP_MAX_SACK_BLOCKS - 1U); i++)
        {
            tcb->sack_left[i] = tcb->sack_left[i + 1U];
            tcb->sack_right[i] = tcb->sack_right[i + 1U];
        }

        tcb->sack_left[TCP_MAX_SACK_BLOCKS - 1U] = left;
        tcb->sack_right[TCP_MAX_SACK_BLOCKS - 1U] = right;
    }
}

/**
 * @brief 检查 SACK 包含
 *
 * @param tcb TCP 控制块
 * @param seq 序列号
 *
 * @return true 包含，false 不包含
 */
bool tcp_sack_contains(tcp_tcb_t *tcb, uint32_t seq)
{
    uint32_t i;

    if (tcb == NULL)
    {
        return false;
    }

    for (i = 0U; i < tcb->sack_count; i++)
    {
        if ((seq >= tcb->sack_left[i]) && (seq < tcb->sack_right[i]))
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief 清除 SACK 块
 *
 * @param tcb TCP 控制块
 */
void tcp_sack_clear(tcp_tcb_t *tcb)
{
    uint32_t i;

    if (tcb == NULL)
    {
        return;
    }

    tcb->sack_count = 0U;
    for (i = 0U; i < TCP_MAX_SACK_BLOCKS; i++)
    {
        tcb->sack_left[i] = 0U;
        tcb->sack_right[i] = 0U;
    }
}

/* ========================================================================
 * 延迟 ACK 函数
 * ======================================================================== */

/**
 * @brief 延迟 ACK 处理
 *
 * @param tcb TCP 控制块
 */
void tcp_delayed_ack(tcp_tcb_t *tcb)
{
    if (tcb == NULL)
    {
        return;
    }

    /* 增加延迟 ACK 计数 */
    tcb->delayed_ack++;

    /* 最多延迟 2 个 ACK */
    if (tcb->delayed_ack > 2U)
    {
        tcb->delayed_ack = 2U;
    }
}

/**
 * @brief 立即发送 ACK
 *
 * @param tcb TCP 控制块
 */
void tcp_send_immediate_ack(tcp_tcb_t *tcb)
{
    if (tcb == NULL)
    {
        return;
    }

    /* 清除延迟 ACK 计数 */
    tcb->delayed_ack = 0U;
}
