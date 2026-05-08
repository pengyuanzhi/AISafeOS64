/**
 * @file    fs_inode_cache.h
 * @brief   inode 缓存接口
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details inode LRU 缓存，用于加速重复 inode 访问：
 *          - LRU（最久未使用）淘汰策略
 *          - 缓存命中率统计
 *          - 访问计数追踪
 *          - 简单自旋锁保护（多核安全）
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_INODE_CACHE_H
#define FS_INODE_CACHE_H

#include "fs_ops.h"
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 缓存最大条目数 */
#define FS_INODE_CACHE_SIZE    32U

/** @brief 缓存无效时间戳标记 */
#define FS_CACHE_INVALID_TIME  0xFFFFFFFFFFFFFFFFULL

/* ========================================================================
 * 类型定义
 * ======================================================================== */

/**
 * @brief 缓存条目状态
 */
typedef enum
{
    FS_CACHE_INVALID = 0U,       /**< @brief 缓存无效 */
    FS_CACHE_VALID   = 1U        /**< @brief 缓存有效 */
} inode_cache_state_t;

/**
 * @brief inode 缓存条目
 *
 * @details 存储单个 inode 的缓存数据及统计信息
 */
typedef struct
{
    uint32_t            ino;            /**< @brief inode 编号 */
    inode_cache_state_t state;          /**< @brief 缓存状态 */
    uint64_t            last_access;    /**< @brief 最后访问时间（单调递增时钟） */
    uint64_t            access_count;   /**< @brief 访问次数 */
    fs_inode_t          inode;          /**< @brief inode 数据副本 */
} inode_cache_entry_t;

/**
 * @brief inode 缓存统计信息
 */
typedef struct
{
    uint32_t hit_count;                 /**< @brief 缓存命中次数 */
    uint32_t miss_count;                /**< @brief 缓存未命中次数 */
    uint32_t evict_count;               /**< @brief 淘汰次数 */
    uint32_t entry_count;               /**< @brief 当前有效条目数 */
} inode_cache_stats_t;

/**
 * @brief inode 缓存管理器
 *
 * @details LRU 缓存，管理 inode 数据的缓存和淘汰
 */
typedef struct
{
    inode_cache_entry_t entries[FS_INODE_CACHE_SIZE]; /**< @brief 缓存条目数组 */
    inode_cache_stats_t stats;                        /**< @brief 统计信息 */
    uint64_t            clock;                        /**< @brief 单调递增时钟 */
    bool                initialized;                  /**< @brief 初始化标记 */
} inode_cache_t;

/* ========================================================================
 * 公共接口
 * ======================================================================== */

/**
 * @brief 初始化 inode 缓存
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 *
 * @note 必须在使用缓存前调用
 */
void inode_cache_init(inode_cache_t *cache);

/**
 * @brief 从缓存获取 inode
 *
 * @details 根据 inode 编号查找缓存。命中时更新访问时间和计数。
 *          未命中时，若 fallback 非 NULL 则将其写入缓存。
 *
 * @param cache     缓存管理器指针（不能为 NULL）
 * @param ino       inode 编号
 * @param fallback  后备 inode 数据（可为 NULL）
 *
 * @return 命中时返回缓存中的 inode 指针，未命中且无 fallback 返回 NULL
 *
 * @note 返回的指针在下次缓存操作后可能失效
 */
fs_inode_t *inode_cache_get(inode_cache_t *cache, uint32_t ino,
                             const fs_inode_t *fallback);

/**
 * @brief 放入 inode 到缓存
 *
 * @details 将 inode 数据写入缓存。若已存在则更新，否则分配新条目。
 *          缓存满时淘汰最久未使用的条目。
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 * @param inode inode 数据指针（不能为 NULL）
 *
 * @return 0 成功，-1 失败
 */
int32_t inode_cache_put(inode_cache_t *cache, const fs_inode_t *inode);

/**
 * @brief 使指定 inode 缓存无效
 *
 * @details 根据 inode 编号使对应缓存条目失效
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 * @param ino   要无效的 inode 编号
 */
void inode_cache_invalidate(inode_cache_t *cache, uint32_t ino);

/**
 * @brief 刷新所有脏缓存条目
 *
 * @details 遍历缓存，处理所有标记为脏的条目
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 *
 * @return 0 成功
 */
int32_t inode_cache_flush(inode_cache_t *cache);

/**
 * @brief 清空缓存
 *
 * @details 将所有缓存条目标记为无效，重置统计信息
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 */
void inode_cache_clear(inode_cache_t *cache);

/**
 * @brief 获取缓存统计信息
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 *
 * @return 缓存统计信息副本
 */
inode_cache_stats_t inode_cache_get_stats(const inode_cache_t *cache);

/**
 * @brief 打印缓存统计信息到标准输出
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 */
void inode_cache_print_stats(const inode_cache_t *cache);

#endif /* FS_INODE_CACHE_H */
