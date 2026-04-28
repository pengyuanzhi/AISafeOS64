/**
 * @file    fat32_types.h
 * @brief   FAT32 文件系统类型定义
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 文件系统核心数据类型定义：
 *          - BPB (BIOS Parameter Block) 结构
 *          - FAT 表项定义
 *          - 目录项结构
 *          - 文件系统运行时结构
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_FAT32_TYPES_H
#define FS_FAT32_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * FAT32 常量定义
 * ======================================================================== */

/** @brief 扇区大小（字节） */
#define FAT32_SECTOR_SIZE          512U

/** @brief 最大簇数（FAT32 规范） */
#define FAT32_MAX_CLUSTERS         0x0FFFFFF0U

/** @brief FAT 表项：空闲簇 */
#define FAT32_CLUSTER_FREE         0x00000000U

/** @brief FAT 表项：保留簇起始 */
#define FAT32_CLUSTER_RESERVED     0x0FFFFFF0U

/** @brief FAT 表项：坏簇 */
#define FAT32_CLUSTER_BAD          0x0FFFFFF7U

/** @brief FAT 表项：文件结束标记 */
#define FAT32_CLUSTER_EOC          0x0FFFFFF8U

/** @brief FAT 表项：最大 EOC 值 */
#define FAT32_CLUSTER_EOC_MAX      0x0FFFFFFFU

/** @brief 根目录簇号 */
#define FAT32_ROOT_CLUSTER         2U

/** @brief 最大路径长度 */
#define FAT32_MAX_PATH             260U

/** @brief 文件名最大长度（8.3 格式） */
#define FAT32_NAME_MAX             13U

/** @brief LFN 条目最大数量 */
#define FAT32_LFN_ENTRIES_MAX      20U

/** @brief 目录项大小 */
#define FAT32_DIR_ENTRY_SIZE       32U

/** @brief 每扇区目录项数 */
#define FAT32_DIR_ENTRIES_PER_SEC  (FAT32_SECTOR_SIZE / FAT32_DIR_ENTRY_SIZE)

/** @brief BPB 签名 */
#define FAT32_BPB_SIGNATURE        0xAA55U

/** @brief FAT32 文件系统类型字符串 */
#define FAT32_FSTYPE_STR           "FAT32   "

/** @brief FAT 表副本数量 */
#define FAT32_FAT_COPIES           2U

/* ========================================================================
 * FAT32 目录项属性标志
 * ======================================================================== */

/** @brief 只读属性 */
#define FAT32_ATTR_READ_ONLY       0x01U

/** @brief 隐藏属性 */
#define FAT32_ATTR_HIDDEN          0x02U

/** @brief 系统属性 */
#define FAT32_ATTR_SYSTEM          0x04U

/** @brief 卷标属性 */
#define FAT32_ATTR_VOLUME_ID       0x08U

/** @brief 目录属性 */
#define FAT32_ATTR_DIRECTORY       0x10U

/** @brief 归档属性 */
#define FAT32_ATTR_ARCHIVE         0x20U

/** @brief LFN 属性掩码 */
#define FAT32_ATTR_LFN_MASK        0x3FU

/** @brief LFN 属性值 */
#define FAT32_ATTR_LFN             0x0FU

/* ========================================================================
 * BPB (BIOS Parameter Block) 结构
 * ======================================================================== */

/**
 * @brief FAT32 BPB 结构（位于卷起始扇区）
 *
 * @details BPB 包含文件系统基本参数：
 *          - 扇区大小、簇大小
 *          - FAT 表位置和大小
 *          - 根目录位置
 */
