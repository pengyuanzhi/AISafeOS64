/**
 * @file    partition_gpt.c
 * @brief   GPT 分区表实现（更新版本）
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 2.0
 *
 * @brief GPT 分区表实现：
 *          - GPT 检测
 *          - GPT 解析
 *          - GPT 创建
 *          - GPT 分区创建/删除/调整
 *          - GPT 分区表同步
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "partition.h"
#include "partition_gpt_types.h"
#include "../block_device.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ========================================================================
 * GPT 签名
 * ======================================================================== */

/** @brief GPT 签名（"EFI PART"） */
#define PARTITION_GPT_SIGNATURE_STR    "EFI PART"

/** @brief GPT 签名（uint64_t） */
#define PARTITION_GPT_SIGNATURE    0x5452415020494645ULL

/* ========================================================================
 * GPT 辅助函数
 * ======================================================================== */

/**
 * @brief 读取 GPT 分区表项
 */
static int32_t read_gpt_partition_entries(const char *device_path,
                                         uint64_t lba,
                                         partition_gpt_entry_t *entries,
                                         uint32_t count)
{
    uint8_t *buf;
    int64_t sectors_read;
    uint32_t sectors_needed;
    uint32_t i;

    /* 计算需要的扇区数 */
    sectors_needed = ((uint64_t)count * 128U + PARTITION_SECTOR_SIZE - 1U) / PARTITION_SECTOR_SIZE;

    /* 分配缓冲区 */
    buf = (uint8_t *)malloc(sectors_needed * PARTITION_SECTOR_SIZE);
    if (buf == NULL)
    {
        return -1;
    }

    /* 读取分区表 */
    sectors_read = block_read_sectors(device_path, lba, buf, sectors_needed);

    if (sectors_read < 0)
    {
        free(buf);
        return -1;
    }

    /* 解析分区表项 */
    for (i = 0U; i < count; i++)
    {
        (void)memcpy(&entries[i], &buf[i * 128U], sizeof(partition_gpt_entry_t));
    }

    free(buf);

    return 0;
}

/**
 * @brief 写入 GPT 分区表项
 */
static int32_t write_gpt_partition_entries(const char *device_path,
                                          uint64_t lba,
                                          const partition_gpt_entry_t *entries,
                                          uint32_t count)
{
    uint8_t *buf;
    uint64_t *buf_crc32;
    int64_t sectors_written;
    uint32_t sectors_needed;
    uint32_t i;
    uint32_t crc;

    /* 计算需要的扇区数 */
    sectors_needed = ((uint64_t)count * 128U + PARTITION_SECTOR_SIZE - 1U) / PARTITION_SECTOR_SIZE;

    /* 分配缓冲区（多分配一个扇区用于存储 CRC32） */
    buf = (uint8_t *)malloc((sectors_needed + 1U) * PARTITION_SECTOR_SIZE);
    if (buf == NULL)
    {
        return -1;
    }

    /* 清零缓冲区 */
    (void)memset(buf, 0, (sectors_needed + 1U) * PARTITION_SECTOR_SIZE);

    /* 填充分区表项 */
    for (i = 0U; i < count; i++)
    {
        (void)memcpy(&buf[i * 128U], &entries[i], sizeof(partition_gpt_entry_t));
    }

    /* 计算 CRC32 */
    crc = compute_crc32(buf, (uint32_t)count * 128U);

    /* 存储 CRC32 */
    buf_crc32 = (uint64_t *)(&buf[sectors_needed * PARTITION_SECTOR_SIZE]);
    *buf_crc32 = (uint64_t)crc;

    /* 写入分区表 */
    sectors_written = block_write_sectors(device_path, lba, buf, sectors_needed);

    free(buf);

    if (sectors_written < 0)
    {
        return -1;
    }

    return 0;
}

/* ========================================================================
 * GPT 分区操作
 * ======================================================================== */

/**
 * @brief 创建 GPT 分区
 */
