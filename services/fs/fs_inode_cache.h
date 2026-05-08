/**
 * @file    fs_inode_cache.h
 * @brief   Inode 缓存管理头文件
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details Inode 缓存管理模块
 *          - LRU 淘汰策略
 *          - 缓存命中率统计
 *          - 访问计数
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_INODE_CACHE_H
#define FS_INODE_CACHE_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大缓存条目数 */
#define FS_INODE_CACHE_SIZE        32U

/** @brief 缓存无效状态 */
#define FS_CACHE_INVALID           0U

/** @brief 缓存有效状态 */
#define FS_CACHE_VALID             1U

/* ========================================================================
 * 数据结构
 * ======================================================================== */

/**
 * @brief Inode 缓存条目
 */
typedef struct
{
    uint32_t        ino;                    /**< Inode 编号 */
    uint32_t        state;                  /**< 缓存状态 */
    uint64_t        last_access;            /**< 最后访问时间（纳秒） */
    uint32_t        access_count;           /**< 访问次数 */
    fs_inode_t      inode;                  /**< Inode 数据副本 */
} inode_cache_entry_t;

/**
 * @brief Inode 缓存管理器
 */
typedef struct
{
    inode_cache_entry_t   entries[FS_INODE_CACHE_SIZE];
    uint32_t               hit_count;          /**< 缓存命中次数 */
    uint32_t               miss_count;         /**< 缓存未命中次数 */
    uint32_t               evict_count;        /**< 淘汰次数 */
    bool                   initialized;        /**< 初始化标志 */
} inode_cache_t;

/* ========================================================================
 * 公共接口
 * ======================================================================== */

/**
 * @brief 初始化 inode 缓存管理器
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 *
 * @return 0 成功，<0 失败
 */
int32_t inode_cache_init(inode_cache_t *cache);

/**
 * @brief 从缓存获取 inode
 *
 * @details 如果缓存中存在该 inode，返回缓存副本；否则返回 NULL
 *          如果返回 NULL，调用者应从文件系统重新读取 inode
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 * @param ino   Inode 编号（不能为 0）
 * @param fallback 回退 inode 指针（缓存命中时忽略）
 *
 * @return 缓存条目指针，未命中返回 NULL
 */
inode_cache_entry_t *inode_cache_get(inode_cache_t *cache, uint32_t ino,
                                     const fs_inode_t *fallback);

/**
 * @brief 将 inode 放入缓存
 *
 * @details 使用 LRU 策略，缓存满时淘汰最久未使用的条目
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 * @param inode Inode 数据指针（不能为 NULL）
 *
 * @return 0 成功，<0 失败（缓存已满）
 */
int32_t inode_cache_put(inode_cache_t *cache, const fs_inode_t *inode);

/**
 * @brief 使指定 inode 的缓存失效
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 * @param ino   Inode 编号（不能为 0）
 *
 * @return 0 成功，<0 失败
 */
int32_t inode_cache_invalidate(inode_cache_t *cache, uint32_t ino);

/**
 * @brief 刷新所有缓存
 *
 * @details 清空所有缓存条目，统计数据重置
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 */
void inode_cache_flush(inode_cache_t *cache);

/**
 * @brief 清空缓存（保留统计信息）
 *
 * @details 保留缓存统计数据，只清空条目
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 */
void inode_cache_clear(inode_cache_t *cache);

/**
 * @brief 获取缓存统计信息
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 *
 * @return 0 成功
 */
int32_t inode_cache_stats(const inode_cache_t *cache);

/**
 * @brief 打印缓存统计报告
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 *
 * @return 0 成功
 */
int32_t inode_cache_report(const inode_cache_t *cache);

#endif /* FS_INODE_CACHE_H */
