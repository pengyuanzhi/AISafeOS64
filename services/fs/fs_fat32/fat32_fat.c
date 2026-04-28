/**
 * @file    fat32_fat.c
 * @brief   FAT32 FAT 表操作实现
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 FAT 表操作实现：
 *          - FAT 表项读取和写入（支持主表和副本）
 *          - 空闲簇线性扫描分配
 *          - 簇释放标记为 FREE
 *          - 簇链追踪直到 EOC
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fat32_fat.h"
#include <string.h>
#include <stdint.h>

/* ========================================================================
 * FAT 表操作实现
 * ======================================================================== */

/**
 * @brief 读取 FAT 表项
 */
int32_t fat32_fat_read_entry(fat32_context_t *ctx, uint32_t cluster,
                              uint32_t *value)
{
    uint32_t sec_num;
    uint32_t offset;
    uint32_t entries_per_sec;
    int32_t ret;

    if ((ctx == NULL) || (value == NULL))
    {
        return -22; /* -EINVAL */
    }

    if (!ctx->mounted)
    {
        return -22; /* -EINVAL */
    }

    if (cluster > (ctx->total_clusters + 1U))
    {
        return -22; /* -EINVAL */
    }

    /* 每个 FAT 表项 4 字节 */
    entries_per_sec = ctx->bytes_per_sec / 4U;
    sec_num = ctx->rsvd_sec_cnt + (cluster / entries_per_sec);
    offset  = (cluster % entries_per_sec) * 4U;

    /* 读取 FAT 扇区 */
    ret = ctx->block_read((uint64_t)sec_num, ctx->sector_buf, 1U);
    if (ret != 0)
    {
        return -5; /* -EIO */
    }

    /* 提取 32 位 FAT 表项（掩码保留低 28 位） */
    {
        uint32_t raw_val;
        (void)memcpy(&raw_val, &ctx->sector_buf[offset], sizeof(uint32_t));
        *value = raw_val & 0x0FFFFFFFU;
    }

    return 0;
}

/**
 * @brief 写入 FAT 表项
 */
int32_t fat32_fat_write_entry(fat32_context_t *ctx, uint32_t cluster,
                               uint32_t value)
{
    uint32_t sec_num;
    uint32_t offset;
    uint32_t entries_per_sec;
    uint32_t fat_idx;
    int32_t ret;

    if (ctx == NULL)
    {
        return -22; /* -EINVAL */
    }

    if (!ctx->mounted)
    {
        return -22; /* -EINVAL */
    }

    if (cluster > (ctx->total_clusters + 1U))
    {
        return -22; /* -EINVAL */
    }

    /* 掩码保留低 28 位 */
    value &= 0x0FFFFFFFU;

    /* 计算 FAT 表项位置 */
    entries_per_sec = ctx->bytes_per_sec / 4U;
    sec_num = ctx->rsvd_sec_cnt + (cluster / entries_per_sec);
    offset  = (cluster % entries_per_sec) * 4U;

    /* 更新所有 FAT 副本 */
    for (fat_idx = 0U; fat_idx < ctx->num_fats; fat_idx++)
    {
        uint32_t target_sec = sec_num + (fat_idx * ctx->fat_sz32);

        /* 读取当前扇区 */
        ret = ctx->block_read((uint64_t)target_sec, ctx->sector_buf, 1U);
        if (ret != 0)
        {
            return -5; /* -EIO */
        }

        /* 修改 FAT 表项（保留高 4 位） */
        {
            uint32_t old_val;
            (void)memcpy(&old_val, &ctx->sector_buf[offset], sizeof(uint32_t));
            old_val = (old_val & 0xF0000000U) | value;
            (void)memcpy(&ctx->sector_buf[offset], &old_val, sizeof(uint32_t));
        }

        /* 写回扇区 */
        ret = ctx->block_write((uint64_t)target_sec, ctx->sector_buf, 1U);
        if (ret != 0)
        {
            return -5; /* -EIO */
        }
    }

    return 0;
}

/**
 * @brief 分配一个空闲簇
 */
int32_t fat32_alloc_cluster(fat32_context_t *ctx, uint32_t *cluster)
{
    uint32_t i;
    uint32_t value;
    int32_t ret;

    if ((ctx == NULL) || (cluster == NULL))
    {
        return -22; /* -EINVAL */
    }

    /* 从簇 2 开始线性扫描 */
    for (i = 2U; i <= (ctx->total_clusters + 1U); i++)
    {
        ret = fat32_fat_read_entry(ctx, i, &value);
        if (ret != 0)
        {
            return ret;
        }

        if (fat32_is_free(value))
        {
            /* 标记为 EOC */
            ret = fat32_fat_write_entry(ctx, i, FAT32_CLUSTER_EOC);
            if (ret != 0)
            {
                return ret;
            }

            *cluster = i;
            return 0;
        }
    }

    return -12; /* -ENOMEM */
}

/**
 * @brief 释放一个簇
 */
int32_t fat32_free_cluster(fat32_context_t *ctx, uint32_t cluster)
{
    if (ctx == NULL)
    {
        return -22; /* -EINVAL */
    }

    if (cluster < 2U)
    {
        return -22; /* -EINVAL */
    }

    return fat32_fat_write_entry(ctx, cluster, FAT32_CLUSTER_FREE);
}

/**
 * @brief 获取簇链
 */
int32_t fat32_get_cluster_chain(fat32_context_t *ctx, uint32_t start,
                                 uint32_t *chain, uint32_t max_len,
                                 uint32_t *out_len)
{
    uint32_t current;
    uint32_t value;
    uint32_t count;
    int32_t ret;

    if ((ctx == NULL) || (chain == NULL) || (out_len == NULL))
    {
        return -22; /* -EINVAL */
    }

    if (max_len == 0U)
    {
        return -22; /* -EINVAL */
    }

    *out_len = 0U;
    count = 0U;
    current = start;

    for (;;)
    {
        if (current < 2U)
        {
            return -22; /* -EINVAL */
        }

        if (count >= max_len)
        {
            *out_len = count;
            return 0;
        }

        chain[count] = current;
        count++;

        ret = fat32_fat_read_entry(ctx, current, &value);
        if (ret != 0)
        {
            return ret;
        }

        if (fat32_is_eoc(value))
        {
            break;
        }

        if (fat32_is_bad(value))
        {
            return -5; /* -EIO */
        }

        current = value;
    }

    *out_len = count;
    return 0;
}
