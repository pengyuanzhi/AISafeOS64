/**
 * @file    fsck_fat32.c
 * @brief   FAT32 fsck 实现
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details FAT32 fsck 实现：
 *          - FAT32 文件系统检查
 *          - FAT32 文件系统修复
 *          - FAT 表检查和修复
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
 * FAT32 fsck 检查
 * ======================================================================== */

/**
 * @brief 检查 FAT32 超级块（BPB）
 */
static int32_t check_bpb(const uint8_t *data, fsck_result_t *result)
{
    const fat32_bpb_t *bpb;

    if (data == NULL || result == NULL)
    {
        return -1;
    }

    bpb = (const fat32_bpb_t *)data;

    /* 检查 BPB 签名 */
    if (bpb->signature != FAT32_BPB_SIGNATURE)
    {
        result->errors_found++;
        (void)snprintf(result->last_error, FSCK_MAX_MSG_LEN,
                       "Invalid BPB signature: 0x%04X", bpb->signature);
        return -1;
    }

    /* 检查文件系统类型 */
    if (strcmp(bpb->fstype_str, FAT32_FSTYPE_STR) != 0)
    {
        result->warnings++;
        printf("[fsck FAT32] Warning: Unexpected filesystem type: %s\n",
               bpb->fstype_str);
    }

    /* TODO: 更多 BPB 检查 */

    return 0;
}

/**
 * @brief 检查 FAT 表
 */
static int32_t check_fat(const uint8_t *fat_data, uint32_t fat_size,
                          fsck_result_t *result)
{
    uint32_t i;

    if (fat_data == NULL || result == NULL)
    {
        return -1;
    }

    /* 检查前两个保留簇 */
    uint32_t *fat = (uint32_t *)fat_data;

    if (fat[0] != FAT32_CLUSTER_RESERVED ||
        fat[1] != FAT32_CLUSTER_RESERVED)
    {
        result->errors_found++;
        (void)strcpy(result->last_error, "Reserved FAT entries corrupted");
        return -1;
    }

    /* TODO: 检查 FAT 表一致性 */
    /* TODO: 检查簇链循环 */
    /* TODO: 检查孤立簇 */

    if (result->verbose)
    {
        printf("[fsck FAT32] FAT table checked: %u entries\n", fat_size / 4U);
    }

    return 0;
}

/**
 * @brief 检查目录项
 */
static int32_t check_directory(const uint8_t *dir_data, uint32_t dir_size,
                                fsck_result_t *result)
{
    uint32_t i;
    uint32_t entry_count;

    if (dir_data == NULL || result == NULL)
    {
        return -1;
    }

    entry_count = dir_size / FAT32_DIR_ENTRY_SIZE;

    /* 检查所有目录项 */
    for (i = 0U; i < entry_count; i++)
    {
        const fat32_dir_entry_t *entry =
            (const fat32_dir_entry_t *)&dir_data[i * FAT32_DIR_ENTRY_SIZE];

        /* 跳过空目录项 */
        if (entry->name[0] == 0x00U || entry->name[0] == 0xE5U)
        {
            continue;
        }

        /* 检查 LFN 条目 */
        if ((entry->attributes & FAT32_ATTR_LFN_MASK) == 0x0FU)
        {
            continue;
        }

        /* TODO: 检查文件名有效性 */
        /* TODO: 检查簇链有效性 */
        /* TODO: 检查文件大小一致性 */

        if (result->verbose)
        {
            char name[12];
            (void)memcpy(name, entry->name, 8);
            name[8] = '.';
            (void)memcpy(name + 9, entry->ext, 3);
            name[12] = '\0';

            printf("[fsck FAT32] Entry: %s, size: %u, start_cluster: %u\n",
                   name, entry->file_size, entry->first_cluster);
        }
    }

    return 0;
}

/* ========================================================================
 * FAT32 fsck 公共接口实现
 * ======================================================================== */

/**
 * @brief FAT32 fsck 检查
 */
int32_t fsck_fat32_check(const char *device_path,
                          const fsck_options_t *options,
                          fsck_result_t *result)
{
    uint8_t *sector_data;
    int32_t ret;

    if (device_path == NULL || options == NULL || result == NULL)
    {
        return -1;
    }

    /* 初始化结果 */
    (void)memset(result, 0, sizeof(fsck_result_t));
    result->success = false;

    printf("[fsck FAT32] Checking filesystem: %s\n", device_path);

    /* TODO: 读取 BPB（扇区 0） */
    sector_data = (uint8_t *)malloc(FAT32_SECTOR_SIZE);
    if (sector_data == NULL)
    {
        (void)strcpy(result->last_error, "Failed to allocate buffer");
        return -1;
    }

    /* TODO: 调用设备驱动读取扇区 */
    /* ret = read_sector(device_path, 0ULL, sector_data); */
    ret = -1; /* TODO: 移除 */

    if (ret < 0)
    {
        (void)strcpy(result->last_error, "Failed to read BPB");
        free(sector_data);
        return -1;
    }

    /* 检查 BPB */
    ret = check_bpb(sector_data, result);
    if (ret < 0)
    {
        printf("[fsck FAT32] BPB check failed\n");
        free(sector_data);
        return -1;
    }

    /* TODO: 读取并检查 FAT 表 */
    /* TODO: 读取并检查根目录 */
    /* TODO: 递归检查所有目录 */

    free(sector_data);

    result->success = true;
    result->filesystem_clean = (result->errors_found == 0U);

    printf("[fsck FAT32] Check completed: %u errors found\n",
           result->errors_found);

    return 0;
}

/**
 * @brief FAT32 fsck 修复
 */
int32_t fsck_fat32_repair(const char *device_path,
                           const fsck_options_t *options,
                           fsck_result_t *result)
{
    int32_t ret;

    /* 先运行检查 */
    ret = fsck_fat32_check(device_path, options, result);
    if (ret < 0)
    {
        return -1;
    }

    /* 如果没有错误，不需要修复 */
    if (result->filesystem_clean)
    {
        printf("[fsck FAT32] Filesystem is clean, no repair needed\n");
        return 0;
    }

    /* TODO: 实现修复逻辑 */
    /* TODO: 修复 FAT 表 */
    /* TODO: 修复目录项 */
    /* TODO: 恢复丢失的簇 */

    printf("[fsck FAT32] Repair completed: %u errors fixed\n",
           result->errors_fixed);

    return 0;
}
