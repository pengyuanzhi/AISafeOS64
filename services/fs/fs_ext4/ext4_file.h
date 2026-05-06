/**
 * @file    ext4_file.h
 * @brief   Ext4 文件操作头文件
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 文件操作接口：
 *          - 打开文件
 *          - 关闭文件
 *          - 读取文件
 *          - 写入文件
 *          - 创建文件
 *          - 删除文件
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef EXT4_FILE_H
#define EXT4_FILE_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 文件描述符
 * ======================================================================== */

/** @brief 文件描述符数量 */
#define EXT4_MAX_FD_COUNT    64U

/** @brief 文件描述符状态 */
typedef enum
{
    EXT4_FD_UNUSED  = 0U,
    EXT4_FD_OPEN    = 1U
} ext4_fd_state_t;

/** @brief 文件描述符 */
typedef struct
{
    uint32_t    inode;        /**< @brief Inode 编号 */
    uint32_t    offset;       /**< @brief 当前偏移 */
    uint32_t    flags;        /**< @brief 文件标志 */
    ext4_fd_state_t state;    /**< @brief 状态 */
} ext4_fd_t;

/* ========================================================================
 * 文件操作标志
 * ======================================================================== */

/** @brief O_RDONLY - 只读 */
#define O_RDONLY            0U

/** @brief O_WRONLY - 只写 */
#define O_WRONLY            1U

/** @brief O_RDWR - 读写 */
#define O_RDWR              2U

/** @brief O_CREAT - 创建 */
#define O_CREAT             0x40U

/** @brief O_TRUNC - 截断 */
#define O_TRUNC             0x200U

/* ========================================================================
 * 文件操作接口
 * ======================================================================== */

/**
 * @brief 打开文件
 *
 * @param path      文件路径
 * @param flags     文件标志
 * @param mode      权限模式
 * @param uid       用户 ID
 * @param gid       组 ID
 *
 * @return 文件描述符（>=0 成功），<0 失败
 */
int32_t ext4_open(const char *path, uint32_t flags,
                   uint32_t mode, uint32_t uid, uint32_t gid);

/**
 * @brief 关闭文件
 *
 * @param fd        文件描述符
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_close(int32_t fd);

/**
 * @brief 读取文件
 *
 * @param fd        文件描述符
 * @param buf       输入缓冲区
 * @param count     读取字节数
 *
 * @return 实际读取字节数（>=0 成胜），<0 失败
 */
int32_t ext4_read(int32_t fd, void *buf, uint32_t count);

/**
 * @brief 写入文件
 *
 * @param fd        文件描述符
 * @param buf       输出缓冲区
 * @param count     写入字节数
 *
 * @return 实际写入字节数（>=0 成胜），<0 失败
 */
int32_t ext4_write(int32_t fd, const void *buf, uint32_t count);

/**
 * @brief 创建文件
 *
 * @param path      文件路径
 * @param mode      权限模式
 * @param uid       用户 ID
 * @param gid       组 ID
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_create(const char *path, uint32_t mode,
                     uint32_t uid, uint32_t gid);

/**
 * @brief 删除文件
 *
 * @param path      文件路径
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_unlink(const char *path);

/**
 * @brief 获取文件状态
 *
 * @param fd        文件描述符
 * @param size      输出文件大小
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_fstat(int32_t fd, uint32_t *size);

#endif /* EXT4_FILE_H */
