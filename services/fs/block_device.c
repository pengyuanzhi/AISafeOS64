/**
 * @file    block_device.c
 * @brief   块设备驱动实现
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @brief 块设备驱动实现：扇区读取/写入
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "block_device.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大设备数量 */
#define BLOCK_MAX_DEVICES    16U

/* ========================================================================
 * 块设备表
 * ======================================================================== */

static block_device_t s_devices[BLOCK_MAX_DEVICES];

static bool s_initialized = false;

/* ========================================================================
 * 常见块设备路径
 * ======================================================================== */

/** @brief 常见块设备路径 */
static const char *g_common_disk_devices[] = {
    "/dev/sda",
    "/dev/sdb",
    "/dev/sdc",
    "/dev/nvme0n1",
    "/dev/nvme1n1",
    "/dev/mmcblk0",
    "/dev/mmcblk1",
    NULL  /* 结束标记 */
};

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 初始化块设备管理
 */
static void block_device_init(void)
{
    uint32_t i;

    if (!s_initialized)
    {
        for (i = 0U; i < BLOCK_MAX_DEVICES; i++)
        {
            (void)memset(&s_devices[i], 0, sizeof(block_device_t));
        }

        s_initialized = true;
    }
}

/**
 * @brief 分配块设备槽位
 */
static block_device_t *alloc_device(void)
{
    uint32_t i;

    for (i = 0U; i < BLOCK_MAX_DEVICES; i++)
    {
        if (!s_devices[i].in_use)
        {
            (void)memset(&s_devices[i], 0, sizeof(block_device_t));
            return &s_devices[i];
        }
    }

    return NULL;
}

/* ========================================================================
 * 块设备管理接口实现
 * ======================================================================== */

/**
 * @brief 打开块设备
 */
block_device_t *block_open(const char *device_path)
{
    block_device_t *device;
    uint64_t total_sectors;

    block_device_init();

    if (device_path == NULL)
    {
        return NULL;
    }

    /* 检查设备路径长度 */
    if (strlen(device_path) >= BLOCK_MAX_PATH_LEN)
    {
        return NULL;
    }

    /* 分配设备槽位 */
    device = alloc_device();
    if (device == NULL)
    {
        return NULL;
    }

    /* 填充设备信息 */
    (void)strncpy(device->device_path, device_path, BLOCK_MAX_PATH_LEN - 1U);
    device->device_path[BLOCK_MAX_PATH_LEN - 1U] = '\0';
    device->type = BLOCK_DEV_TYPE_DISK;
    device->sector_size = BLOCK_SECTOR_SIZE;
    device->in_use = true;
    device->ref_count = 1U;

    /* TODO: 通过 ioctl 获取设备大小 */
    /* total_sectors = device->ops->get_size(device); */
    total_sectors = 0ULL; /* TODO: 移除 */

    if (total_sectors > 0ULL)
    {
        device->total_sectors = total_sectors;
    }
    else
    {
        /* 默认大小：8GB */
        device->total_sectors = (8ULL * 1024ULL * 1024ULL * 1024ULL) / BLOCK_SECTOR_SIZE;
    }

    printf("[Block Device] Opened: %s, size: %llu sectors (%.2f GB)\n",
           device_path, device->total_sectors,
           (double)device->total_sectors * BLOCK_SECTOR_SIZE / (1024.0 * 1024.0 * 1024.0));

    return device;
}

/**
 * @brief 关闭块设备
 */
int32_t block_close(block_device_t *device)
{
    if (device == NULL)
    {
        return -1;
    }

    device->ref_count--;

    if (device->ref_count == 0U)
    {
        device->in_use = false;
        printf("[Block Device] Closed: %s\n", device->device_path);
    }

    return 0;
}

/**
 * @brief 读取单个扇区
 */
int32_t block_read_sector(const char *device_path, uint64_t lba, uint8_t *buf)
{
    block_device_t *device;
    int64_t sectors_read;

    device = block_open(device_path);
    if (device == NULL)
    {
        return -1;
    }

    sectors_read = device->ops->read(device, lba, buf);

    (void)block_close(device);

    if (sectors_read < 0)
    {
        return (int32_t)sectors_read;
    }

    return 0;
}

/**
 * @brief 写入单个扇区
 */
int32_t block_write_sector(const char *device_path, uint64_t lba, const uint8_t *buf)
{
    block_device_t *device;
    int64_t sectors_written;

    device = block_open(device_path);
    if (device == NULL)
    {
        return -1;
    }

    sectors_written = device->ops->write(device, lba, buf);

    (void)block_close(device);

    if (sectors_written < 0)
    {
        return (int32_t)sectors_written;
    }

    return 0;
}

/**
 * @brief 读取多扇区
 */
int64_t block_read_sectors(const char *device_path, uint64_t lba, uint8_t *buf, uint32_t count)
{
    block_device_t *device;
    int64_t sectors_read;

    device = block_open(device_path);
    if (device == NULL)
    {
        return -1;
    }

    sectors_read = device->ops->read_multiple(device, lba, buf, count);

    (void)block_close(device);

    return sectors_read;
}

/**
 * @brief 写入多扇区
 */
int64_t block_write_sectors(const char *device_path, uint64_t lba, const uint8_t *buf, uint32_t count)
{
    block_device_t *device;
    int64_t sectors_written;

    device = block_open(device_path);
    if (device == NULL)
    {
        return -1;
    }

    sectors_written = device->ops->write_multiple(device, lba, buf, count);

    (void)block_close(device);

    return sectors_written;
}
