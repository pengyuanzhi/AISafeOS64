/**
 * @file    stack_guard.h
 * @brief   栈保护机制接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了栈保护机制接口：
 *          - 栈金丝雀（Stack Canary）检测
 *          - 栈溢出检测
 *          - 栈保护页配置
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SE-004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_STACK_GUARD_H
#define KERNEL_STACK_GUARD_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 栈保护常量
 * ======================================================================== */

/** @brief 栈金丝雀魔数值 */
#define STACK_CANARY_MAGIC     0xDEADBEEFU

/** @brief 栈最大保护页数量 */
#define STACK_GUARD_PAGES     2U

/** @brief 栈最小大小（对齐到 4KB 页边界） */
#define STACK_GUARD_MIN_SIZE    PAGE_SIZE_4K

/* ========================================================================
 * 栈保护配置
 * ======================================================================== */

/**
 * @brief 栈保护配置
 */
typedef struct
{
    vaddr_t     stack_bottom;        /**< @brief 栈底地址 */
    uint64_t    stack_size;          /**< @brief 栈大小（字节） */
    uint32_t    canary_offset;      /**< @brief 金丝雀偏移（字节，从栈底算起） */
    uint32_t    guard_count;       /**< @brief 保护页数量 */
    bool       enabled;            /**< @brief 是否启用 */
} stack_guard_config_t;

/* ========================================================================
 * 栈保护 API
 * ======================================================================== */

/**
 * @brief 初始化栈保护子系统
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: SE-004
 */
kernel_status_t stack_guard_subsys_init(void);

/**
 * @brief 为线程配置栈保护
 *
 * @param stack_bottom 栈底地址
 * @param stack_size  栈大小（字节）
 * @param config     栈保护配置指针
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: SE-004
 */
kernel_status_t stack_guard_setup(vaddr_t stack_bottom,
                                    uint64_t stack_size,
                                    stack_guard_config_t *config);

/**
 * @brief 检查栈金丝雀
 *
 * @param config 栈保护配置指针
 *
 * @return true 栈完好
 * @return false 栈溢出（金丝雀被破坏）
 *
 * @note 对应需求: SE-004
 */
bool stack_guard_check(const stack_guard_config_t *config);

/**
 * @brief 刷新栈金丝雀
 *
 * @param config 栈保护配置指针
 */
void stack_guard_refresh(const stack_guard_config_t *config);

#endif /* KERNEL_STACK_GUARD_H */
