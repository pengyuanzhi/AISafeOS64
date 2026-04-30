/**
 * @file    fat32_dir.c
 * @brief   FAT32 目录项解析实现
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 目录项解析实现：
 *          - 8.3 文件名提取和转换
 *          - 目录遍历（按簇链读取目录数据）
 *          - 文件名匹配（大小写不敏感）
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fat32_dir.h"
#include "fat32_fat.h"
#include "fat32_path.h"
#include <string.h>
#include <stdint.h>

/* ========================================================================
 * 目录项操作实现
 * ======================================================================== */

/**
 * @brief 从目录项提取 8.3 格式文件名
 */
int32_t fat32_dir_extract_name(const fat32_dir_entry_t *entry,
                                char *name_out, uint32_t buf_size)
{
    uint32_t i;
    uint32_t pos;
    bool has_ext;

    if ((entry == NULL) || (name_out == NULL))
    {
        return -22; /* -EINVAL */
    }

    if (buf_size < FAT32_NAME_MAX)
    {
        return -22; /* -EINVAL */
    }

    has_ext = false;
    pos = 0U;

    /* 提取主文件名（8 字节），跳过尾部空格 */
    for (i = 0U; i < 8U; i++)
    {
        uint8_t ch = entry->name[i];
        if (ch == 0x20U)
        {
            break;
        }
        if (pos < (buf_size - 1U))
        {
            name_out[pos] = (char)ch;
            pos++;
        }
    }

    /* 检查是否有扩展名（非空格） */
    for (i = 8U; i < 11U; i++)
    {
        if (entry->name[i] != 0x20U)
        {
            has_ext = true;
            break;
        }
    }

    /* 如果是目录，不添加扩展名 */
    if ((entry->attr & FAT32_ATTR_DIRECTORY) != 0U)
    {
        has_ext = false;
    }

    /* 添加扩展名 */
    if (has_ext)
    {
        if (pos < (buf_size - 1U))
        {
            name_out[pos] = '.';
            pos++;
        }

        for (i = 8U; i < 11U; i++)
        {
            uint8_t ch = entry->name[i];
            if (ch == 0x20U)
            {
                break;
            }
            if (pos < (buf_size - 1U))
            {
                name_out[pos] = (char)ch;
                pos++;
            }
        }
    }

    name_out[pos] = '\0';

    return 0;
}

/* fat32_name_match_83 定义在 fat32_path.c 中 */

/* ========================================================================
 * 目录遍历和文件查找
 * ======================================================================== */

/**
 * @brief 在指定目录簇中查找文件
 */
int32_t fat32_lookup_file(fat32_context_t *ctx, uint32_t dir_clust,
                           const char *name, fat32_dir_entry_t *entry)
{
    uint32_t current_cluster;
    uint32_t sec_num;
    uint32_t sec_count;
    uint32_t entry_idx;
    uint32_t fat_val;
    int32_t ret;

    if ((ctx == NULL) || (name == NULL) || (entry == NULL))
    {
        return -22; /* -EINVAL */
    }

    current_cluster = dir_clust;

    /* 遍历簇链 */
    for (;;)
    {
        /* 计算当前簇的起始扇区 */
        sec_num = fat32_cluster_to_sector(ctx, current_cluster);

        /* 遍历簇内每个扇区的目录项 */
        for (sec_count = 0U; sec_count < ctx->sec_per_clust; sec_count++)
        {
            uint32_t target_sec = sec_num + sec_count;

            ret = ctx->block_read((uint64_t)target_sec, ctx->sector_buf, 1U);
            if (ret != 0)
            {
                return -5; /* -EIO */
            }

            /* 检查每个目录项 */
            for (entry_idx = 0U;
                 entry_idx < FAT32_DIR_ENTRIES_PER_SEC;
                 entry_idx++)
            {
                fat32_dir_entry_t *dir_entry;
                dir_entry = (fat32_dir_entry_t *)&ctx->sector_buf[
                    entry_idx * FAT32_DIR_ENTRY_SIZE];

                /* 空目录项表示目录结束 */
                if (fat32_dir_entry_is_empty(dir_entry))
                {
                    return -2; /* -ENOENT */
                }

                /* 跳过已删除和 LFN 条目 */
                if (fat32_dir_entry_is_deleted(dir_entry))
                {
                    continue;
                }

                if (fat32_dir_entry_is_lfn(dir_entry))
                {
                    continue;
                }

                /* 跳过卷标 */
                if ((dir_entry->attr & FAT32_ATTR_VOLUME_ID) != 0U)
                {
                    continue;
                }

                /* 匹配文件名 */
                if (fat32_name_match_83((const char *)dir_entry->name, name))
                {
                    (void)memcpy(entry, dir_entry, sizeof(fat32_dir_entry_t));
                    return 0;
                }
            }
        }

        /* 读取下一个簇 */
        ret = fat32_fat_read_entry(ctx, current_cluster, &fat_val);
        if (ret != 0)
        {
            return ret;
        }

        if (fat32_is_eoc(fat_val))
        {
            break;
        }

        current_cluster = fat_val;
    }

    return -2; /* -ENOENT */
}
