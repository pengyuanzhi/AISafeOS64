/**
 * @file    twin_driver.h
 * @brief   双生驱动框架接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 双生驱动将设备操作分为两个面：
 *          - 控制面（Control Plane）：运行在 Linux 驱动容器中，
 *            负责设备初始化、配置、电源管理等复杂操作
 *          - 数据面（Data Plane）：运行在原生用户态驱动中，
 *            负责 I/O 数据传输、中断处理等高性能操作
 *
 *          两面通过 IC2 通道协同工作，实现：
 *          - Linux 驱动的兼容性 + 原生驱动的性能
 *          - 控制面崩溃不影响数据面（故障隔离）
 *          - 热切换：控制面重启后自动恢复连接
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: DR-006~008
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_TWIN_DRIVER_H
#define KERNEL_TWIN_DRIVER_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/driver_framework.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 双生驱动常量
 * ======================================================================== */

/** @brief 最大双生驱动对数 */
#define TWIN_MAX_PAIRS                 8U

/** @brief 双生驱动名称最大长度 */
#define TWIN_NAME_MAX                  32U

/** @brief IC2 控制通道默认缓冲区大小 */
#define TWIN_CTRL_BUF_SIZE             2048U

/* ========================================================================
 * 双生驱动操作类型
 * ======================================================================== */

/**
 * @brief 双生驱动面类型
 */
typedef enum
{
    TWIN_PLANE_CONTROL = 0U,   /**< @brief 控制面（Linux 容器侧） */
    TWIN_PLANE_DATA             /**< @brief 数据面（原生驱动侧） */
} twin_plane_t;

/**
 * @brief 双生驱动对状态
 */
typedef enum
{
    TWIN_STATE_IDLE = 0U,      /**< @brief 空闲（未绑定） */
    TWIN_STATE_INITIALIZING,   /**< @brief 初始化中 */
    TWIN_STATE_ACTIVE,         /**< @brief 活跃（双面运行） */
    TWIN_STATE_CTRL_DOWN,      /**< @brief 控制面故障 */
    TWIN_STATE_DEGRADED,       /**< @brief 降级模式（仅数据面） */
    TWIN_STATE_STOPPED         /**< @brief 已停止 */
} twin_state_t;

/**
 * @brief IC2 控制面消息类型
 */
typedef enum
{
    TWIN_MSG_INIT = 1U,        /**< @brief 初始化请求 */
    TWIN_MSG_CONFIG,           /**< @brief 配置更新 */
    TWIN_MSG_RESET,            /**< @brief 复位请求 */
    TWIN_MSG_STATUS_QUERY,     /**< @brief 状态查询 */
    TWIN_MSG_STATUS_REPLY,     /**< @brief 状态回复 */
    TWIN_MSG_POWER_STATE,      /**< @brief 电源状态变更 */
    TWIN_MSG_ERROR_REPORT      /**< @brief 错误报告 */
} twin_msg_type_t;

/* ========================================================================
 * 双生驱动描述符
 * ======================================================================== */

/**
 * @brief 双生驱动统计信息
 */
typedef struct
{
    uint32_t    ctrl_tx_count;      /**< @brief 控制面发送计数 */
    uint32_t    ctrl_rx_count;      /**< @brief 控制面接收计数 */
    uint32_t    data_tx_count;      /**< @brief 数据面发送计数 */
    uint32_t    data_rx_count;      /**< @brief 数据面接收计数 */
    uint32_t    ctrl_failover_count; /**< @brief 控制面故障转移计数 */
    uint32_t    degraded_count;     /**< @brief 降级模式进入计数 */
} twin_stats_t;

/**
 * @brief 双生驱动对描述符
 *
 * @details 描述一个双生驱动的控制面和数据面配对关系
 */
