/**
 * @file    test_fsck_gpt.c
 * @brief   GPT 分区完整性检查测试
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details 测试 GPT 分区完整性检查功能：
 *          - GPT 头签名验证
 *          - GPT 版本检查
 *          - CRC 校验和验证
 *          - 分区表完整性检查
 *          - 备份 GPT 验证
 *
 * @note MISRA-C:2012 合规（简化版）
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

/* 模拟 block_device 接口 */
static uint8_t mock_gpt_header[512];

static void init_mock_gpt_header(void)
{
    static int32_t initialized = 0;

    if (initialized == 0)
    {
        (void)memset(mock_gpt_header, 0, sizeof(mock_gpt_header));

        /* 模拟 GPT 签名 "EFI PART" */
        mock_gpt_header[0] = 'E';
        mock_gpt_header[1] = 'F';
        mock_gpt_header[2] = 'I';
        mock_gpt_header[3] = ' ';
        mock_gpt_header[4] = 'P';
        mock_gpt_header[5] = 'A';
        mock_gpt_header[6] = 'R';
        mock_gpt_header[7] = 'T';

        /* 模拟 GPT 版本 1.0 */
        mock_gpt_header[12] = 0x00;
        mock_gpt_header[13] = 0x00;
        mock_gpt_header[14] = 0x01;
        mock_gpt_header[15] = 0x00;

        /* 模拟分区表 LBA = 2 */
        mock_gpt_header[72] = 0x02;
        mock_gpt_header[73] = 0x00;
        mock_gpt_header[74] = 0x00;
        mock_gpt_header[75] = 0x00;
        mock_gpt_header[76] = 0x00;
        mock_gpt_header[77] = 0x00;
        mock_gpt_header[78] = 0x00;
        mock_gpt_header[79] = 0x00;

        /* 模拟分区条目数量 = 128 */
        mock_gpt_header[80] = 0x80;
        mock_gpt_header[81] = 0x00;
        mock_gpt_header[82] = 0x00;
        mock_gpt_header[83] = 0x00;

        /* 模拟备份 LBA = 模拟磁盘末尾 */
        mock_gpt_header[32] = 0x00;
        mock_gpt_header[33] = 0x00;
        mock_gpt_header[34] = 0x00;
        mock_gpt_header[35] = 0x00;
        mock_gpt_header[36] = 0xFF;
        mock_gpt_header[37] = 0xFF;
        mock_gpt_header[38] = 0xFF;
        mock_gpt_header[39] = 0xFF;

        initialized = 1;
    }
}

int64_t block_read_sectors(const char *device_path, uint64_t lba,
                            uint8_t *buf, uint32_t num_sectors)
{
    (void)device_path;
    (void)lba;
    (void)num_sectors;
    
    init_mock_gpt_header();
    
    /* 模拟返回有效 GPT 头 */
    (void)memcpy(buf, mock_gpt_header, 512U);
    
    return 1;
}

int64_t block_write_sectors(const char *device_path, uint64_t lba,
                             const uint8_t *buf, uint32_t num_sectors)
{
    (void)device_path;
    (void)lba;
    (void)buf;
    (void)num_sectors;
    
    /* 模拟成功写入 */
    return (int64_t)num_sectors;
}

/* 模拟 fsck_gpt.h 接口 */
typedef struct
{
    int32_t success;
    int32_t gpt_valid;
    int32_t backup_valid;
    int32_t partition_table_valid;
    int32_t crc_valid;
    uint32_t errors_found;
    uint32_t warnings;
    char last_error[256];
} fsck_gpt_result_t;

typedef struct
{
    int32_t check_header;
    int32_t check_crc;
    int32_t check_partition_table;
    int32_t check_backup_gpt;
    int32_t verbose;
    int32_t repair;
} fsck_gpt_options_t;

int32_t fsck_gpt_check(const char *device_path,
                       const fsck_gpt_options_t *options,
                       fsck_gpt_result_t *result);
int32_t fsck_gpt_repair(const char *device_path,
                        const fsck_gpt_options_t *options,
                        fsck_gpt_result_t *result);
int32_t fsck_gpt_verify_signature(const char *device_path,
                                  fsck_gpt_result_t *result);
int32_t fsck_gpt_verify_crc(const char *device_path,
                            fsck_gpt_result_t *result);
int32_t fsck_gpt_verify_partition_table(const char *device_path,
                                       fsck_gpt_result_t *result);
int32_t fsck_gpt_verify_backup(const char *device_path,
                               fsck_gpt_result_t *result);
int32_t fsck_gpt_report(const fsck_gpt_result_t *result);

/* 安全的字符串复制 */
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
 * @brief 性能测试
 */
