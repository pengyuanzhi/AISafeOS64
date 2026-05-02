/**
 * @file    icache_optimize.h
 * @brief   指令缓存优化接口
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 提供指令缓存优化 API：
 *          - 指令缓存预热
 *          - 指令缓存无效化
 *          - 指令缓存同步
 *          - 指令缓存统计
 *
 * @note MISRA C:2012 合规
 * @note 对应阶段 1.6 - 缓存优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_ICACHE_OPTIMIZE_H
#define KERNEL_ICACHE_OPTIMIZE_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/alignment.h>
#include <stdint.h>

/* ========================================================================
 * 指令缓存优化配置常量
 * ======================================================================== */

/** @brief 指令缓存预热函数数量 */
#define ICACHE_WARMUP_FUNCS  32U

/** @brief 指令缓存预热迭代次数 */
#define ICACHE_WARMUP_ITERATIONS  10U

/* ========================================================================
 * 指令缓存预热函数类型
 * ======================================================================== */

/**
 * @brief 指令缓存预热函数类型
 */
typedef void (*icache_warmup_func_t)(void);

/* ========================================================================
 * 指令缓存统计
 * ======================================================================== */

/**
 * @brief 指令缓存统计
 */
typedef struct CACHE_ALIGN(64)
{
    uint64_t warmup_count;      /**< @brief 预热次数 */
    uint64_t invalidate_count;   /**< @brief 无效化次数 */
    uint64_t sync_count;        /**< @brief 同步次数 */
    uint64_t hit_count;         /**< @brief 命中次数 */
    uint64_t miss_count;        /**< @brief 未命中次数 */
} icache_stats_t;

/* ========================================================================
 * 指令缓存优化操作 API
 * ======================================================================== */

/**
 * @brief 初始化指令缓存优化
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t icache_optimize_init(void);

/**
 * @brief 指令缓存预热
 *
 * @details 预热指令缓存，加载热函数到指令缓存。
 *          此函数在启动时调用，提高后续执行速度。
 */
void icache_warmup(void);

/**
 * @brief 注册指令缓存预热函数
 *
 * @details 注册函数到指令缓存预热列表。
 *
 * @param func 预热函数
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t icache_warmup_register_func(icache_warmup_func_t func);

/**
 * @brief 指令缓存无效化
 *
 * @details 无效化指令缓存，清空指令缓存。
 *          此函数在加载新代码时调用。
 *
 * @param addr 起始地址
 * @param size 大小
 */
void icache_invalidate(void *addr, uint64_t size);

/**
 * @brief 指令缓存同步
 *
 * @details 同步指令缓存，确保指令缓存与内存一致。
 *          此函数在修改代码后调用。
 *
 * @param addr 起始地址
 * @param size 大小
 */
void icache_sync(void *addr, uint64_t size);

/**
 * @brief 获取指令缓存统计
 *
 * @details 获取指令缓存的命中率统计。
 *
 * @param stats 输出参数，指令缓存统计
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t icache_optimize_get_stats(icache_stats_t *stats);

#endif /* KERNEL_ICACHE_OPTIMIZE_H */
