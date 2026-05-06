/**
 * @file    ext4_dir.h
 * @brief   Ext4 目录操作头文件
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 目录操作接口：
 *          - 创建目录
 *          - 删除目录
 *          - 目录项查找
 *          - 目录列表
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef EXT4_DIR_H
#define EXT4_DIR_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 目录项结构
 * ======================================================================== */

/** @brief 最大文件名长度 */
#define EXT4_DIR_NAME_LEN        255U

/** @brief 最小目录项长度 */
#define EXT4_DIR_MIN_REC_LEN     8U

/** @brief Ext4 目录项 */
typedef struct ext4_dir_entry
{
    uint32_t      inode;        /**< @brief Inode 编号 */
    uint16_t      rec_len;      /**< @brief 记录长度 */
    uint8_t       name_len;     /**< @brief 文件名长度 */
    uint8_t       file_type;    /**< @brief 文件类型 */
    char          name[EXT4_DIR_NAME_LEN]; /**< @brief 文件名 */
} ext4_dir_entry_t;

/* ========================================================================
 * 文件类型
 * ======================================================================== */

/** @brief 目录项文件类型 */
typedef enum
{
    EXT4_FT_UNKNOWN       = 0U,
    EXT4_FT_REG_FILE      = 1U,
    EXT4_FT_DIR           = 2U,
    EXT4_FT_CHRDEV        = 3U,
    EXT4_FT_BLKDEV        = 4U,
    EXT4_FT_FIFO          = 5U,
    EXT4_FT_SOCK          = 6U,
    EXT4_FT_SYMLINK       = 7U
} ext4_file_type_t;

/* ========================================================================
 * 目录接口
 * ======================================================================== */

/**
 * @brief 创建目录
 *
 * @param parent_ino  父目录 Inode
 * @param name        目录名
 * @param mode        权限模式
 * @param uid         用户 ID
 * @param gid         组 ID
 *
 * @return Inode 编号（>=0 成胜），<0 失败
 */
int32_t ext4_mkdir(uint32_t parent_ino, const char *name,
                    uint32_t mode, uint32_t uid, uint32_t gid);

/**
 * @brief 删除目录
 *
 * @param ino    目录 Inode
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_rmdir(uint32_t ino);

/**
 * @brief 查找目录项
 *
 * @param parent_ino 父目录 Inode
 * @param name       文件名
 * @param entry      输出目录项
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_lookup(uint32_t parent_ino, const char *name,
                     ext4_dir_entry_t *entry);

/**
 * @brief 列出目录内容
 *
 * @param parent_ino 父目录 Inode
 * @param entries    输出目录项数组
 * @param max_count  最大条目数
 *
 * @return 实际条目数（>=0 成胜），<0 失败
 */
int32_t ext4_readdir(uint32_t parent_ino, ext4_dir_entry_t *entries,
                      uint32_t max_count);

/**
 * @brief 检查目录是否为空
 *
 * @param ino    目录 Inode
 *
 * @return true 空，false 非空
 */
bool ext4_is_dir_empty(uint32_t ino);

#endif /* EXT4_DIR_H */
