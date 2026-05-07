/**
 * @file    partition_types.h
 * @brief   磁盘分区管理类型定义
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details 磁盘分区管理核心数据类型定义：
 *          - MBR 分区表结构
 *          - GPT 分区表结构
 *          - 分区类型定义
 *          - 分区操作接口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef PARTITION_TYPES_H
#define PARTITION_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 磁盘扇区大小 */
#define PARTITION_SECTOR_SIZE    512U

/** @brief 最大分区数量 */
#define PARTITION_MAX_PARTITIONS    128U

/** @brief 最大分区名称长度 */
#define PARTITION_MAX_NAME_LEN       64U

/** @brief MBR 分区表数量 */
#define PARTITION_MBR_ENTRIES        4U

/** @brief GPT 分区表大小 */
#define PARTITION_GPT_HEADER_SIZE    92U

/** @brief GPT 分区表签名 */
#define PARTITION_GPT_SIGNATURE      0x5452415020494645ULL

/* ========================================================================
 * MBR 分区表结构
 * ======================================================================== */

/**
 * @brief MBR 分区表项
 */
typedef struct
{
    uint8_t  boot_indicator; /**< @brief 引导指示符（0x80 = 可引导） */
    uint8_t  starting_chs[3]; /**< @brief 起始 CHS 地址 */
    uint8_t  partition_type; /**< @brief 分区类型 */
    uint8_t  ending_chs[3];   /**< @brief 结束 CHS 地址 */
    uint32_t starting_lba;   /**< @brief 起始 LBA 扇区 */
    uint32_t size_in_sectors; /**< @brief 分区大小（扇区数） */
} partition_mbr_entry_t;

/**
 * @brief MBR 引导记录
 */
typedef struct
{
    uint8_t                  bootstrap[446]; /**< @brief 引导代码 */
    partition_mbr_entry_t    entries[4];    /**< @brief 4个分区表项 */
    uint16_t                 signature;     /**< @brief MBR 签名（0x55AA） */
} partition_mbr_t;

/* ========================================================================
 * GPT 分区表结构
 * ======================================================================== */

/**
 * @brief GPT 分区表项
 */
typedef struct
{
    uint8_t  partition_type_guid[16];  /**< @brief 分区类型 GUID */
    uint8_t  unique_partition_guid[16]; /**< @brief 唯一分区 GUID */
    uint64_t starting_lba;            /**< @brief 起始 LBA */
    uint64_t ending_lba;              /**< @brief 结束 LBA */
    uint64_t attributes;              /**< @brief 属性 */
    uint8_t  partition_name[72];      /**< @brief 分区名称（UTF-16LE） */
} partition_gpt_entry_t;

/**
 * @brief GPT 头
 */
typedef struct
{
    uint8_t  signature[8];            /**< @brief 签名（"EFI PART"） */
    uint32_t revision;               /**< @brief 版本 */
    uint32_t header_size;            /**< @brief 头大小 */
    uint32_t header_crc32;           /**< @brief 头 CRC32 */
    uint32_t reserved;               /**< @brief 保留 */
    uint64_t my_lba;                 /**< @brief 当前头 LBA */
    uint64_t alternate_lba;           /**< @brief 备用头 LBA */
    uint64_t first_usable_lba;       /**< @brief 第一个可用 LBA */
    uint64_t last_usable_lba;        /**< @brief 最后一个可用 LBA */
    uint8_t  disk_guid[16];          /**< @brief 磁盘 GUID */
    uint64_t partition_entry_lba;    /**< @brief 分区表项起始 LBA */
    uint32_t number_of_entries;       /**< @brief 分区表项数量 */
    uint32_t size_of_entry;          /**< @brief 分区表项大小 */
    uint32_t partition_entry_crc32;  /**< @brief 分区表 CRC32 */
} partition_gpt_header_t;

/* ========================================================================
 * 分区表类型
 * ======================================================================== */

/**
 * @brief 分区表类型
 */
typedef enum
{
    PARTITION_TYPE_NONE = 0U,        /**< @brief 未检测到 */
    PARTITION_TYPE_MBR = 1U,         /**< @brief MBR 分区表 */
    PARTITION_TYPE_GPT = 2U          /**< @brief GPT 分区表 */
} partition_table_type_t;

/* ========================================================================
 * 分区描述符
 * ======================================================================== */

/**
 * @brief 分区描述符
 */
typedef struct
{
    uint32_t            partition_number; /**< @brief 分区编号 */
    uint64_t            start_lba;        /**< @brief 起始 LBA */
    uint64_t            size_in_sectors;  /**< @brief 分区大小（扇区数） */
    uint32_t            partition_type;   /**< @brief 分区类型（MBR/GPT） */
    bool                active;           /**< @brief 是否激活 */
    bool                bootable;         /**< @brief 是否可引导 */
    char                name[PARTITION_MAX_NAME_LEN]; /**< @brief 分区名称 */
    uint8_t             type_guid[16];    /**< @brief 类型 GUID（GPT） */
    uint8_t             partition_guid[16]; /**< @brief 唯一分区 GUID（GPT） */
} partition_entry_t;

/* ========================================================================
 * 磁盘描述符
 * ======================================================================== */

/**
 * @brief 磁盘描述符
 */
typedef struct
{
    char                device_path[256]; /**< @brief 设备路径 */
    uint64_t            total_sectors;    /**< @brief 总扇区数 */
    partition_table_type_t table_type;    /**< @brief 分区表类型 */
    partition_entry_t   partitions[PARTITION_MAX_PARTITIONS]; /**< @brief 分区表 */
    uint32_t            partition_count;   /**< @brief 分区数量 */
    bool                in_use;           /**< @brief 使用标记 */
} partition_disk_t;

/* ========================================================================
 * 分区操作接口
 * ======================================================================== */

/**
 * @brief 分区操作接口
 */
typedef struct partition_ops
{
    /**
     * @brief 打开磁盘
     *
     * @param device_path 设备路径
     *
     * @return 磁盘描述符指针（成功），NULL（失败）
     */
    partition_disk_t *(*open)(const char *device_path);

    /**
     * @brief 关闭磁盘
     *
     * @param disk 磁盘描述符
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*close)(partition_disk_t *disk);

    /**
     * @brief 扫描分区表
     *
     * @param disk 磁盘描述符
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*scan)(partition_disk_t *disk);

    /**
     * @brief 创建分区
     *
     * @param disk 磁盘描述符
     * @param partition 分区信息
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*create)(partition_disk_t *disk, const partition_entry_t *partition);

    /**
     * @brief 删除分区
     *
     * @param disk 磁盘描述符
     * @param partition_number 分区编号
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*delete)(partition_disk_t *disk, uint32_t partition_number);

    /**
     * @brief 调整分区大小
     *
     * @param disk 磁盘描述符
     * @param partition_number 分区编号
     * @param new_size 新大小（扇区数）
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*resize)(partition_disk_t *disk, uint32_t partition_number,
                       uint64_t new_size);

    /**
     * @brief 同步分区表
     *
     * @param disk 磁盘描述符
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*sync)(partition_disk_t *disk);

} partition_ops_t;

#endif /* PARTITION_TYPES_H */
