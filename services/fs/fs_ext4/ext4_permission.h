/**
 * @file    ext4_permission.h
 * @brief   Ext4 权限管理头文件
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 权限管理接口：
 *          - chmod: 修改权限
 *          - chown: 修改所有者
 *          - 权限检查
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef EXT4_PERMISSION_H
#define EXT4_PERMISSION_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * Ext4 权限接口
 * ======================================================================== */

/**
 * @brief 修改文件权限
 *
 * @param ino  Inode 编号
 * @param mode 新权限模式
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_chmod(uint32_t ino, uint32_t mode);

/**
 * @brief 修改文件所有者
 *
 * @param ino  Inode 编号
 * @param uid  新用户 ID
 * @param gid  新组 ID
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_chown(uint32_t ino, uint32_t uid, uint32_t gid);

/**
 * @brief 检查访问权限
 *
 * @param ino  Inode 编号
 * @param uid  用户 ID
 * @param gid  组 ID
 * @param mode 访问模式（读/写/执行）
 *
 * @return true 允许，false 拒绝
 */
bool ext4_check_permission(uint32_t ino, uint32_t uid,
                            uint32_t gid, uint32_t mode);

#endif /* EXT4_PERMISSION_H */