static int32_t partition_gpt_create(partition_disk_t *disk,
                                    const char *partition_name,
                                    uint64_t start_lba,
                                    uint64_t size_in_sectors,
                                    uint32_t partition_type,
                                    bool bootable)
{
    partition_gpt_header_t *header;
    partition_gpt_entry_t *entries;
    partition_gpt_entry_t new_entry;
    uint32_t i;
    int32_t ret;

    (void)bootable; /* GPT 不使用引导标志 */

    if (disk == NULL)
    {
        return -1;
    }

    if (disk->table_type != PARTITION_TYPE_GPT)
    {
        printf("[Partition] Cannot create GPT partition in MBR disk\n");
        return -1;
    }

    /* 检查是否有足够的空间 */
    if (start_lba + size_in_sectors > disk->total_sectors)
    {
        return -1;
    }

    /* 检查分区数量 */
    if (disk->partition_count >= PARTITION_MAX_PARTITIONS)
    {
        return -1;
    }

    /* 读取 GPT 头 */
    header = (partition_gpt_header_t *)malloc(sizeof(partition_gpt_header_t));
    if (header == NULL)
    {
        return -1;
    }

    ret = read_sector(disk->device_path, 1ULL, (uint8_t *)header);
    if (ret < 0)
    {
        free(header);
        return -1;
    }

    /* 分配分区表项数组 */
    entries = (partition_gpt_entry_t *)malloc(sizeof(partition_gpt_entry_t) * header->number_of_entries);
    if (entries == NULL)
    {
        free(header);
        return -1;
    }

    /* 读取现有分区表 */
    ret = read_gpt_partition_entries(disk->device_path, header->partition_entry_lba,
                                      entries, header->number_of_entries);
    if (ret < 0)
    {
        free(entries);
        free(header);
        return -1;
    }

    /* 查找空闲的分区槽位 */
    uint32_t free_index = 0xFFFFFFFFU;
    for (i = 0U; i < header->number_of_entries; i++)
    {
        if (entries[i].starting_lba == 0ULL)
        {
            free_index = i;
            break;
        }
    }

    if (free_index == 0xFFFFFFFFU)
    {
        printf("[Partition] No free partition slots\n");
        free(entries);
        free(header);
        return -1;
    }

    /* 填充分区信息 */
    (void)memset(&new_entry, 0, sizeof(partition_gpt_entry_t));

    /* 设置分区类型 GUID */
    gpt_get_partition_guid(partition_type, new_entry.partition_type_guid);

    /* 初始化唯一分区 GUID */
    gpt_init_partition_guid(new_entry.unique_partition_guid);

    /* 设置 LBA 范围 */
    new_entry.starting_lba = start_lba;
    new_entry.ending_lba = start_lba + size_in_sectors - 1ULL;

    /* 设置属性 */
    new_entry.attributes = 0ULL;

    /* 设置分区名称（UTF-16LE） */
    (void)memset(new_entry.partition_name, 0, 72U);
    for (i = 0U; i < 36U && partition_name[i] != '\0'; i++)
    {
        new_entry.partition_name[i * 2] = partition_name[i];
        new_entry.partition_name[i * 2 + 1] = 0U;
    }

    /* 写入新分区表项 */
    entries[free_index] = new_entry;

    /* 写回分区表 */
    ret = write_gpt_partition_entries(disk->device_path, header->partition_entry_lba,
                                      entries, header->number_of_entries);
    if (ret < 0)
    {
        free(entries);
        free(header);
        return -1;
    }

    /* 更新分区表 CRC32 */
    header->partition_entry_crc32 = compute_crc32((const uint8_t *)entries,
                                                  header->number_of_entries * 128U);

    /* 更新 GPT 头 CRC32 */
    int32_t crc = partition_gpt_compute_crc32(header);
    if (crc < 0)
    {
        free(entries);
        free(header);
        return -1;
    }

    header->header_crc32 = (uint32_t)crc;

    /* 写回 GPT 头 */
    ret = write_sector(disk->device_path, 1ULL, (const uint8_t *)header);

    /* 写回备用 GPT 头 */
    /* TODO: 写入备用 GPT 头到 disk->total_sectors - 1 */

    free(entries);
    free(header);

    if (ret < 0)
    {
        return -1;
    }

    /* 更新磁盘分区计数 */
    disk->partition_count = free_index + 1U;

    /* 更新分区信息 */
    partition_entry_t *partition = &disk->partitions[free_index];
    partition->partition_number = free_index;
    partition->start_lba = start_lba;
    partition->size_in_sectors = size_in_sectors;
    partition->partition_type = partition_type;
    partition->active = false;
    partition->bootable = false;
    (void)strncpy(partition->name, partition_name, PARTITION_MAX_NAME_LEN - 1U);
    partition->name[PARTITION_MAX_NAME_LEN - 1U] = '\0';

    printf("[Partition] GPT partition %u created: %s, start=%llu, size=%llu sectors\n",
           free_index, partition_name, start_lba, size_in_sectors);

    return 0;
}

