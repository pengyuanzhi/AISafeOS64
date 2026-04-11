/**
 * @file    sys/stat.h
 * @brief   POSIX 文件状态接口
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 提供文件状态查询和操作：
 *          - struct stat 文件状态结构体
 *          - stat/fstat/lstat 文件状态查询
 *          - mkdir/chmod/fchmod 目录和权限操作
 *          - 文件类型判断宏（S_ISREG, S_ISDIR 等）
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef AISAFE_SYS_STAT_H
#define AISAFE_SYS_STAT_H

#include <sys/types.h>

/* ========================================================================
 * 文件类型掩码
 * ======================================================================== */

#define S_IFMT   0xF000   /**< @brief 文件类型掩码 */
#define S_IFDIR  0x4000   /**< @brief 目录 */
#define S_IFCHR  0x2000   /**< @brief 字符设备 */
#define S_IFBLK  0x6000   /**< @brief 块设备 */
#define S_IFREG  0x8000   /**< @brief 普通文件 */
#define S_IFIFO  0x1000   /**< @brief 管道 */
#define S_IFLNK  0xA000   /**< @brief 符号链接 */

/* ========================================================================
 * 文件类型判断宏
 * ======================================================================== */

/** @brief 是否为普通文件 */
#define S_ISREG(m)  (((m) & 0xF000) == 0x8000)

/** @brief 是否为目录 */
#define S_ISDIR(m)  (((m) & 0xF000) == 0x4000)

/** @brief 是否为字符设备 */
#define S_ISCHR(m)  (((m) & 0xF000) == 0x2000)

/** @brief 是否为块设备 */
#define S_ISBLK(m)  (((m) & 0xF000) == 0x6000)

/** @brief 是否为管道 */
#define S_ISFIFO(m) (((m) & 0xF000) == 0x1000)

/** @brief 是否为符号链接 */
#define S_ISLNK(m)  (((m) & 0xF000) == 0xA000)

/* ========================================================================
 * struct stat 结构体
 * ======================================================================== */

/**
 * @brief 文件状态结构体
 *
 * @details 包含文件的元数据信息，由 stat()/fstat()/lstat() 填充
 */
struct stat
{
    dev_t     st_dev;      /**< @brief 设备号 */
    ino_t     st_ino;      /**< @brief inode 号 */
    mode_t    st_mode;     /**< @brief 文件权限和类型 */
    nlink_t   st_nlink;    /**< @brief 硬链接数 */
    uid_t     st_uid;      /**< @brief 用户 ID */
    gid_t     st_gid;      /**< @brief 组 ID */
    dev_t     st_rdev;     /**< @brief 块设备号 */
    off_t     st_size;     /**< @brief 文件大小（字节） */
    time_t    st_atime;    /**< @brief 最后访问时间 */
    time_t    st_mtime;    /**< @brief 最后修改时间 */
    time_t    st_ctime;    /**< @brief 最后状态变化时间 */
    blksize_t st_blksize;  /**< @brief I/O 块大小 */
    blkcnt_t  st_blocks;   /**< @brief 分配的块数 */
};

/* ========================================================================
 * 函数声明
 * ======================================================================== */

/**
 * @brief 获取文件状态
 *
 * @param pathname 文件路径
 * @param statbuf  输出状态缓冲区
 *
 * @return 成功返回 0，失败返回 -1 并设置 errno
 */
int stat(const char *pathname, struct stat *statbuf);

/**
 * @brief 通过文件描述符获取文件状态
 *
 * @param fd       文件描述符
 * @param statbuf  输出状态缓冲区
 *
 * @return 成功返回 0，失败返回 -1 并设置 errno
 */
int fstat(int fd, struct stat *statbuf);

/**
 * @brief 获取文件状态（不跟随符号链接）
 *
 * @param pathname 文件路径
 * @param statbuf  输出状态缓冲区
 *
 * @return 成功返回 0，失败返回 -1 并设置 errno
 */
int lstat(const char *pathname, struct stat *statbuf);

/**
 * @brief 创建目录
 *
 * @param pathname 目录路径
 * @param mode     目录权限
 *
 * @return 成功返回 0，失败返回 -1 并设置 errno
 */
int mkdir(const char *pathname, mode_t mode);

/**
 * @brief 修改文件权限
 *
 * @param pathname 文件路径
 * @param mode     新权限
 *
 * @return 成功返回 0，失败返回 -1 并设置 errno
 */
int chmod(const char *pathname, mode_t mode);

/**
 * @brief 通过文件描述符修改权限
 *
 * @param fd   文件描述符
 * @param mode 新权限
 *
 * @return 成功返回 0，失败返回 -1 并设置 errno
 */
int fchmod(int fd, mode_t mode);

#endif /* AISAFE_SYS_STAT_H */
