/**
 * @file    main.c
 * @brief   FS 服务主程序（服务器端）
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 2.0
 *
 * @details FS 服务器端实现：
 *          - IPC 消息处理
 *          - 文件系统操作分发
 *          - RAMFS / ROMFS / FAT32 / EXT4 / DEVFS 注册
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fs_ops.h"
#include "fs_server_ipc.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ========================================================================
 * 文件系统外部声明
 * ======================================================================== */

extern const fs_ops_t *ramfs_get_ops(void);
extern const fs_ops_t *romfs_get_ops(void);
extern const fs_ops_t *fat32_get_ops(void);
extern const fs_ops_t *ext4_get_ops(void);
extern const fs_ops_t *devfs_get_ops(void);

/* ========================================================================
 * 初始化
 * ======================================================================== */

/**
 * @brief 初始化 FS 服务
 */
static int32_t fs_service_init(void)
{
    int32_t ret;

    /* 注册 RAMFS */
    ret = fs_register_fs(FS_FSTYPE_RAMFS, ramfs_get_ops());
    if (ret != 0)
    {
        return -1;
    }

    /* 注册 ROMFS */
    ret = fs_register_fs(FS_FSTYPE_ROMFS, romfs_get_ops());
    if (ret != 0)
    {
        return -2;
    }

    /* 注册 FAT32 */
    ret = fs_register_fs(FS_FSTYPE_FAT32, fat32_get_ops());
    if (ret != 0)
    {
        return -3;
    }

    /* 注册 EXT4 */
    /* TODO: ext4 头文件冲突待修复，暂时跳过注册 */
    (void)ext4_get_ops;

    /* 注册 DEVFS */
    ret = fs_register_fs(FS_FSTYPE_DEVFS, devfs_get_ops());
    if (ret != 0)
    {
        return -5;
    }

    /* 挂载 RAMFS 到 /ram */
    ret = fs_mount("/ram", FS_FSTYPE_RAMFS, NULL, 0U);
    if (ret < 0)
    {
        return -5;
    }

    /* 挂载 ROMFS 到 /rom */
    ret = fs_mount("/rom", FS_FSTYPE_ROMFS, NULL, 0U);
    if (ret < 0)
    {
        return -6;
    }

    /* 挂载 DEVFS 到 /dev */
    ret = fs_mount("/dev", FS_FSTYPE_DEVFS, NULL, 0U);
    if (ret < 0)
    {
        return -7;
    }

    return 0;
}

/* ========================================================================
 * 服务主函数
 * ======================================================================== */

int main(void)
{
    int32_t ret;

    /* 初始化 FS 服务 */
    ret = fs_service_init();
    if (ret != 0)
    {
        return -1;
    }

    /* 启动 FS 服务器 IPC 处理 */
    fs_server_main();

    return 0;
}