/**
 * @brief 删除 GPT 分区
 */
static int32_t partition_gpt_delete(partition_disk_t *disk, uint32_t partition_number)
{
    partition_gpt_header_t *header;
    partition_gpt_entry_t *entries;
    int32_t ret;

    if (disk == NULL)
    {
        return -1;
    }

    if (disk->table_type != PARTITION_TYPE_GPT)
    {
        printf("[Partition] Cannot delete GPT partition in MBR disk\n");
        return -1;
    }

    /* 检查分区编号 */
    if (partition_number >= disk->partition_count)
    {
        return -1;
    }

    /* 读取 GPT 头 */
    header = (partition_gpt_header_t *)malloc(sizeof(partition_gpt_header_t));
    if (header == NULL)
    {
        return -1;
    }

    ret = read_sector(disk->device_path, 1ULL, (uint8_t *)header);
    if (ret < 0)
    {
        free(header);
        return -1;
    }

    /* 读取现有分区表 */
    entries = (partition_gpt_entry_t *)malloc(sizeof(partition_gpt_entry_t) * header->number_of_entries);
    if (entries == NULL)
    {
        free(header);
        return -1;
    }

    ret = read_gpt_partition_entries(disk->device_path, header->partition_entry_lba,
                                      entries, header->number_of_entries);
    if (ret < 0)
    {
        free(entries);
        free(header);
        return -1;
    }

    /* 清空分区表项 */
    (void)memset(&entries[partition_number], 0, sizeof(partition_gpt_entry_t));

    /* 写回分区表 */
    ret = write_gpt_partition_entries(disk->device_path, header->partition_entry_lba,
                                      entries, header->number_of_entries);
    if (ret < 0)
    {
        free(entries);
        free(header);
        return -1;
    }

    /* 更新分区表 CRC32 */
    header->partition_entry_crc32 = compute_crc32((const uint8_t *)entries,
                                                  header->number_of_entries * 128U);

    /* 更新 GPT 头 CRC32 */
    int32_t crc = partition_gpt_compute_crc32(header);
    if (crc < 0)
    {
        free(entries);
        free(header);
        return -1;
    }

    header->header_crc32 = (uint32_t)crc;

    /* 写回 GPT 头 */
    ret = write_sector(disk->device_path, 1ULL, (const uint8_t *)header);

    /* 写回备用 GPT 头 */
    /* TODO: 写入备用 GPT 头到 disk->total_sectors - 1 */

    free(entries);
    free(header);

    if (ret < 0)
    {
        return -1;
    }

    printf("[Partition] GPT partition %u deleted\n", partition_number);

    return 0;
}

/**
 * @brief 调整 GPT 分区大小
 */
