/**
 * @file    fat32_fat.c
 * @brief   FAT32 FAT 表和簇管理实现
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 FAT 表解析和簇管理实现（简化版）
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fat32_fat.h"
#include <string.h>
#include <stdbool.h>

/* ========================================================================
 * FAT 表上下文（简化版）
 * ======================================================================== */

/** @brief FAT 表缓存（简化版：仅用于测试） */
static uint32_t s_fat_cache[1024U];

/** @brief FAT 表初始化标志 */
static bool s_fat_initialized = false;

/* ========================================================================
 * FAT 表初始化
 * ======================================================================== */

/**
 * @brief 初始化 FAT 表上下文
 */
int32_t fat32_fat_init(fat32_fat_context_t *context,
                       const fat32_volume_info_t *volume_info)
{
    if (context == NULL)
    {
        return -1;
    }

    if (volume_info == NULL)
    {
        return -1;
    }

    /* 初始化上下文 */
    context->volume_info = volume_info;
    context->free_clusters = volume_info->tot_clusters;
    context->next_free = FAT32_MIN_CLUSTER;
    context->initialized = true;

    /* 初始化 FAT 表缓存（简化版） */
    if (!s_fat_initialized)
    {
        uint32_t i;

        for (i = 0U; i < 1024U; i++)
        {
            s_fat_cache[i] = FAT32_FREE_CLUSTER;
        }

        s_fat_initialized = true;
    }

    return 0;
}

/* ========================================================================
 * FAT 表项读写
 * ======================================================================== */

/**
 * @brief 读取 FAT 表项
 */
uint32_t fat32_read_fat_entry(const fat32_fat_context_t *context,
                                uint32_t cluster)
{
    if (context == NULL)
    {
        return 0xFFFFFFFFU;
    }

    if (!context->initialized)
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

    /* TODO: 从实际设备读取 FAT 表项 */
    /* 简化实现：从缓存读取 */
    if (cluster < 1024U)
    {
        return s_fat_cache[cluster];
    }

    return FAT32_EOC_CLUSTER;
}

/**
 * @brief 写入 FAT 表项
 */
int32_t fat32_write_fat_entry(const fat32_fat_context_t *context,
                                uint32_t cluster,
                                uint32_t value)
{
    if (context == NULL)
    {
        return -1;
    }

    if (!context->initialized)
    {
        return -1;
    }

    if (cluster < FAT32_MIN_CLUSTER)
    {
        return -1;
    }

    if (cluster > FAT32_MAX_CLUSTER)
    {
        return -1;
    }

    /* TODO: 写入实际设备的 FAT 表项 */
    /* 简化实现：写入缓存 */
    if (cluster < 1024U)
    {
        s_fat_cache[cluster] = value;
        return 0;
    }

    return -1;
}

/* ========================================================================
 * 簇状态检查
 * ======================================================================== */

/**
 * @brief 检查簇是否是链的末尾
 */
bool fat32_is_eoc(uint32_t cluster)
{
    /* FAT32 EOC 值范围：0x0FFFFFF8 - 0x0FFFFFFF */
    return (cluster >= 0x0FFFFFF8U) && (cluster <= 0x0FFFFFFFU);
}

/**
 * @brief 检查簇是否损坏
 */
bool fat32_is_bad(uint32_t cluster)
{
    return (cluster == FAT32_BAD_CLUSTER);
}

/* ========================================================================
 * 簇分配和释放
 * ======================================================================== */

/**
 * @brief 分配一个自由簇
 */
uint32_t fat32_alloc_cluster(fat32_fat_context_t *context)
{
    uint32_t cluster;

    if (context == NULL)
    {
        return 0xFFFFFFFFU;
    }

    if (!context->initialized)
    {
        return 0xFFFFFFFFU;
    }

    if (context->free_clusters == 0U)
    {
        return 0xFFFFFFFFU;
    }

    /* 从 next_free 开始查找自由簇 */
    cluster = context->next_free;

    while (cluster <= context->volume_info->tot_clusters)
    {
        uint32_t fat_entry;

        fat_entry = fat32_read_fat_entry(context, cluster);

        if (fat_entry == FAT32_FREE_CLUSTER)
        {
            /* 找到自由簇 */
            if (fat32_write_fat_entry(context, cluster, FAT32_EOC_CLUSTER) == 0)
            {
                context->free_clusters--;
                context->next_free = cluster + 1U;
                return cluster;
            }
        }

        cluster++;
    }

    /* 磁盘已满 */
    return 0xFFFFFFFFU;
}

/**
 * @brief 释放簇
 */
int32_t fat32_free_cluster(fat32_fat_context_t *context,
                           uint32_t cluster)
{
    if (context == NULL)
    {
        return -1;
    }

    if (!context->initialized)
    {
        return -1;
    }

    if (cluster < FAT32_MIN_CLUSTER)
    {
        return -1;
    }

    if (cluster > FAT32_MAX_CLUSTER)
    {
        return -1;
    }

    /* 释放簇：写入 FREE 标记 */
    if (fat32_write_fat_entry(context, cluster, FAT32_FREE_CLUSTER) == 0)
    {
        context->free_clusters++;
        return 0;
    }

    return -1;
}
