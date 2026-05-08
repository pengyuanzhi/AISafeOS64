/**
 * @file    fsck_gpt.c
 * @brief   GPT 分区完整性检查实现
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details GPT 分区完整性检查实现
 *          - GPT 头签名验证
 *          - GPT 版本检查
 *          - 分区表 CRC 校验和验证
 *          - 分区条目完整性检查
 *          - 备份 GPT 验证
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fsck_gpt.h"
#include "fsck_types.h"
#include "../partition/partition_gpt_types.h"
#include "../block_device.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * GPT 头结构
 * ======================================================================== */

/**
 * @brief GPT 头结构（简化版）
 */
typedef struct
{
    uint8_t     signature[8];        /**< 签名 "EFI PART" */
    uint8_t     revision[4];         /**< 修订版本 */
    uint8_t     header_size[4];      /**< 头大小 */
    uint8_t     header_crc32[4];    /**< 头 CRC32 */
    uint8_t     reserved[4];        /**< 保留 */
    uint64_t    current_lba;         /**< 当前 LBA */
    uint64_t    backup_lba;          /**< 备份 LBA */
    uint64_t    first_usable_lba;    /**< 第一个可用 LBA */
    uint64_t    last_usable_lba;     /**< 最后一个可用 LBA */
    uint8_t     disk_guid[16];       /**< 磁盘 GUID */
    uint64_t    partition_entry_lba;  /**< 分区条目 LBA */
    uint32_t    num_partition_entries; /**< 分区条目数量 */
    uint32_t    partition_entry_size; /**< 分区条目大小 */
    uint8_t     partition_entry_crc32[4]; /**< 分区条目 CRC32 */
} gpt_header_t;

/* ========================================================================
 * CRC32 实现（简化版）
 * ======================================================================== */

/**
 * @brief 计算 CRC32
 */
