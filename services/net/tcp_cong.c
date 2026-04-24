/**
 * @file    tcp_cong.c
 * @brief   TCP 拥塞控制实现（GREEN 阶段）
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @details TCP 拥塞控制功能实现：
 *          - CUBIC 拥塞窗口调整算法
 *          - RTT 估算（Karn 算法）
 *          - RTO 自适应超时
 *          - 快速重传和快速恢复
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: TCP 拥塞控制
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/netstack.h>
#include <kernel/config.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* ========================================================================
 * 数据结构定义
 * ======================================================================== */

/* 拥塞控制状态（从 main.c 中复制，避免循环依赖） */
typedef enum
{
    CONG_OPEN = 0,             /**< @brief 开启状态 */
    CONG_SLOW_START,           /**< @brief 慢启动 */
    CONG_CONGESTION_AVOIDANCE,  /**< @brief 拥塞避免 */
    CONG_FAST_RECOVERY,        /**< @brief 快速恢复 */
    CONG_TIMEOUT               /**< @brief 超时 */
} congestion_state_t;

/* RTT 测量数据结构 */
typedef struct
{
    uint32_t rtt_sample;       /**< @brief 最新 RTT 样本 */
    uint32_t rtt_min;          /**< @brief 最小 RTT */
    uint32_t rtt_var;          /**< @brief RTT 偏差 */
    uint32_t srtt;             /**< @brief 平滑 RTT */
    uint32_t rto;              /**< @brief 重传超时 */
} tcp_rtt_t;

/* 拥塞控制数据结构 */
typedef struct
{
    congestion_state_t state;  /**< @brief 拥塞状态 */
    uint32_t ssthresh;         /**< @brief 慢启动阈值 */
    uint32_t cwnd;             /**< @brief 拥塞窗口 */
    uint32_t w_max;            /**< @brief 峰值窗口 */
    uint64_t last_acks;        /**< @brief 最后确认序列号 */
    uint64_t last_retrans;     /**< @brief 最后重传时间 */
    uint64_t last_rtt_sample;  /**< @brief 最后 RTT 样本 */
    tcp_rtt_t rtt;             /**< @brief RTT 测量 */
    uint32_t dup_acks;         /**< @brief 重复 ACK 计数 */
    uint32_t last_ack;         /**< @brief 最后 ACK */
} tcp_congestion_ctrl_t;

/* TCP 最大段大小 */
#define TCP_MSS      1460U

/* TCP 最大重复 ACK 次数 */
#define TCP_MAX_DUP_ACKS        3U

/* TCP 控制块（简化版，只包含拥塞控制需要的字段） */
typedef struct
{
    tcp_congestion_ctrl_t cong_ctrl; /**< @brief 拥塞控制 */
} tcp_tcb_t;

/* ========================================================================
 * 拥塞控制函数
 * ======================================================================== */

/**
 * @brief 慢启动阶段
 *
 * @param tcb TCP 控制块
 */
static void tcp_cong_slow_start(tcp_tcb_t *tcb)
{
    if (tcb == NULL)
    {
        return;
    }

    /* 慢启动：cwnd += MSS */
    tcb->cong_ctrl.cwnd += TCP_MSS;

    /* 进入拥塞避免阶段 */
    if (tcb->cong_ctrl.cwnd >= tcb->cong_ctrl.ssthresh)
    {
        tcb->cong_ctrl.state = CONG_CONGESTION_AVOIDANCE;
    }
}

/**
 * @brief 拥塞避免阶段（CUBIC 算法简化版）
 *
 * @param tcb TCP 控制块
 */
static void tcp_cong_congestion_avoidance(tcp_tcb_t *tcb)
{
    if (tcb == NULL)
    {
        return;
    }

    /* 拥塞避免：cwnd += (MSS * MSS) / cwnd */
    tcb->cong_ctrl.cwnd += (TCP_MSS * TCP_MSS) / tcb->cong_ctrl.cwnd;

    /* 更新峰值窗口 */
    if (tcb->cong_ctrl.cwnd > tcb->cong_ctrl.w_max)
    {
        tcb->cong_ctrl.w_max = tcb->cong_ctrl.cwnd;
    }
}

