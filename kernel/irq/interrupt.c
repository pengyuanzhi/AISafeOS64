/**
 * @file    interrupt.c
 * @brief   中断路由子系统实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件实现了中断到用户态路由子系统：
 *          - 中断描述符表管理
 *          - 中断绑定到通知对象（InterruptAttach）
 *          - 内核中断处理函数注册（InterruptRegister）
 *          - 中断路由分发（InterruptDispatch）
 *          - 使用 TicketLock 保护临界区
 *
 *          中断处理流程：
 *          1. 硬件中断触发
 *          2. GIC 确认中断（gic_acknowledge_irq）
 *          3. 调用 interrupt_dispatch 进行路由
 *          4. 通知 GIC 中断处理完成（gic_end_of_interrupt）
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: IN-001~006
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */

#include <kernel/interrupt.h>
#include <kernel/gic.h>
#include <kernel/config.h>
#include <kernel/barrier.h>
#include <kernel/spinlock.h>
#include <kernel/errno.h>
#include <kernel/ipc_notification.h>
#include <kernel/smp.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 全局状态
 * ======================================================================== */

/**
 * @brief 中断描述符数组
 *
 * @details 静态数组，索引为中断号，存储每个中断的配置和处理信息。
 *          大小由 IRQ_MAX_HANDLERS（256）定义。
 */
static irq_desc_t s_irq_descs[IRQ_MAX_HANDLERS];

/**
 * @brief 中断子系统自旋锁
 *
 * @details 保护中断描述符表的并发访问。
 *          所有对 s_irq_descs 的修改操作必须在持锁状态下进行。
 */
static TicketLock_t s_irq_lock;

/**
 * @brief 中断子系统初始化标志
 */
static bool s_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 清零单个中断描述符
 *
 * @param desc 指向中断描述符的指针
 */
static void irq_desc_reset(irq_desc_t *desc)
{
    if (desc != NULL)
    {
        desc->irq             = 0U;
        desc->cpu_id          = 0U;
        desc->trigger_mode    = 0U;
        desc->priority        = (uint8_t)IRQ_PRIORITY_DEFAULT;
        desc->handler         = NULL;
        desc->handler_arg     = NULL;
        desc->notification_id = KOBJ_ID_INVALID;
        desc->in_use          = false;
    }
}

/* ========================================================================
 * 中断路由 API 实现
 * ======================================================================== */

/**
 * @brief 初始化中断路由子系统
 *
 * @details 初始化中断描述符表，将所有槽位清零。
 *          初始化自旋锁用于后续的并发保护。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: IN-001
 */
kernel_status_t interrupt_subsys_init(void)
{
    uint32_t i;

    /* 初始化自旋锁 */
    ticket_lock_init(&s_irq_lock);

    /* 清零所有中断描述符 */
    for (i = 0U; i < IRQ_MAX_HANDLERS; i++)
    {
        irq_desc_reset(&s_irq_descs[i]);
    }

    /* 设置初始化标志 */
    s_initialized = true;
    barrier();

    return KERNEL_OK;
}

