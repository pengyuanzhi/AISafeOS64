/**
 * @file    fsck_types.h
 * @brief   fsck 工具类型定义
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details fsck 工具核心数据类型定义：
 *          - fsck 检查选项
 *          - fsck 检查结果
 *          - 文件系统错误类型
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FSCK_TYPES_H
#define FSCK_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief fsck 设备路径最大长度 */
#define FSCK_MAX_PATH_LEN   256U

/** @brief fsck 消息最大长度 */
#define FSCK_MAX_MSG_LEN    256U

/* ========================================================================
 * fsck 检查选项
 * ======================================================================== */

/**
 * @brief fsck 检查选项
 */
typedef struct
{
    bool auto_repair;         /**< @brief 自动修复错误 */
    bool force_check;         /**< @brief 强制检查（即使文件系统标记为干净） */
    bool verbose;             /**< @brief 详细输出 */
    bool interactive;         /**< @brief 交互模式（询问用户） */
    bool check_bad_blocks;     /**< @brief 检查坏块 */
} fsck_options_t;

/* ========================================================================
 * fsck 检查结果
 * ======================================================================== */

/**
 * @brief fsck 检查结果
 */
typedef struct
{
    bool success;              /**< @brief 检查是否成功 */
    bool filesystem_clean;     /**< @brief 文件系统是否干净 */
    bool filesystem_modified;  /**< @brief 文件系统是否被修改 */
    uint32_t errors_found;     /**< @brief 发现的错误数量 */
    uint32_t errors_fixed;     /**< @brief 修复的错误数量 */
    uint32_t warnings;         /**< @brief 警告数量 */
    char last_error[FSCK_MAX_MSG_LEN]; /**< @brief 最后一个错误消息 */
} fsck_result_t;

/* ========================================================================
 * 文件系统错误类型
 * ======================================================================== */

/**
 * @brief 文件系统错误类型
 */
typedef enum
{
    FSCK_ERR_NONE = 0U,            /**< @brief 无错误 */
    FSCK_ERR_INVALID_SUPERBLOCK,   /**< @brief 超级块无效 */
    FSCK_ERR_INVALID_FAT,          /**< @brief FAT 表无效 */
    FSCK_ERR_INVALID_INODE,        /**< @brief inode 无效 */
    FSCK_ERR_INVALID_DIRECTORY,    /**< @brief 目录无效 */
    FSCK_ERR_INVALID_FILE_SIZE,    /**< @brief 文件大小无效 */
    FSCK_ERR_CROSS_LINKED_FILES,   /**< @brief 文件交叉链接 */
    FSCK_ERR_ORPHAN_INODES,        /**< @brief 孤立 inode */
    FSCK_ERR_BAD_BLOCKS,           /**< @brief 坏块 */
    FSCK_ERR_CLUSTER_CHAIN_ERROR,   /**< @brief 簇链错误 */
    FSCK_ERR_CHECKSUM_ERROR,       /**< @brief 校验和错误 */
    FSCK_ERR_MOUNTED_FS,            /**< @brief 文件系统已挂载 */
    FSCK_ERR_UNKNOWN               /**< @brief 未知错误 */
} fsck_error_type_t;

/* ========================================================================
 * fsck 检查统计
 * ======================================================================== */

/**
 * @brief fsck 检查统计
 */
typedef struct
{
    uint32_t files_checked;       /**< @brief 已检查的文件数量 */
    uint32_t dirs_checked;        /**< @brief 已检查的目录数量 */
    uint32_t inodes_checked;      /**< @brief 已检查的 inode 数量 */
    uint32_t clusters_checked;    /**< @brief 已检查的簇数量 */
    uint32_t bad_blocks_found;    /**< @brief 发现的坏块数量 */
    uint64_t total_size;          /**< @brief 总大小（字节） */
    uint64_t used_size;           /**< @brief 已使用大小（字节） */
    uint64_t free_size;           /**< @brief 空闲大小（字节） */
} fsck_stats_t;

/* ========================================================================
 * fsck 操作接口
 * ======================================================================== */

/**
 * @brief fsck 操作接口
 */
typedef struct fsck_ops
{
    /**
     * @brief 检查文件系统
     *
     * @param device_path 设备路径
     * @param options 检查选项
     * @param result 输出检查结果
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*check)(const char *device_path,
                       const fsck_options_t *options,
                       fsck_result_t *result);

    /**
     * @brief 修复文件系统
     *
     * @param device_path 设备路径
     * @param options 检查选项
     * @param result 输出检查结果
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*repair)(const char *device_path,
                        const fsck_options_t *options,
                        fsck_result_t *result);

    /**
     * @brief 获取文件系统状态
     *
     * @param device_path 设备路径
     * @param stats 输出统计信息
     *
     * @return 0 成功，<0 失败
     */
    int32_t (*get_stats)(const char *device_path,
                          fsck_stats_t *stats);

} fsck_ops_t;

#endif /* FSCK_TYPES_H */
