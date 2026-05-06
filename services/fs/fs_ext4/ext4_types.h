/**
 * @file    ext4_types.h
 * @brief   Ext4 文件系统类型定义
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 文件系统类型定义，参考 BSD ext4 实现
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef EXT4_TYPES_H
#define EXT4_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * Ext4 常量定义
 * ======================================================================== */

/** @brief Ext4 魔数 */
#define EXT4_MAGIC               0xEF53U

/** @brief 最大文件名长度 */
#define EXT4_NAME_LEN            255U

/** @brief 最大路径长度 */
#define EXT4_PATH_MAX            4096U

/** @brief 默认块大小（4KB） */
#define EXT4_BLOCK_SIZE          4096U

/** @brief 默认块组大小 */
#define EXT4_BLOCKS_PER_GROUP    32768U

/** @brief Inode 表大小 */
#define EXT4_INODE_SIZE          256U

/** @brief 直接块指针数量 */
#define EXT4_NDIR_BLOCKS         12U

/** @brief 间接块指针层级 */
#define EXT4_IND_BLOCK           12U
#define EXT4_DIND_BLOCK          13U
#define EXT4_TIND_BLOCK          14U

/** @brief 文件类型 */
#define EXT4_S_IFREG            0100000U  /* 普通文件 */
#define EXT4_S_IFDIR            0040000U  /* 目录 */
#define EXT4_S_IFCHR            0020000U  /* 字符设备 */
#define EXT4_S_IFBLK            0060000U  /* 块设备 */
#define EXT4_S_IFIFO            0010000U  /* FIFO */
#define EXT4_S_IFSOCK           0140000U  /* 套接字 */
#define EXT4_S_IFLNK            0120000U  /* 符号链接 */

/** @brief 权限掩码 */
#define EXT4_S_IRWXU            00700U     /* 用户权限 */
#define EXT4_S_IRUSR            00400U     /* 用户读 */
#define EXT4_S_IWUSR            00200U     /* 用户写 */
#define EXT4_S_IXUSR            00100U     /* 用户执行 */
#define EXT4_S_IRWXG            00070U     /* 组权限 */
#define EXT4_S_IRGRP            00040U     /* 组读 */
#define EXT4_S_IWGRP            00020U     /* 组写 */
#define EXT4_S_IXGRP            00010U     /* 组执行 */
#define EXT4_S_IRWXO            00007U     /* 其他权限 */
#define EXT4_S_IROTH            00004U     /* 其他读 */
#define EXT4_S_IWOTH            00002U     /* 其他写 */
#define EXT4_S_IXOTH            00001U     /* 其他执行 */

/* ========================================================================
 * Ext4 超级块结构
 * ======================================================================== */

/**
 * @brief Ext4 超级块
 */
