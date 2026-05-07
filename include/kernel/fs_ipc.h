/**
 * @file    fs_ipc.h
 * @brief   FS 服务 IPC 接口
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FS 服务 IPC 客户端接口，通过 IPC 与 FS 服务器端通信：
 *          - fs_open/fs_close
 *          - fs_read/fs_write
 *          - fs_lseek
 *          - fs_fstat
 *          - fs_ioctl/fs_fcntl
 *          - fs_chmod/chown
 *
 * @note MISRA-C:2012 合规
 * @note 用户态服务架构
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_FS_IPC_H
#define KERNEL_FS_IPC_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * FS IPC 常量
 * ======================================================================== */

/** @brief SEEK_SET */
#define FS_SEEK_SET            0U

/** @brief SEEK_CUR */
#define FS_SEEK_CUR            1U

/** @brief SEEK_END */
#define FS_SEEK_END            2U

/* ========================================================================
 * FS 打开模式
 * ======================================================================== */

/** @brief 只读 */
#define FS_O_RDONLY            0x01U

/** @brief 只写 */
#define FS_O_WRONLY            0x02U

/** @brief 读写 */
#define FS_O_RDWR              0x03U

/** @brief 创建 */
#define FS_O_CREAT             0x10U

/** @brief 截断 */
#define FS_O_TRUNC             0x20U

/** @brief 追加 */
#define FS_O_APPEND            0x40U

/* ========================================================================
 * FS 文件权限
 * ======================================================================== */

/** @brief 目录文件类型 */
#define FS_S_IFDIR             0040000U

/** @brief 普通文件类型 */
#define FS_S_IFREG             0100000U

/** @brief 用户执行 */
#define FS_S_IXUSR             0000100U

/** @brief 用户写入 */
#define FS_S_IWUSR             0000200U

/** @brief 用户读取 */
#define FS_S_IRUSR             0000400U

/** @brief 组执行 */
#define FS_S_IXGRP             0000010U

/** @brief 组写入 */
#define FS_S_IWGRP             0000020U

/** @brief 组读取 */
#define FS_S_IRGRP             0000040U

/** @brief 其他执行 */
#define FS_S_IXOTH             0000001U

/** @brief 其他写入 */
#define FS_S_IWOTH             0000002U

/** @brief 其他读取 */
#define FS_S_IROTH             0000004U

/* ========================================================================
 * FS ioctl/fcntl 命令
 * ======================================================================== */

/** @brief FIONBIO: 设置非阻塞 I/O */
#define FS_FIONBIO             0x5421U

/** @brief FIONREAD: 获取可读字节数 */
#define FS_FIONREAD            0x541BU

/** @brief F_GETFL: 获取文件标志 */
#define FS_F_GETFL             3U

/** @brief F_SETFL: 设置文件标志 */
#define FS_F_SETFL             4U

/* ========================================================================
 * FS 文件锁类型
 * ======================================================================== */

/** @brief 共享锁（读锁） */
#define FS_LOCK_SH             1U

/** @brief 排他锁（写锁） */
#define FS_LOCK_EX             2U

/** @brief 解锁 */
#define FS_LOCK_UN             8U

/* ========================================================================
 * FS 文件状态结构体
 * ======================================================================== */

/**
 * @brief FS 文件状态
 */
typedef struct
{
    uint32_t        st_dev;      /**< @brief 设备 ID */
    uint32_t        st_ino;      /**< @brief inode 编号 */
    uint32_t        st_mode;     /**< @brief 文件模式和权限 */
    uint32_t        st_nlink;    /**< @brief 硬链接数 */
    uint32_t        st_uid;      /**< @brief 用户 ID */
    uint32_t        st_gid;      /**< @brief 组 ID */
    uint32_t        st_rdev;     /**< @brief 设备 ID（特殊文件） */
    uint32_t        st_size;     /**< @brief 文件大小（字节） */
    uint32_t        st_blksize;  /**< @brief 块大小 */
    uint32_t        st_blocks;   /**< @brief 块数 */
    uint64_t        st_atime;    /**< @brief 访问时间 */
    uint64_t        st_mtime;    /**< @brief 修改时间 */
    uint64_t        st_ctime;    /**< @brief 创建时间 */
} fs_stat_t;

