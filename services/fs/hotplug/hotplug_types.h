/**
 * @file    hotplug_types.h
 * @brief   热插拔支持类型定义
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details 热插拔支持核心数据类型定义：
 *          - 设备插入/移除事件
 *          - 热插拔回调接口
 *          - 设备监控
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef HOTPLUG_TYPES_H
#define HOTPLUG_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大设备数量 */
#define HOTPLUG_MAX_DEVICES    32U

/** @brief 设备路径最大长度 */
#define HOTPLUG_MAX_PATH_LEN   256U

/* ========================================================================
 * 热插拔事件类型
 * ======================================================================== */

/**
 * @brief 热插拔事件类型
 */
typedef enum
{
    HOTPLUG_EVENT_NONE = 0U,       /**< @brief 无事件 */
    HOTPLUG_EVENT_INSERT,          /**< @brief 设备插入 */
    HOTPLUG_EVENT_REMOVE,          /**< @brief 设备移除 */
    HOTPLUG_EVENT_CHANGE           /**< @brief 设备状态变化 */
} hotplug_event_type_t;

/* ========================================================================
 * 设备类型
 * ======================================================================== */

/**
 * @brief 设备类型
 */
typedef enum
{
    HOTPLUG_DEV_TYPE_BLOCK = 0U,   /**< @brief 块设备 */
    HOTPLUG_DEV_TYPE_CHAR,         /**< @brief 字符设备 */
    HOTPLUG_DEV_TYPE_NET           /**< @brief 网络设备 */
} hotplug_dev_type_t;

/* ========================================================================
 * 设备状态
 * ======================================================================== */

/**
 * @brief 设备状态
 */
typedef enum
{
    HOTPLUG_DEV_STATE_DISCONNECTED = 0U, /**< @brief 未连接 */
    HOTPLUG_DEV_STATE_CONNECTING,       /**< @brief 连接中 */
    HOTPLUG_DEV_STATE_CONNECTED,         /**< @brief 已连接 */
    HOTPLUG_DEV_STATE_READY,             /**< @brief 就绪 */
    HOTPLUG_DEV_STATE_ERROR              /**< @brief 错误 */
} hotplug_dev_state_t;

/* ========================================================================
 * 热插拔事件
 * ======================================================================== */

/**
 * @brief 热插拔事件
 */
typedef struct
{
    hotplug_event_type_t type;     /**< @brief 事件类型 */
    hotplug_dev_type_t dev_type;   /**< @brief 设备类型 */
    char device_path[HOTPLUG_MAX_PATH_LEN]; /**< @brief 设备路径 */
    uint64_t timestamp;            /**< @brief 事件时间戳 */
    uint32_t event_id;             /**< @brief 事件 ID */
} hotplug_event_t;

/* ========================================================================
 * 热插拔设备描述符
 * ======================================================================== */

/**
 * @brief 热插拔设备描述符
 */
typedef struct
{
    char device_path[HOTPLUG_MAX_PATH_LEN]; /**< @brief 设备路径 */
    hotplug_dev_type_t type;       /**< @brief 设备类型 */
    hotplug_dev_state_t state;     /**< @brief 设备状态 */
    uint64_t size_in_sectors;      /**< @brief 设备大小（扇区数） */
    uint32_t ref_count;            /**< @brief 引用计数 */
    bool in_use;                   /**< @brief 使用标记 */
    bool mounted;                   /**< @brief 是否已挂载 */
    char mount_point[HOTPLUG_MAX_PATH_LEN]; /**< @brief 挂载点 */
} hotplug_device_t;

/* ========================================================================
 * 热插拔回调接口
 * ======================================================================== */

/**
 * @brief 热插拔回调函数类型
 *
 * @param event 热插拔事件
 * @param user_data 用户数据
 *
 * @return 0 成功，<0 失败
 */
typedef int32_t (*hotplug_callback_t)(const hotplug_event_t *event,
                                       void *user_data);

/* ========================================================================
 * 热插拔管理器
 * ======================================================================== */

/**
 * @brief 热插拔管理器
 */
typedef struct
{
    hotplug_device_t devices[HOTPLUG_MAX_DEVICES]; /**< @brief 设备表 */
    hotplug_callback_t callbacks[16];            /**< @brief 回调表 */
    void *callback_data[16];                      /**< @brief 回调数据 */
    uint32_t event_id;                            /**< @brief 事件 ID */
    bool initialized;                             /**< @brief 初始化标志 */
} hotplug_manager_t;

#endif /* HOTPLUG_TYPES_H */