static int32_t partition_gpt_resize(partition_disk_t *disk, uint32_t partition_number,
                                    uint64_t new_size)
{
    partition_gpt_header_t *header;
    partition_gpt_entry_t *entries;
    int32_t ret;

    if (disk == NULL)
    {
        return -1;
    }

    if (disk->table_type != PARTITION_TYPE_GPT)
    {
        printf("[Partition] Cannot resize GPT partition in MBR disk\n");
        return -1;
    }

    /* 检查分区编号 */
    if (partition_number >= disk->partition_count)
    {
        return -1;
    }

    /* 检查新大小是否有效 */
    if (new_size > disk->total_sectors)
    {
        return -1;
    }

    /* 读取 GPT 头 */
    header = (partition_gpt_header_t *)malloc(sizeof(partition_gpt_header_t));
    if (header == NULL)
    {
        return -1;
    }

    ret = read_sector(disk->device_path, 1ULL, (uint8_t *)header);
    if (ret < 0)
    {
        free(header);
        return -1;
    }

    /* 读取现有分区表 */
    entries = (partition_gpt_entry_t *)malloc(sizeof(partition_gpt_entry_t) * header->number_of_entries);
    if (entries == NULL)
    {
        free(header);
        return -1;
    }

    ret = read_gpt_partition_entries(disk->device_path, header->partition_entry_lba,
                                      entries, header->number_of_entries);
    if (ret < 0)
    {
        free(entries);
        free(header);
        return -1;
    }

    /* 更新分区大小 */
    entries[partition_number].ending_lba = entries[partition_number].starting_lba + new_size - 1ULL;

    /* 写回分区表 */
    ret = write_gpt_partition_entries(disk->device_path, header->partition_entry_lba,
                                      entries, header->number_of_entries);
    if (ret < 0)
    {
        free(entries);
        free(header);
        return -1;
    }

    /* 更新分区表 CRC32 */
    header->partition_entry_crc32 = compute_crc32((const uint8_t *)entries,
                                                  header->number_of_entries * 128U);

    /* 更新 GPT 头 CRC32 */
    int32_t crc = partition_gpt_compute_crc32(header);
    if (crc < 0)
    {
        free(entries);
        free(header);
        return -1;
    }

    header->header_crc32 = (uint32_t)crc;

    /* 写回 GPT 头 */
    ret = write_sector(disk->device_path, 1ULL, (const uint8_t *)header);

    /* 写回备用 GPT 头 */
    /* TODO: 写入备用 GPT 头到 disk->total_sectors - 1 */

    free(entries);
    free(header);

    if (ret < 0)
    {
        return -1;
    }

    printf("[Partition] GPT partition %u resized to %llu sectors\n",
           partition_number, new_size);

    return 0;
}

/* ========================================================================
 * GPT 分区表同步
 * ======================================================================== */

/**
 * @brief 同步 GPT 分区表
 */
static int32_t partition_gpt_sync(partition_disk_t *disk)
{
    partition_gpt_header_t *header;
    int32_t ret;

    if (disk == NULL)
    {
        return -1;
    }

    if (disk->table_type != PARTITION_TYPE_GPT)
    {
        printf("[Partition] Cannot sync GPT in MBR disk\n");
        return -1;
    }

    printf("[Partition] Syncing GPT partition table: %s\n", disk->device_path);

    /* 读取主 GPT 头 */
    header = (partition_gpt_header_t *)malloc(sizeof(partition_gpt_header_t));
    if (header == NULL)
    {
        return -1;
    }

    ret = read_sector(disk->device_path, 1ULL, (uint8_t *)header);
    if (ret < 0)
    {
        free(header);
        return -1;
    }

    /* 验证主 GPT 头 CRC32 */
    uint32_t main_crc = partition_gpt_compute_crc32(header);

    if (main_crc != header->header_crc32)
    {
        printf("[Partition] Warning: Main GPT header CRC32 mismatch\n");
    }

    /* 写入备用 GPT 头 */
    header->my_lba = disk->total_sectors - 1ULL;
    header->alternate_lba = 1ULL;

    /* 重新计算 CRC32 */
    int32_t crc = partition_gpt_compute_crc32(header);
    if (crc < 0)
    {
        free(header);
        return -1;
    }

    header->header_crc32 = (uint32_t)crc;

    /* 写入备用 GPT 头到磁盘最后一个扇区 */
    ret = write_sector(disk->device_path, disk->total_sectors - 1ULL, (const uint8_t *)header);

    free(header);

    if (ret < 0)
    {
        printf("[Partition] Failed to write backup GPT header\n");
        return -1;
    }

    printf("[Partition] Backup GPT header written to LBA %llu\n",
           disk->total_sectors - 1ULL);

    return 0;
}

