/**
 * @file    devfs.h
 * @brief   DEVFS 公共接口
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details DEVFS 公共接口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef DEVFS_H
#define DEVFS_H

#include "devfs_types.h"
#include "fs_ops.h"

/**
 * @brief 初始化 DEVFS 实例
 *
 * @param inst DEVFS 实例
 *
 * @return 0 成功，<0 失败
 */
int32_t devfs_instance_init(devfs_instance_t *inst);

/**
 * @brief 清理 DEVFS 实例
 *
 * @param inst DEVFS 实例
 *
 * @return 0 成功，<0 失败
 */
int32_t devfs_instance_cleanup(devfs_instance_t *inst);

/**
 * @brief 注册设备
 *
 * @param inst DEVFS 实例
 * @param name 设备名称
 * @param type 设备类型
 * @param major 主设备号
 * @param minor 次设备号
 * @param ops 设备操作接口
 * @param private_data 设备私有数据
 *
 * @return 0 成功，<0 失败
 */
int32_t devfs_register_device(devfs_instance_t *inst,
                               const char *name,
                               devfs_device_type_t type,
                               uint32_t major,
                               uint32_t minor,
                               const devfs_ops_t *ops,
                               void *private_data);

/**
 * @brief 注销设备
 *
 * @param inst DEVFS 实例
 * @param name 设备名称
 *
 * @return 0 成功，<0 失败
 */
int32_t devfs_unregister_device(devfs_instance_t *inst, const char *name);

/**
 * @brief 查找设备
 *
 * @param inst DEVFS 实例
 * @param name 设备名称
 *
 * @return 设备指针（成功），NULL（失败）
 */
devfs_device_t *devfs_find_device(devfs_instance_t *inst, const char *name);

/**
 * @brief 获取 DEVFS 操作接口
 *
 * @return DEVFS 操作接口指针
 */
const fs_ops_t *devfs_get_ops(void);

#endif /* DEVFS_H */
