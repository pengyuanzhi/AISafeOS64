/**
 * @file    block_device.h
 * @brief   块设备驱动接口
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @brief 块设备驱动接口定义
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef BLOCK_DEVICE_H
#define BLOCK_DEVICE_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 扇区大小 */
#define BLOCK_SECTOR_SIZE     512U

/** @brief 最大扇区数 */
#define BLOCK_MAX_SECTORS     (1ULL << 32)

/** @brief 设备路径最大长度 */
#define BLOCK_MAX_PATH_LEN    256U

/* ========================================================================
 * 块设备类型
 * ======================================================================== */

/**
 * @brief 块设备类型
 */
typedef enum
{
    BLOCK_DEV_TYPE_UNKNOWN = 0U,
    BLOCK_DEV_TYPE_DISK,        /**< @brief 磁盘 */
    BLOCK_DEV_TYPE_PARTITION,   /**< @brief 分区 */
    BLOCK_DEV_TYPE_CDROM        /**< @brief CD-ROM */
} block_dev_type_t;

/* ========================================================================
 * 块设备描述符
 * ======================================================================== */

/**
 * @brief 块设备描述符
 */
typedef struct block_device
{
    char                device_path[BLOCK_MAX_PATH_LEN];  /**< @brief 设备路径 */
    block_dev_type_t    type;                            /**< @brief 设备类型 */
    uint64_t            total_sectors;                   /**< @brief 总扇区数 */
    uint64_t            sector_size;                     /**< @brief 扇区大小 */
    bool                in_use;                          /**< @brief 使用标记 */
    uint32_t            ref_count;                       /**< @brief 引用计数 */

} block_device_t;

/* ========================================================================
 * 块设备操作接口
 * ======================================================================== */

/**
 * @brief 块设备操作接口
 */
typedef struct block_device_ops
{
    /**
     * @brief 打开块设备
     *
     * @param device_path 设备路径
     *
     * @return 块设备描述符指针（成功），NULL（失败）
     */
    block_device_t *(*open)(const char *device_path);

    /**
     * @brief 关闭块设备
     *
     * @param device 块设备描述符
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*close)(block_device_t *device);

    /**
     * @brief 读取扇区
     *
     * @param device 块设备描述符
     * @param lba LBA 扇区号
     * @param buf 缓冲区
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*read)(block_device_t *device, uint64_t lba, uint8_t *buf);

    /**
     * @brief 写入扇区
     *
     * @param device 块设备描述符
     * @param lba LBA 扇区号
     * @param buf 缓冲区
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*write)(block_device_t *device, uint64_t lba, const uint8_t *buf);

    /**
     * @brief 读取多扇区
     *
     * @param device 块设备描述符
     * @param lba LBA 扇区号
     * @param buf 缓冲区
     * @param count 扇区数量
     *
     * @return 实际读取的扇区数，<0 失败
     */
    int64_t (*read_multiple)(block_device_t *device, uint64_t lba, uint8_t *buf, uint32_t count);

    /**
     * @brief 写入多扇区
     *
     * @param device 块设备描述符
     * @param lba LBA 扇区号
     * @param buf 缓冲区
     * @param count 扇区数量
     *
     * @return 实际写入的扇区数，<0 失败
     */
    int64_t (*write_multiple)(block_device_t *device, uint64_t lba, const uint8_t *buf, uint32_t count);

    /**
     * @brief 获取设备大小
     *
     * @param device 块设备描述符
     *
     * @return 总扇区数
     */
    uint64_t (*get_size)(block_device_t *device);

} block_device_ops_t;

/* ========================================================================
 * 块设备管理
 * ======================================================================== */

/**
 * @brief 打开块设备
 *
 * @param device_path 设备路径
 *
 * @return 块设备描述符指针（成功），NULL（失败）
 */
block_device_t *block_open(const char *device_path);

/**
 * @brief 关闭块设备
 *
 * @param device 块设备描述符
 *
 * @return 0 成功，<0 失败
 */
int32_t block_close(block_device_t *device);

/**
 * @brief 读取单个扇区
 *
 * @param device_path 设备路径
 * @param lba LBA 扇区号
 * @param buf 缓冲区
 *
 * @return 0 成功，<0 失败
 */
int32_t block_read_sector(const char *device_path, uint64_t lba, uint8_t *buf);

/**
 * @brief 写入单个扇区
 *
 * @param device_path 设备路径
 * @param lba LBA 扇区号
 * @param buf 缓冲区
 *
 * @return 0 成功，<0 失败
 */
int32_t block_write_sector(const char *device_path, uint64_t lba, const uint8_t *buf);

/**
 * @brief 读取多扇区
 *
 * @param device_path 设备路径
 * @param lba LBA 扇区号
 * @param buf 缓冲区
 * @param count 扇区数量
 *
 * @return 实际读取的扇区数，<0 失败
 */
int64_t block_read_sectors(const char *device_path, uint64_t lba, uint8_t *buf, uint32_t count);

/**
 * @brief 写入多扇区
 *
 * @param device_path 设备路径
 * @param lba LBA 扇区号
 * @param buf 缓冲区
 * @param count 扇区数量
 *
 * @return 实际写入的扇区数，<0 失败
 */
int64_t block_write_sectors(const char *device_path, uint64_t lba, const uint8_t *buf, uint32_t count);

#endif /* BLOCK_DEVICE_H */