typedef struct __attribute__((packed)) fat32_bpb
{
    /* ---- 跳转指令 ---- */
    uint8_t     jmp_boot[3];        /**< @brief 跳转指令 */
    uint8_t     oem_name[8];        /**< @brief OEM 名称 */

    /* ---- BPB (BIOS Parameter Block) ---- */
    uint16_t    bytes_per_sec;      /**< @brief 每扇区字节数 */
    uint8_t     sec_per_clust;      /**< @brief 每簇扇区数 */
    uint16_t    rsvd_sec_cnt;       /**< @brief 保留扇区数 */
    uint8_t     num_fats;           /**< @brief FAT 表副本数 */
    uint16_t    root_ent_cnt;       /**< @brief 根目录项数（FAT32 为 0） */
    uint16_t    total_sec16;        /**< @brief 总扇区数（16位，FAT32 为 0） */
    uint8_t     media_type;         /**< @brief 介质类型 */
    uint16_t    fat_sz16;           /**< @brief FAT 表大小（16位，FAT32 为 0） */
    uint16_t    sec_per_trk;        /**< @brief 每磁道扇区数 */
    uint16_t    num_heads;          /**< @brief 磁头数 */
    uint32_t    hidd_sec;           /**< @brief 隐藏扇区数 */
    uint32_t    total_sec32;        /**< @brief 总扇区数（32位） */

    /* ---- FAT32 扩展 BPB ---- */
    uint32_t    fat_sz32;           /**< @brief FAT 表大小（扇区数） */
    uint16_t    ext_flags;          /**< @brief 扩展标志 */
    uint16_t    fs_ver;             /**< @brief 文件系统版本 */
    uint32_t    root_cluster;       /**< @brief 根目录簇号 */
    uint16_t    fs_info_sec;        /**< @brief FSINFO 扇区号 */
    uint16_t    bk_boot_sec;        /**< @brief 备份引导扇区号 */
    uint8_t     reserved[12];       /**< @brief 保留 */
    uint8_t     drv_num;            /**< @brief 驱动器号 */
    uint8_t     reserved1;          /**< @brief 保留 */
    uint8_t     boot_sig;           /**< @brief 扩展引导签名 */
    uint32_t    vol_id;             /**< @brief 卷序列号 */
    uint8_t     vol_label[11];      /**< @brief 卷标 */
    uint8_t     fs_type[8];         /**< @brief 文件系统类型 */
} fat32_bpb_t;

/* ========================================================================
 * FAT32 目录项结构
 * ======================================================================== */

/**
 * @brief FAT32 标准目录项结构（32 字节）
 *
 * @details 目录项包含文件/目录的元数据：
 *          - 文件名（8.3 格式）
 *          - 文件属性
 *          - 文件大小
 *          - 起始簇号
 *          - 时间戳
 */
typedef struct __attribute__((packed)) fat32_dir_entry
{
    uint8_t     name[11];           /**< @brief 文件名（8+3 格式） */
    uint8_t     attr;               /**< @brief 文件属性 */
    uint8_t     nt_res;             /**< @brief NT 保留 */
    uint8_t     crt_time_tenth;     /**< @brief 创建时间（十分之一秒） */
    uint16_t    crt_time;           /**< @brief 创建时间 */
    uint16_t    crt_date;           /**< @brief 创建日期 */
    uint16_t    lst_acc_date;       /**< @brief 最后访问日期 */
    uint16_t    fst_clus_hi;        /**< @brief 起始簇号高16位 */
    uint16_t    wrt_time;           /**< @brief 最后写入时间 */
    uint16_t    wrt_date;           /**< @brief 最后写入日期 */
    uint16_t    fst_clus_lo;        /**< @brief 起始簇号低16位 */
    uint32_t    file_size;          /**< @brief 文件大小（字节） */
} fat32_dir_entry_t;

/**
 * @brief FAT32 LFN 目录项结构（32 字节）
 *
 * @details LFN（长文件名）目录项用于存储长文件名
 */
typedef struct __attribute__((packed)) fat32_lfn_entry
{
    uint8_t     order;              /**< @brief 序号 */
    uint16_t    name1[5];           /**< @brief 文件名第1-5字符（UTF-16） */
    uint8_t     attr;               /**< @brief 属性（必须为 0x0F） */
    uint8_t     type;               /**< @brief 类型 */
    uint8_t     checksum;           /**< @brief 短文件名校验和 */
    uint16_t    name2[6];           /**< @brief 文件名第6-11字符（UTF-16） */
    uint16_t    fst_clus_lo;        /**< @brief 起始簇号（必须为 0） */
    uint16_t    name3[2];           /**< @brief 文件名第12-13字符（UTF-16） */
} fat32_lfn_entry_t;

/* ========================================================================
 * FAT32 文件系统运行时结构
 * ======================================================================== */

/** @brief 块设备读取回调函数类型 */
typedef int32_t (*fat32_block_read_fn)(uint64_t sector, void *buf, uint32_t count);

/** @brief 块设备写入回调函数类型 */
typedef int32_t (*fat32_block_write_fn)(uint64_t sector, const void *buf, uint32_t count);

/**
 * @brief FAT32 文件系统运行时上下文
 *
 * @details 包含已解析的 BPB 参数和运行时状态
 */
