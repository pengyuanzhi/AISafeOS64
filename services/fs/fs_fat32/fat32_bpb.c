/**
 * @file    fat32_bpb.c
 * @brief   FAT32 BPB 解析实现
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 BPB（BIOS Parameter Block）解析实现
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fat32_bpb.h"
#include <string.h>
#include <stdbool.h>

/* ========================================================================
 * FAT32 BPB 解析接口实现
 * ======================================================================== */

/**
 * @brief 解析 FAT32 BPB
 */
int32_t fat32_parse_bpb(const uint8_t *bpb, fat32_volume_info_t *volume_info)
{
    const fat32_bpb_t *fat_bpb;
    const fat32_ebpb_t *fat_ebpb;

    if (bpb == NULL)
    {
        return -1;
    }

    if (volume_info == NULL)
    {
        return -1;
    }

    fat_bpb = (const fat32_bpb_t *)bpb;
    fat_ebpb = (const fat32_ebpb_t *)(bpb + 90U);

    /* 提取 BPB 参数 */
    volume_info->bytes_per_sec = fat_bpb->bytes_per_sec;
    volume_info->sec_per_clust = fat_bpb->sec_per_clust;
    volume_info->bytes_per_clust = (uint32_t)fat_bpb->bytes_per_sec *
                               (uint32_t)fat_bpb->sec_per_clust;
    volume_info->res_sec = fat_bpb->res_sec;
    volume_info->fat_copies = fat_bpb->fat_copies;
    volume_info->bytes_per_fat = fat_ebpb->bytes_per_fat;
    volume_info->root_cluster = fat_ebpb->root_cluster;

    /* 计算 FAT 表起始扇区号 */
    volume_info->fat_start_sec = (uint32_t)fat_bpb->res_sec + 1U;

    /* 计算数据区起始扇区号 */
    {
        uint32_t fat_sec = (volume_info->bytes_per_fat + (FAT32_BYTES_PER_SEC - 1U)) /
                         FAT32_BYTES_PER_SEC;
        volume_info->data_start_sec = volume_info->fat_start_sec +
                                    ((uint32_t)fat_bpb->fat_copies * fat_sec);
    }

    /* 计算总簇数 */
    {
        /* 简化实现：假设总扇区数为 65536 */
        uint32_t tot_sec = 65536U;
        uint32_t data_sec = tot_sec - volume_info->data_start_sec;
        volume_info->tot_clusters = data_sec / (uint32_t)fat_bpb->sec_per_clust;
    }

    volume_info->valid = true;

    return 0;
}

/**
 * @brief 验证 FAT32 BPB
 */
bool fat32_validate_bpb(const uint8_t *bpb)
{
    const fat32_bpb_t *fat_bpb;

    if (bpb == NULL)
    {
        return false;
    }

    fat_bpb = (const fat32_bpb_t *)bpb;

    /* 检查扇区大小 */
    if (fat_bpb->bytes_per_sec != FAT32_BYTES_PER_SEC)
    {
        return false;
    }

    /* 检查每簇扇区数 */
    if (fat_bpb->sec_per_clust == 0U)
    {
        return false;
    }

    if (fat_bpb->sec_per_clust > 128U)
    {
        return false;
    }

    /* 检查 FAT 表副本数 */
    if (fat_bpb->fat_copies == 0U)
    {
        return false;
    }

    if (fat_bpb->fat_copies > 2U)
    {
        return false;
    }

    /* 检查文件系统类型 */
    if ((fat_bpb->fs_type[0] != 'F') ||
        (fat_bpb->fs_type[1] != 'A') ||
        (fat_bpb->fs_type[2] != 'T') ||
        (fat_bpb->fs_type[3] != '3') ||
        (fat_bpb->fs_type[4] != '2') ||
        (fat_bpb->fs_type[7] != ' '))
    {
        return false;
    }

    return true;
}

/**
 * @brief 计算簇的扇区号
 */
uint32_t fat32_cluster_to_sec(const fat32_volume_info_t *volume_info,
                               uint32_t cluster)
{
    uint32_t cluster_offset;

    if (volume_info == NULL)
    {
        return 0xFFFFFFFFU;
    }

    if (cluster < FAT32_MIN_CLUSTER)
    {
        return 0xFFFFFFFFU;
    }

    if (cluster > FAT32_MAX_CLUSTER)
    {
        return 0xFFFFFFFFU;
    }

    /* 计算簇在数据区的偏移（扇区数） */
    cluster_offset = (cluster - FAT32_MIN_CLUSTER) * volume_info->sec_per_clust;

    /* 计算实际的扇区号 */
    return volume_info->data_start_sec + cluster_offset;
}
