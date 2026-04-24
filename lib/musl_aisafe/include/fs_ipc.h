/**
 * @file    fs_ipc.h
 * @brief   AISafeOS64 musl 适配层 — 文件系统 IPC 客户端接口
 * @version 1.0
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

#endif /* FS_IPC_H */
