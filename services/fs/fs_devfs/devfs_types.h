/**
 * @file    devfs_types.h
 * @brief   DEVFS 类型定义
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details DEVFS 核心数据类型定义：
 *          - 设备类型定义
 *          - 设备操作接口
 *          - 设备节点管理
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef DEVFS_TYPES_H
#define DEVFS_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * DEVFS 常量定义
 * ======================================================================== */

/** @brief 最大设备数量 */
#define DEVFS_MAX_DEVICES        64U

/** @brief 最大设备名称长度 */
#define DEVFS_MAX_NAME_LEN       64U

/** @brief 最大设备路径长度 */
#define DEVFS_MAX_PATH_LEN       256U

/* ========================================================================
 * 设备类型
 * ======================================================================== */

/**
 * @brief 设备类型
 */
typedef enum
{
    DEVFS_TYPE_CHAR = 0U,       /**< @brief 字符设备 */
    DEVFS_TYPE_BLOCK,           /**< @brief 块设备 */
    DEVFS_TYPE_FIFO             /**< @brief FIFO 管道 */
} devfs_device_type_t;

/* ========================================================================
 * 设备操作接口
 * ======================================================================== */

/**
 * @brief 设备操作接口
 */
typedef struct devfs_ops
{
    /**
     * @brief 打开设备
     *
     * @param private_data 设备私有数据
     * @param flags 打开标志
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*open)(void *private_data, uint32_t flags);

    /**
     * @brief 关闭设备
     *
     * @param private_data 设备私有数据
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*close)(void *private_data);

    /**
     * @brief 读取设备
     *
     * @param private_data 设备私有数据
     * @param buf 缓冲区
     * @param size 读取大小
     *
     * @return 实际读取字节数，<0 失败
     */
    int64_t (*read)(void *private_data, void *buf, uint64_t size);

    /**
     * @brief 写入设备
     *
     * @param private_data 设备私有数据
     * @param buf 缓冲区
     * @param size 写入大小
     *
     * @return 实际写入字节数，<0 失败
     */
    int64_t (*write)(void *private_data, const void *buf, uint64_t size);

    /**
     * @brief 设备控制
     *
     * @param private_data 设备私有数据
     * @param cmd 命令
     * @param arg 参数
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*ioctl)(void *private_data, uint32_t cmd, uint64_t arg);

} devfs_ops_t;

/* ========================================================================
 * 设备节点
 * ======================================================================== */

/**
 * @brief DEVFS 设备节点
 */
typedef struct devfs_device
{
    char name[DEVFS_MAX_NAME_LEN];     /**< @brief 设备名称 */
    devfs_device_type_t type;          /**< @brief 设备类型 */
    uint32_t major;                     /**< @brief 主设备号 */
    uint32_t minor;                     /**< @brief 次设备号 */
    const devfs_ops_t *ops;             /**< @brief 设备操作接口 */
    void *private_data;                 /**< @brief 设备私有数据 */
    bool in_use;                        /**< @brief 使用标记 */
} devfs_device_t;

/* ========================================================================
 * DEVFS 实例
 * ======================================================================== */

/**
 * @brief DEVFS 实例
 */
typedef struct
{
    devfs_device_t devices[DEVFS_MAX_DEVICES]; /**< @brief 设备表 */
    bool initialized;                         /**< @brief 初始化标志 */
} devfs_instance_t;

#endif /* DEVFS_TYPES_H */
