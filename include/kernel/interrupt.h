/**
 * @file    interrupt.h
 * @brief   中断到用户态路由接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了中断到用户态路由接口：
 *          - 中断绑定到通知对象（InterruptAttach）
 *          - 用户态线程等待中断（InterruptWait）
 *          - 中断 → 内核路由 → Notification → 用户态线程
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: IN-001~006
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_INTERRUPT_H
#define KERNEL_INTERRUPT_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 中断处理函数类型
 * ======================================================================== */

/**
 * @brief 中断处理函数指针
 *
 * @param irq 中断号
 * @param arg 用户参数
 */
typedef void (*irq_handler_t)(uint32_t irq, void *arg);

/* ========================================================================
 * 中断描述符
 * ======================================================================== */

/**
 * @brief 中断描述符
 *
 * @details 描述一个已注册的中断处理配置。
 */
typedef struct
{
    uint32_t        irq;            /**< @brief 中断号 */
    uint32_t        cpu_id;         /**< @brief 绑定的 CPU */
    uint8_t         trigger_mode;   /**< @brief 触发模式 */
    uint8_t         priority;       /**< @brief 优先级 */
    irq_handler_t   handler;        /**< @brief 绑定的处理函数 */
    void            *handler_arg;   /**< @brief 绑定的参数 */
    kobj_id_t       notification_id; /**< @brief 关联的通知对象 ID */
    bool            in_use;       /**< @brief 是否已注册 */
} irq_desc_t;

/* ========================================================================
 * 触发模式
 * ======================================================================== */

/** @brief 边沿触发模式 */
#define IRQ_TRIGGER_EDGE_FALLING    0U

/** @brief 电平触发模式 */
#define IRQ_TRIGGER_LEVEL_HIGH     1U

/* ========================================================================
 * 中断配置常量
 * ======================================================================== */

/** @brief 最大注册中断数量 */
#define IRQ_MAX_HANDLERS        256U

/** @brief 中断优先级范围 */
#define IRQ_PRIORITY_MIN         0U
#define IRQ_PRIORITY_MAX         255U

/** @brief 默认中断优先级 */
#define IRQ_PRIORITY_DEFAULT     128U

/* ========================================================================
 * 中断路由 API
 * ======================================================================== */

/**
 * @brief 初始化中断路由子系统
 *
 * @details 初始化中断描述符表，注册架构特定的中断处理。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: IN-001
 */
kernel_status_t interrupt_subsys_init(void);

/**
 * @brief 绑定中断到通知对象
 *
 * @details 将硬件中断绑定到内核通知对象，用户态线程可通过
 *          该通知对象等待中断。
 *
 * @param irq            硬件中断号
 * @param notification_id 通知对象 ID（由能力引用）
 * @param trigger_mode  触发模式
 * @param priority      优先级
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EBUSY 中断已被绑定
 *
 * @note 对应需求: IN-002, IN-003
 */
kernel_status_t interrupt_attach(uint32_t irq,
                                      kobj_id_t notification_id,
                                      uint8_t trigger_mode,
                                      uint8_t priority);

/**
 * @brief 解除中断绑定
 *
 * @param irq 硬件中断号
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: IN-003
 */
kernel_status_t interrupt_detach(uint32_t irq);

/**
 * @brief 知道中断路由分发
 *
 * @details 在中断上下文中调用，根据中断号查找绑定的通知对象，
 *          向对应的通知对象发送信号。
 *
 * @param irq 中断号
 *
 * @note 对应需求: IN-004
 */
void interrupt_dispatch(uint32_t irq);

/**
 * @brief 获取中断描述符
 *
 * @param irq 中断号
 *
 * @return 中断描述符指针，未注册返回 NULL
 */
irq_desc_t *interrupt_get_desc(uint32_t irq);

/**
 * @brief 注册内核中断处理函数
 *
 * @param irq      中断号
 * @param handler  处理函数
 * @param arg      用户参数
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: IN-005
 */
kernel_status_t interrupt_register(uint32_t irq,
                                      irq_handler_t handler,
                                      void *arg);

/**
 * @brief 注销内核中断处理函数
 *
 * @param irq 中断号
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t interrupt_unregister(uint32_t irq);

#endif /* KERNEL_INTERRUPT_H */
