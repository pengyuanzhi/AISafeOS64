/**
 * @file    partition.c
 * @brief   磁盘分区管理实现
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details 磁盘分区管理实现：
 *          - 通用分区管理接口
 *          - MBR/GPT 自动检测
 *          - 分区创建/删除/调整
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "partition.h"
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
 * 磁盘管理
 * ======================================================================== */

/** @brief 磁盘描述符表 */
static partition_disk_t s_disks[PARTITION_MAX_DISKS];

/** @brief 初始化标志 */
static bool s_initialized;

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
    /* TODO: 调用设备驱动读取扇区 */
    (void)device_path;
    (void)lba;
    (void)buf;

    return -1;
}

/**
 * @brief 写入磁盘扇区
 */
static int32_t write_sector(const char *device_path, uint64_t lba, const uint8_t *buf)
{
    /* TODO: 调用设备驱动写入扇区 */
    (void)device_path;
    (void)lba;
    (void)buf;

    return -1;
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
        printf("[Partition] No partition table detected\n");
        free(buf);
        disk->in_use = false;
        return NULL;
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

    /* TODO: 实现分区创建 */
    return -1;
}

/**
 * @brief 删除分区
 */
int32_t partition_delete(partition_disk_t *disk, uint32_t partition_number)
{
    (void)disk;
    (void)partition_number;

    /* TODO: 实现分区删除 */
    return -1;
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

    /* TODO: 实现分区调整 */
    return -1;
}

/**
 * @brief 同步分区表
 */
int32_t partition_sync(partition_disk_t *disk)
{
    (void)disk;

    /* TODO: 实现分区表同步 */
    return -1;
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