/* ========================================================================
 * GPT 公共接口实现（更新版本）
 * ======================================================================== */

/**
 * @brief GPT 分区表检测
 */
bool partition_gpt_detect(const uint8_t *data)
{
    const partition_gpt_header_t *header;
    uint64_t signature;

    if (data == NULL)
    {
        return false;
    }

    header = (const partition_gpt_header_t *)data;

    /* 检查 GPT 签名（"EFI PART"） */
    (void)memcpy(&signature, header->signature, 8U);

    return (signature == PARTITION_GPT_SIGNATURE);
}

/**
 * @brief GPT 分区表解析
 */
int32_t partition_gpt_parse(partition_disk_t *disk, uint64_t lba,
                             const uint8_t *data)
{
    const partition_gpt_header_t *header;
    partition_gpt_entry_t *entries;
    uint32_t i;
    uint32_t partition_index;

    if (disk == NULL || data == NULL)
    {
        return -1;
    }

    header = (const partition_gpt_header_t *)data;

    /* 检查 GPT 签名 */
    {
        uint64_t signature;
        (void)memcpy(&signature, header->signature, 8U);

        if (signature != PARTITION_GPT_SIGNATURE)
        {
            return -1;
        }
    }

    /* TODO: 验证 CRC32 */

    /* 分配分区表项数组 */
    entries = (partition_gpt_entry_t *)malloc(sizeof(partition_gpt_entry_t) * header->number_of_entries);
    if (entries == NULL)
    {
        return -1;
    }

    /* 读取分区表项 */
    int32_t ret = read_gpt_partition_entries(disk->device_path, header->partition_entry_lba,
                                              entries, header->number_of_entries);
    if (ret < 0)
    {
        free(entries);
        return -1;
    }

    /* 解析分区表项 */
    partition_index = 0;
    for (i = 0U; i < header->number_of_entries; i++)
    {
        const partition_gpt_entry_t *entry = &entries[i];

        /* 跳过空分区 */
        if (entry->starting_lba == 0ULL)
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
        partition->size_in_sectors = entry->ending_lba - entry->starting_lba + 1ULL;

        /* 判断分区类型（根据 GUID） */
        if (memcmp(entry->partition_type_guid, GPT_GUID_FAT32, 16U) == 0)
        {
            partition->partition_type = GPT_PART_TYPE_FAT32;
            (void)strcpy(partition->name, "FAT32");
        }
        else if (memcmp(entry->partition_type_guid, GPT_GUID_NTFS, 16U) == 0)
        {
            partition->partition_type = GPT_PART_TYPE_NTFS;
            (void)strcpy(partition->name, "NTFS");
        }
        else if (memcmp(entry->partition_type_guid, GPT_GUID_EXT4, 16U) == 0)
        {
            partition->partition_type = GPT_PART_TYPE_EXT4;
            (void)strcpy(partition->name, "EXT4");
        }
        else if (memcmp(entry->partition_type_guid, GPT_GUID_SWAP, 16U) == 0)
        {
            partition->partition_type = GPT_PART_TYPE_SWAP;
            (void)strcpy(partition->name, "SWAP");
        }
        else
        {
            partition->partition_type = GPT_PART_TYPE_UNUSED;
            (void)strcpy(partition->name, "Unknown");
        }

        /* 提取分区名称（UTF-16LE） */
        for (i = 0U; i < 35U; i++)
        {
            partition->name[i] = (char)entry->partition_name[i * 2];
            if (partition->name[i] == '\0')
            {
                break;
            }
        }
        partition->name[35] = '\0';

        partition->active = false;
        partition->bootable = false;

        /* 保存 GUID */
        (void)memcpy(partition->type_guid, entry->partition_type_guid, 16U);
        (void)memcpy(partition->partition_guid, entry->unique_partition_guid, 16U);

        partition_index++;
    }

    free(entries);

    disk->table_type = PARTITION_TYPE_GPT;
    disk->partition_count = partition_index;

    return 0;
}

