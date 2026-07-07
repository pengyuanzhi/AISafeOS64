/**
 * @file    ramfs.h
 * @brief   内核 RAMFS 接口
 * @author  AISafe64 Team
 * @date    2026-07-07
 * @version 1.0
 *
 * @details 内存文件系统，支持基本文件操作：
 *          open/close/read/write/lseek/fstat
 *          fd 0/1/2 预留给 stdin/stdout/stderr
 *          stdout/stderr 重定向到 klog
 *
 * @note    后续迁移到用户态 FS 服务后可移除
 *
 * @revision history
 * v1.0 2026-07-07 初始版本
 */

#ifndef KERNEL_RAMFS_H
#define KERNEL_RAMFS_H

#include <stdint.h>

/** @brief open 标志 */
#define RAMFS_O_RDONLY  0x0U
#define RAMFS_O_WRONLY  0x1U
#define RAMFS_O_RDWR    0x2U
#define RAMFS_O_CREAT   0x40U
#define RAMFS_O_TRUNC   0x200U

/**
 * @brief 初始化 RAMFS
 */
void ramfs_init(void);

/**
 * @brief 打开文件
 * @param path 文件路径
 * @param flags 打开标志（RAMFS_O_*）
 * @return >= 0 文件描述符，< 0 错误码
 */
int32_t ramfs_open(const char *path, uint32_t flags);

/**
 * @brief 关闭文件
 * @param fd 文件描述符
 * @return 0 成功，< 0 错误码
 */
int32_t ramfs_close(int32_t fd);

/**
 * @brief 读文件
 * @param fd 文件描述符
 * @param buf 缓冲区
 * @param count 读取字节数
 * @return >= 0 实际读取字节数，< 0 错误码
 */
int32_t ramfs_read(int32_t fd, void *buf, uint32_t count);

/**
 * @brief 写文件
 * @param fd 文件描述符
 * @param buf 数据缓冲区
 * @param count 写入字节数
 * @return >= 0 实际写入字节数，< 0 错误码
 */
int32_t ramfs_write(int32_t fd, const void *buf, uint32_t count);

/**
 * @brief 文件定位
 * @param fd 文件描述符
 * @param offset 偏移量
 * @param whence 基准（0=SET, 1=CUR, 2=END）
 * @return >= 0 新位置，< 0 错误码
 */
int32_t ramfs_lseek(int32_t fd, int32_t offset, uint32_t whence);

/**
 * @brief 获取文件状态
 * @param fd 文件描述符
 * @param statbuf 状态缓冲区（至少 64 字节）
 * @return 0 成功，< 0 错误码
 */
int32_t ramfs_fstat(int32_t fd, void *statbuf);

#endif /* KERNEL_RAMFS_H */