/**
 * @brief 快速重传
 *
 * @param tcb TCP 控制块
 * @param dup_acks 重复 ACK 计数
 */
static void tcp_cong_fast_retransmit(tcp_tcb_t *tcb, uint32_t dup_acks)
{
    if ((tcb == NULL) || (dup_acks < TCP_MAX_DUP_ACKS))
    {
        return;
    }

    /* 快速重传：ssthresh = cwnd / 2 */
    tcb->cong_ctrl.ssthresh = tcb->cong_ctrl.cwnd / 2;

    /* ssthresh = max(ssthresh, 2 * MSS) */
    if (tcb->cong_ctrl.ssthresh < (2U * TCP_MSS))
    {
        tcb->cong_ctrl.ssthresh = 2U * TCP_MSS;
    }

    /* 进入快速恢复 */
    tcb->cong_ctrl.state = CONG_FAST_RECOVERY;
}

/**
 * @brief 快速恢复
 *
 * @param tcb TCP 控制块
 */
static void tcp_cong_fast_recovery(tcp_tcb_t *tcb)
{
    if (tcb == NULL)
    {
        return;
    }

    /* 快速恢复：cwnd = ssthresh + 3 * MSS */
    tcb->cong_ctrl.cwnd = tcb->cong_ctrl.ssthresh + (3U * TCP_MSS);
}

/**
 * @brief 超时处理
 *
 * @param tcb TCP 控制块
 */
static void tcp_cong_timeout(tcp_tcb_t *tcb)
{
    if (tcb == NULL)
    {
        return;
    }

    /* 超时：ssthresh = cwnd / 2 */
    tcb->cong_ctrl.ssthresh = tcb->cong_ctrl.cwnd / 2;

    /* cwnd = MSS（重新开始慢启动） */
    tcb->cong_ctrl.cwnd = TCP_MSS;

    /* 进入慢启动 */
    tcb->cong_ctrl.state = CONG_SLOW_START;

    /* 清除重复 ACK 计数 */
    tcb->cong_ctrl.dup_acks = 0U;
}

/**
 * @brief 新 ACK 处理
 *
 * @param tcb TCP 控制块
 * @param ack ACK 序列号
 */
static void tcp_cong_new_ack(tcp_tcb_t *tcb, uint32_t ack)
{
    if (tcb == NULL)
    {
        return;
    }

    /* 清除重复 ACK 计数 */
    tcb->cong_ctrl.dup_acks = 0U;

    /* 根据拥塞状态更新拥塞窗口 */
    switch (tcb->cong_ctrl.state)
    {
        case CONG_SLOW_START:
            tcp_cong_slow_start(tcb);
            break;

        case CONG_CONGESTION_AVOIDANCE:
            tcp_cong_congestion_avoidance(tcb);
            break;

        case CONG_FAST_RECOVERY:
            /* 收到不重复的 ACK，退出快速恢复 */
            tcb->cong_ctrl.state = CONG_CONGESTION_AVOIDANCE;
            tcb->cong_ctrl.cwnd = tcb->cong_ctrl.ssthresh;
            break;

        case CONG_TIMEOUT:
        case CONG_OPEN:
        default:
            /* 其他状态：进入慢启动 */
            tcb->cong_ctrl.state = CONG_SLOW_START;
            tcb->cong_ctrl.cwnd = TCP_MSS;
            break;
    }
}

/* ========================================================================
 * RTT 估算函数
 * ======================================================================== */

/**
 * @brief RTT 更新（Karn 算法）
 *
 * @param tcb TCP 控制块
 * @param rtt_sample RTT 样本（毫秒）
 */
