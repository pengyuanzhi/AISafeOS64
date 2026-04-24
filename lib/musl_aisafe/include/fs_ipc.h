/**
 * @file    fs_ipc.h
 * @brief   AISafeOS64 musl 适配层 — 文件系统 IPC 客户端接口
 * @version 2.0
 *
 * @details FS 客户端接口定义
 *
 * @note MISRA-C:2012 合规
 */

#ifndef FS_IPC_H
#define FS_IPC_H

#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * 公共接口
 * ======================================================================== */

/**
 * @brief FS 客户端初始化
 *
 * @return 0 表示成功，负数表示错误
 */
int fs_client_init(void);

/**
 * @brief 打开文件
 *
 * @param path 文件路径
 * @param flags 打开标志
 * @param mode 打开模式
 *
 * @return 文件描述符，-1 表示失败
 */
int fs_open(const char *path, int flags, unsigned int mode);

/**
 * @brief 关闭文件
 *
 * @param fd 文件描述符
 *
 * @return 0 表示成功，-1 表示失败
 */
int fs_close(int fd);

/**
 * @brief 读取文件
 *
 * @param fd 文件描述符
 * @param buf 缓冲区
 * @param count 读取字节数
 *
 * @return 实际读取字节数，-1 表示失败
 */
long fs_read(int fd, void *buf, size_t count);

/**
 * @brief 写入文件
 *
 * @param fd 文件描述符
 * @param buf 缓冲区
 * @param count 写入字节数
 *
 * @return 实际写入字节数，-1 表示失败
 */
long fs_write(int fd, const void *buf, size_t count);

/**
 * @brief 文件定位
 *
 * @param fd 文件描述符
 * @param offset 偏移量
 * @param whence 定位方式（SEEK_SET/SEEK_CUR/SEEK_END）
 *
 * @return 新的文件偏移量，-1 表示失败
 */
long fs_lseek(int fd, long offset, int whence);

/**
 * @brief 获取文件状态
 *
 * @param fd 文件描述符
 * @param statbuf stat 结构体指针
 *
 * @return 0 表示成功，-1 表示失败
 */
int fs_fstat(int fd, void *statbuf);

/**
 * @brief 文件控制
 *
 * @param fd 文件描述符
 * @param request 控制请求
 * @param arg 参数
 *
 * @return 0 表示成功，-1 表示失败
 */
int fs_ioctl(int fd, unsigned long request, void *arg);

/**
 * @brief 文件描述符控制
 *
 * @param fd 文件描述符
 * @param cmd 控制命令
 * @param arg 参数
 *
 * @return 0 表示成功，-1 表示失败
 */
int fs_fcntl(int fd, int cmd, int arg);

#endif /* FS_IPC_H */
