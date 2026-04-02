/**
 * @file    vfs.h
 * @brief   虚拟文件系统（VFS）接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 虚拟文件系统开关层：
 *          - 统一文件/设备/命名空间的访问接口
 *          - 支持多文件系统挂载（FAT32、ROMFS 等）
 *          - VFS inode 节点管理
 *          - 文件描述符表管理
 *          - 目录操作支持
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: FS-001~005
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * VFS 常量
 * ======================================================================== */

/** @brief 最大挂载点数量 */
#define VFS_MAX_MOUNTS                  8U

/** @brief 每进程最大打开文件数 */
#define VFS_MAX_FDS                     32U

/** @brief 最大路径长度 */
#define VFS_PATH_MAX                    256U

/** @brief 最大文件名长度 */
#define VFS_NAME_MAX                    64U

/** @brief 最大文件系统类型名称 */
#define VFS_FSTYPE_MAX                  16U

/* ========================================================================
 * VFS 数据类型
 * ======================================================================== */

/**
 * @brief 文件类型枚举
 */
typedef enum
{
    VFS_TYPE_REGULAR = 0U,    /**< @brief 普通文件 */
    VFS_TYPE_DIRECTORY,       /**< @brief 目录 */
    VFS_TYPE_DEVICE,          /**< @brief 设备文件 */
    VFS_TYPE_SYMLINK,         /**< @brief 符号链接 */
    VFS_TYPE_PIPE             /**< @brief 管道 */
} vfs_file_type_t;

/**
 * @brief 文件打开模式
 */
typedef enum
{
    VFS_O_RDONLY = 0x01U,     /**< @brief 只读 */
    VFS_O_WRONLY = 0x02U,     /**< @brief 只写 */
    VFS_O_RDWR   = 0x03U,     /**< @brief 读写 */
    VFS_O_CREAT  = 0x10U,     /**< @brief 创建 */
    VFS_O_TRUNC  = 0x20U,     /**< @brief 截断 */
    VFS_O_APPEND = 0x40U      /**< @brief 追加 */
} vfs_open_flag_t;

/**
 * @brief 文件权限位
 */
typedef enum
{
    VFS_S_IXUSR = 0x01U,     /**< @brief 用户执行 */
    VFS_S_IWUSR = 0x02U,     /**< @brief 用户写入 */
    VFS_S_IRUSR = 0x04U,     /**< @brief 用户读取 */
    VFS_S_IXGRP = 0x08U,     /**< @brief 组执行 */
    VFS_S_IWGRP = 0x10U,     /**< @brief 组写入 */
    VFS_S_IRGRP = 0x20U,     /**< @brief 组读取 */
    VFS_S_IXOTH = 0x40U,     /**< @brief 其他执行 */
    VFS_S_IWOTH = 0x80U,     /**< @brief 其他写入 */
    VFS_S_IROTH = 0x100U     /**< @brief 其他读取 */
} vfs_perm_t;

/**
 * @brief 文件系统类型
 */
typedef enum
{
    VFS_FS_RAMFS = 0U,       /**< @brief 内存文件系统 */
    VFS_FS_ROMFS,            /**< @brief 只读文件系统 */
    VFS_FS_FAT32,            /**< @brief FAT32 文件系统 */
    VFS_FS_DEVFS             /**< @brief 设备文件系统 */
} vfs_fstype_t;

/* ========================================================================
 * VFS inode 描述符
 * ======================================================================== */

/**
 * @brief VFS inode 节点
 *
 * @details 统一表示文件、目录、设备等
 */
typedef struct
{
    uint32_t        ino;            /**< @brief inode 编号 */
    vfs_file_type_t type;           /**< @brief 文件类型 */
    uint32_t        mode;           /**< @brief 权限模式 */
    uint64_t        size;           /**< @brief 文件大小（字节） */
    uint64_t        atime;          /**< @brief 访问时间 */
    uint64_t        mtime;          /**< @brief 修改时间 */
    uint64_t        ctime;          /**< @brief 创建时间 */
    uint32_t        nlinks;         /**< @brief 硬链接数 */
    uint32_t        ref_count;      /**< @brief 引用计数 */
    uint32_t        mount_id;       /**< @brief 所属挂载 ID */
    uint32_t        dev_id;         /**< @brief 设备 ID（设备文件） */
    bool            dirty;          /**< @brief 脏标记 */
} vfs_inode_t;

/* ========================================================================
 * VFS 文件描述符
 * ======================================================================== */

/**
 * @brief VFS 打开文件描述符
 */
typedef struct
{
    uint32_t        fd;             /**< @brief 文件描述符号 */
    uint32_t        ino;            /**< @brief 关联 inode 编号 */
    uint32_t        flags;          /**< @brief 打开标志 */
    uint64_t        offset;         /**< @brief 当前偏移量 */
    uint32_t        mount_id;       /**< @brief 所属挂载 ID */
    bool            in_use;         /**< @brief 使用标记 */
} vfs_fd_t;

/* ========================================================================
 * VFS 挂载点描述符
 * ======================================================================== */

/**
 * @brief VFS 挂载点
 */
