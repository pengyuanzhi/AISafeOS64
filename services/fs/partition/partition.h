/**
 * @file    partition.h
 * @brief   磁盘分区管理公共接口
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details 磁盘分区管理公共接口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef PARTITION_H
#define PARTITION_H

#include "partition_types.h"

/* ========================================================================
 * MBR 操作接口
 * ======================================================================== */

/**
 * @brief 检测 MBR 分区表
 *
 * @param data MBR 数据（512 字节）
 *
 * @return true 是 MBR，false 不是 MBR
 */
bool partition_mbr_detect(const uint8_t *data);

/**
 * @brief 解析 MBR 分区表
 *
 * @param disk 磁盘描述符
 * @param data MBR 数据（512 字节）
 *
 * @return 0 成功，<0 失败
 */
int32_t partition_mbr_parse(partition_disk_t *disk, const uint8_t *data);

/**
 * @brief 创建 MBR 分区表
 *
 * @param disk 磁盘描述符
 *
 * @return 0 成功，<0 失败
 */
int32_t partition_mbr_create_table(partition_disk_t *disk);

/* ========================================================================
 * GPT 操作接口
 * ======================================================================== */

/**
 * @brief 检测 GPT 分区表
 *
 * @param data GPT 头数据（512 字节）
 *
 * @return true 是 GPT，false 不是 GPT
 */
bool partition_gpt_detect(const uint8_t *data);

/**
 * @brief 解析 GPT 分区表
 *
 * @param disk 磁盘描述符
 * @param lba 扇区号
 * @param data GPT 头数据
 *
 * @return 0 成功，<0 失败
 */
int32_t partition_gpt_parse(partition_disk_t *disk, uint64_t lba,
                             const uint8_t *data);

/**
 * @brief 创建 GPT 分区表
 *
 * @param disk 磁盘描述符
 *
 * @return 0 成功，<0 失败
 */
int32_t partition_gpt_create_table(partition_disk_t *disk);

/* ========================================================================
 * 通用分区管理接口
 * ======================================================================== */

/**
 * @brief 打开磁盘设备
 *
 * @param device_path 设备路径
 *
 * @return 磁盘描述符指针（成功），NULL（失败）
 */
partition_disk_t *partition_open(const char *device_path);

/**
 * @brief 关闭磁盘设备
 *
 * @param disk 磁盘描述符
 *
 * @return 0 成功，<0 失败
 */
int32_t partition_close(partition_disk_t *disk);

/**
 * @brief 扫描分区表
 *
 * @param disk 磁盘描述符
 *
 * @return 0 成功，<0 失败
 */
int32_t partition_scan(partition_disk_t *disk);

/**
 * @brief 创建分区
 *
 * @param disk 磁盘描述符
 * @param start_lba 起始 LBA
 * @param size_in_sectors 分区大小（扇区数）
 * @param partition_type 分区类型（MBR 类型）
 * @param bootable 是否可引导
 *
 * @return 分区编号（>=0 成功），<0 失败
 */
int32_t partition_create(partition_disk_t *disk,
                         uint64_t start_lba,
                         uint64_t size_in_sectors,
                         uint32_t partition_type,
                         bool bootable);

/**
 * @brief 删除分区
 *
 * @param disk 磁盘描述符
 * @param partition_number 分区编号
 *
 * @return 0 成功，<0 失败
 */
int32_t partition_delete(partition_disk_t *disk, uint32_t partition_number);

/**
 * @brief 调整分区大小
 *
 * @param disk 磁盘描述符
 * @param partition_number 分区编号
 * @param new_size 新大小（扇区数）
 *
 * @return 0 成功，<0 失败
 */
int32_t partition_resize(partition_disk_t *disk, uint32_t partition_number,
                          uint64_t new_size);

/**
 * @brief 同步分区表
 *
 * @param disk 磁盘描述符
 *
 * @return 0 成功，<0 失败
 */
int32_t partition_sync(partition_disk_t *disk);

/**
 * @brief 获取分区信息
 *
 * @param disk 磁盘描述符
 * @param partition_number 分区编号
 * @param partition 输出分区信息
 *
 * @return 0 成功，<0 失败
 */
int32_t partition_get_info(partition_disk_t *disk,
                            uint32_t partition_number,
                            partition_entry_t *partition);

#endif /* PARTITION_H */
