/**
 * @file    fcntl.h
 * @brief   POSIX 文件控制接口
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 提供文件控制操作：
 *          - open() 标志位定义（O_RDONLY, O_WRONLY, O_RDWR 等）
 *          - 文件权限位（S_IRUSR 等）
 *          - fcntl() 命令和标志
 *          - open()/openat()/creat()/fcntl() 函数声明
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef AISAFE_FCNTL_H
#define AISAFE_FCNTL_H

#include <sys/types.h>

/* ========================================================================
 * open() 标志位定义
 * ======================================================================== */

#define O_RDONLY    0x0000U   /**< @brief 只读打开 */
#define O_WRONLY    0x0001U   /**< @brief 只写打开 */
#define O_RDWR      0x0002U   /**< @brief 读写打开 */
#define O_CREAT     0x0040U   /**< @brief 若不存在则创建 */
#define O_EXCL      0x0080U   /**< @brief 与 O_CREAT 配合使用，文件须不存在 */
#define O_TRUNC     0x0200U   /**< @brief 截断为 0 长度 */
#define O_APPEND    0x0400U   /**< @brief 追加写入 */
#define O_NONBLOCK  0x0800U   /**< @brief 非阻塞模式 */

/* ========================================================================
 * 文件权限位
 * ======================================================================== */

#define S_IRUSR  0400   /**< @brief 用户读 */
#define S_IWUSR  0200   /**< @brief 用户写 */
#define S_IXUSR  0100   /**< @brief 用户执行 */
#define S_IRGRP  0040   /**< @brief 组读 */
#define S_IWGRP  0020   /**< @brief 组写 */
#define S_IXGRP  0010   /**< @brief 组执行 */
#define S_IROTH  0004   /**< @brief 其他读 */
#define S_IWOTH  0002   /**< @brief 其他写 */
#define S_IXOTH  0001   /**< @brief 其他执行 */

/* ========================================================================
 * fcntl 命令
 * ======================================================================== */

#define F_DUPFD     0    /**< @brief 复制文件描述符 */
#define F_GETFD     1    /**< @brief 获取文件描述符标志 */
#define F_SETFD     2    /**< @brief 设置文件描述符标志 */
#define F_GETFL     3    /**< @brief 获取文件状态标志 */
#define F_SETFL     4    /**< @brief 设置文件状态标志 */

/* ========================================================================
 * 文件描述符标志
 * ======================================================================== */

#define FD_CLOEXEC  1    /**< @brief exec 时关闭 */

/* ========================================================================
 * 函数声明
 * ======================================================================== */

/**
 * @brief 打开文件
 *
 * @param pathname 文件路径
 * @param flags    打开标志（O_RDONLY, O_WRONLY, O_RDWR 等组合）
 * @param ...      可选参数：文件权限 mode_t（当 flags 包含 O_CREAT 时）
 *
 * @return 成功返回文件描述符，失败返回 -1 并设置 errno
 */
int open(const char *pathname, int flags, ...);

/**
 * @brief 在指定目录下打开文件
 *
 * @param dirfd    目录文件描述符（AT_FDCWD 使用当前目录）
 * @param pathname 文件路径
 * @param flags    打开标志
 * @param ...      可选参数：文件权限 mode_t
 *
 * @return 成功返回文件描述符，失败返回 -1 并设置 errno
 */
int openat(int dirfd, const char *pathname, int flags, ...);

/**
 * @brief 创建文件
 *
 * @param pathname 文件路径
 * @param mode     文件权限
 *
 * @return 成功返回文件描述符，失败返回 -1 并设置 errno
 */
int creat(const char *pathname, mode_t mode);

/**
 * @brief 文件控制操作
 *
 * @param fd   文件描述符
 * @param cmd  操作命令（F_DUPFD, F_GETFD 等）
 * @param ...  可选参数（取决于 cmd）
 *
 * @return 成功返回值取决于 cmd，失败返回 -1 并设置 errno
 */
int fcntl(int fd, int cmd, ...);

#endif /* AISAFE_FCNTL_H */
