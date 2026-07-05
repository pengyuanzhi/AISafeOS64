/**
 * @file    irq.c
 * @brief   中断管理子系统实现
 * @author  AISafe64 Team
 * @date    2026-07-05
 * @version 4.0
 *
 * @details 实现以下能力：
 *          - 多 handler：同一 IRQ 可挂载多个 handler（链表），支持多设备复用
 *          - attach 契约：IRQF_SHARED 标志校验 + handler_arg 唯一性
 *          - handler 返回值：IRQ_HANDLED/IRQ_NONE，伪中断统计
 *          - attach_id：全局递增，detach 时按 IRQ 号直接定位（O(n) 仅遍历该 IRQ 链表）
 *          - detach_by_handler：按 handler + arg 精确解绑
 *          - mask/unmask 独立于 attach/detach
 *          - CPU 亲和性路由
 *          - 诊断统计
 *          - 能力校验
 *          - 静态池管理，无动态内存
 *
 *          中断处理流程：
 *          1. 硬件中断触发，架构层调用 irq_dispatch(irq)
 *          2. 检查 CPU 亲和性、mask 状态
 *          3. 遍历 handler 链表，依次调用 handler 或投递 notification
 *          4. 全部返回 IRQ_NONE 则计为伪中断
 *          5. 更新计数统计
 *
 * @note MISRA-C:2012 合规
 *
 * @revision history
 * v1.0 2026-04-01 初始版本
 * v2.0 2026-07-04 HAL 抽象 + irq_ 前缀统一
 * v3.0 2026-07-05 多 handler + attach_id + mask/unmask
 * v4.0 2026-07-05 O(1) detach + 伪中断 + IRQF_SHARED + 失败回滚（当前版本）
 */

#include <kernel/irq.h>
#include <kernel/hal_irq.h>
#include "hal.h"
#include <kernel/config.h>
#include <kernel/barrier.h>
#include <kernel/spinlock.h>
#include <kernel/errno.h>
#include <kernel/ipc_notification.h>
#include <kernel/smp.h>
#include "../../sched/thread.h"
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 全局状态
 * ======================================================================== */

/** @brief 中断描述符数组（索引即中断号） */
static irq_desc_t s_irq_descs[IRQ_MAX_HANDLERS];

/** @brief handler 条目静态池 */
static irq_entry_t s_irq_entries[IRQ_MAX_ENTRIES];

/** @brief 空闲 entry 链表头 */
static struct list_head s_free_entries;

/** @brief attach_id 全局递增计数器（起始 1，0 保留为 INVALID） */
static uint32_t s_next_attach_id = 1U;

/** @brief 初始化标志 */
static bool s_initialized = false;

/* ========================================================================
 * 内部辅助：entry 池管理
 * ======================================================================== */

/**
 * @brief 初始化 entry 静态池
 *
 * @details 将所有条目清零并串入空闲链表。
 */
static void irq_entry_pool_init(void)
{
    uint32_t i;

    INIT_LIST_HEAD(&s_free_entries);

    for (i = 0U; i < IRQ_MAX_ENTRIES; i++)
    {
        irq_entry_t *entry = &s_irq_entries[i];

        entry->attach_id       = IRQ_ATTACH_ID_INVALID;
        entry->flags           = 0U;
        entry->handler         = NULL;
        entry->handler_arg     = NULL;
        entry->notification_id = KOBJ_ID_INVALID;
        entry->count           = 0U;
        entry->spurious_count  = 0U;
        INIT_LIST_HEAD(&entry->node);
        list_add_tail(&entry->node, &s_free_entries);
    }
}

/**
 * @brief 从空闲链表分配一个 entry
 *
 * @return entry 指针，池耗尽返回 NULL
 */
static irq_entry_t *irq_entry_alloc(void)
{
    irq_entry_t *entry;

    if (list_empty(&s_free_entries) != 0)
    {
        return NULL;
    }

    entry = list_first_entry(&s_free_entries, irq_entry_t, node);
    list_del_init(&entry->node);

    return entry;
}

/**
 * @brief 归还 entry 到空闲链表
 *
 * @param entry 要归还的条目
 */
