/**
 * @file    fat32_cache.h
 * @brief   FAT32 扇区写入缓存头文件
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 1.0
 *
 * @details FAT32 扇区写入缓存（掉电保护核心）
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_FAT32_CACHE_H
#define FS_FAT32_CACHE_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * FAT32 掉电保护常量
 * ======================================================================== */

/** @brief 缓存条目数 */
#define FAT32_CACHE_SIZE        256U

/** @brief 缓存条目状态 */
#define FAT32_CACHE_UNUSED      0U
#define FAT32_CACHE_DIRTY       1U

/** @brief 扇区标记为脏 */
#define FAT32_SECTOR_DIRTY      0x01U

/** @brief 扇区标记为已清理 */
#define FAT32_SECTOR_CLEAN      0x00U

/* ========================================================================
 * 缓存条目结构
 * ======================================================================== */

/**
 * @brief 缓存条目
 */
typedef struct
{
    uint32_t      sector;       /**< @brief 扇区号 */
    uint8_t       status;       /**< @brief 状态（脏/清理） */
    uint8_t       reserved[7];  /**< @brief 保留 */
    uint8_t       data[512];    /**< @brief 扇区数据 */
} fat32_cache_entry_t;

/* ========================================================================
 * 写入缓存接口
 * ======================================================================== */

/**
 * @brief 初始化写入缓存
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_cache_init(void);

/**
 * @brief 清理写入缓存
 */
void fat32_cache_cleanup(void);

/**
 * @brief 写入扇区到缓存
 *
 * @param sector 扇区号
 * @param buf    扇区数据
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_cache_write(uint32_t sector, const void *buf);

/**
 * @brief 从缓存读取扇区
 *
 * @param sector 扇区号
 * @param buf    输出缓冲区
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_cache_read(uint32_t sector, void *buf);

/**
 * @brief 标记扇区为脏
 *
 * @param sector 扇区号
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_cache_mark_dirty(uint32_t sector);

/**
 * @brief 刷新所有脏扇区到磁盘
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_cache_flush(void);

/**
 * @brief 刷新特定扇区到磁盘
 *
 * @param sector 扇区号
 *
 * @return 0 成功，<0 失败
 */
int32_t fat32_cache_flush_sector(uint32_t sector);

/**
 * @brief 检查扇区是否在缓存中
 *
 * @param sector 扇区号
 *
 * @return true 在缓存中，false 不在缓存中
 */
bool fat32_cache_is_cached(uint32_t sector);

#endif /* FS_FAT32_CACHE_H */
