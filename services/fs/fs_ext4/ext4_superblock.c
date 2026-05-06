/**
 * @file    ext4_superblock.c
 * @brief   Ext4 超级块管理实现
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 超级块管理实现：
 *          - 读取和解析 Ext4 超级块
 *          - 验证 Ext4 魔数
 *          - 解析卷信息
 *          - 特性检查
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "ext4_superblock.h"
#include <string.h>

/* ========================================================================
 * 内部变量
 * ======================================================================== */

/** @brief 缓存的超级块 */
static ext4_superblock_t s_cached_sb;
static bool s_sb_cached = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 从磁盘读取块（模拟）
 *
 * @param block_id 块 ID
 * @param buf      输出缓冲区
 *
 * @return 0 成功，<0 失败
 */
static int32_t mock_block_read(uint32_t block_id, void *buf)
{
    if (buf == NULL)
    {
        return -22; /* EINVAL */
    }

    /* 模拟数据：设置 Ext4 魔数 */
    if (block_id == EXT4_SUPERBLOCK_OFFSET)
    {
        (void)memset(buf, 0, EXT4_SUPERBLOCK_SIZE);
        ext4_superblock_t *sb = (ext4_superblock_t *)buf;

        sb->s_inodes_count = 1000000U;
        sb->s_blocks_count = 1000000U;
        sb->s_first_data_block = 0U;
        sb->s_log_block_size = 2U; /* 4KB */
        sb->s_blocks_per_group = 32768U;
        sb->s_inodes_per_group = 8192U;
        sb->s_magic = EXT4_MAGIC;
        sb->s_state = EXT4_FS_CLEAN;
        sb->s_feature_compat = EXT4_FEATURE_COMPAT_SUPP;
        sb->s_feature_incompat = EXT4_FEATURE_INCOMPAT_SUPP;
        sb->s_feature_ro_compat = EXT4_FEATURE_RO_COMPAT_SUPP;

        return 0;
    }

    return -5; /* EIO */
}

/**
 * @brief 转换块号到偏移量
 *
 * @param block_id 块 ID
 *
 * @return 偏移量
 */
static uint64_t block_to_offset(uint32_t block_id)
{
    return (uint64_t)block_id * (uint64_t)EXT4_BLOCK_SIZE;
}

/* ========================================================================
 * 超级块接口实现
 * ======================================================================== */

/**
 * @brief 获取超级块
 *
 * @param dev_id 设备 ID
 * @param sb     输出超级块
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_get_superblock(uint32_t dev_id, ext4_superblock_t *sb)
{
    int32_t ret;

    if (sb == NULL)
    {
        return -22; /* EINVAL */
    }

    /* 模拟读取超级块 */
    ret = mock_block_read(EXT4_SUPERBLOCK_OFFSET, sb);

    if (ret == 0)
    {
        (void)memcpy(&s_cached_sb, sb, sizeof(ext4_superblock_t));
        s_sb_cached = true;
    }

    return ret;
}

/**
 * @brief 验证超级块
 *
 * @param sb 超级块
 *
 * @return true 有效，false 无效
 */
bool ext4_validate_superblock(const ext4_superblock_t *sb)
{
    if (sb == NULL)
    {
        return false;
    }

    /* 检查魔数 */
    if (!ext4_check_magic(sb))
    {
        return false;
    }

    /* 检查块大小 */
    if (sb->s_log_block_size > 4U)
    {
        return false;
    }

    /* 检查每组块数 */
    if (sb->s_blocks_per_group == 0U)
    {
        return false;
    }

    /* 检查每组 inode 数 */
    if (sb->s_inodes_per_group == 0U)
    {
        return false;
    }

    return true;
}

/**
 * @brief 检查 Ext4 魔数
 *
 * @param sb 超级块
 *
 * @return true 有效，false 无效
 */
bool ext4_check_magic(const ext4_superblock_t *sb)
{
    if (sb == NULL)
    {
        return false;
    }

    return (sb->s_magic == EXT4_MAGIC);
}

/**
 * @brief 检查文件系统状态
 *
 * @param sb 超级块
 *
 * @return 文件系统状态
 */
ext4_fs_state_t ext4_get_fs_state(const ext4_superblock_t *sb)
{
    if (sb == NULL)
    {
        return EXT4_FS_ERROR;
    }

    return (ext4_fs_state_t)sb->s_state;
}

/**
 * @brief 检查兼容特性
 *
 * @param sb     超级块
 * @param feature 特性
 *
 * @return true 支持，false 不支持
 */
bool ext4_has_feature(const ext4_superblock_t *sb, uint32_t feature)
{
    if (sb == NULL)
    {
        return false;
    }

    return (sb->s_feature_compat & feature) == feature;
}

/**
 * @brief 检查不兼容特性
 *
 * @param sb     超级块
 * @param feature 特性
 *
 * @return true 支持，false 不支持
 */
bool ext4_has_incompat_feature(const ext4_superblock_t *sb, uint32_t feature)
{
    if (sb == NULL)
    {
        return false;
    }

    return (sb->s_feature_incompat & feature) == feature;
}

/**
 * @brief 检查只读兼容特性
 *
 * @param sb     超级块
 * @param feature 特性
 *
 * @return true 支持，false 不支持
 */
bool ext4_has_rocompat_feature(const ext4_superblock_t *sb, uint32_t feature)
{
    if (sb == NULL)
    {
        return false;
    }

    return (sb->s_feature_ro_compat & feature) == feature;
}

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 解析特性
 *
 * @param sb     超级块
 * @param features 特性结构
 */
void ext4_parse_features(const ext4_superblock_t *sb, ext4_features_t *features)
{
    if (sb == NULL || features == NULL)
    {
        return;
    }

    features->journaling = ext4_has_incompat_feature(sb, 0x00000002U);
    features->resize_inode = ext4_has_incompat_feature(sb, 0x00000004U);
    features->sparse_super = ext4_has_feature(sb, 0x0001U);
    features->huge_file = ext4_has_rocompat_feature(sb, 0x0002U);
    features->dir_nlink = ext4_has_rocompat_feature(sb, 0x0004U);
    features->ext_attr = ext4_has_rocompat_feature(sb, 0x0008U);
}

/**
 * @brief 格式化超级块信息
 *
 * @param sb 超级块
 *
 * @return 格式化后的字符串
 */
const char *ext4_format_superblock(const ext4_superblock_t *sb)
{
    static char buffer[512];
    ext4_features_t features;

    if (sb == NULL)
    {
        return "Invalid superblock";
    }

    ext4_parse_features(sb, &features);

    (void)snprintf(buffer, sizeof(buffer),
                   "Ext4 Superblock:\n"
                   "  Magic: 0x%08X\n"
                   "  Inode count: %u\n"
                   "  Block count: %u\n"
                   "  Block size: %u bytes\n"
                   "  Groups: %u blocks, %u inodes\n"
                   "  State: %s\n"
                   "  Features: %s\n",
                   sb->s_magic,
                   sb->s_inodes_count,
                   sb->s_blocks_count,
                   (1U << sb->s_log_block_size),
                   sb->s_blocks_per_group,
                   sb->s_inodes_per_group,
                   (sb->s_state == EXT4_FS_CLEAN) ? "Clean" : "Dirty",
                   features.journaling ? "Journaling " : "");

    return buffer;
}
