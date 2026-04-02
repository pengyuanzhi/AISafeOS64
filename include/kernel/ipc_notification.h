/**
 * @file    ipc_notification.h
 * @brief   IPC 通知（Notification）管理接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了异步通知机制的内核接口：
 *          - 通知对象创建/销毁
 *          - 信号触发（Signal）
 *          - 信号等待（Wait）
 *          - 信号测试（TryWait）
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-006（异步通知，延迟 < 500ns）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_IPC_NOTIFICATION_H
#define KERNEL_IPC_NOTIFICATION_H

#include <kernel/types.h>
#include <kernel/ipc_types.h>

/* ========================================================================
 * 通知管理 API
 * ======================================================================== */

/**
 * @brief 初始化通知子系统
 *
 * @details 初始化全局通知对象池和链表。
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t ipc_notification_subsys_init(void);

/**
 * @brief 创建通知对象
 *
 * @param owner_tid 拥有者线程 ID
 * @param notify_id 输出参数，返回通知对象 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOMEM 无空闲通知对象
 *
 * @note 对应需求: KR-006
 */
kernel_status_t ipc_notification_create(thread_id_t owner_tid,
                                         kobj_id_t *notify_id);

/**
 * @brief 销毁通知对象
 *
 * @param notify_id 通知对象 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 通知对象无效
 */
kernel_status_t ipc_notification_destroy(kobj_id_t notify_id);

/**
 * @brief 触发通知信号
 *
 * @details 向通知对象发送信号。如果 mask 中的位在 waited_mask 中也设置了，
 *          且有线程正在等待，则立即唤醒等待线程。
 *
 * @param notify_id 通知对象 ID
 * @param signal    要触发的信号位掩码
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL   通知对象无效
 * @return IPC_ERR_NO_WAITER 无等待者（信号被缓存）
 *
 * @note 对应需求: KR-006（通知延迟 < 500ns）
 * @note 此函数可在中断上下文中调用
 */
kernel_status_t ipc_notification_signal(kobj_id_t notify_id,
                                         uint64_t signal);

/**
 * @brief 等待通知信号（阻塞）
 *
 * @details 阻塞当前线程，直到通知对象收到 waited_mask 中指定的信号。
 *          收到信号后，通过 triggered 输出实际触发的信号位掩码，
 *          然后清除已触发的信号位。
 *
 * @param notify_id   通知对象 ID
 * @param waited_mask 等待的信号位掩码
 * @param triggered   输出参数，实际触发的信号位掩码
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL   参数无效
 *
 * @note 对应需求: KR-006
 */
kernel_status_t ipc_notification_wait(kobj_id_t notify_id,
                                       uint64_t waited_mask,
                                       uint64_t *triggered);

/**
 * @brief 非阻塞等待通知信号
 *
 * @details 检查通知对象是否有待处理信号。如果有，立即返回；
 *          如果没有，返回 -EAGAIN。
 *
 * @param notify_id   通知对象 ID
 * @param waited_mask 等待的信号位掩码
 * @param triggered   输出参数，实际触发的信号位掩码
 *
 * @return KERNEL_OK 成功
 * @return -EAGAIN   无待处理信号
 * @return -EINVAL   参数无效
 */
kernel_status_t ipc_notification_try_wait(kobj_id_t notify_id,
                                           uint64_t waited_mask,
                                           uint64_t *triggered);

/**
 * @brief 带超时的通知等待
 *
 * @param notify_id   通知对象 ID
 * @param waited_mask 等待的信号位掩码
 * @param triggered   输出参数，实际触发的信号位掩码
 * @param timeout_ms  超时时间（毫秒）
 *
 * @return KERNEL_OK 成功
 * @return IPC_ERR_TIMEOUT 超时
 */
kernel_status_t ipc_notification_wait_timeout(kobj_id_t notify_id,
                                               uint64_t waited_mask,
                                               uint64_t *triggered,
                                               uint32_t timeout_ms);

#endif /* KERNEL_IPC_NOTIFICATION_H */