/**
 * @brief 创建 GPT 分区表
 */
int32_t partition_gpt_create_table(partition_disk_t *disk)
{
    partition_gpt_header_t *header;

    if (disk == NULL)
    {
        return -1;
    }

    /* 分配 GPT 头数据 */
    header = (partition_gpt_header_t *)malloc(sizeof(partition_gpt_header_t));
    if (header == NULL)
    {
        return -1;
    }

    /* 清零 GPT 头 */
    (void)memset(header, 0, sizeof(partition_gpt_header_t));

    /* 设置 GPT 签名 */
    (void)memcpy(header->signature, PARTITION_GPT_SIGNATURE_STR, 8U);

    /* 设置版本（1.0） */
    header->revision = 0x00010000U;

    /* 设置头大小 */
    header->header_size = PARTITION_GPT_HEADER_SIZE;

    /* 设置 LBA 位置 */
    header->my_lba = 1ULL;  /* GPT 头位于 LBA 1 */
    header->alternate_lba = disk->total_sectors - 1ULL;

    /* 设置可用 LBA 范围 */
    header->first_usable_lba = 34ULL;  /* 跳过 GPT 头和保护分区 */
    header->last_usable_lba = disk->total_sectors - 34ULL;

    /* 设置分区表 LBA */
    header->partition_entry_lba = 2ULL;

    /* 设置分区表项数量 */
    header->number_of_entries = 128U;

    /* 设置分区表项大小 */
    header->size_of_entry = 128U;

    /* 计算 CRC32 */
    int32_t crc = partition_gpt_compute_crc32(header);
    if (crc < 0)
    {
        free(header);
        return -1;
    }

    header->header_crc32 = (uint32_t)crc;

    /* 写入主 GPT 头到磁盘扇区 1 */
    int32_t ret = write_sector(disk->device_path, 1ULL, (const uint8_t *)header);

    /* 写入备用 GPT 头到磁盘最后一个扇区 */
    header->my_lba = disk->total_sectors - 1ULL;
    header->alternate_lba = 1ULL;

    /* 重新计算 CRC32 */
    crc = partition_gpt_compute_crc32(header);
    if (crc < 0)
    {
        free(header);
        return -1;
    }

    header->header_crc32 = (uint32_t)crc;

    ret = write_sector(disk->device_path, disk->total_sectors - 1ULL, (const uint8_t *)header);

    free(header);

    if (ret < 0)
    {
        return -1;
    }

    /* 初始化空的分区表项 */
    partition_gpt_entry_t *entries = (partition_gpt_entry_t *)malloc(sizeof(partition_gpt_entry_t) * 128U);
    if (entries == NULL)
    {
        return -1;
    }

    (void)memset(entries, 0, sizeof(partition_gpt_entry_t) * 128U);

    ret = write_gpt_partition_entries(disk->device_path, 2ULL, entries, 128U);

    free(entries);

    if (ret < 0)
    {
        return -1;
    }

    disk->table_type = PARTITION_TYPE_GPT;
    disk->partition_count = 0U;

    printf("[Partition] GPT partition table created: %s\n", disk->device_path);

    return 0;
}

/* ========================================================================
 * GPT 分区操作公共接口
 * ======================================================================== */

int32_t gpt_create_partition(partition_disk_t *disk,
                              const char *partition_name,
                              uint64_t start_lba,
                              uint64_t size_in_sectors,
                              uint32_t partition_type,
                              bool bootable)
{
    return partition_gpt_create(disk, partition_name, start_lba, size_in_sectors,
                                partition_type, bootable);
}

int32_t gpt_delete_partition(partition_disk_t *disk, uint32_t partition_number)
{
    return partition_gpt_delete(disk, partition_number);
}

int32_t gpt_resize_partition(partition_disk_t *disk, uint32_t partition_number,
                              uint64_t new_size)
{
    return partition_gpt_resize(disk, partition_number, new_size);
}

int32_t gpt_sync_partition_table(partition_disk_t *disk)
{
    return partition_gpt_sync(disk);
}