/**
 * @brief 绑定中断到通知对象
 *
 * @details 将硬件中断绑定到内核通知对象。配置 GIC 的优先级、
 *          触发模式和目标 CPU，然后使能中断。用户态线程可通过
 *          该通知对象等待中断。
 *
 * @param irq             硬件中断号
 * @param notification_id 通知对象 ID（由能力引用）
 * @param trigger_mode    触发模式（IRQ_TRIGGER_EDGE_FALLING 或 IRQ_TRIGGER_LEVEL_HIGH）
 * @param priority        优先级（0-255，值越小优先级越高）
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
                                      uint8_t priority)
{
    kernel_status_t ret;
    gic_trigger_mode_t gic_mode;
    irq_desc_t *desc;

    /* 参数验证 */
    if (irq >= IRQ_MAX_HANDLERS)
    {
        return -(int32_t)EINVAL;
    }

    if (notification_id == KOBJ_ID_INVALID)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取锁 */
    ticket_lock_acquire(&s_irq_lock);

    desc = &s_irq_descs[irq];

    /* 检查是否已被占用 */
    if (desc->in_use)
    {
        ticket_lock_release(&s_irq_lock);
        return -(int32_t)EBUSY;
    }

    /* 转换触发模式 */
    if (trigger_mode == IRQ_TRIGGER_EDGE_FALLING)
    {
        gic_mode = GIC_TRIGGER_EDGE;
    }
    else
    {
        gic_mode = GIC_TRIGGER_LEVEL;
    }

    /* 配置 GIC 优先级 */
    ret = gic_set_priority(irq, priority);
    if (ret != KERNEL_OK)
    {
        ticket_lock_release(&s_irq_lock);
        return ret;
    }

    /* 配置 GIC 触发模式（仅 SPI 有效） */
    ret = gic_set_trigger_mode(irq, gic_mode);
    if (ret != KERNEL_OK)
    {
        ticket_lock_release(&s_irq_lock);
        return ret;
    }

    /* 配置 GIC 亲和性：默认绑定到 CPU 0 */
    if (irq >= GIC_SPI_BASE)
    {
        ret = gic_set_affinity(irq, 0x01U);
        if (ret != KERNEL_OK)
        {
            ticket_lock_release(&s_irq_lock);
            return ret;
        }
    }

    /* 填充描述符 */
    desc->irq             = irq;
    desc->cpu_id          = 0U;
    desc->trigger_mode    = trigger_mode;
    desc->priority        = priority;
    desc->handler         = NULL;
    desc->handler_arg     = NULL;
    desc->notification_id = notification_id;
    desc->in_use          = true;
    barrier();

    /* 使能中断 */
    ret = gic_enable_irq(irq);
    if (ret != KERNEL_OK)
    {
        /* 使能失败，回滚描述符 */
        irq_desc_reset(desc);
        ticket_lock_release(&s_irq_lock);
        return ret;
    }

    /* 释放锁 */
    ticket_lock_release(&s_irq_lock);

    return KERNEL_OK;
}

/**
 * @brief 解除中断绑定
 *
 * @details 禁用指定中断，清除描述符中的所有信息。
 *
 * @param irq 硬件中断号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效或未绑定
 *
 * @note 对应需求: IN-003
 */
kernel_status_t interrupt_detach(uint32_t irq)
{
    irq_desc_t *desc;

    /* 参数验证 */
    if (irq >= IRQ_MAX_HANDLERS)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取锁 */
    ticket_lock_acquire(&s_irq_lock);

    desc = &s_irq_descs[irq];

    /* 检查是否已注册 */
    if (!desc->in_use)
    {
        ticket_lock_release(&s_irq_lock);
        return -(int32_t)EINVAL;
    }

    /* 禁用中断 */
    (void)gic_disable_irq(irq);

    /* 清除描述符 */
    irq_desc_reset(desc);
    barrier();

    /* 释放锁 */
    ticket_lock_release(&s_irq_lock);

    return KERNEL_OK;
}

/**
 * @brief 中断路由分发
 *
 * @details 在中断上下文中调用。根据中断号查找绑定的处理方式：
 *          - 获取当前 CPU ID 用于多核中断路由
 *          - 如果描述符的 cpu_id 不匹配当前 CPU，直接处理（简化实现）
 *          - 如果有关联的通知对象 ID，通知等待的线程
 *          - 如果有注册的内核处理函数，调用该函数
 *
 * @param irq 中断号
 *
 * @note 对应需求: IN-004
 * @note 此函数在中断上下文中执行，必须快速返回
 */
void interrupt_dispatch(uint32_t irq)
{
    irq_desc_t *desc;
    uint32_t cpu_id;

    /* 参数验证 */
    if (irq >= IRQ_MAX_HANDLERS)
    {
        return;
    }

    desc = &s_irq_descs[irq];

    /* 检查描述符是否有效 */
    if (!desc->in_use)
    {
        return;
    }

    /* 获取当前 CPU ID（用于多核中断路由诊断） */
    cpu_id = smp_get_cpu_id();
    (void)cpu_id; /* 简化实现：直接处理，不检查 CPU 亲和性 */

    /*
     * 优先检查是否有通知对象绑定。
     * 如果有，需要触发通知（实际通知投递由 IPC 子系统实现，
     * 此处为简化实现，仅标记需要通知）。
     */
    if (desc->notification_id != KOBJ_ID_INVALID)
    {
        /* 触发 IPC 通知，将中断事件投递到用户态等待线程 */
        (void)ipc_notification_signal(desc->notification_id, (uint64_t)1U << (irq & 63U));
    }

    /* 如果有注册的内核处理函数，调用它 */
    if (desc->handler != NULL)
    {
        desc->handler(irq, desc->handler_arg);
    }
}

