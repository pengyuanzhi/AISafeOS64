/**
 * @file    user_timer.h
 * @brief   用户态定时器接口
 * @author  AISafe64 Team
 * @date    2026-07-05
 * @version 1.0
 *
 * @details 提供用户态可创建的定时器：
 *          - 到期后通过 notification 通知
 *          - 支持单次和周期模式
 *          - 基于内核 tick 计数
 *
 * @note MISRA-C:2012 合规
 *
 * @revision history
 * v1.0 2026-07-05 初始版本
 */

#ifndef KERNEL_USER_TIMER_H
#define KERNEL_USER_TIMER_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/list.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 用户定时器描述符
 */
typedef struct user_timer
{
    uint32_t          timer_id;     /**< @brief 定时器 ID */
    uint32_t          owner_tid;    /**< @brief 所属线程 */
    uint32_t          notify_ep;    /**< @brief 通知端点 ID */
    tick_t            expire_tick;  /**< @brief 到期时刻（绝对 tick） */
    tick_t            interval;     /**< @brief 周期间隔（0=单次） */
    bool              active;       /**< @brief 是否活跃 */
    struct list_head  node;         /**< @brief 活跃链表节点 */
} user_timer_t;

/**
 * @brief 初始化用户定时器子系统
 */
kernel_status_t user_timer_subsys_init(void);

/**
 * @brief 创建定时器
 *
 * @param owner_tid 所属线程
 * @param notify_ep 通知端点 ID
 * @param out_id 输出定时器 ID
 * @return KERNEL_OK 成功
 */
kernel_status_t user_timer_create(uint32_t owner_tid, uint32_t notify_ep,
                                   uint32_t *out_id);

/**
 * @brief 设置定时时间
 *
 * @param timer_id 定时器 ID
 * @param ms 延迟毫秒数
 * @param interval_ms 周期毫秒数（0=单次）
 * @return KERNEL_OK 成功
 */
kernel_status_t user_timer_settime(uint32_t timer_id, uint32_t ms,
                                    uint32_t interval_ms);

/**
 * @brief 删除定时器
 *
 * @param timer_id 定时器 ID
 * @return KERNEL_OK 成功
 */
kernel_status_t user_timer_delete(uint32_t timer_id);

/**
 * @brief 获取当前时间（纳秒）
 *
 * @return 系统启动以来的纳秒数
 */
uint64_t user_clock_gettime(void);

/**
 * @brief 纳秒级睡眠
 *
 * @param ns 纳秒数
 * @return KERNEL_OK 成功
 */
kernel_status_t user_nanosleep(uint64_t ns);

/**
 * @brief 定时器 tick 回调
 *
 * @details 由 timer_interrupt_handler 在每个 tick 调用。
 *          检查活跃定时器链表，到期则投递通知。
 */
void user_timer_tick(void);

#endif /* KERNEL_USER_TIMER_H */
