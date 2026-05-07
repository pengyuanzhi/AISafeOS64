/**
 * @file    hotplug.h
 * @brief   热插拔支持公共接口
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details 热插拔支持公共接口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef HOTPLUG_H
#define HOTPLUG_H

#include "hotplug_types.h"

/* ========================================================================
 * 热插拔管理接口
 * ======================================================================== */

/**
 * @brief 初始化热插拔管理器
 *
 * @return 0 成功，<0 失败
 */
int32_t hotplug_init(void);

/**
 * @brief 启动热插拔监控
 *
 * @return 0 成功，<0 失败
 */
int32_t hotplug_start(void);

/**
 * @brief 停止热插拔监控
 *
 * @return 0 成功，<0 失败
 */
int32_t hotplug_stop(void);

/* ========================================================================
 * 设备管理接口
 * ======================================================================== */

/**
 * @brief 添加设备
 *
 * @param device_path 设备路径
 * @param type 设备类型
 * @param size_in_sectors 设备大小（扇区数）
 *
 * @return 0 成功，<0 失败
 */
int32_t hotplug_add_device(const char *device_path,
                              hotplug_dev_type_t type,
                              uint64_t size_in_sectors);

/**
 * @brief 移除设备
 *
 * @param device_path 设备路径
 *
 * @return 0 成功，<0 失败
 */
int32_t hotplug_remove_device(const char *device_path);

/**
 * @brief 查找设备
 *
 * @param device_path 设备路径
 *
 * @return 设备描述符指针（成功），NULL（失败）
 */
hotplug_device_t *hotplug_find_device(const char *device_path);

/**
 * @brief 设置设备挂载点
 *
 * @param device_path 设备路径
 * @param mount_point 挂载点路径
 *
 * @return 0 成功，<0 失败
 */
int32_t hotplug_set_mount_point(const char *device_path,
                                  const char *mount_point);

/* ========================================================================
 * 回调管理接口
 * ======================================================================== */

/**
 * @brief 注册热插拔回调
 *
 * @param callback 回调函数
 * @param user_data 用户数据
 *
 * @return 回调 ID（>=0 成功），<0 失败
 */
int32_t hotplug_register_callback(hotplug_callback_t callback,
                                    void *user_data);

/**
 * @brief 注销热插拔回调
 *
 * @param callback_id 回调 ID
 *
 * @return 0 成功，<0 失败
 */
int32_t hotplug_unregister_callback(uint32_t callback_id);

/* ========================================================================
 * 事件处理接口
 * ======================================================================== */

/**
 * @brief 处理热插拔事件
 *
 * @param event 热插拔事件
 *
 * @return 0 成功，<0 失败
 */
int32_t hotplug_handle_event(const hotplug_event_t *event);

/**
 * @brief 触发设备插入事件
 *
 * @param device_path 设备路径
 * @param type 设备类型
 * @param size_in_sectors 设备大小（扇区数）
 *
 * @return 0 成功，<0 失败
 */
int32_t hotplug_trigger_insert(const char *device_path,
                                  hotplug_dev_type_t type,
                                  uint64_t size_in_sectors);

/**
 * @brief 触发设备移除事件
 *
 * @param device_path 设备路径
 *
 * @return 0 成功，<0 失败
 */
int32_t hotplug_trigger_remove(const char *device_path);

#endif /* HOTPLUG_H */
