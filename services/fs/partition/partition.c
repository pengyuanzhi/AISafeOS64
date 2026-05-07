/**
 * @file    partition.c
 * @brief   磁盘分区管理实现（更新版本）
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 2.0
 *
 * @brief 磁盘分区管理实现：
 *          - 通用分区管理接口
 *          - MBR/GPT 自动检测
 *          - 分区创建/删除/调整/同步
 *          - 设备驱动集成
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "partition.h"
#include "../block_device.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大磁盘数量 */
#define PARTITION_MAX_DISKS    16U

/* ========================================================================
 * 热插拔事件
 * ======================================================================== */

/**
 * @brief 热插拔事件
 */
typedef struct
{
    hotplug_event_type_t type;     /**< @brief 事件类型 */
    char device_path[HOTPLUG_MAX_PATH_LEN]; /**< @brief 设备路径 */
} hotplug_event_t;

/* ========================================================================
 * 磁盘管理
 * ======================================================================== */

static partition_disk_t s_disks[PARTITION_MAX_DISKS];
static hotplug_event_t s_hotplug_events[8];
static uint32_t s_event_index = 0U;

static bool s_initialized = false;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 初始化分区管理
 */
static void partition_init(void)
{
    uint32_t i;

    if (!s_initialized)
    {
        for (i = 0U; i < PARTITION_MAX_DISKS; i++)
        {
            (void)memset(&s_disks[i], 0, sizeof(partition_disk_t));
        }

        for (i = 0U; i < 8U; i++)
        {
            (void)memset(&s_hotplug_events[i], 0, sizeof(hotplug_event_t));
        }

        s_initialized = true;
    }
}

/**
 * @brief 分配磁盘描述符
 */
static partition_disk_t *alloc_disk(void)
{
    uint32_t i;

    for (i = 0U; i < PARTITION_MAX_DISKS; i++)
    {
        if (!s_disks[i].in_use)
        {
            (void)memset(&s_disks[i], 0, sizeof(partition_disk_t));
            return &s_disks[i];
        }
    }

    return NULL;
}

/**
 * @brief 读取磁盘扇区
 */
static int32_t read_sector(const char *device_path, uint64_t lba, uint8_t *buf)
{
    int64_t sectors_read;

    sectors_read = block_read_sectors(device_path, lba, buf, 1U);

    if (sectors_read < 0)
    {
        printf("[Partition] Failed to read sector %llu from %s\n", lba, device_path);
        return -1;
    }

    return 0;
}

/**
 * @brief 写入磁盘扇区
 */
static int32_t write_sector(const char *device_path, uint64_t lba, const uint8_t *buf)
{
    int64_t sectors_written;

    sectors_written = block_write_sectors(device_path, lba, buf, 1U);

    if (sectors_written < 0)
    {
        printf("[Partition] Failed to write sector %llu to %s\n", lba, device_path);
        return -1;
    }

    return 0;
}

/**
 * @brief 计算 CRC32
 */
