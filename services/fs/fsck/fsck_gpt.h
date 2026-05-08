/**
 * @file    fsck_gpt.h
 * @brief   GPT 分区完整性检查接口
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details GPT 分区完整性检查接口
 *          - GPT 头签名验证
 *          - GPT 版本检查
 *          - 分区表 CRC 校验和验证
 *          - 分区条目完整性检查
 *          - 备份 GPT 验证
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FSCK_GPT_H
#define FSCK_GPT_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * GPT 分区完整性检查结果
 * ======================================================================== */

/**
 * @brief GPT 检查结果
 */
typedef struct
{
    bool success;                    /**< @brief 检查是否成功 */
    bool gpt_valid;                 /**< @brief GPT 是否有效 */
    bool backup_valid;               /**< @brief 备份 GPT 是否有效 */
    bool partition_table_valid;       /**< @brief 分区表是否有效 */
    bool crc_valid;                 /**< @brief CRC 校验是否有效 */
    uint32_t errors_found;           /**< @brief 发现的错误数量 */
    uint32_t warnings;               /**< @brief 警告数量 */
    char last_error[256];            /**< @brief 最后一个错误消息 */
} fsck_gpt_result_t;

/**
 * @brief GPT 检查选项
 */
typedef struct
{
    bool check_header;               /**< @brief 检查 GPT 头 */
    bool check_crc;                  /**< @brief 检查 CRC 校验 */
    bool check_partition_table;        /**< @brief 检查分区表 */
    bool check_backup_gpt;            /**< @brief 检查备份 GPT */
    bool verbose;                    /**< @brief 详细输出 */
    bool repair;                     /**< @brief 自动修复 */
} fsck_gpt_options_t;

/* ========================================================================
 * GPT 分区完整性检查接口
 * ======================================================================== */

/**
 * @brief 检查 GPT 分区完整性
 *
 * @param device_path 设备路径
 * @param options 检查选项
 * @param result 检查结果
 *
 * @return 0 成功，<0 失败
 */
int32_t fsck_gpt_check(const char *device_path,
                       const fsck_gpt_options_t *options,
                       fsck_gpt_result_t *result);

/**
 * @brief 修复 GPT 分区错误
 *
 * @param device_path 设备路径
 * @param options 检查选项
 * @param result 检查结果
 *
 * @return 0 成功，<0 失败
 */
int32_t fsck_gpt_repair(const char *device_path,
                        const fsck_gpt_options_t *options,
                        fsck_gpt_result_t *result);

/**
 * @brief 验证 GPT 头签名
 *
 * @param device_path 设备路径
 * @param result 验证结果
 *
 * @return 0 成功，<0 失败
 */
int32_t fsck_gpt_verify_signature(const char *device_path,
                                  fsck_gpt_result_t *result);

/**
 * @brief 验证 GPT CRC 校验和
 *
 * @param device_path 设备路径
 * @param result 验证结果
 *
 * @return 0 成功，<0 失败
 */
int32_t fsck_gpt_verify_crc(const char *device_path,
                            fsck_gpt_result_t *result);

/**
 * @brief 验证分区表完整性
 *
 * @param device_path 设备路径
 * @param result 验证结果
 *
 * @return 0 成功，<0 失败
 */
int32_t fsck_gpt_verify_partition_table(const char *device_path,
                                       fsck_gpt_result_t *result);

/**
 * @brief 验证备份 GPT
 *
 * @param device_path 设备路径
 * @param result 验证结果
 *
 * @return 0 成功，<0 失败
 */
int32_t fsck_gpt_verify_backup(const char *device_path,
                               fsck_gpt_result_t *result);

/**
 * @brief 打印 GPT 检查报告
 *
 * @param result 检查结果
 *
 * @return 0 成功
 */
int32_t fsck_gpt_report(const fsck_gpt_result_t *result);

#endif /* FSCK_GPT_H */
