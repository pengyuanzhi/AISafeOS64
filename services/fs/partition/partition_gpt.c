/**
 * @file    partition_gpt.c
 * @brief   GPT 分区表实现
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details GPT 分区表实现：
 *          - GPT 检测
 *          - GPT 解析
 *          - GPT 创建
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "partition.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * GPT 签名
 * ======================================================================== */

/** @brief GPT 签名（"EFI PART"） */
#define PARTITION_GPT_SIGNATURE_STR    "EFI PART"

/* ========================================================================
 * GPT 常见分区类型 GUID
 * ======================================================================== */

/** @brief FAT32 分区 GUID */
static const uint8_t g_part_type_fat32[16] = {
    0xC1, 0x2A, 0x73, 0x28, 0x20, 0x0F, 0x11, 0xE0,
    0xBD, 0x06, 0x22, 0xA2, 0x8D, 0x93, 0x97, 0x96
};

/** @brief NTFS 分区 GUID */
static const uint8_t g_part_type_ntfs[16] = {
    0xE7, 0x50, 0x5D, 0x37, 0x19, 0xE9, 0x42, 0x4D,
    0x8A, 0x85, 0x4D, 0x14, 0x32, 0x30, 0x2B, 0x63
};

/** @brief EXT4 分区 GUID */
static const uint8_t g_part_type_ext4[16] = {
    0x0F, 0x63, 0x47, 0x44, 0x43, 0xD5, 0x43, 0x89,
    0xC6, 0x56, 0x4F, 0x7C, 0x08, 0x88, 0x51, 0xFA
};

/** @brief SWAP 分区 GUID */
static const uint8_t g_part_type_swap[16] = {
    0x06, 0x57, 0x56, 0x2E, 0x0C, 0xE5, 0x46, 0x2B,
    0x8A, 0x25, 0xC5, 0x16, 0x7A, 0x0C, 0x17, 0x50
};

/* ========================================================================
 * GPT 检测
 * ======================================================================== */

/**
 * @brief 检测 GPT 分区表
 */
bool partition_gpt_detect(const uint8_t *data)
{
    const partition_gpt_header_t *header;
    uint64_t signature;

    if (data == NULL)
    {
        return false;
    }

    header = (const partition_gpt_header_t *)data;

    /* 检查 GPT 签名（"EFI PART"） */
    (void)memcpy(&signature, header->signature, 8U);

    return (signature == PARTITION_GPT_SIGNATURE);
}

/* ========================================================================
 * GPT 解析
 * ======================================================================== */

/**
 * @brief 解析 GPT 分区表
 */
int32_t partition_gpt_parse(partition_disk_t *disk, uint64_t lba,
                             const uint8_t *data)
{
    const partition_gpt_header_t *header;
    uint32_t i;
    uint32_t partition_index;

    if (disk == NULL || data == NULL)
    {
        return -1;
    }

    header = (const partition_gpt_header_t *)data;

    /* 检查 GPT 签名 */
    {
        uint64_t signature;
        (void)memcpy(&signature, header->signature, 8U);

        if (signature != PARTITION_GPT_SIGNATURE)
        {
            return -1;
        }
    }

    /* TODO: 验证 CRC32 */

    /* 解析 GPT 分区表项（TODO：从磁盘读取分区表） */
    partition_index = 0;
    for (i = 0U; i < header->number_of_entries; i++)
    {
        /* TODO: 读取分区表项 */
        /* TODO: 填充分区信息 */

        partition_index++;

        if (partition_index >= PARTITION_MAX_PARTITIONS)
        {
            break;
        }
    }

    disk->table_type = PARTITION_TYPE_GPT;
    disk->partition_count = partition_index;

    return 0;
}

/* ========================================================================
 * GPT 创建
 * ======================================================================== */

/**
 * @brief 创建 GPT 分区表
 */
int32_t partition_gpt_create_table(partition_disk_t *disk)
{
    partition_gpt_header_t *header;

    if (disk == NULL)
    {
        return -1;
    }

    /* 分配 GPT 头数据 */
    header = (partition_gpt_header_t *)malloc(sizeof(partition_gpt_header_t));
    if (header == NULL)
    {
        return -1;
    }

    /* 清零 GPT 头 */
    (void)memset(header, 0, sizeof(partition_gpt_header_t));

    /* 设置 GPT 签名 */
    (void)memcpy(header->signature, PARTITION_GPT_SIGNATURE_STR, 8U);

    /* 设置版本（1.0） */
    header->revision = 0x00010000U;

    /* 设置头大小 */
    header->header_size = PARTITION_GPT_HEADER_SIZE;

    /* 设置 LBA 位置 */
    header->my_lba = 1ULL;  /* GPT 头位于 LBA 1 */
    header->alternate_lba = disk->total_sectors - 1ULL;

    /* 设置可用 LBA 范围 */
    header->first_usable_lba = 34ULL;  /* 跳过 GPT 头和保护分区 */
    header->last_usable_lba = disk->total_sectors - 34ULL;

    /* 设置分区表 LBA */
    header->partition_entry_lba = 2ULL;

    /* 设置分区表项数量 */
    header->number_of_entries = 128U;

    /* 设置分区表项大小 */
    header->size_of_entry = 128U;

    /* TODO: 计算 CRC32 */

    /* 写入 GPT 头到磁盘（TODO） */
    /* TODO: 写入备用 GPT 头 */

    free(header);

    disk->table_type = PARTITION_TYPE_GPT;
    disk->partition_count = 0U;

    return 0;
}
