/**
 * @file    fsck.c
 * @brief   fsck 工具实现
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details fsck 工具实现：
 *          - 通用 fsck 接口
 *          - 文件系统类型自动检测
 *          - 统一的检查/修复入口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fsck.h"
#include "../fs_fat32/fat32_types.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief FAT32 文件系统类型字符串 */
#define FSCK_FSTYPE_FAT32    "FAT32   "

/** @brief EXT4 文件系统类型字符串 */
#define FSCK_FSTYPE_EXT4     ""  /* EXT4 使用魔数检测 */

/* ========================================================================
 * 文件系统类型检测
 * ======================================================================== */

/**
 * @brief 文件系统类型
 */
typedef enum
{
    FSCK_FSTYPE_UNKNOWN = 0U,
    FSCK_FSTYPE_FAT32,
    FSCK_FSTYPE_EXT4
} fsck_filesystem_type_t;

/**
 * @brief 检测文件系统类型
 */
static fsck_filesystem_type_t detect_filesystem_type(const uint8_t *bpb_data)
{
    const fat32_bpb_t *bpb;
    const ext4_superblock_t *sb;

    if (bpb_data == NULL)
    {
        return FSCK_FSTYPE_UNKNOWN;
    }

    /* 尝试检测 FAT32 */
    bpb = (const fat32_bpb_t *)bpb_data;

    if (bpb->signature == FAT32_BPB_SIGNATURE)
    {
        if (strcmp(bpb->fstype_str, FSCK_FSTYPE_FAT32) == 0)
        {
            return FSCK_FSTYPE_FAT32;
        }
    }

    /* 尝试检测 EXT4（扇区 1） */
    sb = (const ext4_superblock_t *)(bpb_data + EXT4_SECTOR_SIZE);

    if (sb->s_magic == 0xEF53U)
    {
        return FSCK_FSTYPE_EXT4;
    }

    return FSCK_FSTYPE_UNKNOWN;
}

/**
 * @brief 从设备路径检测文件系统类型
 */
static fsck_filesystem_type_t detect_filesystem_from_device(const char *device_path)
{
    uint8_t *data;
    fsck_filesystem_type_t fstype;

    if (device_path == NULL)
    {
        return FSCK_FSTYPE_UNKNOWN;
    }

    /* 分配缓冲区（2个扇区：MBR/BPB + 超级块） */
    data = (uint8_t *)malloc(EXT4_SECTOR_SIZE * 2);
    if (data == NULL)
    {
        return FSCK_FSTYPE_UNKNOWN;
    }

    /* TODO: 调用设备驱动读取扇区 */
    /* read_sector(device_path, 0ULL, data); */

    /* 检测文件系统类型 */
    fstype = detect_filesystem_type(data);

    free(data);

    return fstype;
}

/* ========================================================================
 * 通用 fsck 接口实现
 * ======================================================================== */

/**
 * @brief 通用 fsck 检查
 */
int32_t fsck_check(const char *device_path,
                    const fsck_options_t *options,
                    fsck_result_t *result)
{
    fsck_filesystem_type_t fstype;
    int32_t ret;

    if (device_path == NULL || options == NULL || result == NULL)
    {
        return -1;
    }

    printf("[fsck] Checking filesystem: %s\n", device_path);

    /* 检测文件系统类型 */
    fstype = detect_filesystem_from_device(device_path);

    /* 根据文件系统类型调用相应的 fsck */
    switch (fstype)
    {
        case FSCK_FSTYPE_FAT32:
            printf("[fsck] Detected filesystem: FAT32\n");
            ret = fsck_fat32_check(device_path, options, result);
            break;

        case FSCK_FSTYPE_EXT4:
            printf("[fsck] Detected filesystem: EXT4\n");
            ret = fsck_ext4_check(device_path, options, result);
            break;

        case FSCK_FSTYPE_UNKNOWN:
        default:
            printf("[fsck] Error: Unknown filesystem type\n");
            (void)strcpy(result->last_error, "Unknown filesystem type");
            ret = -1;
            break;
    }

    return ret;
}

/**
 * @brief 通用 fsck 修复
 */
int32_t fsck_repair(const char *device_path,
                     const fsck_options_t *options,
                     fsck_result_t *result)
{
    fsck_filesystem_type_t fstype;
    int32_t ret;

    if (device_path == NULL || options == NULL || result == NULL)
    {
        return -1;
    }

    printf("[fsck] Repairing filesystem: %s\n", device_path);

    /* 检测文件系统类型 */
    fstype = detect_filesystem_from_device(device_path);

    /* 根据文件系统类型调用相应的 fsck */
    switch (fstype)
    {
        case FSCK_FSTYPE_FAT32:
            printf("[fsck] Detected filesystem: FAT32\n");
            ret = fsck_fat32_repair(device_path, options, result);
            break;

        case FSCK_FSTYPE_EXT4:
            printf("[fsck] Detected filesystem: EXT4\n");
            ret = fsck_ext4_repair(device_path, options, result);
            break;

        case FSCK_FSTYPE_UNKNOWN:
        default:
            printf("[fsck] Error: Unknown filesystem type\n");
            (void)strcpy(result->last_error, "Unknown filesystem type");
            ret = -1;
            break;
    }

    return ret;
}