typedef struct ext4_superblock
{
    uint32_t s_inodes_count;      /**< @brief Inode 总数 */
    uint32_t s_blocks_count;     /**< @brief 块总数 */
    uint32_t s_r_blocks_count;   /**< @brief 保留块数 */
    uint32_t s_free_blocks_count; /**< @brief 空闲块数 */
    uint32_t s_free_inodes_count; /**< @brief 空闲 inode 数 */
    uint32_t s_first_data_block; /**< @brief 第一个数据块 */
    uint32_t s_log_block_size;   /**< @brief 块大小 log2 */
    uint32_t s_log_frag_size;    /**< @brief 碎片大小 log2 */
    uint32_t s_blocks_per_group; /**< @brief 每组块数 */
    uint32_t s_frags_per_group;  /**< @brief 每组碎片数 */
    uint32_t s_inodes_per_group; /**< @brief 每组 inode 数 */
    uint32_t s_mtime;            /**< @brief 挂载时间 */
    uint32_t s_wtime;            /**< @brief 写入时间 */
    uint16_t s_mnt_count;        /**< @brief 挂载计数 */
    uint16_t s_max_mnt_count;    /**< @brief 最大挂载计数 */
    uint16_t s_magic;            /**< @brief 魔数 0xEF53 */
    uint16_t s_state;            /**< @brief 文件系统状态 */
    uint16_t s_errors;           /**< @brief 错误行为 */
    uint16_t s_minor_rev_level;   /**< @brief 次要版本 */
    uint32_t s_lastcheck;        /**< @brief 最后检查时间 */
    uint32_t s_checkinterval;     /**< @brief 检查间隔 */
    uint32_t s_creator_os;       /**< @brief 创建者 OS */
    uint32_t s_rev_level;        /**< @brief 主版本 */
    uint16_t s_def_resuid;       /**< @brief 默认保留用户 ID */
    uint16_t s_def_resgid;       /**< @brief 默认保留组 ID */
    uint32_t s_first_ino;        /**< @brief 第一个 inode */
    uint16_t s_inode_size;       /**< @brief Inode 结构大小 */
    uint16_t s_block_group_nr;   /**< @brief 块组号 */
    uint32_t s_feature_compat;    /**< @brief 兼容特性 */
    uint32_t s_feature_incompat;  /**< @brief 不兼容特性 */
    uint32_t s_feature_ro_compat; /**< @brief 只读兼容特性 */
    uint8_t  s_uuid[16];         /**< @brief UUID */
    uint8_t  s_volume_name[16];   /**< @brief 卷名 */
    uint8_t  s_last_mounted[64];  /**< @brief 最后挂载路径 */
    /* ... 更多字段省略 */
} ext4_superblock_t;

/* ========================================================================
 * Ext4 Inode 结构
 * ======================================================================== */

/**
 * @brief Ext4 Inode
 */
typedef struct ext4_inode
{
    uint16_t i_mode;            /**< @brief 文件模式/类型 */
    uint16_t i_uid;             /**< @brief 用户 ID */
    uint32_t i_size;            /**< @brief 文件大小 */
    uint32_t i_atime;           /**< @brief 访问时间 */
    uint32_t i_ctime;           /**< @brief 创建时间 */
    uint32_t i_mtime;           /**< @brief 修改时间 */
    uint32_t i_dtime;           /**< @brief 删除时间 */
    uint16_t i_gid;             /**< @brief 组 ID */
    uint16_t i_links_count;      /**< @brief 链接计数 */
    uint32_t i_blocks;          /**< @brief 块数 */
    uint32_t i_flags;           /**< @brief 文件标志 */
    uint32_t i_block[15];       /**< @brief 块指针 */
    uint32_t i_generation;       /**< @brief inode 生成号 */
    uint32_t i_file_acl;        /**< @brief 文件 ACL */
    uint32_t i_dir_acl;         /**< @brief 目录 ACL */
    /* ... 更多字段省略 */
} ext4_inode_t;

/* ========================================================================
 * Ext4 目录项结构
 * ======================================================================== */

/**
 * @brief Ext4 目录项
 */
typedef struct ext4_dir_entry
{
    uint32_t inode;             /**< @brief Inode 编号 */
    uint16_t rec_len;          /**< @brief 记录长度 */
    uint8_t  name_len;         /**< @brief 文件名长度 */
    uint8_t  file_type;        /**< @brief 文件类型 */
    char     name[EXT4_NAME_LEN]; /**< @brief 文件名 */
} ext4_dir_entry_t;

/* ========================================================================
 * Ext4 实例
 * ======================================================================== */

/**
 * @brief Ext4 实例
 */
typedef struct ext4_instance
{
    ext4_superblock_t sb;       /**< @brief 超级块 */
    uint32_t              dev_id; /**< @brief 设备 ID */
    bool                  mounted;/**< @brief 挂载状态 */
} ext4_instance_t;

/* ========================================================================
 * Ext4 文件描述符
 * ======================================================================== */

/**
 * @brief Ext4 文件描述符
 */
typedef struct ext4_fd
{
    uint32_t      inode;        /**< @brief Inode 编号 */
    uint32_t      offset;       /**< @brief 当前偏移 */
    uint32_t      flags;        /**< @brief 打开标志 */
    bool          in_use;       /**< @brief 使用标记 */
} ext4_fd_t;

#endif /* EXT4_TYPES_H */
