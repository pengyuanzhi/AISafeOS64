/**
 * @file    fat32_bpb.c
 * @brief   FAT32 BPB 解析实现
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 BPB 解析实现：
 *          - 从扇区 0 读取引导扇区
 *          - 验证 BPB 签名和关键字段
 *          - 计算数据区起始扇区和总簇数
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fat32_bpb.h"
#include <string.h>
#include <stdint.h>

/* ========================================================================
 * BPB 验证辅助函数
 * ======================================================================== */

/** @brief 有效的扇区大小值 */
static const uint16_t s_valid_sector_sizes[] =
{
    512U, 1024U, 2048U, 4096U
};

/** @brief 有效扇区大小数量 */
#define VALID_SECTOR_SIZES_COUNT  4U

/** @brief 有效的每簇扇区数 */
static const uint8_t s_valid_sec_per_clust[] =
{
    1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U
};

/** @brief 有效每簇扇区数数量 */
#define VALID_SEC_PER_CLUST_COUNT  8U

/**
 * @brief 验证扇区大小是否有效
 *
 * @param bytes_per_sec 每扇区字节数
 *
 * @return true 有效，false 无效
 */
static bool is_valid_sector_size(uint16_t bytes_per_sec)
{
    uint32_t i;
    bool valid = false;

    for (i = 0U; i < VALID_SECTOR_SIZES_COUNT; i++)
    {
        if (bytes_per_sec == s_valid_sector_sizes[i])
        {
            valid = true;
            break;
        }
    }

    return valid;
}

/**
 * @brief 验证每簇扇区数是否有效
 *
 * @param sec_per_clust 每簇扇区数
 *
 * @return true 有效，false 无效
 */
static bool is_valid_sec_per_clust(uint8_t sec_per_clust)
{
    uint32_t i;
    bool valid = false;

    for (i = 0U; i < VALID_SEC_PER_CLUST_COUNT; i++)
    {
        if (sec_per_clust == s_valid_sec_per_clust[i])
        {
            valid = true;
            break;
        }
    }

    return valid;
}

/* ========================================================================
 * BPB 解析实现
 * ======================================================================== */

/**
 * @brief 解析 FAT32 BPB
 */
int32_t fat32_bpb_parse(fat32_context_t *ctx)
{
    fat32_bpb_t *bpb;
    int32_t ret;
    uint32_t data_sectors;

    /* 参数验证 */
    if (ctx == NULL)
    {
        return -22; /* -EINVAL */
    }

    if (ctx->block_read == NULL)
    {
        return -22; /* -EINVAL */
    }

    /* 读取引导扇区（扇区 0）到缓冲区 */
    ret = ctx->block_read(0ULL, ctx->sector_buf, 1U);
    if (ret != 0)
    {
        return -5; /* -EIO */
    }

    /* 检查引导扇区签名（偏移 510-511） */
    if ((ctx->sector_buf[510] != 0x55U) ||
        (ctx->sector_buf[511] != 0xAAU))
    {
        return -22; /* -EINVAL: 无效签名 */
    }

    /* 强制转换为 BPB 结构 */
    bpb = (fat32_bpb_t *)ctx->sector_buf;

    /* 验证关键字段 */
    if (!is_valid_sector_size(bpb->bytes_per_sec))
    {
        return -22; /* -EINVAL */
    }

    if (!is_valid_sec_per_clust(bpb->sec_per_clust))
    {
        return -22; /* -EINVAL */
    }

    if (bpb->fat_sz32 == 0U)
    {
        return -22; /* -EINVAL */
    }

    if (bpb->root_cluster < 2U)
    {
        return -22; /* -EINVAL */
    }

    /* 计算并填充上下文参数 */
    ctx->bytes_per_sec  = (uint32_t)bpb->bytes_per_sec;
    ctx->sec_per_clust  = (uint32_t)bpb->sec_per_clust;
    ctx->cluster_size   = ctx->bytes_per_sec * ctx->sec_per_clust;
    ctx->rsvd_sec_cnt   = (uint32_t)bpb->rsvd_sec_cnt;
    ctx->num_fats       = (uint32_t)bpb->num_fats;
    ctx->fat_sz32       = bpb->fat_sz32;
    ctx->root_cluster   = bpb->root_cluster;

    /* 总扇区数：优先使用 32 位值 */
    if (bpb->total_sec32 != 0U)
    {
        ctx->total_sectors = bpb->total_sec32;
    }
    else
    {
        ctx->total_sectors = (uint32_t)bpb->total_sec16;
    }

    /* 数据区起始扇区 = 保留扇区 + FAT表数 * FAT表大小 */
    ctx->data_sec_start = ctx->rsvd_sec_cnt +
                          (ctx->num_fats * ctx->fat_sz32);

    /* 总簇数 = (总扇区数 - 数据区起始) / 每簇扇区数 */
    if (ctx->total_sectors > ctx->data_sec_start)
    {
        data_sectors = ctx->total_sectors - ctx->data_sec_start;
        ctx->total_clusters = data_sectors / ctx->sec_per_clust;
    }
    else
    {
        ctx->total_clusters = 0U;
    }

    ctx->mounted = true;

    return 0;
}
