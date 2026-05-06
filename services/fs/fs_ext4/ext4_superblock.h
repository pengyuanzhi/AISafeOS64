/**
 * @file    ext4_superblock.h
 * @brief   Ext4 超级块管理头文件
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 超级块管理接口：
 *          - 读取和解析 Ext4 超级块
 *          - 验证 Ext4 魔数
 *          - 解析卷信息
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef EXT4_SUPERBLOCK_H
#define EXT4_SUPERBLOCK_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief Ext4 超级块偏移（512 字节块） */
#define EXT4_SUPERBLOCK_OFFSET        1024U

/** @brief Ext4 超级块大小（通常是 1024 字节） */
#define EXT4_SUPERBLOCK_SIZE          1024U

/** @brief Ext4 超级块版本（新版） */
#define EXT4_FEATURE_COMPAT_SUPP      0x00000001U

/** @brief Ext4 不兼容特性 */
#define EXT4_FEATURE_INCOMPAT_SUPP    0x00000001U

/** @brief Ext4 只读兼容特性 */
#define EXT4_FEATURE_RO_COMPAT_SUPP    0x00000001U

/* ========================================================================
 * 特性标志
 * ======================================================================== */

/** @brief 文件系统特性 */
typedef struct
{
    bool   journaling;         /**< @brief 日志支持 */
    bool   resize_inode;       /**< @brief 支持调整文件系统大小 */
    bool   sparse_super;       /**< @brief 稀疏超级块 */
    bool   huge_file;          /**< @brief 大文件支持 */
    bool   dir_nlink;          /**< @brief 目录硬链接计数 */
    bool   ext_attr;           /**< @support 扩展属性 */
} ext4_features_t;

/* ========================================================================
 * 超级块状态
 * ======================================================================== */

/** @brief 文件系统状态 */
typedef enum
{
    EXT4_FS_CLEAN    = 0U,     /**< @brief 清洁状态 */
    EXT4_FS_DIRTY   = 1U,     /**< @brief 脏状态 */
    EXT4_FS_ERROR   = 2U      /**< @brief 错误状态 */
} ext4_fs_state_t;

/* ========================================================================
 * 超级块接口
 * ======================================================================== */

/**
 * @brief 获取超级块
 *
 * @param dev_id 设备 ID
 * @param sb     输出超级块
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_get_superblock(uint32_t dev_id, ext4_superblock_t *sb);

/**
 * @brief 验证超级块
 *
 * @param sb 超级块
 *
 * @return true 有效，false 无效
 */
bool ext4_validate_superblock(const ext4_superblock_t *sb);

/**
 * @brief 检查 Ext4 魔数
 *
 * @param sb 超级块
 *
 * @return true 有效，false 无效
 */
bool ext4_check_magic(const ext4_superblock_t *sb);

/**
 * @brief 检查文件系统状态
 *
 * @param sb 超级块
 *
 * @return 文件系统状态
 */
ext4_fs_state_t ext4_get_fs_state(const ext4_superblock_t *sb);

/**
 * @brief 检查兼容特性
 *
 * @param sb     超级块
 * @param feature 特性
 *
 * @return true 支持，false 不支持
 */
bool ext4_has_feature(const ext4_superblock_t *sb, uint32_t feature);

/**
 * @brief 检查不兼容特性
 *
 * @param sb     超级块
 * @param feature 特性
 *
 * @return true 支持，false 不支持
 */
bool ext4_has_incompat_feature(const ext4_superblock_t *sb, uint32_t feature);

/**
 * @brief 检查只读兼容特性
 *
 * @param sb     超级块
 * @param feature 特性
 *
 * @return true 支持，false 不支持
 */
bool ext4_has_rocompat_feature(const ext4_superblock_t *sb, uint32_t feature);

#endif /* EXT4_SUPERBLOCK_H */
