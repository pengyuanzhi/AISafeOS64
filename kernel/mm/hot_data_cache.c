/**
 * @file    hot_data_cache.c
 * @brief   热数据缓存机制实现
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 实现热数据缓存机制：
 *          - 热数据缓存初始化
 *          - 热数据缓存获取/更新
 *          - 热数据缓存刷新/失效
 *          - LRU 缓存替换策略
 *
 * @note MISRA C:2012 合规
 * @note 对应阶段 1.6 - 缓存优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/hot_data_cache.h>
#include <kernel/timer.h>
#include <kernel/barrier.h>
#include <kernel/compiler.h>
#include <kernel/errno.h>
#include <string.h>
#include <kernel/spinlock.h>

/* ========================================================================
 * 热数据缓存（全局单例）
 * ======================================================================== */

/**
 * @brief 全局热数据缓存
 */
static hot_data_cache_t s_hot_data_cache CACHE_ALIGN(64);

/**
 * @brief 热数据缓存锁
 */
static TicketLock_t s_hot_data_cache_lock;

/**
 * @brief 热数据缓存初始化标志
 */
static bool s_hot_data_cache_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 查找热数据缓存条目
 *
 * @details 在热数据缓存中查找指定类型的条目。
 *
 * @param type 热数据类型
 *
 * @return 找到的条目索引，如果未找到则返回 HOT_DATA_CACHE_ENTRIES
 */
static uint32_t hot_data_cache_find_entry(hot_data_type_t type)
{
    uint32_t i;

    for (i = 0U; i < HOT_DATA_CACHE_ENTRIES; i++)
    {
        if ((s_hot_data_cache.entries[i].type == type) &&
            (s_hot_data_cache.entries[i].valid != 0U))
        {
            return i;
        }
    }

    return HOT_DATA_CACHE_ENTRIES;
}

/**
 * @brief 查找空闲热数据缓存条目
 *
 * @details 在热数据缓存中查找空闲条目。
 *          如果没有空闲条目，返回 HOT_DATA_CACHE_ENTRIES。
 *
 * @return 空闲条目索引，如果没有则返回 HOT_DATA_CACHE_ENTRIES
 */
static uint32_t hot_data_cache_find_free_entry(void)
{
    uint32_t i;

    for (i = 0U; i < HOT_DATA_CACHE_ENTRIES; i++)
    {
        if (s_hot_data_cache.entries[i].valid == 0U)
        {
            return i;
        }
    }

    return HOT_DATA_CACHE_ENTRIES;
}

/**
 * @brief 查找 LRU 热数据缓存条目
 *
 * @details 在热数据缓存中查找最少使用的条目（LRU）。
 *          使用最后访问时间和访问次数判断。
 *
 * @return LRU 条目索引
 */
static uint32_t hot_data_cache_find_lru_entry(void)
{
    uint32_t lru_idx = 0U;
    uint64_t lru_score = 0xFFFFFFFFFFFFFFFFULL;
    uint32_t i;

    for (i = 0U; i < HOT_DATA_CACHE_ENTRIES; i++)
    {
        if (s_hot_data_cache.entries[i].valid != 0U)
        {
            /* 计算 LRU 分数：最后访问时间越小，访问次数越少，分数越小 */
            uint64_t score = s_hot_data_cache.entries[i].last_access -
                             (s_hot_data_cache.entries[i].access_count * 1000ULL);

            if (score < lru_score)
            {
                lru_score = score;
                lru_idx = i;
            }
        }
    }

    return lru_idx;
}

/* ========================================================================
 * 热数据缓存操作 API 实现
 * ======================================================================== */

