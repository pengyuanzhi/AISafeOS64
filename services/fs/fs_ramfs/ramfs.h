/**
 * @file    ramfs.h
 * @brief   RAMFS 内存文件系统头文件
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 2.0
 *
 * @details RAMFS 内存文件系统接口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_RAMFS_H
#define FS_RAMFS_H

#include "fs_ops.h"

/**
 * @brief 获取 RAMFS 操作接口
 *
 * @return RAMFS 操作接口指针
 */
const fs_ops_t *ramfs_get_ops(void);

/**
 * @brief RAMFS 创建软链接
 *
 * @param mount_id 挂载点 ID
 * @param oldpath 旧路径（链接目标）
 * @param newpath 新路径（链接名称）
 *
 * @return 0 成功，<0 失败
 */
int32_t ramfs_do_symlink(uint32_t mount_id, const char *oldpath,
                          const char *newpath);

/**
 * @brief RAMFS 创建硬链接
 *
 * @param mount_id 挂载点 ID
 * @param oldpath 旧路径（现有文件）
 * @param newpath 新路径（链接名称）
 *
 * @return 0 成功，<0 失败
 */
int32_t ramfs_do_link(uint32_t mount_id, const char *oldpath,
                       const char *newpath);

/**
 * @brief RAMFS 读取软链接
 *
 * @param mount_id 挂载点 ID
 * @param path 链接路径
 * @param buf 输出缓冲区
 * @param bufsize 缓冲区大小
 *
 * @return 0 成功，<0 失败
 */
int32_t ramfs_do_readlink(uint32_t mount_id, const char *path,
                           char *buf, uint64_t bufsize);

#endif /* FS_RAMFS_H */
