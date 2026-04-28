/**
 * @file    fs_ops.h
 * @brief   文件系统抽象层接口
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details 文件系统抽象层接口，支持多种文件系统：
 *          - RAMFS: 内存文件系统
 *          - ROMFS: 只读文件系统
 *          - FAT32: FAT32 文件系统
 *          - DEVFS: 设备文件系统
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_FS_OPS_H
#define KERNEL_FS_OPS_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * FS inode 描述符
 * ======================================================================== */

/**
 * @brief 文件类型枚举
 */
typedef enum
{
    FS_TYPE_REGULAR = 0U,    /**< @brief 普通文件 */
    FS_TYPE_DIRECTORY,       /**< @brief 目录 */
    FS_TYPE_DEVICE,          /**< @brief 设备文件 */
    FS_TYPE_SYMLINK,         /**< @brief 符号链接 */
    FS_TYPE_PIPE             /**< @brief 管道 */
} fs_file_type_t;

/**
 * @brief 文件系统类型
 */
typedef enum
{
    FS_FSTYPE_RAMFS = 0U,       /**< @brief 内存文件系统 */
    FS_FSTYPE_ROMFS,            /**< @brief 只读文件系统 */
    FS_FSTYPE_FAT32,            /**< @brief FAT32 文件系统 */
    FS_FSTYPE_DEVFS             /**< @brief 设备文件系统 */
} fs_fstype_t;

/**
 * @brief FS inode 节点
 */
typedef struct
{
    uint32_t        ino;            /**< @brief inode 编号 */
    fs_file_type_t  type;           /**< @brief 文件类型 */
    uint32_t        mode;           /**< @brief 权限模式 */
    uint64_t        size;           /**< @brief 文件大小（字节） */
    uint64_t        atime;          /**< @brief 访问时间 */
    uint64_t        mtime;          /**< @brief 修改时间 */
    uint64_t        ctime;          /**< @brief 创建时间 */
    uint32_t        nlinks;         /**< @brief 硬链接数 */
    uint32_t        uid;            /**< @brief 用户 ID */
    uint32_t        gid;            /**< @brief 组 ID */
    bool            dirty;          /**< @brief 脏标记 */
} fs_inode_t;

/* ========================================================================
 * FS 挂载点描述符
 * ======================================================================== */

/**
 * @brief FS 挂载点描述符
 */
typedef struct
{
    uint32_t        mount_id;       /**< @brief 挂载点 ID */
    fs_fstype_t     fstype;         /**< @brief 文件系统类型 */
    uint32_t        flags;          /**< @brief 挂载标志 */
    char            path[256];      /**< @brief 挂载路径 */
    void           *private_data;   /**< @brief 文件系统私有数据 */
} fs_mount_t;

/* ========================================================================
 * 文件系统操作接口
 * ======================================================================== */

/**
 * @brief 文件系统操作接口
 */
typedef struct fs_ops
{
    /**
     * @brief 挂载文件系统
     *
     * @param mnt    挂载点描述符
     * @param device 设备路径（块设备）
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*mount)(fs_mount_t *mnt, const char *device);

    /**
     * @brief 卸载文件系统
     *
     * @param mnt 挂载点描述符
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*unmount)(fs_mount_t *mnt);

    /**
     * @brief 查找文件
     *
     * @param mount_id 挂载点 ID
     * @param path     文件路径
     * @param inode    输出 inode
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*lookup)(uint32_t mount_id, const char *path, fs_inode_t *inode);

    /**
     * @brief 创建文件
     *
     * @param mount_id 挂载点 ID
     * @param path     文件路径
     * @param mode     文件权限
     * @param inode    输出 inode
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*create)(uint32_t mount_id, const char *path, uint32_t mode,
                       fs_inode_t *inode);

    /**
     * @brief 读取文件
     *
     * @param mount_id 挂载点 ID
     * @param ino      inode 编号
     * @param offset   读取偏移
     * @param buf      缓冲区
     * @param size     读取大小
     *
     * @return 实际读取字节数，<0 失败
     */
    int64_t (*read)(uint32_t mount_id, uint32_t ino, uint64_t offset,
                     void *buf, uint64_t size);

    /**
     * @brief 写入文件
     *
     * @param mount_id 挂载点 ID
     * @param ino      inode 编号
     * @param offset   写入偏移
     * @param buf      缓冲区
     * @param size     写入大小
     *
     * @return 实际写入字节数，<0 失败
     */
    int64_t (*write)(uint32_t mount_id, uint32_t ino, uint64_t offset,
                      const void *buf, uint64_t size);

    /**
     * @brief 创建目录
     *
     * @param mount_id 挂载点 ID
     * @param path     目录路径
     * @param mode     目录权限
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*mkdir)(uint32_t mount_id, const char *path, uint32_t mode);

    /**
     * @brief 删除文件
     *
     * @param mount_id 挂载点 ID
     * @param path     文件路径
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*unlink)(uint32_t mount_id, const char *path);

    /**
     * @brief 同步文件系统
     *
     * @param mount_id 挂载点 ID
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*sync)(uint32_t mount_id);

} fs_ops_t;

/* ========================================================================
 * 文件系统管理接口
 * ======================================================================== */

/**
 * @brief 注册文件系统
 *
 * @param fstype 文件系统类型
 * @param ops    文件系统操作接口
 *
 * @return 0 成功，<0 失败
 */
int32_t fs_register_fs(fs_fstype_t fstype, const fs_ops_t *ops);

/**
 * @brief 挂载文件系统
 *
 * @param path   挂载路径
 * @param fstype 文件系统类型
 * @param device 设备路径（块设备）
 * @param flags  挂载标志
 *
 * @return 挂载点 ID（>=0 成功），<0 失败
 */
int32_t fs_mount(const char *path, fs_fstype_t fstype,
                  const char *device, uint32_t flags);

/**
 * @brief 卸载文件系统
 *
 * @param mount_id 挂载点 ID
 *
 * @return 0 成功，<0 失败
 */
int32_t fs_unmount(uint32_t mount_id);

#endif /* KERNEL_FS_OPS_H */