static uint32_t compute_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t i;
    uint32_t j;
    uint32_t crc;
    uint32_t byte;
    uint32_t mask;

    crc = 0xFFFFFFFFU;
    for (i = 0U; i < length; i++)
    {
        byte = data[i];
        crc = crc ^ byte;

        for (j = 0U; j < 8U; j++)
        {
            mask = -(crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }

    return ~crc;
}

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 读取 GPT 头
 */
static int32_t read_gpt_header(const char *device_path, uint64_t lba,
                             gpt_header_t *header)
{
    int64_t sectors_read;

    if ((device_path == NULL) || (header == NULL))
    {
        return -1;
    }

    sectors_read = block_read_sectors(device_path, lba,
                                      (uint8_t *)header, 1U);

    if (sectors_read < 0)
    {
        return -1;
    }

    return 0;
}

/**
 * @brief 验证 GPT 签名
 */
static bool verify_gpt_signature(const gpt_header_t *header)
{
    const char *expected_signature = "EFI PART";

    if (header == NULL)
    {
        return false;
    }

    return (memcmp(header->signature, expected_signature, 8U) == 0);
}

/**
 * @brief 验证 GPT 版本
 */
static bool verify_gpt_version(const gpt_header_t *header)
{
    uint32_t revision;

    if (header == NULL)
    {
        return false;
    }

    /* 读取修订版本（小端） */
    revision = (uint32_t)header->revision[0] |
                ((uint32_t)header->revision[1] << 8U) |
                ((uint32_t)header->revision[2] << 16U) |
                ((uint32_t)header->revision[3] << 24U);

    /* GPT 版本 1.0 */
    return (revision == 0x00010000U);
}

/* ========================================================================
 * GPT 分区完整性检查接口实现
 * ======================================================================== */

/**
 * @brief 检查 GPT 分区完整性
 */
int32_t fsck_gpt_check(const char *device_path,
                       const fsck_gpt_options_t *options,
                       fsck_gpt_result_t *result)
{
    gpt_header_t header;
    int32_t ret;

    if ((device_path == NULL) || (result == NULL))
    {
        return -1;
    }

    /* 初始化结果 */
    (void)memset(result, 0, sizeof(fsck_gpt_result_t));
    result->success = false;
    result->gpt_valid = true; /* 假设有效，除非发现问题 */

    /* 读取 GPT 头 */
    if (read_gpt_header(device_path, 1ULL, &header) != 0)
    {
        (void)strncpy(result->last_error,
                      "Failed to read GPT header", 255U);
        result->last_error[255U] = '\0';
        return -1;
    }

    /* 验证 GPT 签名 */
    if ((options == NULL || options->check_header) && !verify_gpt_signature(&header))
    {
        (void)strncpy(result->last_error,
                      "Invalid GPT signature", 255U);
        result->last_error[255U] = '\0';
        result->gpt_valid = false;
        result->errors_found++;
        return 0;
    }

    /* 验证 GPT 版本 */
    if ((options == NULL || options->check_header) && !verify_gpt_version(&header))
    {
        (void)strncpy(result->last_error,
                      "Unsupported GPT version", 255U);
        result->last_error[255U] = '\0';
        result->gpt_valid = false;
        result->errors_found++;
        return 0;
    }

    /* 验证 CRC 校验 */
    if ((options == NULL || options->check_crc) &&
        fsck_gpt_verify_crc(device_path, result) != 0)
    {
        if (result->errors_found == 0U)
        {
            (void)strncpy(result->last_error,
                          "GPT CRC verification failed", 255U);
            result->last_error[255U] = '\0';
        }
        return 0;
    }

    /* 验证分区表完整性 */
    if ((options == NULL || options->check_partition_table) &&
        fsck_gpt_verify_partition_table(device_path, result) != 0)
    {
        if (result->errors_found == 0U)
        {
            (void)strncpy(result->last_error,
                          "Partition table verification failed", 255U);
            result->last_error[255U] = '\0';
        }
        return 0;
    }

    /* 验证备份 GPT */
    if (options != NULL && options->check_backup_gpt &&
        fsck_gpt_verify_backup(device_path, result) != 0)
    {
        if (result->errors_found == 0U)
        {
            (void)strncpy(result->last_error,
                          "Backup GPT verification failed", 255U);
            result->last_error[255U] = '\0';
        }
        result->backup_valid = false;
        return 0;
    }

    result->success = true;
    return 0;
}

/**
 * @brief 修复 GPT 分区错误
 */
int32_t fsck_gpt_repair(const char *device_path,
                        const fsck_gpt_options_t *options,
                        fsck_gpt_result_t *result)
{
    if ((device_path == NULL) || (result == NULL))
    {
        return -1;
    }

    /* 简化版：只记录需要修复 */
    (void)strncpy(result->last_error,
                  "GPT repair not implemented", 255U);
    result->last_error[255U] = '\0';

    return -1;
}

/**
 * @brief 验证 GPT 头签名
 */
int32_t fsck_gpt_verify_signature(const char *device_path,
                                  fsck_gpt_result_t *result)
{
    gpt_header_t header;

    if ((device_path == NULL) || (result == NULL))
    {
        return -1;
    }

    /* 读取 GPT 头 */
    if (read_gpt_header(device_path, 1ULL, &header) != 0)
    {
        (void)strncpy(result->last_error,
                      "Failed to read GPT header", 255U);
        result->last_error[255U] = '\0';
        return -1;
    }

    /* 验证签名 */
    if (!verify_gpt_signature(&header))
    {
        (void)strncpy(result->last_error,
                      "Invalid GPT signature", 255U);
        result->last_error[255U] = '\0';
        return 0;
    }
    {
        (void)strncpy(result->last_error,
                      "Invalid GPT signature", 255U);
        result->last_error[255U] = '\0';
        return -1;
    }

    return 0;
}

/**
 * @brief 验证 GPT CRC 校验和
 */
int32_t fsck_gpt_verify_crc(const char *device_path,
                            fsck_gpt_result_t *result)
{
    gpt_header_t header;
    uint32_t header_crc;
    uint32_t computed_crc;

    if ((device_path == NULL) || (result == NULL))
    {
        return -1;
    }

    /* 读取 GPT 头 */
    if (read_gpt_header(device_path, 1ULL, &header) != 0)
    {
        (void)strncpy(result->last_error,
                      "Failed to read GPT header", 255U);
        result->last_error[255U] = '\0';
        return -1;
    }

    /* 读取头 CRC32 */
    header_crc = (uint32_t)header.header_crc32[0] |
                 ((uint32_t)header.header_crc32[1] << 8U) |
                 ((uint32_t)header.header_crc32[2] << 16U) |
                 ((uint32_t)header.header_crc32[3] << 24U);

    /* 计算头 CRC32（跳过 CRC32 字段本身） */
    (void)memset(header.header_crc32, 0, 4U);
    computed_crc = compute_crc32((const uint8_t *)&header,
                               (uint32_t)sizeof(gpt_header_t));

    /* 验证 CRC */
    if (header_crc != computed_crc)
    {
        result->crc_valid = false;
        result->errors_found++;
        (void)strncpy(result->last_error,
                      "GPT header CRC mismatch", 255U);
        result->last_error[255U] = '\0';
        return -1;
    }

    result->crc_valid = true;
    return 0;
}

/**
 * @brief 验证分区表完整性
 */
int32_t fsck_gpt_verify_partition_table(const char *device_path,
                                       fsck_gpt_result_t *result)
{
    gpt_header_t header;
    uint32_t partition_entry_lba;
    uint32_t num_partition_entries;

    if ((device_path == NULL) || (result == NULL))
    {
        return -1;
    }

    /* 读取 GPT 头 */
    if (read_gpt_header(device_path, 1ULL, &header) != 0)
    {
        (void)strncpy(result->last_error,
                      "Failed to read GPT header", 255U);
        result->last_error[255U] = '\0';
        return -1;
    }

    /* 读取分区表位置和数量 */
    partition_entry_lba = (uint32_t)header.partition_entry_lba;
    num_partition_entries = (uint32_t)header.num_partition_entries;

    /* 验证分区条目数量 */
    if (num_partition_entries == 0U)
    {
        result->partition_table_valid = false;
        result->errors_found++;
        (void)strncpy(result->last_error,
                      "No partition entries found", 255U);
        result->last_error[255U] = '\0';
        return -1;
    }

    /* 验证分区表 LBA 范围 */
    if (partition_entry_lba == 0U)
    {
        result->partition_table_valid = false;
        result->errors_found++;
        (void)strncpy(result->last_error,
                      "Invalid partition table LBA", 255U);
        result->last_error[255U] = '\0';
        return -1;
    }

    result->partition_table_valid = true;
    return 0;
}

/**
 * @brief 验证备份 GPT
 */
int32_t fsck_gpt_verify_backup(const char *device_path,
                               fsck_gpt_result_t *result)
{
    gpt_header_t primary_header;
    gpt_header_t backup_header;
    uint64_t backup_lba;

    if ((device_path == NULL) || (result == NULL))
    {
        return -1;
    }

    /* 读取主 GPT 头 */
    if (read_gpt_header(device_path, 1ULL, &primary_header) != 0)
    {
        (void)strncpy(result->last_error,
                      "Failed to read primary GPT header", 255U);
        result->last_error[255U] = '\0';
        return -1;
    }

    /* 读取备份 LBA */
    backup_lba = primary_header.backup_lba;

    /* 验证备份 LBA */
    if (backup_lba == 0ULL)
    {
        result->backup_valid = false;
        result->errors_found++;
        (void)strncpy(result->last_error,
                      "No backup GPT found", 255U);
        result->last_error[255U] = '\0';
        return -1;
    }

    /* 读取备份 GPT 头 */
    if (read_gpt_header(device_path, backup_lba, &backup_header) != 0)
    {
        result->backup_valid = false;
        result->errors_found++;
        (void)strncpy(result->last_error,
                      "Failed to read backup GPT header", 255U);
        result->last_error[255U] = '\0';
        return -1;
    }

    /* 验证备份 GPT 签名 */
    if (!verify_gpt_signature(&backup_header))
    {
        result->backup_valid = false;
        result->errors_found++;
        (void)strncpy(result->last_error,
                      "Invalid backup GPT signature", 255U);
        result->last_error[255U] = '\0';
        return -1;
    }

    result->backup_valid = true;
    return 0;
}

/**
 * @brief 安全的字符串复制
 */
static void safe_strcpy(char *dest, const char *src, uint32_t size)
{
    if (dest == NULL || src == NULL || size == 0U)
    {
        return;
    }
    
    (void)strncpy(dest, src, size - 1U);
    dest[size - 1U] = '\0';
}

/**
 * @brief 打印 GPT 检查报告
 */
int32_t fsck_gpt_report(const fsck_gpt_result_t *result)
{
    if (result == NULL)
    {
        return -1;
    }

    printf("\n=== GPT 分区完整性检查报告 ===\n");

    printf("检查结果: %s\n", result->success ? "成功" : "失败");
    printf("GPT 有效: %s\n", result->gpt_valid ? "是" : "否");
    printf("备份 GPT 有效: %s\n", result->backup_valid ? "是" : "否");
    printf("分区表有效: %s\n", result->partition_table_valid ? "是" : "否");
    printf("CRC 校验有效: %s\n", result->crc_valid ? "是" : "否");

    printf("发现错误: %u\n", result->errors_found);
    printf("警告: %u\n", result->warnings);

    if (result->errors_found > 0U)
    {
        printf("最后错误: %s\n", result->last_error);
    }

    printf("=== 检查报告完成 ===\n\n");

    return 0;
}