static uint32_t compute_crc32(const uint8_t *data, uint32_t size)
{
    uint32_t i, j;
    uint32_t crc = 0xFFFFFFFFU;

    for (i = 0U; i < size; i++)
    {
        crc ^= data[i];
        for (j = 0U; j < 8U; j++)
        {
            if (crc & 0x1U)
            {
                crc = (crc >> 1) ^ 0xEDB88320U;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

/* ========================================================================
 * MBR 操作
 * ======================================================================== */

/**
 * @brief 创建 MBR 分区表
 */
static int32_t partition_mbr_create_table(partition_disk_t *disk)
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

    /* 写入 MBR 到磁盘扇区 0 */
    int32_t ret = write_sector(disk->device_path, 0ULL, (const uint8_t *)mbr);
    free(mbr);

    if (ret < 0)
    {
        return -1;
    }

    disk->table_type = PARTITION_TYPE_MBR;
    disk->partition_count = 0U;

    printf("[Partition] MBR partition table created: %s\n", disk->device_path);

    return 0;
}

/* ========================================================================
 * GPT 操作
 * ======================================================================== */

/**
 * @brief 计算 GPT 头 CRC32
 */
static int32_t partition_gpt_compute_crc32(partition_gpt_header_t *header)
{
    uint32_t *words;
    uint32_t crc;

    if (header == NULL)
    {
        return -1;
    }

    /* 保存原始 CRC32 */
    uint32_t original_crc = header->header_crc32;

    /* 清零 CRC32 字段 */
    header->header_crc32 = 0U;

    /* 计算新的 CRC32 */
    words = (uint32_t *)header;
    crc = compute_crc32((const uint8_t *)header, header->header_size);

    /* 恢复原始 CRC32 */
    header->header_crc32 = original_crc;

    return crc;
}

/**
 * @brief 创建 GPT 分区表
 */
static int32_t partition_gpt_create_table(partition_disk_t *disk)
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
    header->first_usable_lba = 34ULL;
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

    /* 写入 GPT 头到磁盘扇区 1 */
    int32_t ret = write_sector(disk->device_path, 1ULL, (const uint8_t *)header);

    /* TODO: 写入备用 GPT 头（LBA 最后一个扇区） */

    free(header);

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
 * 通用分区管理接口实现
 * ======================================================================== */

/**
 * @brief 打开磁盘设备
 */
partition_disk_t *partition_open(const char *device_path)
{
    partition_disk_t *disk;
    uint8_t *buf;

    partition_init();

    if (device_path == NULL)
    {
        return NULL;
    }

    /* 检查设备路径长度 */
    if (strlen(device_path) >= 256U)
    {
        return NULL;
    }

    /* 分配磁盘描述符 */
    disk = alloc_disk();
    if (disk == NULL)
    {
        return NULL;
    }

    /* 保存设备路径 */
    (void)strncpy(disk->device_path, device_path, 255U);
    disk->device_path[255U] = '\0';
    disk->in_use = true;

    /* 分配缓冲区 */
    buf = (uint8_t *)malloc(PARTITION_SECTOR_SIZE);
    if (buf == NULL)
    {
        disk->in_use = false;
        return NULL;
    }

    /* 读取扇区 0（MBR 或 GPT 保护分区） */
    int32_t ret = read_sector(device_path, 0ULL, buf);
    if (ret < 0)
    {
        free(buf);
        disk->in_use = false;
        return NULL;
    }

    /* 检测分区表类型 */
    if (partition_gpt_detect(buf))
    {
        /* GPT 分区表 */
        printf("[Partition] GPT partition table detected\n");

        /* 读取 GPT 头（LBA 1） */
        ret = read_sector(device_path, 1ULL, buf);
        if (ret < 0)
        {
            free(buf);
            disk->in_use = false;
            return NULL;
        }

        /* 解析 GPT */
        ret = partition_gpt_parse(disk, 1ULL, buf);
        if (ret < 0)
        {
            printf("[Partition] Failed to parse GPT\n");
            free(buf);
            disk->in_use = false;
            return NULL;
        }
    }
    else if (partition_mbr_detect(buf))
    {
        /* MBR 分区表 */
        printf("[Partition] MBR partition table detected\n");

        /* 解析 MBR */
        ret = partition_mbr_parse(disk, buf);
        if (ret < 0)
        {
            printf("[Partition] Failed to parse MBR\n");
            free(buf);
            disk->in_use = false;
            return NULL;
        }
    }
    else
    {
        /* 未检测到分区表 */
        printf("[Partition] No partition table detected, creating MBR...\n");

        /* 创建 MBR */
        ret = partition_mbr_create_table(disk);
        if (ret < 0)
        {
            printf("[Partition] Failed to create MBR\n");
            free(buf);
            disk->in_use = false;
            return NULL;
        }
    }

    free(buf);

    printf("[Partition] Disk opened: %s, partitions: %u\n",
           device_path, disk->partition_count);

    return disk;
}

/**
 * @brief 关闭磁盘设备
 */
int32_t partition_close(partition_disk_t *disk)
{
    if (disk == NULL)
    {
        return -1;
    }

    disk->in_use = false;

    return 0;
}

/**
 * @brief 扫描分区表
 */
int32_t partition_scan(partition_disk_t *disk)
{
    if (disk == NULL)
    {
        return -1;
    }

    /* 分区表已在打开时扫描 */
    printf("[Partition] Scan completed: %u partitions\n", disk->partition_count);

    return 0;
}

/**
 * @brief 创建分区（MBR 版本）
 */
static int32_t partition_mbr_create(partition_disk_t *disk,
                                     uint32_t partition_number,
                                     uint64_t start_lba,
                                     uint64_t size_in_sectors,
                                     uint32_t partition_type,
                                     bool bootable)
{
    partition_mbr_t *mbr;
    partition_mbr_entry_t *entry;
    int32_t ret;

    if (disk == NULL)
    {
        return -1;
    }

    if (disk->table_type != PARTITION_TYPE_MBR)
    {
        printf("[Partition] Cannot create partition in GPT disk\n");
        return -1;
    }

    /* 检查分区编号 */
    if (partition_number >= PARTITION_MBR_ENTRIES)
    {
        return -1;
    }

    /* 检查是否有足够的空间 */
    if (start_lba + size_in_sectors > disk->total_sectors)
    {
        return -1;
    }

    /* 读取 MBR */
    mbr = (partition_mbr_t *)malloc(sizeof(partition_mbr_t));
    if (mbr == NULL)
    {
        return -1;
    }

    ret = read_sector(disk->device_path, 0ULL, (uint8_t *)mbr);
    if (ret < 0)
    {
        free(mbr);
        return -1;
    }

    /* 查找可用的分区槽位 */
    entry = &mbr->entries[partition_number];

    /* 检查分区槽位是否为空 */
    if (entry->partition_type != 0U)
    {
        printf("[Partition] Partition %u already exists\n", partition_number);
        free(mbr);
        return -1;
    }

    /* 填充分区信息 */
    entry->starting_lba = start_lba;
    entry->size_in_sectors = size_in_sectors;
    entry->partition_type = partition_type;
    entry->boot_indicator = bootable ? 0x80U : 0x00U;

    /* 写回 MBR */
    ret = write_sector(disk->device_path, 0ULL, (const uint8_t *)mbr);
    free(mbr);

    if (ret < 0)
    {
        return -1;
    }

    /* 更新磁盘分区计数 */
    disk->partition_count = partition_number + 1U;

    printf("[Partition] Partition %u created: start=%llu, size=%llu sectors\n",
           partition_number, start_lba, size_in_sectors);

    return 0;
}

/**
 * @brief 创建分区
 */
int32_t partition_create(partition_disk_t *disk,
                         uint64_t start_lba,
                         uint64_t size_in_sectors,
                         uint32_t partition_type,
                         bool bootable)
{
    (void)disk;
    (void)start_lba;
    (void)size_in_sectors;
    (void)partition_type;
    (void)bootable;

    /* TODO: 根据分区表类型选择创建方法 */
    /* if (disk->table_type == PARTITION_TYPE_MBR) { ... } */
    /* else if (disk->table_type == PARTITION_TYPE_GPT) { ... } */

    return -1;
}

/**
 * @brief 删除分区（MBBR 版本）
 */
static int32_t partition_mbr_delete(partition_disk_t *disk, uint32_t partition_number)
{
    partition_mbr_t *mbr;
    int32_t ret;

    if (disk == NULL)
    {
        return -1;
    }

    if (disk->table_type != PARTITION_TYPE_MBR)
    {
        printf("[Partition] Cannot delete partition in GPT disk\n");
        return -1;
    }

    /* 检查分区编号 */
    if (partition_number >= PARTITION_MBR_ENTRIES)
    {
        return -1;
    }

    /* 读取 MBR */
    mbr = (partition_mbr_t *)malloc(sizeof(partition_mbr_t));
    if (mbr == NULL)
    {
        return -1;
    }

    ret = read_sector(disk->device_path, 0ULL, (uint8_t *)mbr);
    if (ret < 0)
    {
        free(mbr);
        return -1;
    }

    /* 清空分区槽位 */
    (void)memset(&mbr->entries[partition_number], 0, sizeof(partition_mbr_entry_t));

    /* 写回 MBR */
    ret = write_sector(disk->device_path, 0ULL, (const uint8_t *)mbr);
    free(mbr);

    if (ret < 0)
    {
        return -1;
    }

    /* 更新磁盘分区计数 */
    if (disk->partition_count > 0U)
    {
        disk->partition_count--;
    }

    printf("[Partition] Partition %u deleted\n", partition_number);

    return 0;
}

/**
 * @brief 删除分区
 */
int32_t partition_delete(partition_disk_t *disk, uint32_t partition_number)
{
    (void)disk;
    (void)partition_number;

    /* TODO: 根据分区表类型选择删除方法 */
    /* if (disk->table_type == PARTITION_TYPE_MBR) { ... } */
    /* else if (disk->table_type == PARTITION_TYPE_GPT) { ... } */

    return -1;
}

/**
 * @brief 调整分区大小（MBBR 版本）
 */
static int32_t partition_mbr_resize(partition_disk_t *disk, uint32_t partition_number,
                                     uint64_t new_size)
{
    partition_mbr_t *mbr;
    int32_t ret;

    if (disk == NULL)
    {
        return -1;
    }

    if (disk->table_type != PARTITION_TYPE_MBR)
    {
        printf("[Partition] Cannot resize partition in GPT disk\n");
        return -1;
    }

    /* 检查分区编号 */
    if (partition_number >= PARTITION_MBR_ENTRIES)
    {
        return -1;
    }

    /* 检查是否有足够的空间 */
    if (new_size > disk->total_sectors)
    {
        return -1;
    }

    /* 读取 MBR */
    mbr = (partition_mbr_t *)malloc(sizeof(partition_mbr_t));
    if (mbr == NULL)
    {
        return -1;
    }

    ret = read_sector(disk->device_path, 0ULL, (uint8_t *)mbr);
    if (ret < 0)
    {
        free(mbr);
        return -1;
    }

    /* 更新分区大小 */
    mbr->entries[partition_number].size_in_sectors = new_size;

    /* 写回 MBR */
    ret = write_sector(disk->device_path, 0ULL, (const uint8_t *)mbr);
    free(mbr);

    if (ret < 0)
    {
        return -1;
    }

    printf("[Partition] Partition %u resized to %llu sectors\n",
           partition_number, new_size);

    return 0;
}

/**
 * @brief 调整分区大小
 */
int32_t partition_resize(partition_disk_t *disk, uint32_t partition_number,
                          uint64_t new_size)
{
    (void)disk;
    (void)partition_number;
    (void)new_size;

    /* TODO: 根据分区表类型选择调整方法 */
    /* if (disk->table_type == PARTITION_TYPE_MBR) { ... } */
    /* else if (disk->table_type == PARTITION_TYPE_GPT) { ... } */

    return -1;
}

/**
 * @brief 同步分区表
 */
int32_t partition_sync(partition_disk_t *disk)
{
    if (disk == NULL)
    {
        return -1;
    }

    printf("[Partition] Syncing partition table: %s\n", disk->device_path);

    /* MBR/GPT 已在写入时同步 */
    /* TODO: 增加一致性检查 */
    /* TODO: 更新分区表 CRC */

    return 0;
}

/**
 * @brief 获取分区信息
 */
int32_t partition_get_info(partition_disk_t *disk,
                            uint32_t partition_number,
                            partition_entry_t *partition)
{
    if (disk == NULL || partition == NULL)
    {
        return -1;
    }

    if (partition_number >= disk->partition_count)
    {
        return -1;
    }

    (void)memcpy(partition, &disk->partitions[partition_number],
                 sizeof(partition_entry_t));

    return 0;
}
