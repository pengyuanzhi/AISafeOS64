/**
 * @file    hot_data_cache.h
 * @brief   热数据缓存机制
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 提供热数据缓存 API：
 *          - 热数据缓存初始化
 *          - 热数据缓存获取
 *          - 热数据缓存更新
 *          - 热数据缓存刷新
 *
 * @note MISRA C:2012 合规
 * @note 对应阶段 1.6 - 缓存优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_HOT_DATA_CACHE_H
#define KERNEL_HOT_DATA_CACHE_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/alignment.h>
#include <stdint.h>

/* ========================================================================
 * 热数据缓存配置常量
 * ======================================================================== */

/** @brief 热数据缓存条目数量 */
#define HOT_DATA_CACHE_ENTRIES  8U

/** @brief 热数据缓存大小（字节） */
#define HOT_DATA_CACHE_SIZE     (CACHE_LINE_SIZE * HOT_DATA_CACHE_ENTRIES)

/** @brief 热数据缓存超时时间（微秒） */
#define HOT_DATA_CACHE_TIMEOUT_US  1000U

/* ========================================================================
 * 热数据缓存类型
 * ======================================================================== */

/**
 * @brief 热数据类型
 */
typedef enum
{
    HOT_DATA_TYPE_CURRENT_THREAD = 0U, /**< @brief 当前运行线程 */
    HOT_DATA_TYPE_READY_QUEUE,         /**< @brief 就绪队列 */
    HOT_DATA_TYPE_IPC_CHANNEL,         /**< @brief IPC 通道 */
    HOT_DATA_TYPE_IPC_ENDPOINT,        /**< @brief IPC 端点 */
    HOT_DATA_TYPE_VMA_TREE,            /**< @brief VMA 树 */
    HOT_DATA_TYPE_PAGE_TABLE,          /**< @brief 页表 */
    HOT_DATA_TYPE_SLAB_CACHE,          /**< @brief Slab 缓存 */
    HOT_DATA_TYPE_COUNT                /**< @brief 热数据类型数量 */
} hot_data_type_t;

/* ========================================================================
 * 热数据缓存条目
 * ======================================================================== */

/**
 * @brief 热数据缓存条目
 */
typedef struct CACHE_ALIGN(64)
{
    hot_data_type_t type;      /**< @brief 数据类型 */
    void            *ptr;     /**< @brief 数据指针 */
    uint64_t        last_access; /**< @brief 最后访问时间 */
    uint64_t        access_count;  /**< @brief 访问次数 */
    uint8_t         valid;    /**< @brief 是否有效 */
    uint8_t         reserved[7]; /**< @brief 保留对齐 */
} hot_data_entry_t;

/* ========================================================================
 * 热数据缓存
 * ======================================================================== */

/**
 * @brief 热数据缓存
 */
typedef struct CACHE_ALIGN(64)
{
    hot_data_entry_t entries[HOT_DATA_CACHE_ENTRIES]; /**< @brief 缓存条目 */
    uint32_t          hit_count;                      /**< @brief 命中次数 */
    uint32_t          miss_count;                     /**< @brief 未命中次数 */
} hot_data_cache_t;

/* ========================================================================
 * 热数据缓存操作 API
 * ======================================================================== */

/**
 * @brief 初始化热数据缓存
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t hot_data_cache_init(void);

/**
 * @brief 获取热数据
 *
 * @details 从热数据缓存中获取指定类型的数据。
 *          如果缓存命中，返回缓存的指针。
 *          如果缓存未命中，返回 NULL。
 *
 * @param type 热数据类型
 *
 * @return 缓存指针，如果未命中则返回 NULL
 */
void *hot_data_cache_get(hot_data_type_t type);

/**
 * @brief 更新热数据
 *
 * @details 更新热数据缓存中的指定类型的数据。
 *          如果缓存已满，使用 LRU 策略替换。
 *
 * @param type 热数据类型
 * @param ptr  数据指针
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t hot_data_cache_put(hot_data_type_t type, void *ptr);

/**
 * @brief 刷新热数据缓存
 *
 * @details 刷新热数据缓存，清空所有条目。
 */
void hot_data_cache_flush(void);

/**
 * @brief 失效化热数据缓存
 *
 * @details 失效化热数据缓存，标记所有条目为无效。
 */
void hot_data_cache_invalidate(void);

/**
 * @brief 获取热数据缓存统计
 *
 * @details 获取热数据缓存的命中率统计。
 *
 * @param hit_count    输出参数，命中次数
 * @param miss_count   输出参数，未命中次数
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t hot_data_cache_get_stats(uint32_t *hit_count,
                                        uint32_t *miss_count);

#endif /* KERNEL_HOT_DATA_CACHE_H */