typedef struct
{
    uint32_t        mount_id;       /**< @brief 挂载 ID */
    char            path[VFS_PATH_MAX]; /**< @brief 挂载路径 */
    vfs_fstype_t    fstype;         /**< @brief 文件系统类型 */
    uint32_t        flags;          /**< @brief 挂载标志 */
    uint32_t        root_ino;       /**< @brief 根 inode */
    uint32_t        block_size;     /**< @brief 块大小 */
    uint64_t        total_blocks;   /**< @brief 总块数 */
    uint64_t        free_blocks;    /**< @brief 空闲块数 */
    bool            mounted;        /**< @brief 已挂载标记 */
} vfs_mount_t;

/* ========================================================================
 * VFS 文件系统操作
 * ======================================================================== */

/**
 * @brief 文件系统操作接口
 *
 * @details 每种文件系统需实现此操作表
 */
typedef struct
{
    /** @brief 挂载文件系统 */
    int32_t (*mount)(vfs_mount_t *mnt, const char *device);

    /** @brief 卸载文件系统 */
    int32_t (*unmount)(vfs_mount_t *mnt);

    /** @brief 查找 inode */
    int32_t (*lookup)(uint32_t mount_id, const char *name, vfs_inode_t *out);

    /** @brief 创建文件 */
    int32_t (*create)(uint32_t mount_id, const char *name, uint32_t mode, vfs_inode_t *out);

    /** @brief 删除文件 */
    int32_t (*unlink)(uint32_t mount_id, const char *name);

    /** @brief 创建目录 */
    int32_t (*mkdir)(uint32_t mount_id, const char *name, uint32_t mode);

    /** @brief 读取目录项 */
    int32_t (*readdir)(uint32_t mount_id, uint32_t ino, uint32_t *offset,
                        char *name_out, vfs_inode_t *entry_out);

    /** @brief 读取文件数据 */
    int64_t (*read)(uint32_t mount_id, uint32_t ino, uint64_t offset,
                     void *buf, uint64_t size);

    /** @brief 写入文件数据 */
    int64_t (*write)(uint32_t mount_id, uint32_t ino, uint64_t offset,
                      const void *buf, uint64_t size);

    /** @brief 截断文件 */
    int32_t (*truncate)(uint32_t mount_id, uint32_t ino, uint64_t size);

    /** @brief 同步到存储 */
    int32_t (*sync)(uint32_t mount_id);
} vfs_ops_t;

/* ========================================================================
 * VFS API
 * ======================================================================== */

/**
 * @brief 初始化 VFS 子系统
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: FS-001
 */
kernel_status_t vfs_init(void);

/**
 * @brief 注册文件系统类型
 *
 * @param fstype 文件系统类型
 * @param ops    操作接口
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vfs_register_fs(vfs_fstype_t fstype, const vfs_ops_t *ops);

/**
 * @brief 挂载文件系统
 *
 * @param path   挂载路径
 * @param fstype 文件系统类型
 * @param device 块设备路径（可为 NULL）
 * @param flags  挂载标志
 *
 * @return 成功返回挂载 ID，失败返回负错误码
 *
 * @note 对应需求: FS-002
 */
int32_t vfs_mount(const char *path, vfs_fstype_t fstype,
                   const char *device, uint32_t flags);

/**
 * @brief 卸载文件系统
 *
 * @param mount_id 挂载 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vfs_unmount(uint32_t mount_id);

/**
 * @brief 打开文件
 *
 * @param path  文件路径
 * @param flags 打开标志
 * @param mode  创建权限（仅 O_CREAT 时使用）
 *
 * @return 成功返回文件描述符，失败返回负错误码
 *
 * @note 对应需求: FS-003
 */
int32_t vfs_open(const char *path, uint32_t flags, uint32_t mode);

/**
 * @brief 关闭文件
 *
 * @param fd 文件描述符
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vfs_close(uint32_t fd);

/**
 * @brief 读取文件
 *
 * @param fd  文件描述符
 * @param buf 缓冲区
 * @param size 大小
 *
 * @return 实际读取字节数，负数表示错误
 *
 * @note 对应需求: FS-003
 */
int64_t vfs_read(uint32_t fd, void *buf, uint64_t size);

/**
 * @brief 写入文件
 *
 * @param fd  文件描述符
 * @param buf 数据
 * @param size 大小
 *
 * @return 实际写入字节数，负数表示错误
 */
int64_t vfs_write(uint32_t fd, const void *buf, uint64_t size);

/**
 * @brief 设置文件偏移量
 *
 * @param fd     文件描述符
 * @param offset 偏移量
 * @param whence 定位方式（0=起始，1=当前，2=末尾）
 *
 * @return 新的偏移量，负数表示错误
 */
int64_t vfs_seek(uint32_t fd, int64_t offset, uint32_t whence);

/**
 * @brief 获取文件状态
 *
 * @param path 路径
 * @param[out] stat 输出 inode 信息
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vfs_stat(const char *path, vfs_inode_t *stat);

/**
 * @brief 创建目录
 *
 * @param path 目录路径
 * @param mode 权限
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: FS-004
 */
kernel_status_t vfs_mkdir(const char *path, uint32_t mode);

/**
 * @brief 删除文件
 *
 * @param path 文件路径
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vfs_unlink(const char *path);

/**
 * @brief 同步文件系统
 *
 * @param mount_id 挂载 ID（-1 表示全部）
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: FS-005
 */
kernel_status_t vfs_sync(int32_t mount_id);

#endif /* KERNEL_VFS_H */
