/**
 * @file    fat32_file.h
 * @brief   FAT32 文件操作接口
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 文件操作接口：
 *          - 文件打开/关闭
 *          - 文件读取/写入
 *          - 文件位置管理
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_FAT32_FILE_H
#define FS_FAT32_FILE_H

#include "fat32_types.h"

/**
 * @brief 打开文件
 *
 * @param inst FAT32 实例
 * @param path 文件路径
 *
 * @return 文件描述符（>=0 成功），<0 失败
 */
int32_t fat32_open(fat32_instance_t *inst, const char *path);

/**
 * @brief 关闭文件
 *
 * @param inst FAT32 实例
 * @param fd   文件描述符
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_close(fat32_instance_t *inst, uint32_t fd);

/**
 * @brief 读取文件
 *
 * @param inst   FAT32 实例
 * @param fd     文件描述符
 * @param buf    输出缓冲区
 * @param size   读取大小
 *
 * @return 实际读取字节数，<0 失败
 */
int64_t fat32_read(fat32_instance_t *inst, uint32_t fd,
                    void *buf, uint64_t size);

/**
 * @brief 写入文件
 *
 * @param inst   FAT32 实例
 * @param fd     文件描述符
 * @param buf    输入缓冲区
 * @param size   写入大小
 *
 * @return 实际写入字节数，<0 失败
 */
int64_t fat32_write(fat32_instance_t *inst, uint32_t fd,
                     const void *buf, uint64_t size);

#endif /* FS_FAT32_FILE_H */
