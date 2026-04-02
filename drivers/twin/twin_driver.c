/**
 * @file    twin_driver.c
 * @brief   双生驱动框架实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 双生驱动框架实现：
 *          - 控制面 + 数据面配对管理
 *          - IC2 通道连接控制面和数据面
 *          - 控制面故障自动降级
 *          - 控制面恢复后自动重建连接
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: DR-006~008
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/twin_driver.h>
#include <kernel/ipc_ic2.h>
#include <kernel/driver_framework.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 双生驱动全局状态
 * ======================================================================== */

/** @brief 双生驱动对实例池 */
static twin_pair_t s_pairs[TWIN_MAX_PAIRS];

/** @brief 配对使用标记 */
static bool s_pair_used[TWIN_MAX_PAIRS];

/** @brief 子系统初始化标志 */
static bool s_initialized = false;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 安全字符串复制
 */
static void twin_strcpy(char *dst, const char *src, uint32_t n)
{
    uint32_t i;

    if ((dst == NULL) || (src == NULL) || (n == 0U))
    {
        return;
    }

    for (i = 0U; (i < (n - 1U)) && (src[i] != '\0'); i++)
    {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* ========================================================================
 * 初始化
 * ======================================================================== */

kernel_status_t twin_init(void)
{
    uint32_t i;

    (void)memset(s_pairs, 0, sizeof(s_pairs));
    (void)memset(s_pair_used, 0, sizeof(s_pair_used));

    for (i = 0U; i < TWIN_MAX_PAIRS; i++)
    {
        s_pairs[i].pair_id = i;
        s_pairs[i].state = TWIN_STATE_IDLE;
        s_pairs[i].ctrl_container_id = 0U;
        s_pairs[i].ctrl_device_id = 0U;
        s_pairs[i].ctrl_ic2_channel = 0U;
        s_pairs[i].data_driver_id = 0U;
        s_pairs[i].data_ic2_channel = 0U;
        s_pairs[i].auto_recovery = false;
        s_pairs[i].failover_timeout_ms = 5000U;
    }

    s_initialized = true;

    return KERNEL_OK;
}

/* ========================================================================
 * 创建双生驱动对
 * ======================================================================== */

int32_t twin_create(const char *name, uint32_t ctrl_container_id,
                     uint32_t ctrl_device_id, uint32_t data_driver_id,
                     bool auto_recovery)
{
    uint32_t i;
    twin_pair_t *pair;

    if (!s_initialized)
    {
        return -(int32_t)22; /* -EINVAL */
    }

    if (name == NULL)
    {
        return -(int32_t)22;
    }

    /* 查找空闲配对槽 */
    for (i = 0U; i < TWIN_MAX_PAIRS; i++)
    {
        if (!s_pair_used[i])
        {
            break;
        }
    }

    if (i >= TWIN_MAX_PAIRS)
    {
        return -(int32_t)12; /* -ENOMEM */
    }

    pair = &s_pairs[i];

    (void)memset(pair, 0, sizeof(twin_pair_t));

    pair->pair_id = i;
    twin_strcpy(pair->name, name, TWIN_NAME_MAX);
    pair->state = TWIN_STATE_IDLE;

    /* 绑定控制面 */
    pair->ctrl_container_id = ctrl_container_id;
    pair->ctrl_device_id = ctrl_device_id;
    pair->ctrl_ic2_channel = 0U; /* 启动时创建 */

    /* 绑定数据面 */
    pair->data_driver_id = data_driver_id;
    pair->data_ic2_channel = 0U; /* 启动时创建 */

    /* 配置 */
    pair->auto_recovery = auto_recovery;
    pair->failover_timeout_ms = 5000U;

    s_pair_used[i] = true;

    return (int32_t)i;
}

/* ========================================================================
 * 销毁双生驱动对
 * ======================================================================== */

kernel_status_t twin_destroy(uint32_t pair_id)
{
    twin_pair_t *pair;

    if (pair_id >= TWIN_MAX_PAIRS)
    {
        return -(int32_t)22;
    }

    if (!s_pair_used[pair_id])
    {
        return -(int32_t)2; /* -ENOENT */
    }

    pair = &s_pairs[pair_id];

    /* 先停止 */
    if (pair->state != TWIN_STATE_IDLE)
    {
        (void)twin_stop(pair_id);
    }

    /* 销毁 IC2 通道 */
    if (pair->ctrl_ic2_channel != 0U)
    {
        (void)ic2_channel_destroy(pair->ctrl_ic2_channel);
    }

    if (pair->data_ic2_channel != 0U)
    {
        (void)ic2_channel_destroy(pair->data_ic2_channel);
    }

    s_pair_used[pair_id] = false;

    return KERNEL_OK;
}

/* ========================================================================
 * 启动双生驱动对
 * ======================================================================== */

kernel_status_t twin_start(uint32_t pair_id)
{
    twin_pair_t *pair;
    int32_t ch_ctrl;
    int32_t ch_data;

    if (pair_id >= TWIN_MAX_PAIRS)
    {
        return -(int32_t)22;
    }

    pair = &s_pairs[pair_id];

    if (!s_pair_used[pair_id])
    {
        return -(int32_t)2;
    }

    if (pair->state == TWIN_STATE_ACTIVE)
    {
        return KERNEL_OK;
    }

    pair->state = TWIN_STATE_INITIALIZING;

    /*
     * 创建控制面 IC2 通道：
     * A 端 = 数据面驱动进程（发起控制命令）
     * B 端 = 控制面容器进程（响应控制命令）
     */
    ch_ctrl = ic2_channel_create("twin-ctrl",
                                  pair->data_driver_id,
                                  pair->ctrl_container_id,
                                  TWIN_CTRL_BUF_SIZE);

    if (ch_ctrl < 0)
    {
        pair->state = TWIN_STATE_STOPPED;
        return -(int32_t)12;
    }

    pair->ctrl_ic2_channel = (uint32_t)ch_ctrl;

    /*
     * 创建数据面 IC2 通道：
     * A 端 = 控制面容器进程（发送配置更新）
     * B 端 = 数据面驱动进程（接收配置并应用）
     */
    ch_data = ic2_channel_create("twin-data",
                                  pair->ctrl_container_id,
                                  pair->data_driver_id,
                                  TWIN_CTRL_BUF_SIZE);

    if (ch_data < 0)
    {
        (void)ic2_channel_destroy(pair->ctrl_ic2_channel);
        pair->ctrl_ic2_channel = 0U;
        pair->state = TWIN_STATE_STOPPED;
        return -(int32_t)12;
    }

    pair->data_ic2_channel = (uint32_t)ch_data;

    pair->state = TWIN_STATE_ACTIVE;

    return KERNEL_OK;
}

/* ========================================================================
 * 停止双生驱动对
 * ======================================================================== */

kernel_status_t twin_stop(uint32_t pair_id)
{
    twin_pair_t *pair;

    if (pair_id >= TWIN_MAX_PAIRS)
    {
        return -(int32_t)22;
    }

    pair = &s_pairs[pair_id];

    if (pair->state == TWIN_STATE_IDLE)
    {
        return KERNEL_OK;
    }

    /* 关闭 IC2 通道 */
    if (pair->ctrl_ic2_channel != 0U)
    {
        (void)ic2_channel_destroy(pair->ctrl_ic2_channel);
        pair->ctrl_ic2_channel = 0U;
    }

    if (pair->data_ic2_channel != 0U)
    {
        (void)ic2_channel_destroy(pair->data_ic2_channel);
        pair->data_ic2_channel = 0U;
    }

    pair->state = TWIN_STATE_STOPPED;

    return KERNEL_OK;
}

/* ========================================================================
 * 控制面通信
 * ======================================================================== */

int32_t twin_ctrl_send(uint32_t pair_id, twin_msg_type_t msg_type,
                        const void *data, uint32_t size)
{
    twin_pair_t *pair;

    if (pair_id >= TWIN_MAX_PAIRS)
    {
        return -(int32_t)22;
    }

    if (data == NULL)
    {
        return -(int32_t)22;
    }

    pair = &s_pairs[pair_id];

    if (pair->state != TWIN_STATE_ACTIVE)
    {
        return -(int32_t)22;
    }

    /* 通过控制面 IC2 通道发送 */
    pair->stats.ctrl_tx_count++;

    return ic2_send(pair->ctrl_ic2_channel, data, size,
                     (uint32_t)msg_type, 0U);
}

int32_t twin_ctrl_recv(uint32_t pair_id, twin_msg_type_t *msg_type,
                        void *buf, uint32_t buf_size)
{
    twin_pair_t *pair;
    int32_t recv_bytes;
    uint32_t type = 0U;

    if (pair_id >= TWIN_MAX_PAIRS)
    {
        return -(int32_t)22;
    }

    if (buf == NULL)
    {
        return -(int32_t)22;
    }

    pair = &s_pairs[pair_id];

    if (pair->state != TWIN_STATE_ACTIVE)
    {
        return -(int32_t)22;
    }

    /* 从数据面 IC2 通道接收（控制面回复走 data 通道） */
    recv_bytes = ic2_recv(pair->data_ic2_channel, buf, buf_size, &type);

    if (recv_bytes > 0)
    {
        pair->stats.ctrl_rx_count++;

        if (msg_type != NULL)
        {
            *msg_type = (twin_msg_type_t)type;
        }
    }

    return recv_bytes;
}

/* ========================================================================
 * 数据面 I/O
 * ======================================================================== */

int32_t twin_data_io(uint32_t pair_id, uint32_t cmd,
                      void *buf, uint32_t size)
{
    twin_pair_t *pair;

    if (pair_id >= TWIN_MAX_PAIRS)
    {
        return -(int32_t)22;
    }

    if (buf == NULL)
    {
        return -(int32_t)22;
    }

    pair = &s_pairs[pair_id];

    /* 数据面在活跃或降级模式下均可工作 */
    if ((pair->state != TWIN_STATE_ACTIVE) &&
        (pair->state != TWIN_STATE_DEGRADED))
    {
        return -(int32_t)22;
    }

    /*
     * 数据面 I/O 直接由原生驱动处理：
     * 实际实现中调用 driver_ops_t 的 read/write/ioctl
     * 此处为框架实现
     */
    (void)cmd;
    pair->stats.data_tx_count++;

    return (int32_t)size;
}

/* ========================================================================
 * 故障处理与恢复
 * ======================================================================== */

kernel_status_t twin_handle_ctrl_failure(uint32_t pair_id)
{
    twin_pair_t *pair;

    if (pair_id >= TWIN_MAX_PAIRS)
    {
        return -(int32_t)22;
    }

    pair = &s_pairs[pair_id];

    if (!s_pair_used[pair_id])
    {
        return -(int32_t)2;
    }

    /* 标记控制面故障 */
    pair->state = TWIN_STATE_CTRL_DOWN;
    pair->stats.ctrl_failover_count++;

    /* 销毁控制面 IC2 通道（通道另一端已不可达） */
    if (pair->ctrl_ic2_channel != 0U)
    {
        (void)ic2_channel_destroy(pair->ctrl_ic2_channel);
        pair->ctrl_ic2_channel = 0U;
    }

    if (pair->data_ic2_channel != 0U)
    {
        (void)ic2_channel_destroy(pair->data_ic2_channel);
        pair->data_ic2_channel = 0U;
    }

    /* 切换到降级模式：数据面独立运行 */
    pair->state = TWIN_STATE_DEGRADED;
    pair->stats.degraded_count++;

    return KERNEL_OK;
}

kernel_status_t twin_recover_ctrl(uint32_t pair_id)
{
    twin_pair_t *pair;

    if (pair_id >= TWIN_MAX_PAIRS)
    {
        return -(int32_t)22;
    }

    pair = &s_pairs[pair_id];

    if (!s_pair_used[pair_id])
    {
        return -(int32_t)2;
    }

    /* 仅在降级模式下执行恢复 */
    if (pair->state != TWIN_STATE_DEGRADED)
    {
        return -(int32_t)22;
    }

    /* 重新创建 IC2 通道 */
    int32_t ch_ctrl = ic2_channel_create("twin-ctrl-recovery",
                                           pair->data_driver_id,
                                           pair->ctrl_container_id,
                                           TWIN_CTRL_BUF_SIZE);

    if (ch_ctrl < 0)
    {
        return -(int32_t)12;
    }

    pair->ctrl_ic2_channel = (uint32_t)ch_ctrl;

    int32_t ch_data = ic2_channel_create("twin-data-recovery",
                                           pair->ctrl_container_id,
                                           pair->data_driver_id,
                                           TWIN_CTRL_BUF_SIZE);

    if (ch_data < 0)
    {
        (void)ic2_channel_destroy(pair->ctrl_ic2_channel);
        pair->ctrl_ic2_channel = 0U;
        return -(int32_t)12;
    }

    pair->data_ic2_channel = (uint32_t)ch_data;

    /* 恢复到活跃状态 */
    pair->state = TWIN_STATE_ACTIVE;

    return KERNEL_OK;
}

/* ========================================================================
 * 查询
 * ======================================================================== */

twin_pair_t *twin_get_pair(uint32_t pair_id)
{
    if (pair_id >= TWIN_MAX_PAIRS)
    {
        return NULL;
    }

    if (!s_pair_used[pair_id])
    {
        return NULL;
    }

    return &s_pairs[pair_id];
}

kernel_status_t twin_get_stats(uint32_t pair_id, twin_stats_t *stats)
{
    twin_pair_t *pair;

    if (pair_id >= TWIN_MAX_PAIRS)
    {
        return -(int32_t)22;
    }

    if (stats == NULL)
    {
        return -(int32_t)22;
    }

    pair = &s_pairs[pair_id];

    if (!s_pair_used[pair_id])
    {
        return -(int32_t)2;
    }

    (void)memcpy(stats, &pair->stats, sizeof(twin_stats_t));

    return KERNEL_OK;
}