/* ========================================================================
 * FS IPC 接口函数声明
 * ======================================================================== */

/**
 * @brief 打开文件
 *
 * @param path  文件路径
 * @param flags 打开标志
 * @param mode  文件权限（创建时使用）
 *
 * @return 文件描述符（>=0 成功，<0 失败）
 */
int32_t fs_open(const char *path, uint32_t flags, uint32_t mode);

/**
 * @brief 关闭文件
 *
 * @param fd 文件描述符
 *
 * @return 0 成功，<0 失败
 */
int32_t fs_close(uint32_t fd);

/**
 * @brief 读取文件
 *
 * @param fd   文件描述符
 * @param buf  缓冲区
 * @param size 读取大小
 *
 * @return 实际读取字节数（>=0 成功，<0 失败）
 */
int64_t fs_read(uint32_t fd, void *buf, uint64_t size);

/**
 * @brief 写入文件
 *
 * @param fd   文件描述符
 * @param buf  缓冲区
 * @param size 写入大小
 *
 * @return 实际写入字节数（>=0 成功，<0 失败）
 */
int64_t fs_write(uint32_t fd, const void *buf, uint64_t size);

/**
 * @brief 定位文件偏移
 *
 * @param fd     文件描述符
 * @param offset 偏移量
 * @param whence 定位方式（SEEK_SET/SEEK_CUR/SEEK_END）
 *
 * @return 新的文件偏移（>=0 成功，<0 失败）
 */
int64_t fs_lseek(uint32_t fd, int64_t offset, uint32_t whence);

/**
 * @brief 获取文件状态
 *
 * @param fd   文件描述符
 * @param stat 文件状态
 *
 * @return 0 成功，<0 失败
 */
int32_t fs_fstat(uint32_t fd, fs_stat_t *stat);

/**
 * @brief 文件控制操作
 *
 * @param fd      文件描述符
 * @param request 请求命令
 * @param argp    参数指针
 *
 * @return 0 成功，<0 失败
 */
int32_t fs_ioctl(uint32_t fd, uint32_t request, void *argp);

/**
 * @brief 文件描述符控制操作
 *
 * @param fd  文件描述符
 * @param cmd 命令
 * @param arg 参数
 *
 * @return 0 成功，<0 失败
 */
int32_t fs_fcntl(uint32_t fd, uint32_t cmd, int32_t arg);

/**
 * @brief 修改文件权限
 *
 * @param path 文件路径
 * @param mode 新的权限
 *
 * @return 0 成功，<0 失败
 */
int32_t fs_chmod(const char *path, uint32_t mode);

/**
 * @brief 修改文件所有者
 *
 * @param path 文件路径
 * @param uid  新的用户 ID
 * @param gid  新的组 ID
 *
 * @return 0 成功，<0 失败
 */
int32_t fs_chown(const char *path, uint32_t uid, uint32_t gid);

/**
 * @brief 文件锁操作
 *
 * @param fd        文件描述符
 * @param lock_type 锁类型（FS_LOCK_SH/FS_LOCK_EX/FS_LOCK_UN）
 *
 * @return 0 成功，<0 失败
 */
int32_t fs_flock(uint32_t fd, uint32_t lock_type);

/**
 * @brief 创建符号链接
 *
 * @param target   目标路径
 * @param linkpath 链接路径
 *
 * @return 0 成功，<0 失败
 */
int32_t fs_symlink(const char *target, const char *linkpath);

/**
 * @brief 创建硬链接
 *
 * @param oldpath 原始路径
 * @param newpath 新路径
 *
 * @return 0 成功，<0 失败
 */
int32_t fs_link(const char *oldpath, const char *newpath);

/**
 * @brief 读取符号链接目标
 *
 * @param path     符号链接路径
 * @param buf      输出缓冲区
 * @param bufsize  缓冲区大小
 *
 * @return 实际读取字节数，<0 失败
 */
int64_t fs_readlink(const char *path, char *buf, uint64_t bufsize);

#endif /* KERNEL_FS_IPC_H */