/**
 * @brief 初始化热数据缓存
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t hot_data_cache_init(void)
{
    uint32_t i;

    if (s_hot_data_cache_initialized)
    {
        return KERNEL_OK;
    }

    /* 初始化缓存条目 */
    for (i = 0U; i < HOT_DATA_CACHE_ENTRIES; i++)
    {
        s_hot_data_cache.entries[i].type = HOT_DATA_TYPE_COUNT;
        s_hot_data_cache.entries[i].ptr = NULL;
        s_hot_data_cache.entries[i].last_access = 0ULL;
        s_hot_data_cache.entries[i].access_count = 0ULL;
        s_hot_data_cache.entries[i].valid = 0U;
    }

    /* 初始化统计 */
    s_hot_data_cache.hit_count = 0U;
    s_hot_data_cache.miss_count = 0U;

    /* 初始化锁 */
    ticket_lock_init(&s_hot_data_cache_lock);

    s_hot_data_cache_initialized = true;

    return KERNEL_OK;
}

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
void *hot_data_cache_get(hot_data_type_t type)
{
    void *ptr = NULL;
    uint32_t entry_idx;

    if (!s_hot_data_cache_initialized)
    {
        return NULL;
    }

    ticket_lock_acquire(&s_hot_data_cache_lock);

    /* 查找缓存条目 */
    entry_idx = hot_data_cache_find_entry(type);

    if (entry_idx < HOT_DATA_CACHE_ENTRIES)
    {
        /* 缓存命中 */
        ptr = s_hot_data_cache.entries[entry_idx].ptr;

        /* 更新访问信息 */
        s_hot_data_cache.entries[entry_idx].last_access = timer_get_ticks();
        s_hot_data_cache.entries[entry_idx].access_count++;

        /* 更新统计 */
        s_hot_data_cache.hit_count++;
    }
    else
    {
        /* 缓存未命中 */
        s_hot_data_cache.miss_count++;
    }

    ticket_lock_release(&s_hot_data_cache_lock);

    return ptr;
}

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
kernel_status_t hot_data_cache_put(hot_data_type_t type, void *ptr)
{
    uint32_t entry_idx;

    if (!s_hot_data_cache_initialized)
    {
        return -(int32_t)EINVAL;
    }

    if (ptr == NULL)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&s_hot_data_cache_lock);

    /* 查找缓存条目 */
    entry_idx = hot_data_cache_find_entry(type);

    if (entry_idx < HOT_DATA_CACHE_ENTRIES)
    {
        /* 更新现有条目 */
        s_hot_data_cache.entries[entry_idx].ptr = ptr;
        s_hot_data_cache.entries[entry_idx].last_access = timer_get_ticks();
        s_hot_data_cache.entries[entry_idx].access_count++;
    }
    else
    {
        /* 查找空闲条目 */
        entry_idx = hot_data_cache_find_free_entry();

        if (entry_idx >= HOT_DATA_CACHE_ENTRIES)
        {
            /* 缓存已满，使用 LRU 替换 */
            entry_idx = hot_data_cache_find_lru_entry();

            /* 替换 LRU 条目 */
            s_hot_data_cache.entries[entry_idx].type = type;
            s_hot_data_cache.entries[entry_idx].ptr = ptr;
            s_hot_data_cache.entries[entry_idx].last_access = timer_get_ticks();
            s_hot_data_cache.entries[entry_idx].access_count = 1ULL;
            s_hot_data_cache.entries[entry_idx].valid = 1U;
        }
        else
        {
            /* 使用空闲条目 */
            s_hot_data_cache.entries[entry_idx].type = type;
            s_hot_data_cache.entries[entry_idx].ptr = ptr;
            s_hot_data_cache.entries[entry_idx].last_access = timer_get_ticks();
            s_hot_data_cache.entries[entry_idx].access_count = 1ULL;
            s_hot_data_cache.entries[entry_idx].valid = 1U;
        }
    }

    ticket_lock_release(&s_hot_data_cache_lock);

    return KERNEL_OK;
}

/**
 * @brief 刷新热数据缓存
 *
 * @details 刷新热数据缓存，清空所有条目。
 */
void hot_data_cache_flush(void)
{
    uint32_t i;

    if (!s_hot_data_cache_initialized)
    {
        return;
    }

    ticket_lock_acquire(&s_hot_data_cache_lock);

    /* 清空所有条目 */
    for (i = 0U; i < HOT_DATA_CACHE_ENTRIES; i++)
    {
        s_hot_data_cache.entries[i].type = HOT_DATA_TYPE_COUNT;
        s_hot_data_cache.entries[i].ptr = NULL;
        s_hot_data_cache.entries[i].last_access = 0ULL;
        s_hot_data_cache.entries[i].access_count = 0ULL;
        s_hot_data_cache.entries[i].valid = 0U;
    }

    /* 重置统计 */
    s_hot_data_cache.hit_count = 0U;
    s_hot_data_cache.miss_count = 0U;

    ticket_lock_release(&s_hot_data_cache_lock);
}

/**
 * @brief 失效化热数据缓存
 *
 * @details 失效化热数据缓存，标记所有条目为无效。
 */
void hot_data_cache_invalidate(void)
{
    uint32_t i;

    if (!s_hot_data_cache_initialized)
    {
        return;
    }

    ticket_lock_acquire(&s_hot_data_cache_lock);

    /* 标记所有条目为无效 */
    for (i = 0U; i < HOT_DATA_CACHE_ENTRIES; i++)
    {
        s_hot_data_cache.entries[i].valid = 0U;
    }

    ticket_lock_release(&s_hot_data_cache_lock);
}

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
                                        uint32_t *miss_count)
{
    if (!s_hot_data_cache_initialized)
    {
        return -(int32_t)EINVAL;
    }

    if ((hit_count == NULL) || (miss_count == NULL))
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&s_hot_data_cache_lock);

    *hit_count = s_hot_data_cache.hit_count;
    *miss_count = s_hot_data_cache.miss_count;

    ticket_lock_release(&s_hot_data_cache_lock);

    return KERNEL_OK;
}
#include <kernel/spinlock.h>
