/**
 * @file    fs_ipc_types.h
 * @brief   FS 服务 IPC 消息类型定义
 * @author  AISafe64 Team
 * @date    2026-04-30
 * @version 2.0
 *
 * @details FS 服务与客户端之间的 IPC 消息类型定义
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_IPC_TYPES_H
#define FS_IPC_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * FS IPC 消息类型
 * ======================================================================== */

/** @brief 打开文件 */
#define FS_IPC_OPEN          1U

/** @brief 关闭文件 */
#define FS_IPC_CLOSE         2U

/** @brief 读取文件 */
#define FS_IPC_READ          3U

/** @brief 写入文件 */
#define FS_IPC_WRITE         4U

/** @brief 定位文件偏移 */
#define FS_IPC_LSEEK         5U

/** @brief 获取文件状态 */
#define FS_IPC_FSTAT         6U

/** @brief 文件控制操作 */
#define FS_IPC_IOCTL         7U

/** @brief 文件描述符控制操作 */
#define FS_IPC_FCNTL         8U

/** @brief 修改文件权限 */
#define FS_IPC_CHMOD         9U

/** @brief 修改文件所有者 */
#define FS_IPC_CHOWN         10U

/** @brief 文件锁操作 */
#define FS_IPC_FLOCK         11U

/** @brief 创建软链接 */
#define FS_IPC_SYMLINK       12U

/** @brief 创建硬链接 */
#define FS_IPC_LINK          13U

/** @brief 读取软链接 */
#define FS_IPC_READLINK      14U

/* ========================================================================
 * FS IPC 消息头
 * ======================================================================== */

/**
 * @brief FS IPC 消息头
 */
typedef struct
{
    uint32_t msg_type;      /**< @brief 消息类型 */
    uint32_t msg_id;        /**< @brief 消息 ID（用于匹配请求/响应） */
    uint32_t status;        /**< @brief 状态码（仅响应） */
    uint32_t reserved;      /**< @brief 保留 */
} fs_ipc_msg_header_t;

/* ========================================================================
 * FS IPC 请求/响应消息
 * ======================================================================== */

/**
 * @brief FS 打开文件请求
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    char path[256];          /**< @brief 文件路径 */
    uint32_t flags;          /**< @brief 打开标志 */
    uint32_t mode;           /**< @brief 文件权限 */
} fs_ipc_open_req_t;

/**
 * @brief FS 打开文件响应
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    int32_t result;          /**< @brief 结果（>=0 为 fd，<0 为错误码） */
} fs_ipc_open_resp_t;

/**
 * @brief FS 关闭文件请求
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    uint32_t fd;             /**< @brief 文件描述符 */
} fs_ipc_close_req_t;

/**
 * @brief FS 关闭文件响应
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    int32_t result;          /**< @brief 结果（0 成功，<0 错误码） */
} fs_ipc_close_resp_t;

/**
 * @brief FS 读取文件请求
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    uint32_t fd;             /**< @brief 文件描述符 */
    uint64_t offset;         /**< @brief 读取偏移 */
    uint64_t size;           /**< @brief 读取大小 */
} fs_ipc_read_req_t;

/**
 * @brief FS 读取文件响应
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    int64_t result;          /**< @brief 实际读取字节数（>=0），<0 为错误码 */
    uint8_t data[4096];      /**< @brief 数据缓冲区 */
} fs_ipc_read_resp_t;

/**
 * @brief FS 写入文件请求
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    uint32_t fd;             /**< @brief 文件描述符 */
    uint64_t offset;         /**< @brief 写入偏移 */
    uint64_t size;           /**< @brief 写入大小 */
    uint8_t data[4096];      /**< @brief 数据缓冲区 */
} fs_ipc_write_req_t;

/**
 * @brief FS 写入文件响应
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    int64_t result;          /**< @brief 实际写入字节数（>=0），<0 为错误码 */
} fs_ipc_write_resp_t;

/**
 * @brief FS 定位文件偏移请求
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    uint32_t fd;             /**< @brief 文件描述符 */
    int64_t offset;          /**< @brief 偏移量 */
    uint32_t whence;         /**< @brief 定位方式 */
} fs_ipc_lseek_req_t;

/**
 * @brief FS 定位文件偏移响应
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    int64_t result;          /**< @brief 新的文件偏移（>=0），<0 为错误码 */
} fs_ipc_lseek_resp_t;

/**
 * @brief FS 获取文件状态请求
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    uint32_t fd;             /**< @brief 文件描述符 */
} fs_ipc_fstat_req_t;

/**
 * @brief FS 获取文件状态响应
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    int32_t result;          /**< @brief 结果（0 成功，<0 错误码） */
    uint32_t st_dev;         /**< @brief 设备 ID */
    uint32_t st_ino;         /**< @brief inode 编号 */
    uint32_t st_mode;        /**< @brief 文件模式和权限 */
    uint32_t st_nlink;       /**< @brief 硬链接数 */
    uint32_t st_uid;         /**< @brief 用户 ID */
    uint32_t st_gid;         /**< @brief 组 ID */
    uint64_t st_size;        /**< @brief 文件大小 */
    uint64_t st_atime;       /**< @brief 访问时间 */
    uint64_t st_mtime;       /**< @brief 修改时间 */
    uint64_t st_ctime;       /**< @brief 创建时间 */
} fs_ipc_fstat_resp_t;

/**
 * @brief FS 文件锁请求
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    uint32_t fd;             /**< @brief 文件描述符 */
    uint32_t lock_type;      /**< @brief 锁类型（LOCK_SH/LOCK_EX/LOCK_UN） */
} fs_ipc_flock_req_t;

/**
 * @brief FS 文件锁响应
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    int32_t result;          /**< @brief 结果（0 成功，<0 错误码） */
} fs_ipc_flock_resp_t;

/**
 * @brief FS 创建软链接请求
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    char oldpath[256];       /**< @brief 旧路径（链接目标） */
    char newpath[256];       /**< @brief 新路径（链接名称） */
} fs_ipc_symlink_req_t;

/**
 * @brief FS 创建软链接响应
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    int32_t result;          /**< @brief 结果（0 成功，<0 错误码） */
} fs_ipc_symlink_resp_t;

/**
 * @brief FS 创建硬链接请求
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    char oldpath[256];       /**< @brief 旧路径（现有文件） */
    char newpath[256];       /**< @brief 新路径（链接名称） */
} fs_ipc_link_req_t;

/**
 * @brief FS 创建硬链接响应
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    int32_t result;          /**< @brief 结果（0 成功，<0 错误码） */
} fs_ipc_link_resp_t;

/**
 * @brief FS 读取软链接请求
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    char path[256];          /**< @brief 链接路径 */
} fs_ipc_readlink_req_t;

/**
 * @brief FS 读取软链接响应
 */
typedef struct
{
    fs_ipc_msg_header_t header;
    int32_t result;          /**< @brief 结果（0 成功，<0 错误码） */
    char target[256];        /**< @brief 链接目标路径 */
} fs_ipc_readlink_resp_t;

#endif /* FS_IPC_TYPES_H */