typedef struct
{
    uint32_t        pair_id;            /**< @brief 配对 ID */
    char            name[TWIN_NAME_MAX]; /**< @brief 驱动名称 */
    twin_state_t    state;              /**< @brief 当前状态 */

    /* 控制面（Linux 容器侧） */
    uint32_t        ctrl_container_id;  /**< @brief 控制面容器 ID */
    uint32_t        ctrl_device_id;     /**< @brief 控制面设备 ID */
    uint32_t        ctrl_ic2_channel;   /**< @brief 控制面 IC2 通道 */

    /* 数据面（原生驱动侧） */
    uint32_t        data_driver_id;     /**< @brief 数据面驱动 ID */
    uint32_t        data_ic2_channel;   /**< @brief 数据面 IC2 通道 */

    /* 统计 */
    twin_stats_t    stats;              /**< @brief 统计信息 */

    /* 配置 */
    bool            auto_recovery;      /**< @brief 自动恢复标志 */
    uint32_t        failover_timeout_ms; /**< @brief 故障转移超时（毫秒） */
} twin_pair_t;

/* ========================================================================
 * 双生驱动 API
 * ======================================================================== */

/**
 * @brief 初始化双生驱动子系统
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: DR-006
 */
kernel_status_t twin_init(void);

/**
 * @brief 创建双生驱动对
 *
 * @param name               驱动名称
 * @param ctrl_container_id  控制面容器 ID
 * @param ctrl_device_id     控制面设备 ID
 * @param data_driver_id     数据面驱动 ID
 * @param auto_recovery      自动恢复标志
 *
 * @return 成功返回配对 ID，失败返回负错误码
 */
int32_t twin_create(const char *name, uint32_t ctrl_container_id,
                     uint32_t ctrl_device_id, uint32_t data_driver_id,
                     bool auto_recovery);

/**
 * @brief 销毁双生驱动对
 *
 * @param pair_id 配对 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t twin_destroy(uint32_t pair_id);

/**
 * @brief 启动双生驱动对
 *
 * @details 启动控制面和数据面之间的 IC2 通道
 *
 * @param pair_id 配对 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t twin_start(uint32_t pair_id);

/**
 * @brief 停止双生驱动对
 *
 * @param pair_id 配对 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t twin_stop(uint32_t pair_id);

/**
 * @brief 通过控制面发送配置命令
 *
 * @param pair_id 配对 ID
 * @param msg_type 消息类型
 * @param data     数据缓冲区
 * @param size     数据大小
 *
 * @return 成功发送的字节数，失败返回负错误码
 */
int32_t twin_ctrl_send(uint32_t pair_id, twin_msg_type_t msg_type,
                        const void *data, uint32_t size);

/**
 * @brief 从控制面接收回复
 *
 * @param pair_id   配对 ID
 * @param msg_type  输出消息类型（可为 NULL）
 * @param buf       接收缓冲区
 * @param buf_size  缓冲区大小
 *
 * @return 成功接收的字节数，失败返回负错误码
 */
int32_t twin_ctrl_recv(uint32_t pair_id, twin_msg_type_t *msg_type,
                        void *buf, uint32_t buf_size);

/**
 * @brief 数据面 I/O 请求
 *
 * @details 数据面直接执行高性能 I/O，不经过控制面
 *
 * @param pair_id 配对 ID
 * @param cmd     I/O 命令
 * @param buf     数据缓冲区
 * @param size    数据大小
 *
 * @return 实际传输字节数，负数表示错误
 */
int32_t twin_data_io(uint32_t pair_id, uint32_t cmd,
                      void *buf, uint32_t size);

/**
 * @brief 处理控制面故障
 *
 * @details 当控制面崩溃时，自动将双生驱动切换到降级模式
 *
 * @param pair_id 配对 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t twin_handle_ctrl_failure(uint32_t pair_id);

/**
 * @brief 恢复控制面连接
 *
 * @details 控制面重启后，重新建立 IC2 通道并同步状态
 *
 * @param pair_id 配对 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t twin_recover_ctrl(uint32_t pair_id);

/**
 * @brief 获取双生驱动对描述符
 *
 * @param pair_id 配对 ID
 *
 * @return 描述符指针，不存在返回 NULL
 */
twin_pair_t *twin_get_pair(uint32_t pair_id);

/**
 * @brief 获取双生驱动统计信息
 *
 * @param pair_id 配对 ID
 * @param[out] stats 统计信息输出
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t twin_get_stats(uint32_t pair_id, twin_stats_t *stats);

#endif /* KERNEL_TWIN_DRIVER_H */
