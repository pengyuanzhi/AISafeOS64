/**
 * @file    stack_guard.c
 * @brief   栈保护机制实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 实现所有 stack_guard.h 中声明的函数：
 *          - stack_guard_subsys_init:  初始化栈保护子系统
 *          - stack_guard_setup:       为线程配置栈保护
 *          - stack_guard_check:       检查栈金丝雀是否完好
 *          - stack_guard_refresh:     刷新栈金丝雀
 *
 *          栈保护机制通过以下方式检测栈溢出：
 *          1. 在栈底部放置金丝雀魔数值（STACK_CANARY_MAGIC）
 *          2. 定期检查金丝雀是否被覆盖
 *          3. 金丝雀被破坏则表明栈溢出已发生
 *
 *          栈内存布局（从低地址到高地址）：
 *          +---------------------------+ <-- stack_bottom
 *          | 金丝雀（4字节）           |
 *          +---------------------------+ <-- stack_bottom + canary_offset
 *          | 栈可用区域                |
 *          |                           |
 *          +---------------------------+ <-- stack_bottom + stack_size
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SE-004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/stack_guard.h>
#include <kernel/page_table.h>
#include <kernel/security.h>
#include <kernel/barrier.h>
#include <kernel/config.h>
#include <kernel/errno.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 栈保护子系统初始化
 * ======================================================================== */

/**
 * @brief 初始化栈保护子系统
 *
 * @details 初始化栈保护机制的全局状态。
 *          当前实现无需额外全局初始化，预留扩展接口。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: SE-004
 */
kernel_status_t stack_guard_subsys_init(void)
{
    /* 当前无需全局初始化，预留扩展点 */
    return KERNEL_OK;
}

/* ========================================================================
 * 为线程配置栈保护
 * ======================================================================== */

/**
 * @brief 为线程配置栈保护
 *
 * @details 在指定栈空间的底部写入金丝雀魔数值，
 *          并设置保护页信息。当栈溢出发生时，
 *          金丝雀会被覆盖，从而被检测到。
 *
 * @param stack_bottom 栈底地址（虚拟地址）
 * @param stack_size   栈大小（字节）
 * @param config       栈保护配置指针（输出参数）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL    参数无效（空指针、地址为0、大小不足）
 *
 * @note 对应需求: SE-004
 * @warning config 指针不得为 NULL
 */
kernel_status_t stack_guard_setup(vaddr_t stack_bottom,
                                  uint64_t stack_size,
                                  stack_guard_config_t *config)
{
    volatile uint32_t *canary_ptr;

    /* 参数有效性检查 */
    if (config == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (stack_bottom == (vaddr_t)0U)
    {
        return -(int32_t)EINVAL;
    }

    if (stack_size < (uint64_t)STACK_GUARD_MIN_SIZE)
    {
        return -(int32_t)EINVAL;
    }

    /* 计算保护页总大小 */
    uint64_t guard_total = (uint64_t)STACK_GUARD_PAGES * PAGE_SIZE_4K;

    /* 栈空间必须足够容纳保护页和金丝雀 */
    if (stack_size <= guard_total)
    {
        return -(int32_t)EINVAL;
    }

    /*
     * 计算金丝雀位置：栈底地址偏移保护页大小之后
     * 金丝雀放在保护页之上、可用栈区域的最底部
     */
    uint32_t canary_offset = (uint32_t)guard_total;
    canary_ptr = (volatile uint32_t *)(uintptr_t)(stack_bottom + (uint64_t)canary_offset);

    /* 写入金丝雀魔数值 */
    *canary_ptr = STACK_CANARY_MAGIC;

    /* 插入存储屏障，确保金丝雀值对其他核可见 */
    barrier_store();

    /* 设置配置信息 */
    config->stack_bottom = stack_bottom;
    config->stack_size = stack_size;
    config->canary_offset = canary_offset;
    config->guard_count = STACK_GUARD_PAGES;
    config->enabled = true;

    return KERNEL_OK;
}

/* ========================================================================
 * 检查栈金丝雀
 * ======================================================================== */

/**
 * @brief 检查栈金丝雀是否完好
 *
 * @details 读取栈底部金丝雀位置的值，
 *          与期望的魔数值 STACK_CANARY_MAGIC 进行比较。
 *          如果不匹配，说明栈溢出已发生。
 *
 * @param config 栈保护配置指针
 *
 * @return true  栈金丝雀完好（未检测到溢出）
 * @return false 栈金丝雀被破坏（检测到溢出）或参数无效
 *
 * @note 对应需求: SE-004
 */
bool stack_guard_check(const stack_guard_config_t *config)
{
    volatile uint32_t *canary_ptr;
    uint32_t canary_value;

    /* 参数有效性检查 */
    if (config == NULL)
    {
        return false;
    }

    if (!config->enabled)
    {
        return false;
    }

    if (config->stack_bottom == (vaddr_t)0U)
    {
        return false;
    }

    /* 读取金丝雀位置处的值 */
    canary_ptr = (volatile uint32_t *)(uintptr_t)(config->stack_bottom +
                                                   (uint64_t)config->canary_offset);

    /* 插入加载屏障，确保读取到最新值 */
    barrier_load();

    canary_value = *canary_ptr;

    /* 比较金丝雀值与魔数 */
    if (canary_value != STACK_CANARY_MAGIC)
    {
        /* 金丝雀被破坏，报告安全事件 */
        security_report_event(SECURITY_EVENT_STACK_OVERFLOW,
                              (uint32_t)config->stack_bottom);
        return false;
    }

    return true;
}

/* ========================================================================
 * 刷新栈金丝雀
 * ======================================================================== */

/**
 * @brief 刷新栈金丝雀
 *
 * @details 重新在金丝雀位置写入 STACK_CANARY_MAGIC，
 *          用于周期性维护或校验后恢复。
 *
 * @param config 栈保护配置指针
 *
 * @note 对应需求: SE-004
 * @warning config 指针不得为 NULL
 */
void stack_guard_refresh(const stack_guard_config_t *config)
{
    volatile uint32_t *canary_ptr;

    /* 参数有效性检查 */
    if (config == NULL)
    {
        return;
    }

    if (!config->enabled)
    {
        return;
    }

    if (config->stack_bottom == (vaddr_t)0U)
    {
        return;
    }

    /* 计算金丝雀位置 */
    canary_ptr = (volatile uint32_t *)(uintptr_t)(config->stack_bottom +
                                                   (uint64_t)config->canary_offset);

    /* 重新写入金丝雀魔数值 */
    *canary_ptr = STACK_CANARY_MAGIC;

    /* 插入存储屏障，确保金丝雀值对其他核可见 */
    barrier_store();
}