typedef struct fat32_context
{
    /* BPB 解析后的参数 */
    uint32_t    bytes_per_sec;      /**< @brief 每扇区字节数 */
    uint32_t    sec_per_clust;      /**< @brief 每簇扇区数 */
    uint32_t    cluster_size;       /**< @brief 簇大小（字节） */
    uint32_t    rsvd_sec_cnt;       /**< @brief 保留扇区数 */
    uint32_t    num_fats;           /**< @brief FAT 表副本数 */
    uint32_t    fat_sz32;           /**< @brief FAT 表大小（扇区数） */
    uint32_t    root_cluster;       /**< @brief 根目录簇号 */
    uint32_t    total_sectors;      /**< @brief 总扇区数 */
    uint32_t    data_sec_start;     /**< @brief 数据区起始扇区 */
    uint32_t    total_clusters;     /**< @brief 总簇数 */

    /* 块设备接口 */
    fat32_block_read_fn   block_read;   /**< @brief 块设备读取回调 */
    fat32_block_write_fn  block_write;  /**< @brief 块设备写入回调 */

    /* 块设备缓冲区（单扇区） */
    uint8_t     sector_buf[FAT32_SECTOR_SIZE]; /**< @brief 扇区缓冲区 */

    /* FSINFO 信息 */
    uint32_t    free_cluster_count; /**< @brief 空闲簇计数 */
    uint32_t    next_free_cluster;  /**< @brief 下一个空闲簇 */

    /* 状态 */
    bool        mounted;            /**< @brief 挂载标志 */
} fat32_context_t;

/**
 * @brief FAT32 文件句柄
 *
 * @details 用于跟踪已打开文件的状态
 */
typedef struct fat32_file_handle
{
    uint32_t    first_cluster;      /**< @brief 文件起始簇号 */
    uint32_t    current_cluster;    /**< @brief 当前簇号 */
    uint32_t    file_size;          /**< @brief 文件大小 */
    uint32_t    position;           /**< @brief 当前读写位置 */
    uint8_t     attributes;         /**< @brief 文件属性 */
    bool        in_use;             /**< @brief 使用标志 */
} fat32_file_handle_t;

/** @brief 最大同时打开文件数 */
#define FAT32_MAX_OPEN_FILES    16U

/**
 * @brief FAT32 文件系统完整实例
 *
 * @details 包含文件系统上下文和文件句柄表
 */
typedef struct fat32_instance
{
    fat32_context_t      context;          /**< @brief 文件系统上下文 */
    fat32_file_handle_t  files[FAT32_MAX_OPEN_FILES]; /**< @brief 文件句柄表 */
} fat32_instance_t;

/* ========================================================================
 * 辅助内联函数
 * ======================================================================== */

/**
 * @brief 判断 FAT 表项是否为 EOC（文件结束）
 *
 * @param cluster 簇号
 *
 * @return true 是 EOC，false 不是
 */
static inline bool fat32_is_eoc(uint32_t cluster)
{
    return (cluster >= FAT32_CLUSTER_EOC);
}

/**
 * @brief 判断 FAT 表项是否为空闲簇
 *
 * @param cluster 簇号
 *
 * @return true 是空闲簇，false 不是
 */
static inline bool fat32_is_free(uint32_t cluster)
{
    return (cluster == FAT32_CLUSTER_FREE);
}

/**
 * @brief 判断 FAT 表项是否为坏簇
 *
 * @param cluster 簇号
 *
 * @return true 是坏簇，false 不是
 */
static inline bool fat32_is_bad(uint32_t cluster)
{
    return (cluster == FAT32_CLUSTER_BAD);
}

/**
 * @brief 判断目录项是否为空（末尾标记）
 *
 * @param entry 目录项指针
 *
 * @return true 是空项，false 不是
 */
static inline bool fat32_dir_entry_is_empty(const fat32_dir_entry_t *entry)
{
    return (entry->name[0] == 0x00U);
}

/**
 * @brief 判断目录项是否已删除
 *
 * @param entry 目录项指针
 *
 * @return true 已删除，false 未删除
 */
static inline bool fat32_dir_entry_is_deleted(const fat32_dir_entry_t *entry)
{
    return (entry->name[0] == 0xE5U);
}

/**
 * @brief 判断目录项是否为 LFN 条目
 *
 * @param entry 目录项指针
 *
 * @return true 是 LFN 条目，false 不是
 */
static inline bool fat32_dir_entry_is_lfn(const fat32_dir_entry_t *entry)
{
    return ((entry->attr & FAT32_ATTR_LFN_MASK) == FAT32_ATTR_LFN);
}

/**
 * @brief 获取目录项的起始簇号
 *
 * @param entry 目录项指针
 *
 * @return 起始簇号
 */
static inline uint32_t fat32_dir_entry_cluster(const fat32_dir_entry_t *entry)
{
    return ((uint32_t)entry->fst_clus_hi << 16U) |
           (uint32_t)entry->fst_clus_lo;
}

/**
 * @brief 簇号转换为扇区号
 *
 * @param ctx FAT32 上下文
 * @param cluster 簇号
 *
 * @return 起始扇区号
 */
static inline uint32_t fat32_cluster_to_sector(const fat32_context_t *ctx,
                                                uint32_t cluster)
{
    return ((cluster - 2U) * ctx->sec_per_clust) + ctx->data_sec_start;
}

#endif /* FS_FAT32_TYPES_H */