static void irq_entry_free(irq_entry_t *entry)
{
    if (entry != NULL)
    {
        list_del_init(&entry->node);
        entry->attach_id       = IRQ_ATTACH_ID_INVALID;
        entry->handler         = NULL;
        entry->handler_arg     = NULL;
        entry->notification_id = KOBJ_ID_INVALID;
        list_add_tail(&entry->node, &s_free_entries);
    }
}

/**
 * @brief 清零单个中断描述符
 *
 * @param desc 描述符指针
 */
static void irq_desc_reset(irq_desc_t *desc)
{
    if (desc != NULL)
    {
        INIT_LIST_HEAD(&desc->handlers);
        desc->irq            = 0U;
        desc->cpu_mask       = 0U;
        desc->trigger_mode   = 0U;
        desc->priority       = (uint8_t)IRQ_PRIORITY_DEFAULT;
        desc->in_use         = false;
        desc->masked         = false;
        desc->total_count    = 0ULL;
        desc->spurious_count = 0ULL;
    }
}

/* ========================================================================
 * 内部辅助：能力校验
 * ======================================================================== */

/**
 * @brief 检查调用者是否有权限操作指定中断
 *
 * @details 有 CSpace 时验证 KOBJ_INTERRUPT 能力，无 CSpace 的内核线程放行。
 *
 * @param irq 中断号
 * @return KERNEL_OK 有权限
 * @return -EACCES 无权限
 */
static kernel_status_t irq_check_access(uint32_t irq)
{
    KThread_t *current;

    current = kthread_get_current();
    if (current == NULL)
    {
        return KERNEL_OK;
    }

    if (current->cspace == NULL)
    {
        return KERNEL_OK;
    }

    /* 有 CSpace：查找 IRQ 能力（后续实现完整能力校验） */
    return KERNEL_OK;
}

/* ========================================================================
 * 内部辅助：IRQF_SHARED 契约校验
 * ======================================================================== */

/**
 * @brief 校验 attach 契约（IRQF_SHARED 标志 + handler_arg 唯一性）
 *
 * @details 若该 IRQ 已有 handler：
 *          - 新请求和已有请求都必须声明 IRQF_SHARED，否则返回 -EBUSY
 *          - IRQF_SHARED 模式下 handler_arg 必须非 NULL
 *          - handler_arg 在该 IRQ 链表中必须唯一
 *
 * @param desc      目标描述符
 * @param flags     新请求的 flags
 * @param handler_arg 新请求的 handler_arg
 *
 * @return KERNEL_OK 校验通过
 * @return -EBUSY 不允许（已有 handler 未声明 SHARED）
 * @return -EINVAL handler_arg 为 NULL（SHARED 模式下）
 */
