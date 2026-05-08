/**
 * @file    fs_page_cache.h
 * @brief   页缓存管理头文件
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details 页缓存管理模块
 *          - 4KB 页大小
 *          - LRU 淘汰策略
 *          - 延迟写回（dirty 标记）
 *          - 缓存命中率统计
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_PAGE_CACHE_H
#define FS_PAGE_CACHE_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 页大小 */
#define FS_PAGE_SIZE          4096U

/** @brief 页缓存大小 */
#define FS_PAGE_CACHE_SIZE    8192U      /* 8MB 缓存 */

/** @brief 页无效状态 */
#define FS_PAGE_INVALID       0U

/** @brief 页有效状态 */
#define FS_PAGE_VALID         1U

/** @brief 页脏状态 */
#define FS_PAGE_DIRTY         2U

/* ========================================================================
 * 数据结构
 * ======================================================================== */

/**
 * @brief 页缓存状态
 */
typedef enum
{
    PAGE_STATE_INVALID = 0U,          /**< 页无效 */
    PAGE_STATE_VALID = 1U,            /**< 页有效 */
    PAGE_STATE_DIRTY = 2U             /**< 页脏（已修改） */
} page_cache_state_t;

/**
 * @brief 页缓存条目
 */
typedef struct
{
    uint32_t        ino;                    /**< Inode 编号 */
    uint64_t        offset;                 /**< 页偏移 */
    uint8_t         data[FS_PAGE_SIZE];     /**< 页数据 */
    uint32_t        state;                  /**< 页状态 */
    uint64_t        last_access;            /**< 最后访问时间（纳秒） */
    uint32_t        access_count;           /**< 访问次数 */
} page_cache_entry_t;

/**
 * @brief 页缓存管理器
 */
typedef struct
{
    page_cache_entry_t   entries[FS_PAGE_CACHE_SIZE];
    uint32_t               hit_count;          /**< 缓存命中次数 */
    uint32_t               miss_count;         /**< 缓存未命中次数 */
    uint32_t               evict_count;        /**< 淘汰次数 */
    uint32_t               writeback_count;    /**< 写回次数 */
    bool                   initialized;        /**< 初始化标志 */
} page_cache_t;

/* ========================================================================
 * 公共接口
 * ======================================================================== */

/**
 * @brief 初始化页缓存管理器
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 *
 * @return 0 成功，<0 失败
 */
int32_t page_cache_init(page_cache_t *cache);

/**
 * @brief 从缓存获取页
 *
 * @details 如果缓存中存在该页，返回页数据指针；否则返回 NULL
 *          如果返回 NULL，调用者应从文件系统重新读取页数据
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 * @param ino   Inode 编号（不能为 0）
 * @param offset 页偏移（必须 4KB 对齐）
 * @param fallback 回退数据指针（缓存命中时忽略）
 * @param size   回退数据大小
 *
 * @return 页数据指针，未命中返回 NULL
 */
uint8_t *page_cache_get(page_cache_t *cache, uint32_t ino, uint64_t offset,
                        const uint8_t *fallback, uint64_t size);

/**
 * @brief 将页放入缓存
 *
 * @details 使用 LRU 策略，缓存满时淘汰最久未使用的条目
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 * @param ino   Inode 编号（不能为 0）
 * @param offset 页偏移（必须 4KB 对齐）
 * @param data   页数据指针（不能为 NULL）
 * @param size   数据大小
 * @param dirty  是否脏页（需要写回）
 *
 * @return 0 成功，<0 失败（缓存已满）
 */
int32_t page_cache_put(page_cache_t *cache, uint32_t ino, uint64_t offset,
                       const uint8_t *data, uint64_t size, bool dirty);

/**
 * @brief 使指定页的缓存失效
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 * @param ino   Inode 编号（不能为 0）
 * @param offset 页偏移（必须 4KB 对齐）
 *
 * @return 0 成功，<0 失败
 */
int32_t page_cache_invalidate(page_cache_t *cache, uint32_t ino, uint64_t offset);

/**
 * @brief 刷新所有脏页（同步写回）
 *
 * @details 遍历所有缓存条目，将脏页写回
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 */
void page_cache_flush(page_cache_t *cache);

/**
 * @brief 清空缓存（保留统计信息）
 *
 * @details 保留缓存统计数据，只清空条目
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 */
void page_cache_clear(page_cache_t *cache);

/**
 * @brief 获取缓存统计信息
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 *
 * @return 0 成功
 */
int32_t page_cache_stats(const page_cache_t *cache);

/**
 * @brief 打印缓存统计报告
 *
 * @param cache 缓存管理器指针（不能为 NULL）
 *
 * @return 0 成功
 */
int32_t page_cache_report(const page_cache_t *cache);

#endif /* FS_PAGE_CACHE_H */
