/**
 * @file    test_partition_gpt.c
 * @brief   GPT 分区管理测试程序
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @brief GPT 分区管理测试程序：
 *        - 测试 GPT 分区创建/删除/调整
 *        - 测试 UUID v4 生成
 *        - 测试 CRC32 验证
 *        - 测试备用 GPT 头同步
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

/* ========================================================================
 * 测试辅助函数
 * ======================================================================== */

/**
 * @brief 测试通过
 */
#define TEST_PASS(msg) \
    do { \
        printf("[PASS] %s\n", msg); \
        pass_count++; \
    } while (0)

/**
 * @brief 测试失败
 */
#define TEST_FAIL(msg) \
    do { \
        printf("[FAIL] %s\n", msg); \
        fail_count++; \
    } while (0)

/**
 * @brief 测试开始
 */
#define TEST_START(msg) \
    do { \
        printf("\n=== %s ===\n", msg); \
    } while (0)

/**
 * @brief 测试 UUID v4 生成
 */
static bool test_uuid_v4_generation(void)
{
    uint8_t uuid[16];
    uint32_t i;
    uint32_t j;
    bool version_correct = false;
    bool variant_correct = false;

    TEST_START("UUID v4 Generation");

    /* 生成 10 个 UUID v4 */
    for (i = 0U; i < 10U; i++)
    {
        gpt_init_partition_guid(uuid);

        /* 检查版本（第7个字节）- 应该是 0x40 + version */
        if ((uuid[6] & 0xF0U) == 0x40U)
        {
            version_correct = true;
        }

        /* 检查变体（第9个字节）- 应该是 0x80 + variant */
        if ((uuid[8] & 0xC0U) == 0x80U)
        {
            variant_correct = true;
        }

        /* 打印 UUID */
        printf("UUID %u: %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X\n",
               i,
               uuid[0], uuid[1], uuid[2], uuid[3],
               uuid[4], uuid[5], uuid[6], uuid[7],
               uuid[8], uuid[9], uuid[10], uuid[11],
               uuid[12], uuid[13], uuid[14], uuid[15]);
    }

    if (version_correct)
    {
        TEST_PASS("UUID v4 version correct");
    }
    else
    {
        TEST_FAIL("UUID v4 version incorrect");
    }

    if (variant_correct)
    {
        TEST_PASS("UUID v4 variant correct");
    }
    else
    {
        TEST_FAIL("UUID v4 variant incorrect");
    }

    return (version_correct && variant_correct);
}

/**
 * @brief 测试 CRC32 验证
 */
static bool test_crc32(void)
{
    uint8_t test_data[256];
    uint32_t i;
    uint32_t expected_crc = 0xFFFFFFFFU;
    uint32_t calculated_crc;
    bool all_zero = true;
    bool single_byte = true;

    TEST_START("CRC32 Calculation");

    /* 测试 256 字节数据 */
    for (i = 0U; i < sizeof(test_data); i++)
    {
        test_data[i] = (uint8_t)i;
    }

    calculated_crc = compute_crc32(test_data, sizeof(test_data));

    if (calculated_crc != 0xCBF43926U)  /* 256字节数据的CRC32 */
    {
        single_byte = false;
    }

    /* 打印结果 */
    printf("CRC32 (256 bytes): 0x%08X\n", calculated_crc);
    printf("Expected:          0x0FFFFFFFF\n");
    printf("Calculated:        0x%08X\n", calculated_crc);

    if (calculated_crc == 0x0FFFFFFFFU)
    {
        TEST_PASS("CRC32 calculation correct");
    }
    else
    {
        TEST_FAIL("CRC32 calculation incorrect");
    }

    /* 测试空数据 */
    calculated_crc = compute_crc32(NULL, 0);
    printf("CRC32 (0 bytes): 0x%08X\n", calculated_crc);

    return (single_byte);
}

/**
 * @brief 测试 GPT 分区表创建
 */
static bool test_gpt_partition_table_creation(void)
{
    partition_disk_t *disk;
    int32_t ret;
    bool success = false;

    TEST_START("GPT Partition Table Creation");

    /* 模拟一个磁盘 */
    disk = (partition_disk_t *)malloc(sizeof(partition_disk_t));
    if (disk == NULL)
    {
        TEST_FAIL("Failed to allocate disk");
        return false;
    }

    disk->total_sectors = 10000000ULL;  /* 10000 MB */
    disk->table_type = PARTITION_TYPE_GPT;
    disk->partition_count = 0U;
    (void)strncpy(disk->device_path, "/dev/sda", 255U);

    /* 创建 GPT 分区表 */
    ret = partition_gpt_create_table(disk);

    if (ret == 0)
    {
        TEST_PASS("GPT partition table created successfully");
        success = true;
    }
    else
    {
        TEST_FAIL("Failed to create GPT partition table");
    }

    free(disk);

    return success;
}