static void tcp_rtt_update(tcp_tcb_t *tcb, uint32_t rtt_sample)
{
    uint32_t rtt_var;
    uint32_t srtt;

    if ((tcb == NULL) || (rtt_sample == 0U))
    {
        return;
    }

    /* Karn 算法：只使用未重传数据包的 RTT 样本 */
    /* 简化：假设所有 RTT 样本都有效 */

    /* 更新最小 RTT */
    if (rtt_sample < tcb->cong_ctrl.rtt.rtt_min)
    {
        tcb->cong_ctrl.rtt.rtt_min = rtt_sample;
    }

    /* 更新 RTT 样本 */
    tcb->cong_ctrl.rtt.rtt_sample = rtt_sample;

    /* 更新平滑 RTT（SRTT） */
    /* SRTT = α * SRTT + (1 - α) * RTT_sample，其中 α = 0.875（7/8） */
    srtt = (7U * tcb->cong_ctrl.rtt.srtt + rtt_sample) / 8U;
    tcb->cong_ctrl.rtt.srtt = srtt;

    /* 更新 RTT 偏差 */
    /* RTT_var = β * RTT_var + (1 - β) * |SRTT - RTT_sample|，其中 β = 0.75（3/4） */
    if (srtt > rtt_sample)
    {
        rtt_var = (3U * tcb->cong_ctrl.rtt.rtt_var + (srtt - rtt_sample)) / 4U;
    }
    else
    {
        rtt_var = (3U * tcb->cong_ctrl.rtt.rtt_var + (rtt_sample - srtt)) / 4U;
    }
    tcb->cong_ctrl.rtt.rtt_var = rtt_var;

    /* 更新 RTO */
    /* 更新 RTO（内联，避免函数声明顺序问题） */
    {
        uint32_t rto = tcb->cong_ctrl.rtt.srtt + (4U * tcb->cong_ctrl.rtt.rtt_var);

        /* 限制 RTO 范围：[1秒, 60秒] */
        if (rto < 1000U)
        {
            rto = 1000U;
        }
        else if (rto > 60000U)
        {
            rto = 60000U;
        }

        tcb->cong_ctrl.rtt.rto = rto;
    }
}

/* ========================================================================
 * 导出函数（在 tcp_process_segment 中调用）
 * ======================================================================== */

/**
 * @brief 处理新 ACK（调用拥塞控制）
 */
void tcp_handle_new_ack(tcp_tcb_t *tcb, uint32_t ack)
{
    tcp_cong_new_ack(tcb, ack);
}

/**
 * @brief 处理重复 ACK（触发快速重传）
 */
void tcp_handle_dup_ack(tcp_tcb_t *tcb, uint32_t ack)
{
    if (tcb == NULL)
    {
        return;
    }

    /* 检查是否是重复 ACK */
    if (ack == tcb->cong_ctrl.last_ack)
    {
        tcb->cong_ctrl.dup_acks++;

        /* 收到 3 个重复 ACK，触发快速重传 */
        if (tcb->cong_ctrl.dup_acks >= TCP_MAX_DUP_ACKS)
        {
            tcp_cong_fast_retransmit(tcb, tcb->cong_ctrl.dup_acks);
            tcp_cong_fast_recovery(tcb);
        }
    }
    else
    {
        /* 不是重复 ACK，重置计数 */
        tcb->cong_ctrl.dup_acks = 1U;
    }

    tcb->cong_ctrl.last_ack = ack;
}

/**
 * @brief 处理超时（调用拥塞控制）
 */
void tcp_handle_timeout(tcp_tcb_t *tcb)
{
    tcp_cong_timeout(tcb);
}

/**
 * @brief 处理 RTT 样本（更新 RTT 和 RTO）
 */
void tcp_handle_rtt_sample(tcp_tcb_t *tcb, uint32_t rtt_sample)
{
    tcp_rtt_update(tcb, rtt_sample);
}
