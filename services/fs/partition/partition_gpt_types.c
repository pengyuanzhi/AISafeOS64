/**
 * @file    partition_gpt_types.c
 * @brief   GPT 分区类型 GUID 实现
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @brief GPT 分区类型 GUID 实现
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "partition_gpt_types.h"
#include <string.h>
#include <stdint.h>

/* ========================================================================
 * GUID 操作函数实现
 * ======================================================================== */

/**
 * @brief 获取分区类型 GUID
 */
void gpt_get_partition_guid(uint32_t type, uint8_t *guid)
{
    const uint8_t *guid_ptr;

    if (guid == NULL)
    {
        return;
    }

    switch (type)
    {
        case GPT_PART_TYPE_UNUSED:
            guid_ptr = GPT_GUID_UNUSED;
            break;
        case GPT_PART_TYPE_FAT12:
            guid_ptr = GPT_GUID_FAT12;
            break;
        case GPT_PART_TYPE_FAT16:
            guid_ptr = GPT_GUID_FAT16;
            break;
        case GPT_PART_TYPE_FAT32:
            guid_ptr = GPT_GUID_FAT32;
            break;
        case GPT_PART_TYPE_EXFAT:
            guid_ptr = GPT_GUID_EXFAT;
            break;
        case GPT_PART_TYPE_NTFS:
            guid_ptr = GPT_GUID_NTFS;
            break;
        case GPT_PART_TYPE_EXT4:
            guid_ptr = GPT_GUID_EXT4;
            break;
        case GPT_PART_TYPE_SWAP:
            guid_ptr = GPT_GUID_SWAP;
            break;
        case GPT_PART_TYPE_LVM:
            guid_ptr = GPT_GUID_LVM;
            break;
        case GPT_PART_TYPE_RAID:
            guid_ptr = GPT_GUID_RAID;
            break;
        case GPT_PART_TYPE_LINUX_DATA:
            guid_ptr = GPT_GUID_LINUX_DATA;
            break;
        case GPT_PART_TYPE_BIOS_BOOT:
            guid_ptr = GPT_GUID_BIOS_BOOT;
            break;
        case GPT_PART_TYPE_EFI_SYSTEM:
            guid_ptr = GPT_GUID_EFI_SYSTEM;
            break;
        case GPT_PART_TYPE_MS_RESERVED:
            guid_ptr = GPT_GUID_MS_RESERVED;
            break;
        case GPT_PART_TYPE_MS_BASIC_DATA:
            guid_ptr = GPT_GUID_MS_BASIC_DATA;
            break;
        default:
            guid_ptr = GPT_GUID_UNUSED;
            break;
    }

    (void)memcpy(guid, guid_ptr, 16U);
}

/**
 * @brief 初始化新分区 GUID
 */
void gpt_init_partition_guid(uint8_t *guid)
{
    static uint32_t s_guid_counter = 1U;
    uint64_t timestamp;
    uint32_t rand1, rand2;

    if (guid == NULL)
    {
        return;
    }

    /* TODO: 获取当前时间戳 */
    /* timestamp = get_timestamp(); */
    timestamp = 0x123456789ABCDEF0ULL;

    /* TODO: 获取随机数 */
    rand1 = s_guid_counter++;
    rand2 = s_guid_counter++;

    /* 生成 UUID v4（随机） */
    guid[0] = (uint8_t)(timestamp >> 56);
    guid[1] = (uint8_t)(timestamp >> 48);
    guid[2] = (uint8_t)(timestamp >> 40);
    guid[3] = (uint8_t)(timestamp >> 32);
    guid[4] = (uint8_t)(timestamp >> 24);
    guid[5] = (uint8_t)(timestamp >> 16);
    guid[6] = (uint8_t)((timestamp >> 8) & 0x0FU) | 0x40U;  /* UUID v4 */
    guid[7] = (uint8_t)(timestamp & 0xFFU);
    guid[8] = (uint8_t)((rand1 >> 24) & 0x3FU) | 0x80U;  /* variant */
    guid[9] = (uint8_t)(rand1 >> 16);
    guid[10] = (uint8_t)(rand1 >> 8);
    guid[11] = (uint8_t)rand1;
    guid[12] = (uint8_t)(rand2 >> 24);
    guid[13] = (uint8_t)(rand2 >> 16);
    guid[14] = (uint8_t)(rand2 >> 8);
    guid[15] = (uint8_t)rand2;
}