/**
 * @brief 测试 GPT 分区创建
 */
static bool test_gpt_partition_creation(void)
{
    partition_disk_t *disk;
    partition_entry_t partition;
    int32_t ret;
    bool success = false;

    TEST_START("GPT Partition Creation");

    /* 模拟一个磁盘 */
    disk = (partition_disk_t *)malloc(sizeof(partition_disk_t));
    if (disk == NULL)
    {
        TEST_FAIL("Failed to allocate disk");
        return false;
    }

    disk->total_sectors = 10000000ULL;
    disk->table_type = PARTITION_TYPE_GPT;
    disk->partition_count = 0U;
    (void)strncpy(disk->device_path, "/dev/sda", 255U);

    /* 创建 GPT 分区表 */
    ret = partition_gpt_create_table(disk);
    if (ret != 0)
    {
        TEST_FAIL("Failed to create GPT partition table");
        free(disk);
        return false;
    }

    /* 创建分区 */
    ret = gpt_create_partition(disk, "DATA", 2048ULL, 100000ULL, GPT_PART_TYPE_EXT4, false);

    if (ret >= 0)
    {
        TEST_PASS("GPT partition created successfully");
        printf("Partition number: %u\n", ret);

        /* 检查分区信息 */
        ret = partition_get_info(disk, (uint32_t)ret, &partition);
        if (ret == 0)
        {
            printf("Partition name: %s\n", partition.name);
            printf("Start LBA: %llu\n", partition.start_lba);
            printf("Size: %llu sectors (%.2f MB)\n",
                   partition.size_in_sectors,
                   (double)partition.size_in_sectors * 512.0 / (1024.0 * 1024.0));

            if (strcmp(partition.name, "DATA") == 0)
            {
                TEST_PASS("Partition information correct");
            }
            else
            {
                TEST_FAIL("Partition information incorrect");
            }
        }
        else
        {
            TEST_FAIL("Failed to get partition information");
        }

        success = true;
    }
    else
    {
        TEST_FAIL("Failed to create GPT partition");
    }

    free(disk);

    return success;
}

/**
 * @brief 测试 GPT 分区删除
 */
static bool test_gpt_partition_deletion(void)
{
    partition_disk_t *disk;
    partition_entry_t partition;
    int32_t ret;
    bool success = false;

    TEST_START("GPT Partition Deletion");

    /* 模拟一个磁盘 */
    disk = (partition_disk_t *)malloc(sizeof(partition_disk_t));
    if (disk == NULL)
    {
        TEST_FAIL("Failed to allocate disk");
        return false;
    }

    disk->total_sectors = 10000000ULL;
    disk->table_type = PARTITION_TYPE_GPT;
    disk->partition_count = 0U;
    (void)strncpy(disk->device_path, "/dev/sda", 255U);

    /* 创建 GPT 分区表 */
    ret = partition_gpt_create_table(disk);
    if (ret != 0)
    {
        TEST_FAIL("Failed to create GPT partition table");
        free(disk);
        return false;
    }

    /* 创建分区 */
    ret = gpt_create_partition(disk, "DATA", 2048ULL, 100000ULL, GPT_PART_TYPE_EXT4, false);
    if (ret < 0)
    {
        TEST_FAIL("Failed to create GPT partition");
        free(disk);
        return false;
    }

    uint32_t partition_number = (uint32_t)ret;

    /* 删除分区 */
    ret = gpt_delete_partition(disk, partition_number);

    if (ret == 0)
    {
        TEST_PASS("GPT partition deleted successfully");
        success = true;
    }
    else
    {
        TEST_FAIL("Failed to delete GPT partition");
    }

    free(disk);

    return success;
}

/**
 * @brief 测试备用 GPT 头同步
 */
