/**
 * @file    fat32_bpb.h
 * @brief   FAT32 BPB 解析接口
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 BIOS 参数块（BPB）解析接口：
 *          - 从块设备读取并验证 BPB
 *          - 解析 BPB 参数到运行时上下文
 *          - 计算数据区位置和簇数
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_FAT32_BPB_H
#define FS_FAT32_BPB_H

#include "fat32_types.h"

/**
 * @brief 解析 FAT32 BPB
 *
 * @details 从块设备读取 BPB，验证关键字段，
 *          并填充 fat32_context_t 中的参数
 *
 * @param ctx FAT32 上下文（block_read 已设置）
 *
 * @return 0 成功，<0 失败
 *
 * @retval 0     解析成功
 * @retval -22   参数无效（ctx 为 NULL 或 block_read 为 NULL）
 * @retval -5    I/O 错误（读取失败）
 * @retval -22   BPB 验证失败
 */
int32_t fat32_bpb_parse(fat32_context_t *ctx);

#endif /* FS_FAT32_BPB_H */
