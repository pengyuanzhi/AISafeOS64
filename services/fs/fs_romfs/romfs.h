/**
 * @file    romfs.h
 * @brief   ROMFS 只读文件系统头文件
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 2.0
 *
 * @details ROMFS 只读文件系统接口：
 *          - 只读文件系统（适合 ROM/Flash）
 *          - 超级块 + inode 表 + 文件数据
 *          - 支持文件和目录遍历
 *          - 支持读取软链接
 *
 * @note MISRA-C:2012 合规
 * @note 对应商业化计划：P0 - fs 服务 ROMFS 后端
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_ROMFS_H
#define FS_ROMFS_H

#include "fs_ops.h"

/**
 * @brief 获取 ROMFS 操作接口
 *
 * @return ROMFS 操作接口指针
 */
const fs_ops_t *romfs_get_ops(void);

/**
 * @brief ROMFS 读取软链接
 *
 * @param mount_id 挂载点 ID
 * @param path 链接路径
 * @param buf 输出缓冲区
 * @param bufsize 缓冲区大小
 *
 * @return 0 成功，<0 失败
 */
int32_t romfs_do_readlink(uint32_t mount_id, const char *path,
                           char *buf, uint64_t bufsize);

#endif /* FS_ROMFS_H */
