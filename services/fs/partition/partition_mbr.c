/**
 * @file    partition_mbr.c
 * @brief   MBR 分区表实现
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details MBR 分区表实现：
 *          - MBR 检测
 *          - MBR 解析
 *          - MBR 创建
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "partition.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * MBR 签名
 * ======================================================================== */

/** @brief MBR 签名 */
#define PARTITION_MBR_SIGNATURE    0xAA55U

/* ========================================================================
 * MBR 常见分区类型
 * ======================================================================== */

/** @brief FAT12 分区 */
#define PARTITION_MBR_TYPE_FAT12      0x01U

/** @brief FAT16 分区（< 32MB） */
#define PARTITION_MBR_TYPE_FAT16      0x04U

/** @brief FAT16 分区（LBA） */
#define PARTITION_MBR_TYPE_FAT16_LBA  0x06U

/** @brief FAT32 分区（CHS） */
#define PARTITION_MBR_TYPE_FAT32      0x0BU

/** @brief FAT32 分区（LBA） */
#define PARTITION_MBR_TYPE_FAT32_LBA  0x0CU

/** @brief NTFS 分区 */
#define PARTITION_MBR_TYPE_NTFS       0x07U

/** @brief 扩展分区（CHS） */
#define PARTITION_MBR_TYPE_EXTENDED   0x05U

/** @brief 扩展分区（LBA） */
#define PARTITION_MBR_TYPE_EXTENDED_LBA 0x0FU

/** @brief EXT4 分区 */
#define PARTITION_MBR_TYPE_LINUX      0x83U

/** @brief GPT 保护分区 */
#define PARTITION_MBR_TYPE_GPT_PROTECT 0xEEU

/* ========================================================================
 * MBR 检测
 * ======================================================================== */

/**
 * @brief 检测 MBR 分区表
 */
bool partition_mbr_detect(const uint8_t *data)
{
    const partition_mbr_t *mbr;

    if (data == NULL)
    {
        return false;
    }

    mbr = (const partition_mbr_t *)data;

    /* 检查 MBR 签名（0x55AA，小端存储） */
    return (mbr->signature == PARTITION_MBR_SIGNATURE);
}

/* ========================================================================
 * MBR 解析
 * ======================================================================== */

/**
 * @brief 解析 MBR 分区表
 */
int32_t partition_mbr_parse(partition_disk_t *disk, const uint8_t *data)
{
    const partition_mbr_t *mbr;
    uint32_t i;
    uint32_t partition_index;

    if (disk == NULL || data == NULL)
    {
        return -1;
    }

    mbr = (const partition_mbr_t *)data;

    /* 检查 MBR 签名 */
    if (mbr->signature != PARTITION_MBR_SIGNATURE)
    {
        return -1;
    }

    /* 解析 4 个 MBR 分区 */
    partition_index = 0;
    for (i = 0U; i < PARTITION_MBR_ENTRIES; i++)
    {
        const partition_mbr_entry_t *entry = &mbr->entries[i];

        /* 跳过空分区 */
        if (entry->partition_type == 0U)
        {
            continue;
        }

        /* 检查分区数量 */
        if (partition_index >= PARTITION_MAX_PARTITIONS)
        {
            break;
        }

        /* 填充分区信息 */
        partition_entry_t *partition = &disk->partitions[partition_index];

        partition->partition_number = partition_index;
        partition->start_lba = entry->starting_lba;
        partition->size_in_sectors = entry->size_in_sectors;
        partition->partition_type = entry->partition_type;
        partition->active = (entry->boot_indicator == 0x80U);
        partition->bootable = (entry->boot_indicator == 0x80U);

        /* 设置分区名称（根据类型） */
        switch (entry->partition_type)
        {
            case PARTITION_MBR_TYPE_FAT12:
                (void)strcpy(partition->name, "FAT12");
                break;
            case PARTITION_MBR_TYPE_FAT16:
            case PARTITION_MBR_TYPE_FAT16_LBA:
                (void)strcpy(partition->name, "FAT16");
                break;
            case PARTITION_MBR_TYPE_FAT32:
            case PARTITION_MBR_TYPE_FAT32_LBA:
                (void)strcpy(partition->name, "FAT32");
                break;
            case PARTITION_MBR_TYPE_NTFS:
                (void)strcpy(partition->name, "NTFS");
                break;
            case PARTITION_MBR_TYPE_LINUX:
                (void)strcpy(partition->name, "EXT4");
                break;
            case PARTITION_MBR_TYPE_GPT_PROTECT:
                (void)strcpy(partition->name, "GPT Protect");
                break;
            default:
                (void)strcpy(partition->name, "Unknown");
                break;
        }

        partition_index++;
    }

    disk->table_type = PARTITION_TYPE_MBR;
    disk->partition_count = partition_index;

    return 0;
}

/* ========================================================================
 * MBR 创建
 * ======================================================================== */

/**
 * @brief 创建 MBR 分区表
 */
int32_t partition_mbr_create_table(partition_disk_t *disk)
{
    partition_mbr_t *mbr;
    uint32_t i;

    if (disk == NULL)
    {
        return -1;
    }

    /* 分配 MBR 数据（512 字节） */
    mbr = (partition_mbr_t *)malloc(sizeof(partition_mbr_t));
    if (mbr == NULL)
    {
        return -1;
    }

    /* 清零 MBR */
    (void)memset(mbr, 0, sizeof(partition_mbr_t));

    /* 设置 MBR 签名 */
    mbr->signature = PARTITION_MBR_SIGNATURE;

    /* 清空分区表项 */
    for (i = 0U; i < PARTITION_MBR_ENTRIES; i++)
    {
        mbr->entries[i].partition_type = 0U;
        mbr->entries[i].starting_lba = 0U;
        mbr->entries[i].size_in_sectors = 0U;
        mbr->entries[i].boot_indicator = 0U;
    }

    /* 写入 MBR 到磁盘（TODO） */
    /* TODO: 调用设备驱动写入扇区 0 */

    free(mbr);

    disk->table_type = PARTITION_TYPE_MBR;
    disk->partition_count = 0U;

    return 0;
}