/**
 * @brief 获取中断描述符
 *
 * @details 根据中断号返回对应的中断描述符指针。
 *          调用者可通过描述符查询中断配置信息。
 *
 * @param irq 中断号
 *
 * @return 中断描述符指针，未注册或参数无效返回 NULL
 */
irq_desc_t *interrupt_get_desc(uint32_t irq)
{
    irq_desc_t *desc;

    /* 参数验证 */
    if (irq >= IRQ_MAX_HANDLERS)
    {
        return NULL;
    }

    desc = &s_irq_descs[irq];

    /* 检查是否已注册 */
    if (!desc->in_use)
    {
        return NULL;
    }

    return desc;
}

/**
 * @brief 注册内核中断处理函数
 *
 * @details 为指定中断号注册内核态处理函数。
 *          配置 GIC 参数后使能中断。
 *          如果中断号已被占用，返回错误。
 *
 * @param irq      中断号
 * @param handler  处理函数指针（不得为 NULL）
 * @param arg      用户参数（可为 NULL）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EBUSY 中断已被绑定
 *
 * @note 对应需求: IN-005
 */
kernel_status_t interrupt_register(uint32_t irq,
                                      irq_handler_t handler,
                                      void *arg)
{
    kernel_status_t ret;
    irq_desc_t *desc;

    /* 参数验证 */
    if (irq >= IRQ_MAX_HANDLERS)
    {
        return -(int32_t)EINVAL;
    }

    if (handler == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取锁 */
    ticket_lock_acquire(&s_irq_lock);

    desc = &s_irq_descs[irq];

    /* 检查是否已被占用 */
    if (desc->in_use)
    {
        ticket_lock_release(&s_irq_lock);
        return -(int32_t)EBUSY;
    }

    /* 配置 GIC 默认优先级 */
    ret = gic_set_priority(irq, (uint8_t)IRQ_PRIORITY_DEFAULT);
    if (ret != KERNEL_OK)
    {
        ticket_lock_release(&s_irq_lock);
        return ret;
    }

    /* 配置 GIC 亲和性：默认绑定到 CPU 0 */
    if (irq >= GIC_SPI_BASE)
    {
        ret = gic_set_affinity(irq, 0x01U);
        if (ret != KERNEL_OK)
        {
            ticket_lock_release(&s_irq_lock);
            return ret;
        }
    }

    /* 填充描述符 */
    desc->irq             = irq;
    desc->cpu_id          = 0U;
    desc->trigger_mode    = IRQ_TRIGGER_LEVEL_HIGH;
    desc->priority        = (uint8_t)IRQ_PRIORITY_DEFAULT;
    desc->handler         = handler;
    desc->handler_arg     = arg;
    desc->notification_id = KOBJ_ID_INVALID;
    desc->in_use          = true;
    barrier();

    /* 使能中断 */
    ret = gic_enable_irq(irq);
    if (ret != KERNEL_OK)
    {
        /* 使能失败，回滚描述符 */
        irq_desc_reset(desc);
        ticket_lock_release(&s_irq_lock);
        return ret;
    }

    /* 释放锁 */
    ticket_lock_release(&s_irq_lock);

    return KERNEL_OK;
}

/**
 * @brief 注销内核中断处理函数
 *
 * @details 禁用指定中断，清除描述符中的处理函数信息。
 *
 * @param irq 中断号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效或未注册
 */
kernel_status_t interrupt_unregister(uint32_t irq)
{
    irq_desc_t *desc;

    /* 参数验证 */
    if (irq >= IRQ_MAX_HANDLERS)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取锁 */
    ticket_lock_acquire(&s_irq_lock);

    desc = &s_irq_descs[irq];

    /* 检查是否已注册 */
    if (!desc->in_use)
    {
        ticket_lock_release(&s_irq_lock);
        return -(int32_t)EINVAL;
    }

    /* 禁用中断 */
    (void)gic_disable_irq(irq);

    /* 清除描述符 */
    irq_desc_reset(desc);
    barrier();

    /* 释放锁 */
    ticket_lock_release(&s_irq_lock);

    return KERNEL_OK;
}
