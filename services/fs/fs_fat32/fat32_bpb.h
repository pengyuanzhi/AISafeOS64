/**
 * @file    fat32_bpb.h
 * @brief   FAT32 BPB 解析头文件
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 BPB（BIOS Parameter Block）结构定义和解析接口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_FAT32_BPB_H
#define FS_FAT32_BPB_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * FAT32 常量
 * ======================================================================== */

/** @brief 扇区大小（字节） */
#define FAT32_BYTES_PER_SEC      512U

/** @brief 最大簇大小（字节） */
#define FAT32_MAX_CLUSTER_SIZE   (128U * 512U)

/** @brief FAT32 簇范围（2 - 0x0FFFFFEF） */
#define FAT32_MIN_CLUSTER       2U
#define FAT32_MAX_CLUSTER       0x0FFFFFEFU

/** @brief FAT32 特殊簇值 */
#define FAT32_FREE_CLUSTER     0x00000000U
#define FAT32_BAD_CLUSTER     0x0FFFFFF7U
#define FAT32_EOC_CLUSTER    0x0FFFFFFFU

/* ========================================================================
 * FAT32 目录项属性
 * ======================================================================== */

/** @brief 目录项属性位掩码 */
#define FAT32_ATTR_READ_ONLY   0x01U
#define FAT32_ATTR_HIDDEN     0x02U
#define FAT32_ATTR_SYSTEM      0x04U
#define FAT32_ATTR_VOLUME_ID  0x08U
#define FAT32_ATTR_DIRECTORY  0x10U
#define FAT32_ATTR_ARCHIVE    0x20U
#define FAT32_ATTR_LONG_NAME  0x0FU

/* ========================================================================
 * FAT32 目录项结构
 * ======================================================================== */

/**
 * @brief FAT32 标准目录项（32 字节）
 */
typedef struct
{
    uint8_t  name[8];          /**< @brief 文件名（8.3 格式） */
    uint8_t  ext[3];           /**< @brief 扩展名 */
    uint8_t  attr;             /**< @brief 属性 */
    uint8_t  nt_res;           /**< @brief 保留 */
    uint8_t  create_time_tenth; /**< @brief 创建时间（十分之一秒） */
    uint16_t create_time;      /**< @brief 创建时间 */
    uint16_t create_date;      /**< @brief 创建日期 */
    uint16_t last_access_date; /**< @brief 最后访问日期 */
    uint16_t first_cluster_hi; /**< @brief 首簇号高 16 位 */
    uint16_t write_time;       /**< @brief 写入时间 */
    uint16_t write_date;       /**< @brief 写入日期 */
    uint16_t first_cluster_lo; /**< @brief 首簇号低 16 位 */
    uint32_t file_size;       /**< @brief 文件大小 */
} __attribute__((packed)) fat32_dir_entry_t;

/* ========================================================================
 * FAT32 BPB 结构
 * ======================================================================== */

/**
 * @brief FAT32 BIOS Parameter Block (BPB)
 */
typedef struct
{
    uint8_t  boot_jmp[3];      /**< @brief 跳转指令 */
    uint8_t  oem_name[8];      /**< @brief OEM 名称 */
    uint16_t bytes_per_sec;    /**< @brief 每扇区字节数（必须为 512） */
    uint8_t  sec_per_clust;    /**< @brief 每簇扇区数（1, 2, 4, 8, 16, 32, 64, 128） */
    uint16_t res_sec;          /**< @brief 引导扇区后、FAT 表前的保留扇区数 */
    uint8_t  fat_copies;       /**< @brief FAT 表副本数（通常为 2） */
    uint16_t root_ent_max;     /**< @brief 根目录项数 */
    uint16_t tot_sec16;        /**< @brief 总扇区数（16 位） */
    uint8_t  media_desc;       /**< @brief 媒体描述符 */
    uint16_t sec_per_fat;      /**< @brief 每个 FAT 表的扇区数 */
    uint16_t sec_per_track;    /**< @brief 每磁道扇区数 */
    uint16_t heads;            /**< @brief 磁头数 */
    uint32_t hidden_sec;       /**< @brief 隐藏扇区数 */
    uint32_t tot_sec32;        /**< @brief 总扇区数（32 位） */
    uint8_t  drive_num;        /**< @brief 驱动器号 */
    uint8_t  signature;        /**< @brief MBR 签名（0x29） */
    uint32_t volume_serial;    /**< @brief 卷序列号 */
    uint8_t  vol_label[11];    /**< @brief 卷标 */
    uint8_t  fs_type[8];        /**< @brief 文件系统类型 ("FAT32   ") */
} __attribute__((packed)) fat32_bpb_t;

/**
 * @brief FAT32 扩展 BPB (EBPB)
 */
typedef struct
{
    uint32_t bytes_per_fat;    /**< @brief 每个 FAT 表的字节数 */
    uint16_t ext_flags;        /**< @brief 扩展标志 */
    uint16_t fs_ver;           /**< @brief 文件系统版本 */
    uint32_t root_cluster;      /**< @brief 根目录首簇号 */
    uint16_t fs_info_sec;      /**< @brief FSINFO 扇区号 */
    uint16_t backup_boot_sec;   /**< @brief 备份引导扇区号 */
    uint8_t  reserved[12];     /**< @brief 保留 */
} __attribute__((packed)) fat32_ebpb_t;

/* ========================================================================
 * FAT32 卷信息结构
 * ======================================================================== */

/**
 * @brief FAT32 卷信息
 */
typedef struct
{
    uint16_t bytes_per_sec;    /**< @brief 每扇区字节数 */
    uint8_t  sec_per_clust;    /**< @brief 每簇扇区数 */
    uint32_t bytes_per_clust;   /**< @brief 每簇字节数 */
    uint16_t res_sec;          /**< @brief 保留扇区数 */
    uint8_t  fat_copies;       /**< @brief FAT 表副本数 */
    uint32_t bytes_per_fat;    /**< @brief 每个 FAT 表的字节数 */
    uint32_t fat_start_sec;    /**< @brief FAT 表起始扇区号 */
    uint32_t data_start_sec;    /**< @brief 数据区起始扇区号 */
    uint32_t root_cluster;      /**< @brief 根目录首簇号 */
    uint32_t tot_clusters;      /**< @brief 总簇数 */
    bool     valid;            /**< @brief 卷信息是否有效 */
} fat32_volume_info_t;

/* ========================================================================
 * FAT32 BPB 解析接口
 * ======================================================================== */

/**
 * @brief 解析 FAT32 BPB
 *
 * @param bpb         BPB 数据
 * @param volume_info 输出卷信息
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_parse_bpb(const uint8_t *bpb, fat32_volume_info_t *volume_info);

/**
 * @brief 验证 FAT32 BPB
 *
 * @param bpb BPB 数据
 *
 * @return true 有效，false 无效
 */
bool fat32_validate_bpb(const uint8_t *bpb);

/**
 * @brief 计算簇的扇区号
 *
 * @param volume_info 卷信息
 * @param cluster     簇号
 *
 * @return 扇区号
 */
uint32_t fat32_cluster_to_sec(const fat32_volume_info_t *volume_info,
                               uint32_t cluster);

#endif /* FS_FAT32_BPB_H */