static bool test_gpt_backup_header_sync(void)
{
    partition_disk_t *disk;
    partition_gpt_header_t *header;
    partition_gpt_header_t *backup_header;
    int32_t ret;
    bool success = false;

    TEST_START("GPT Backup Header Sync");

    /* 模拟一个磁盘 */
    disk = (partition_disk_t *)malloc(sizeof(partition_disk_t));
    if (disk == NULL)
    {
        TEST_FAIL("Failed to allocate disk");
        return false;
    }

    disk->total_sectors = 10000000ULL;
    disk->table_type = PARTITION_TYPE_GPT;
    disk->partition_count = 0U;
    (void)strncpy(disk->device_path, "/dev/sda", 255U);

    /* 创建 GPT 分区表 */
    ret = partition_gpt_create_table(disk);
    if (ret != 0)
    {
        TEST_FAIL("Failed to create GPT partition table");
        free(disk);
        return false;
    }

    /* 读取主 GPT 头 */
    header = (partition_gpt_header_t *)malloc(sizeof(partition_gpt_header_t));
    if (header == NULL)
    {
        TEST_FAIL("Failed to allocate header");
        free(disk);
        return false;
    }

    ret = read_sector(disk->device_path, 1ULL, (uint8_t *)header);
    if (ret != 0)
    {
        TEST_FAIL("Failed to read GPT header");
        free(header);
        free(disk);
        return false;
    }

    printf("Main GPT Header LBA: %llu\n", header->my_lba);
    printf("Alternate GPT Header LBA: %llu\n", header->alternate_lba);

    /* 同步备用 GPT 头 */
    ret = gpt_sync_partition_table(disk);

    if (ret == 0)
    {
        TEST_PASS("GPT backup header synced successfully");
        success = true;

        /* 读取备用 GPT 头 */
        backup_header = (partition_gpt_header_t *)malloc(sizeof(partition_gpt_header_t));
        if (backup_header == NULL)
        {
            TEST_FAIL("Failed to allocate backup header");
            free(header);
            free(disk);
            return false;
        }

        ret = read_sector(disk->device_path, disk->total_sectors - 1ULL, (uint8_t *)backup_header);
        if (ret == 0)
        {
            printf("Backup GPT Header LBA: %llu\n", backup_header->my_lba);
            printf("Main GPT Header LBA: %llu\n", backup_header->alternate_lba);

            if (backup_header->my_lba == disk->total_sectors - 1ULL &&
                backup_header->alternate_lba == 1ULL)
            {
                TEST_PASS("Backup GPT header location correct");
            }
            else
            {
                TEST_FAIL("Backup GPT header location incorrect");
            }

            /* 验证 CRC32 */
            uint32_t main_crc = compute_crc32((uint8_t *)header, sizeof(partition_gpt_header_t));
            uint32_t backup_crc = compute_crc32((uint8_t *)backup_header, sizeof(partition_gpt_header_t));

            printf("Main GPT CRC32: 0x%08X\n", main_crc);
            printf("Backup GPT CRC32: 0x%08X\n", backup_crc);

            if (main_crc == backup_crc)
            {
                TEST_PASS("GPT CRC32 matches");
            }
            else
            {
                TEST_FAIL("GPT CRC32 mismatch");
            }
        }
        else
        {
            TEST_FAIL("Failed to read backup GPT header");
        }

        free(backup_header);
    }
    else
    {
        TEST_FAIL("Failed to sync GPT backup header");
    }

    free(header);
    free(disk);

    return success;
}

/* ========================================================================
 * 主测试函数
 * ======================================================================== */

int main(void)
{
    uint32_t pass_count = 0U;
    uint32_t fail_count = 0U;
    uint32_t total_tests = 0U;

    printf("========================================\n");
    printf("GPT Partition Management Test Suite\n");
    printf("========================================\n");
    printf("Date: %s", ctime(&time(NULL)));

    /* 测试 1: UUID v4 生成 */
    total_tests++;
    if (test_uuid_v4_generation())
    {
        pass_count++;
    }
    else
    {
        fail_count++;
    }

    /* 测试 2: CRC32 计算 */
    total_tests++;
    if (test_crc32())
    {
        pass_count++;
    }
    else
    {
        fail_count++;
    }

    /* 测试 3: GPT 分区表创建 */
    total_tests++;
    if (test_gpt_partition_table_creation())
    {
        pass_count++;
    }
    else
    {
        fail_count++;
    }

    /* 测试 4: GPT 分区创建 */
    total_tests++;
    if (test_gpt_partition_creation())
    {
        pass_count++;
    }
    else
    {
        fail_count++;
    }

    /* 测试 5: GPT 分区删除 */
    total_tests++;
    if (test_gpt_partition_deletion())
    {
        pass_count++;
    }
    else
    {
        fail_count++;
    }

    /* 测试 6: 备用 GPT 头同步 */
    total_tests++;
    if (test_gpt_backup_header_sync())
    {
        pass_count++;
    }
    else
    {
        fail_count++;
    }

    /* 打印测试结果 */
    printf("\n========================================\n");
    printf("Test Results Summary\n");
    printf("========================================\n");
    printf("Total Tests: %u\n", total_tests);
    printf("Passed: %u\n", pass_count);
    printf("Failed: %u\n", fail_count);
    printf("Success Rate: %.2f%%\n",
           (double)pass_count * 100.0 / (double)total_tests);

    if (fail_count == 0U)
    {
        printf("\n✓ All tests passed!\n");
        return 0;
    }
    else
    {
        printf("\n✗ Some tests failed!\n");
        return 1;
    }
}
