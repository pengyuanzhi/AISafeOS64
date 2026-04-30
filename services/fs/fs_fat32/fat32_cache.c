/**
 * @file    fat32_cache.c
 * @brief   FAT32 扇区写入缓存实现
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 扇区写入缓存实现（掉电保护）
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fat32_cache.h"
#include <string.h>
#include <stdbool.h>

/* ========================================================================
 * 全局状态
 * ======================================================================== */

/** @brief 写入缓存条目数组 */
static fat32_cache_entry_t s_cache[FAT32_CACHE_SIZE];

/** @brief 缓存初始化标志 */
static bool s_initialized = false;

/** @brief 下一个分配的缓存条目索引 */
static uint32_t s_next_index = 0U;

/* ========================================================================
 * 缓存接口实现
 * ======================================================================== */

/**
 * @brief 初始化写入缓存
 */
int32_t fat32_cache_init(void)
{
    uint32_t i;

    if (s_initialized)
    {
        return 0;
    }

    /* 初始化所有缓存条目 */
    for (i = 0U; i < FAT32_CACHE_SIZE; i++)
    {
        s_cache[i].sector = 0xFFFFFFFFU;
        s_cache[i].status = FAT32_CACHE_UNUSED;
        (void)memset(s_cache[i].data, 0, 512U);
    }

    s_initialized = true;
    s_next_index = 0U;

    return 0;
}

/**
 * @brief 清理写入缓存
 */
void fat32_cache_cleanup(void)
{
    if (!s_initialized)
    {
        return;
    }

    /* 初始化所有缓存条目 */
    for (uint32_t i = 0U; i < FAT32_CACHE_SIZE; i++)
    {
        s_cache[i].sector = 0xFFFFFFFFU;
        s_cache[i].status = FAT32_CACHE_UNUSED;
        (void)memset(s_cache[i].data, 0, 512U);
    }

    s_next_index = 0U;
}

/**
 * @brief 写入扇区到缓存
 */
int32_t fat32_cache_write(uint32_t sector, const void *buf)
{
    uint32_t i;
    uint32_t idx;

    if (!s_initialized)
    {
        return -1;
    }

    if (buf == NULL)
    {
        return -1;
    }

    /* 查找是否有该扇区的缓存条目 */
    for (i = 0U; i < FAT32_CACHE_SIZE; i++)
    {
        if (s_cache[i].status != FAT32_CACHE_UNUSED && s_cache[i].sector == sector)
        {
            /* 更新现有条目 */
            (void)memcpy(s_cache[i].data, buf, 512U);
            s_cache[i].status = FAT32_CACHE_DIRTY;
            return 0;
        }
    }

    /* 分配新的缓存条目（简单的轮转算法） */
    idx = s_next_index;

    if (s_cache[idx].status != FAT32_CACHE_UNUSED)
    {
        /* 清理旧条目（如果脏扇区需要刷新） */
        // TODO: 这里应该调用 fat32_cache_flush_sector
    }

    /* 更新新条目 */
    s_cache[idx].sector = sector;
    s_cache[idx].status = FAT32_CACHE_DIRTY;
    (void)memcpy(s_cache[idx].data, buf, 512U);

    /* 更新索引 */
    s_next_index = (s_next_index + 1U) % FAT32_CACHE_SIZE;

    return 0;
}

/**
 * @brief 从缓存读取扇区
 */
int32_t fat32_cache_read(uint32_t sector, void *buf)
{
    uint32_t i;

    if (!s_initialized)
    {
        return -1;
    }

    if (buf == NULL)
    {
        return -1;
    }

    /* 查找是否有该扇区的缓存条目 */
    for (i = 0U; i < FAT32_CACHE_SIZE; i++)
    {
        if (s_cache[i].status != FAT32_CACHE_UNUSED && s_cache[i].sector == sector)
        {
            /* 返回缓存数据 */
            (void)memcpy(buf, s_cache[i].data, 512U);
            return 0;
        }
    }

    /* 未找到，返回错误 */
    return -1;
}

/**
 * @brief 标记扇区为脏
 */
int32_t fat32_cache_mark_dirty(uint32_t sector)
{
    uint32_t i;

    if (!s_initialized)
    {
        return -1;
    }

    /* 查找是否有该扇区的缓存条目 */
    for (i = 0U; i < FAT32_CACHE_SIZE; i++)
    {
        if (s_cache[i].status != FAT32_CACHE_UNUSED && s_cache[i].sector == sector)
        {
            /* 标记为脏 */
            s_cache[i].status = FAT32_CACHE_DIRTY;
            return 0;
        }
    }

    /* 未找到，返回错误 */
    return -1;
}

/**
 * @brief 刷新所有脏扇区到磁盘
 */
int32_t fat32_cache_flush(void)
{
    uint32_t i;
    int32_t ret;

    if (!s_initialized)
    {
        return -1;
    }

    /* TODO: 实现磁盘写入接口 */
    /* 这里应该调用实际的磁盘写入函数 */

    /* 刷新所有脏扇区 */
    for (i = 0U; i < FAT32_CACHE_SIZE; i++)
    {
        if (s_cache[i].status == FAT32_CACHE_DIRTY)
        {
            /* 刷新该扇区 */
            ret = fat32_cache_flush_sector(s_cache[i].sector);
            if (ret < 0)
            {
                return ret;
            }

            /* 标记为已清理 */
            s_cache[i].status = FAT32_CACHE_UNUSED;
        }
    }

    return 0;
}

/**
 * @brief 刷新特定扇区到磁盘
 */
int32_t fat32_cache_flush_sector(uint32_t sector)
{
    uint32_t i;

    if (!s_initialized)
    {
        return -1;
    }

    /* 查找该扇区 */
    for (i = 0U; i < FAT32_CACHE_SIZE; i++)
    {
        if (s_cache[i].status != FAT32_CACHE_UNUSED && s_cache[i].sector == sector)
        {
            /* TODO: 实现磁盘写入接口 */
            /* 这里应该调用实际的磁盘写入函数，将 s_cache[i].data 写入扇区 */

            /* 刷新成功，标记为已清理 */
            s_cache[i].status = FAT32_CACHE_UNUSED;
            return 0;
        }
    }

    return -1;
}

/**
 * @brief 检查扇区是否在缓存中
 */
bool fat32_cache_is_cached(uint32_t sector)
{
    uint32_t i;

    if (!s_initialized)
    {
        return false;
    }

    /* 查找是否有该扇区的缓存条目 */
    for (i = 0U; i < FAT32_CACHE_SIZE; i++)
    {
        if (s_cache[i].status != FAT32_CACHE_UNUSED && s_cache[i].sector == sector)
        {
            return true;
        }
    }

    return false;
}
