/**
 * @file    fsck.h
 * @brief   fsck 工具公共接口
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details fsck 工具公共接口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FSCK_H
#define FSCK_H

#include "fsck_types.h"

/* ========================================================================
 * FAT32 fsck 接口
 * ======================================================================== */

/**
 * @brief FAT32 fsck 检查
 *
 * @param device_path 设备路径
 * @param options 检查选项
 * @param result 输出检查结果
 *
 * @return 0 成功，<0 失败
 */
int32_t fsck_fat32_check(const char *device_path,
                          const fsck_options_t *options,
                          fsck_result_t *result);

/**
 * @brief FAT32 fsck 修复
 *
 * @param device_path 设备路径
 * @param options 检查选项
 * @param result 输出检查结果
 *
 * @return 0 成功，<0 失败
 */
int32_t fsck_fat32_repair(const char *device_path,
                           const fsck_options_t *options,
                           fsck_result_t *result);

/* ========================================================================
 * EXT4 fsck 接口
 * ======================================================================== */

/**
 * @brief EXT4 fsck 检查
 *
 * @param device_path 设备路径
 * @param options 检查选项
 * @param result 输出检查结果
 *
 * @return 0 成功，<0 失败
 */
int32_t fsck_ext4_check(const char *device_path,
                         const fsck_options_t *options,
                         fsck_result_t *result);

/**
 * @brief EXT4 fsck 修复
 *
 * @param device_path 设备路径
 * @param options 检查选项
 * @param result 输出检查结果
 *
 * @return 0 成功，<0 失败
 */
int32_t fsck_ext4_repair(const char *device_path,
                          const fsck_options_t *options,
                          fsck_result_t *result);

/* ========================================================================
 * 通用 fsck 接口
 * ======================================================================== */

/**
 * @brief 通用 fsck 检查（自动检测文件系统类型）
 *
 * @param device_path 设备路径
 * @param options 检查选项
 * @param result 输出检查结果
 *
 * @return 0 成功，<0 失败
 */
int32_t fsck_check(const char *device_path,
                    const fsck_options_t *options,
                    fsck_result_t *result);

/**
 * @brief 通用 fsck 修复（自动检测文件系统类型）
 *
 * @param device_path 设备路径
 * @param options 检查选项
 * @param result 输出检查结果
 *
 * @return 0 成功，<0 失败
 */
int32_t fsck_repair(const char *device_path,
                     const fsck_options_t *options,
                     fsck_result_t *result);

#endif /* FSCK_H */
