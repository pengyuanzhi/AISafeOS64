/**
 * @file    sys/types.h
 * @brief   POSIX 基本系统数据类型
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 提供 POSIX 标准的基本数据类型定义：
 *          - size_t / ssize_t
 *          - pid_t / uid_t / gid_t
 *          - NULL 空指针常量
 *
 *          使用 __SIZE_TYPE__ 和 __PTRDIFF_TYPE__ 避免与编译器内置定义冲突
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef AISAFE_SYS_TYPES_H
#define AISAFE_SYS_TYPES_H

/* ========================================================================
 * size_t 定义（使用编译器内置类型避免冲突）
 * ======================================================================== */

#if defined(__SIZE_TYPE__)
typedef __SIZE_TYPE__        size_t;
#elif defined(__x86_64__) || defined(__aarch64__)
typedef unsigned long        size_t;
#else
typedef unsigned int         size_t;
#endif

#if defined(__PTRDIFF_TYPE__)
typedef __PTRDIFF_TYPE__     ssize_t;
#elif defined(__x86_64__) || defined(__aarch64__)
typedef long                 ssize_t;
#else
typedef int                  ssize_t;
#endif

/* ========================================================================
 * 进程/用户 ID 类型
 * ======================================================================== */

typedef int                  pid_t;
typedef unsigned int         uid_t;
typedef unsigned int         gid_t;
typedef long                 off_t;

/* ========================================================================
 * 文件系统相关类型
 * ======================================================================== */

typedef unsigned long        dev_t;       /**< @brief 设备号 */
typedef unsigned long        ino_t;       /**< @brief inode 号 */
typedef unsigned int         nlink_t;     /**< @brief 硬链接数 */
typedef long                 time_t;      /**< @brief 时间（秒） */
typedef long                 blksize_t;   /**< @brief 块大小 */
typedef long                 blkcnt_t;    /**< @brief 块计数 */
typedef unsigned int         mode_t;      /**< @brief 文件权限/类型 */

/* ========================================================================
 * NULL 定义
 * ======================================================================== */

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif /* AISAFE_SYS_TYPES_H */