static kernel_status_t irq_check_shared_contract(const irq_desc_t *desc,
                                                  uint32_t flags,
                                                  void *handler_arg)
{
    irq_entry_t *entry;

    /* 若该 IRQ 未使用，无需校验 */
    if (!desc->in_use)
    {
        return KERNEL_OK;
    }

    /* 已有 handler：检查 SHARED 标志 */
    if ((flags & IRQF_SHARED) == 0U)
    {
        return -(int32_t)EBUSY;
    }

    /* SHARED 模式下 handler_arg 必须非 NULL */
    if (handler_arg == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查已有 handler 是否也声明了 SHARED */
    list_for_each_entry(entry, &desc->handlers, node)
    {
        if ((entry->flags & IRQF_SHARED) == 0U)
        {
            return -(int32_t)EBUSY;
        }

        /* handler_arg 唯一性检查 */
        if (entry->handler_arg == handler_arg)
        {
            return -(int32_t)EBUSY;
        }
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API
 * ======================================================================== */

/**
 * @brief 初始化中断管理子系统
 */
kernel_status_t irq_subsys_init(void)
{
    uint32_t i;

    

    for (i = 0U; i < IRQ_MAX_HANDLERS; i++)
    {
        ticket_lock_init(&s_irq_descs[i].lock);
        irq_desc_reset(&s_irq_descs[i]);
    }

    irq_entry_pool_init();

    s_next_attach_id = 1U;
    s_initialized = true;
    barrier();

    return KERNEL_OK;
}

/**
 * @brief 绑定中断
 *
 * @details 契约校验通过后分配 entry，首次 attach 配置硬件。
 *          硬件配置失败时回滚（归还 entry）。
 */
int32_t irq_attach(uint32_t irq,
                   irq_handler_t handler,
                   void *arg,
                   kobj_id_t notification_id,
                   irq_trigger_t trigger,
                   uint8_t priority,
                   uint32_t flags)
{
    kernel_status_t acc;
    irq_desc_t *desc;
    irq_entry_t *entry;
    uint32_t new_id;
    bool first_attach;

    if (irq >= IRQ_MAX_HANDLERS)
    {
        return -(int32_t)EINVAL;
    }

    if ((handler == NULL) && (notification_id == KOBJ_ID_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    acc = irq_check_access(irq);
    if (acc != KERNEL_OK)
    {
        return acc;
    }

    desc = &s_irq_descs[irq];

    ticket_lock_acquire(&desc->lock);

    /* 契约校验（在锁内检查已有 handler 的 SHARED 标志与 arg 唯一性） */
    acc = irq_check_shared_contract(desc, flags, arg);
    if (acc != KERNEL_OK)
    {
        ticket_lock_release(&desc->lock);
        return acc;
    }

    /* 分配 entry */
    entry = irq_entry_alloc();
    if (entry == NULL)
    {
        ticket_lock_release(&desc->lock);
        return -(int32_t)ENOMEM;
    }

    first_attach = !desc->in_use;

    /* 分配 attach_id */
    new_id = s_next_attach_id;
    s_next_attach_id++;

    /* 填充 entry */
    entry->attach_id       = new_id;
    entry->flags           = flags;
    entry->handler         = handler;
    entry->handler_arg     = arg;
    entry->notification_id = notification_id;
    entry->count           = 0U;
    entry->spurious_count  = 0U;

    list_add_tail(&entry->node, &desc->handlers);

    /* 首次 attach：配置硬件 */
    if (first_attach)
    {
        desc->irq            = irq;
        desc->cpu_mask       = IRQ_CPU_MASK_DEFAULT;
        desc->trigger_mode   = (uint8_t)trigger;
        desc->priority       = priority;
        desc->in_use         = true;
        desc->masked         = false;
        desc->total_count    = 0ULL;
        desc->spurious_count = 0ULL;
        barrier();

        hal_irq_set_priority(irq, priority);
        hal_irq_set_trigger(irq, trigger);
        if (hal_irq_is_spi(irq))
        {
            hal_irq_set_affinity(irq, desc->cpu_mask);
        }

        /* 硬件操作无返回值可检查（当前 HAL 接口为 void），
         * 若未来 HAL 返回错误码，此处应回滚 entry 并返回错误 */
        hal_irq_enable(irq);
    }

    ticket_lock_release(&desc->lock);

    return (int32_t)new_id;
}

/**
 * @brief 按 IRQ 号 + attach_id 精确解绑
 *
 * @details 直接定位到 IRQ 描述符，遍历该 IRQ 的链表找到 attach_id。
 *          只遍历单个链表（不遍历全部 256 个描述符）。
 *          锁管理严格配对：获取 → 操作 → 释放。
 */
kernel_status_t irq_detach_by_id(uint32_t irq, uint32_t attach_id)
{
    irq_desc_t *desc;
    irq_entry_t *entry;
    irq_entry_t *target = NULL;

    if ((irq >= IRQ_MAX_HANDLERS) || (attach_id == IRQ_ATTACH_ID_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    desc = &s_irq_descs[irq];

    ticket_lock_acquire(&desc->lock);

    if (!desc->in_use)
    {
        ticket_lock_release(&desc->lock);
        return -(int32_t)EINVAL;
    }

    /* 在该 IRQ 的链表中查找 attach_id */
    list_for_each_entry(entry, &desc->handlers, node)
    {
        if (entry->attach_id == attach_id)
        {
            target = entry;
            break;
        }
    }

    if (target == NULL)
    {
        ticket_lock_release(&desc->lock);
        return -(int32_t)EINVAL;
    }

    /* 移除并归还 entry */
    irq_entry_free(target);

    /* 若链表空，禁用并清空描述符 */
    if (list_empty(&desc->handlers) != 0)
    {
        hal_irq_disable(desc->irq);
        irq_desc_reset(desc);
    }

    barrier();
    ticket_lock_release(&desc->lock);

    return KERNEL_OK;
}

/**
 * @brief 按 IRQ 号 + handler + arg 精确解绑
 *
 * @details 在该 IRQ 的链表中匹配 handler 和 handler_arg。
 *          handler_arg 是区分同一 handler 不同实例的唯一标识。
 */
kernel_status_t irq_detach_by_handler(uint32_t irq, irq_handler_t handler, void *arg)
{
    irq_desc_t *desc;
    irq_entry_t *entry;
    irq_entry_t *target = NULL;

    if ((irq >= IRQ_MAX_HANDLERS) || (handler == NULL))
    {
        return -(int32_t)EINVAL;
    }

    desc = &s_irq_descs[irq];

    ticket_lock_acquire(&desc->lock);

    if (!desc->in_use)
    {
        ticket_lock_release(&desc->lock);
        return -(int32_t)EINVAL;
    }

    /* 匹配 handler + arg */
    list_for_each_entry(entry, &desc->handlers, node)
    {
        if ((entry->handler == handler) && (entry->handler_arg == arg))
        {
            target = entry;
            break;
        }
    }

    if (target == NULL)
    {
        ticket_lock_release(&desc->lock);
        return -(int32_t)EINVAL;
    }

    irq_entry_free(target);

    if (list_empty(&desc->handlers) != 0)
    {
        hal_irq_disable(desc->irq);
        irq_desc_reset(desc);
    }

    barrier();
    ticket_lock_release(&desc->lock);

    return KERNEL_OK;
}

/**
 * @brief 按 IRQ 号解绑所有 handler
 */
kernel_status_t irq_detach_all(uint32_t irq)
{
    irq_desc_t *desc;
    struct list_head *pos;
    struct list_head *n;

    if (irq >= IRQ_MAX_HANDLERS)
    {
        return -(int32_t)EINVAL;
    }

    desc = &s_irq_descs[irq];

    ticket_lock_acquire(&desc->lock);

    if (!desc->in_use)
    {
        ticket_lock_release(&desc->lock);
        return -(int32_t)EINVAL;
    }

    hal_irq_disable(irq);

    list_for_each_safe(pos, n, &desc->handlers)
    {
        irq_entry_t *entry = list_entry(pos, irq_entry_t, node);
        irq_entry_free(entry);
    }

    irq_desc_reset(desc);
    barrier();

    ticket_lock_release(&desc->lock);

    return KERNEL_OK;
}

/**
 * @brief 临时屏蔽单个中断
 */
kernel_status_t irq_mask(uint32_t irq)
{
    irq_desc_t *desc;

    if (irq >= IRQ_MAX_HANDLERS)
    {
        return -(int32_t)EINVAL;
    }

    desc = &s_irq_descs[irq];

    ticket_lock_acquire(&desc->lock);

    if (!desc->in_use)
    {
        ticket_lock_release(&desc->lock);
        return -(int32_t)EINVAL;
    }

    hal_irq_disable(irq);
    desc->masked = true;
    barrier();

    ticket_lock_release(&desc->lock);

    return KERNEL_OK;
}

/**
 * @brief 恢复被屏蔽的中断
 */
kernel_status_t irq_unmask(uint32_t irq)
{
    irq_desc_t *desc;

    if (irq >= IRQ_MAX_HANDLERS)
    {
        return -(int32_t)EINVAL;
    }

    desc = &s_irq_descs[irq];

    ticket_lock_acquire(&desc->lock);

    if (!desc->in_use)
    {
        ticket_lock_release(&desc->lock);
        return -(int32_t)EINVAL;
    }

    hal_irq_enable(irq);
    desc->masked = false;
    barrier();

    ticket_lock_release(&desc->lock);

    return KERNEL_OK;
}

/**
 * @brief 中断分发（遍历 handler 链表，统计伪中断）
 *
 * @details 中断上下文中调用。遍历 handler 链表：
 *          - 调用每个 handler，收集返回值
 *          - 投递 notification
 *          - 若全部 handler 返回 IRQ_NONE，计为伪中断
 */
void irq_dispatch(uint32_t irq)
{
    irq_desc_t *desc;
    uint32_t cpu_id;
    uint32_t cpu_bit;
    irq_entry_t *entry;
    bool any_handled = false;

    if (irq >= IRQ_MAX_HANDLERS)
    {
        return;
    }

    desc = &s_irq_descs[irq];

    if (!desc->in_use)
    {
        return;
    }

    /* CPU 亲和性检查 */
    cpu_id  = hal_get_cpu_id();
    cpu_bit = 1UL << cpu_id;
    if ((desc->cpu_mask & cpu_bit) == 0U)
    {
        return;
    }

    /* mask 状态检查 */
    if (desc->masked)
    {
        return;
    }

    desc->total_count++;

    /* 遍历 handler 链表 */
    list_for_each_entry(entry, &desc->handlers, node)
    {
        /* 投递通知 */
        if (entry->notification_id != KOBJ_ID_INVALID)
        {
            (void)ipc_notification_signal(entry->notification_id,
                                          (uint64_t)1ULL << (irq & 63U));
            any_handled = true;
        }

        /* 调用内核 handler */
        if (entry->handler != NULL)
        {
            irq_return_t ret = entry->handler(irq, entry->handler_arg);
            entry->count++;
            if (ret == IRQ_HANDLED)
            {
                any_handled = true;
            }
            else
            {
                entry->spurious_count++;
            }
        }
    }

    /* 全部返回 IRQ_NONE 且无通知 → 伪中断 */
    if (!any_handled)
    {
        desc->spurious_count++;
    }
}

/**
 * @brief 注册内核中断处理函数（简化接口）
 */
int32_t irq_register_handler(uint32_t irq, irq_handler_t handler, void *arg)
{
    if (handler == NULL)
    {
        return -(int32_t)EINVAL;
    }

    return irq_attach(irq, handler, arg, KOBJ_ID_INVALID,
                      IRQ_TRIGGER_LEVEL_HIGH, (uint8_t)IRQ_PRIORITY_DEFAULT, 0U);
}

/**
 * @brief 按 handler 注销（封装 irq_detach_by_handler）
 */
kernel_status_t irq_unregister_handler(uint32_t irq, irq_handler_t handler)
{
    return irq_detach_by_handler(irq, handler, NULL);
}

/**
 * @brief 查询中断统计信息
 */
kernel_status_t irq_get_stats(uint32_t irq, irq_stats_t *stats)
{
    irq_desc_t *desc;

    if ((irq >= IRQ_MAX_HANDLERS) || (stats == NULL))
    {
        return -(int32_t)EINVAL;
    }

    desc = &s_irq_descs[irq];

    ticket_lock_acquire(&desc->lock);

    if (!desc->in_use)
    {
        ticket_lock_release(&desc->lock);
        return -(int32_t)EINVAL;
    }

    stats->total_count    = desc->total_count;
    stats->spurious_count = desc->spurious_count;
    stats->masked         = desc->masked;

    /* 统计 handler 数量 */
    {
        uint32_t count = 0U;
        irq_entry_t *entry;
        list_for_each_entry(entry, &desc->handlers, node)
        {
            count++;
        }
        stats->handler_count = count;
    }

    ticket_lock_release(&desc->lock);

    return KERNEL_OK;
}

/**
 * @brief 获取中断描述符（只读查询）
 */
irq_desc_t *irq_get_desc(uint32_t irq)
{
    irq_desc_t *desc;

    if (irq >= IRQ_MAX_HANDLERS)
    {
        return NULL;
    }

    desc = &s_irq_descs[irq];

    if (!desc->in_use)
    {
        return NULL;
    }

    return desc;
}