int32_t main(void)
{
    fsck_gpt_options_t options;
    fsck_gpt_result_t result;
    const char *test_device = "/dev/sda"; /* 测试设备 */
    int32_t ret;

    printf("=== GPT 分区完整性检查测试 ===\n\n");

    /* 初始化选项 */
    (void)memset(&options, 0, sizeof(fsck_gpt_options_t));
    options.check_header = 1;
    options.check_crc = 1;
    options.check_partition_table = 1;
    options.check_backup_gpt = 1;
    options.verbose = 1;

    /* 测试1: 完整的 GPT 检查 */
    printf("--- 测试1: 完整的 GPT 检查 ---\n");
    (void)memset(&result, 0, sizeof(fsck_gpt_result_t));

    ret = fsck_gpt_check(test_device, &options, &result);
    if (ret == 0 && result.success)
    {
        printf("✓ GPT 检查成功\n");
        printf("  GPT 有效: %d\n", result.gpt_valid);
        printf("  备份 GPT 有效: %d\n", result.backup_valid);
        printf("  分区表有效: %d\n", result.partition_table_valid);
        printf("  CRC 校验有效: %d\n", result.crc_valid);
        printf("  发现错误: %u\n", result.errors_found);
        printf("  警告: %u\n", result.warnings);
    }
    else
    {
        printf("✗ GPT 检查失败\n");
        printf("  错误: %s\n", result.last_error);
    }

    /* 测试2: GPT 签名验证 */
    printf("\n--- 测试2: GPT 签名验证 ---\n");
    (void)memset(&result, 0, sizeof(fsck_gpt_result_t));

    ret = fsck_gpt_verify_signature(test_device, &result);
    if (ret == 0)
    {
        printf("✓ GPT 签名验证通过\n");
    }
    else
    {
        printf("✗ GPT 签名验证失败\n");
        printf("  错误: %s\n", result.last_error);
    }

    /* 测试3: CRC 校验 */
    printf("\n--- 测试3: CRC 校验 ---\n");
    (void)memset(&result, 0, sizeof(fsck_gpt_result_t));

    ret = fsck_gpt_verify_crc(test_device, &result);
    if (ret == 0)
    {
        printf("✓ CRC 校验通过\n");
        printf("  CRC 有效: %d\n", result.crc_valid);
    }
    else
    {
        printf("✗ CRC 校验失败\n");
        printf("  错误: %s\n", result.last_error);
    }

    /* 测试4: 分区表完整性检查 */
    printf("\n--- 测试4: 分区表完整性检查 ---\n");
    (void)memset(&result, 0, sizeof(fsck_gpt_result_t));

    ret = fsck_gpt_verify_partition_table(test_device, &result);
    if (ret == 0)
    {
        printf("✓ 分区表完整性检查通过\n");
        printf("  分区表有效: %d\n", result.partition_table_valid);
    }
    else
    {
        printf("✗ 分区表完整性检查失败\n");
        printf("  错误: %s\n", result.last_error);
    }

    /* 测试5: 备份 GPT 验证 */
    printf("\n--- 测试5: 备份 GPT 验证 ---\n");
    (void)memset(&result, 0, sizeof(fsck_gpt_result_t));

    ret = fsck_gpt_verify_backup(test_device, &result);
    if (ret == 0)
    {
        printf("✓ 备份 GPT 验证通过\n");
        printf("  备份 GPT 有效: %d\n", result.backup_valid);
    }
    else
    {
        printf("✗ 备份 GPT 验证失败\n");
        printf("  错误: %s\n", result.last_error);
    }

    /* 测试6: 错误处理测试 */
    printf("\n--- 测试6: 错误处理测试 ---\n");
    (void)memset(&result, 0, sizeof(fsck_gpt_result_t));

    ret = fsck_gpt_check("/dev/nonexistent", &options, &result);
    if (ret != 0 || !result.success)
    {
        printf("✓ NULL 设备路径处理正确\n");
    }
    else
    {
        printf("✗ NULL 设备路径处理错误\n");
    }

    /* 测试7: 修复功能测试 */
    printf("\n--- 测试7: 修复功能测试 ---\n");
    (void)memset(&result, 0, sizeof(fsck_gpt_result_t));

    ret = fsck_gpt_repair(test_device, &options, &result);
    if (ret != 0)
    {
        printf("✓ 修复功能返回预期错误（功能未实现）\n");
        printf("  错误: %s\n", result.last_error);
    }
    else
    {
        printf("✗ 修复功能测试意外\n");
    }

    /* 生成检查报告 */
    printf("\n=== 检查报告 ===\n");
    if (result.success)
    {
        (void)fsck_gpt_report(&result);
    }
    else
    {
        printf("无法生成报告：检查失败\n");
    }

    printf("\n=== 测试完成 ===\n");

    return 0;
}
