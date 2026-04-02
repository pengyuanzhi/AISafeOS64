/**
 * @file    security.h
 * @brief   内核安全机制接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了内核安全基础设施接口：
 *          - 页面权限强制（R/W/X）
 *          - 内核代码段只读保护
 *          - 安全状态管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SE-001~004, KR-011
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_SECURITY_H
#define KERNEL_SECURITY_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 安全状态枚举
 * ======================================================================== */

/**
 * @brief 安全状态
 */
typedef enum
{
    SECURITY_STATE_NORMAL = 0U,      /**< @brief 正常运行 */
    SECURITY_STATE_DEGRADED,         /**< @brief 降级模式 */
    SECURITY_STATE_ALERT,            /**< @brief 安全告警 */
    SECURITY_STATE_PANIC             /**< @brief 安全紧急停止 */
} security_state_t;

/* ========================================================================
 * 安全事件类型
 * ======================================================================== */

/**
 * @brief 安全事件类型
 */
typedef enum
{
    SECURITY_EVENT_NONE = 0U,            /**< @brief 无事件 */
    SECURITY_EVENT_STACK_OVERFLOW,       /**< @brief 栈溢出检测 */
    SECURITY_EVENT_KERNEL_RO_VIOLATION,  /**< @brief 内核只读区写入尝试 */
    SECURITY_EVENT_INVALID_PAGE_ACCESS,  /**< @brief 无效页面访问 */
    SECURITY_EVENT_DOUBLE_FREE,          /**< @brief 双重释放检测 */
    SECURITY_EVENT_USE_AFTER_FREE,       /**< @brief 释放后使用检测 */
    SECURITY_EVENT_CAPABILITY_VIOLATION, /**< @brief 能力权限违规 */
    SECURITY_EVENT_NULL_DEREFERENCE      /**< @brief 空指针解引用 */
} security_event_t;

/* ========================================================================
 * 安全统计
 * ======================================================================== */

/**
 * @brief 安全统计信息
 */
typedef struct
{
    uint32_t    total_events;        /**< @brief 总安全事件数 */
    uint32_t    stack_overflows;     /**< @brief 栈溢出次数 */
    uint32_t    page_faults;         /**< @brief 页错误次数 */
    uint32_t    capability_violations; /**< @brief 能力违规次数 */
} security_stats_t;

/* ========================================================================
 * 安全 API
 * ======================================================================== */

/**
 * @brief 初始化安全子系统
 *
 * @details 设置内核代码段只读保护、初始化安全状态。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: SE-001, SE-002
 */
kernel_status_t security_subsys_init(void);

/**
 * @brief 设置内核代码段只读保护
 *
 * @details 将内核代码段的页表权限修改为只读+可执行，
 *          防止运行时代码修改攻击。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: SE-003
 */
kernel_status_t security_protect_kernel_code(void);

/**
 * @brief 记录安全事件
 *
 * @param event 安全事件类型
 * @param detail 事件详情（可选，0 表示无详情）
 *
 * @note 对应需求: SE-004
 */
void security_report_event(security_event_t event, uint32_t detail);

/**
 * @brief 获取当前安全状态
 *
 * @return 安全状态
 */
security_state_t security_get_state(void);

/**
 * @brief 获取安全统计信息
 *
 * @param[out] stats 输出统计信息
 */
void security_get_stats(security_stats_t *stats);

/**
 * @brief 安全紧急停止
 *
 * @details 进入安全紧急停止状态：禁用中断、停止调度、
 *          保存故障信息到持久存储。
 *
 * @param reason 停止原因
 */
void security_panic(const char *reason);

/**
 * @brief 检查页面访问权限（异常处理器调用）
 *
 * @param fault_addr 故障虚拟地址
 * @param is_write   是否为写操作
 * @param is_exec    是否为执行操作
 *
 * @return KERNEL_OK 权限检查通过
 * @return -EACCES 权限违规
 *
 * @note 对应需求: KR-011, SE-003
 */
kernel_status_t security_check_page_access(uint64_t fault_addr,
                                            bool is_write,
                                            bool is_exec);

#endif /* KERNEL_SECURITY_H */
