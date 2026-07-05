/**
 * @file    irq.c
 * @brief   中断管理子系统实现（对标商用 RTOS）
 * @author  AISafe64 Team
 * @date    2026-07-04
 * @version 3.0
 *
 * @details 本文件实现中断管理子系统，对标商用 RTOS 中断管理子系统：
 *
 *          - **共享中断**：同一 IRQ 可挂载多个 handler（irq_entry_t 链表），
 *            dispatch 时遍历调用全部 handler，支持 PCI MSI-X 等共享中断线场景。
 *          - **attach_id 机制**：每次 attach 分配全局唯一且递增的 ID，
 *            支持 irq_detach_by_id 精确解绑，ID 永不复用，防止 UAF。
 *          - **mask/unmask 独立**：mask 仅调 hal_irq_disable，不修改链表，
 *            用于驱动中临时屏蔽中断做原子操作。
 *          - **CPU 亲和性**：dispatch 检查 cpu_mask & (1 << cpu_id)，
 *            将中断路由到正确的 CPU。
 *          - **诊断统计**：每个 IRQ 与每个 entry 记录触发次数，
 *            irq_get_stats 查询。
 *          - **能力校验**：irq_attach/detach/register_handler 检查调用者
 *            是否有 IRQ 能力（有 CSpace 时），无 CSpace 的内核线程放行。
 *          - **静态池**：irq_entry_t 使用静态池 + 空闲链表管理，无动态内存。
 *
 *          中断处理流程：
 *          1. 硬件中断触发，架构层调用 irq_dispatch(irq)
 *          2. 检查 CPU 亲和性、mask 状态
 *          3. 遍历 handler 链表，调用 handler 或投递 notification
 *          4. 更新计数
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: IN-001~006
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */

#include <kernel/irq.h>
#include <kernel/hal_irq.h>
#include <kernel/config.h>
#include <kernel/barrier.h>
#include <kernel/spinlock.h>
#include <kernel/errno.h>
#include <kernel/ipc_notification.h>
#include <kernel/kobject.h>
#include <kernel/capability.h>
#include <kernel/cspace.h>
#include <kernel/smp.h>
#include "thread.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ========================================================================
 * 全局状态
 * ======================================================================== */

/**
 * @brief 中断描述符数组
 *
 * @details 静态数组，索引为中断号，存储每个中断的配置、handler 链表与统计。
 *          大小由 IRQ_MAX_HANDLERS（256）定义。
 */
static irq_desc_t s_irq_descs[IRQ_MAX_HANDLERS];

/**
 * @brief 中断绑定条目静态池
 *
 * @details 所有 IRQ 共享的 irq_entry_t 池，由空闲链表管理。
 *          大小由 IRQ_MAX_ENTRIES（256）定义。
 */
static irq_entry_t s_irq_entries[IRQ_MAX_ENTRIES];

/**
 * @brief 空闲 entry 链表头
 *
 * @details 使用 irq_entry_t.node 串成单向空闲链表（取下时仅用 next 指针）。
 *          池初始化时所有 entry 依次链入。
 */
static struct list_head s_free_entries;

/**
 * @brief attach_id 全局递增计数器
 *
 * @details 每次 irq_attach 成功分配后递增，永不复用，防止 UAF。
 *          起始为 1，0 保留为 IRQ_ATTACH_ID_INVALID。
 */
static uint32_t s_next_attach_id = 1U;

/**
 * @brief 中断子系统初始化标志
 */
static bool s_initialized = false;

/* ========================================================================
 * 内部辅助：entry 池管理
 * ======================================================================== */

/**
 * @brief 初始化 entry 静态池
 *
 * @details 将所有 s_irq_entries 条目清零并串入空闲链表 s_free_entries。
 */
static void irq_entry_pool_init(void)
{
    uint32_t i;

    INIT_LIST_HEAD(&s_free_entries);

    for (i = 0U; i < IRQ_MAX_ENTRIES; i++)
    {
        irq_entry_t *entry = &s_irq_entries[i];

        entry->attach_id       = IRQ_ATTACH_ID_INVALID;
        entry->handler         = NULL;
        entry->handler_arg     = NULL;
        entry->notification_id = KOBJ_ID_INVALID;
        entry->count           = 0U;
        INIT_LIST_HEAD(&entry->node);
        list_add_tail(&entry->node, &s_free_entries);
    }
}

/**
 * @brief 从空闲链表分配一个 entry
 *
 * @details 从 s_free_entries 头部取下一个空闲 entry，返回其指针。
 *          调用者负责填充字段并将其挂到目标描述符的 handlers 链表。
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
 * @details 将 entry 从其所属描述符链表摘除，重置字段后归还空闲池。
 *          调用前应已持有描述符锁。
 *
 * @param entry 要释放的 entry 指针（不得为 NULL）
 */
static void irq_entry_free(irq_entry_t *entry)
{
    if (entry == NULL)
    {
        return;
    }

    list_del_init(&entry->node);
    entry->attach_id       = IRQ_ATTACH_ID_INVALID;
    entry->handler         = NULL;
    entry->handler_arg     = NULL;
    entry->notification_id = KOBJ_ID_INVALID;
    entry->count           = 0U;
    list_add_tail(&entry->node, &s_free_entries);
}

/* ========================================================================
 * 内部辅助：描述符管理
 * ======================================================================== */

/**
 * @brief 初始化单个中断描述符
 *
 * @details 重置描述符为空闲态：空 handler 链表、默认配置、零计数。
 *
 * @param desc 描述符指针（不得为 NULL）
 */
static void irq_desc_reset(irq_desc_t *desc)
{
    INIT_LIST_HEAD(&desc->handlers);
    desc->irq           = 0U;
    desc->cpu_mask      = IRQ_CPU_MASK_DEFAULT;
    desc->trigger_mode  = (uint8_t)IRQ_TRIGGER_LEVEL_HIGH;
    desc->priority      = (uint8_t)IRQ_PRIORITY_DEFAULT;
    desc->in_use        = false;
    desc->masked        = false;
    desc->total_count   = 0ULL;
}

/**
 * @brief 按全局 attach_id 在所有描述符中查找 entry
 *
 * @details 遍历全部描述符的 handler 链表，查找匹配 attach_id 的 entry。
 *          用于 irq_detach_by_id 精确解绑。调用者无需持锁，函数内部
 *          逐个描述符加锁遍历。
 *
 * @param attach_id    要查找的 attach ID
 * @param out_desc     输出所属描述符指针（可为 NULL）
 *
 * @return 匹配的 entry 指针，未找到返回 NULL
 */
static irq_entry_t *irq_find_entry_by_id(uint32_t attach_id,
                                         irq_desc_t **out_desc)
{
    uint32_t i;

    for (i = 0U; i < IRQ_MAX_HANDLERS; i++)
    {
        irq_desc_t *desc = &s_irq_descs[i];
        irq_entry_t *entry;

        if (!desc->in_use)
        {
            continue;
        }

        ticket_lock_acquire(&desc->lock);
        list_for_each_entry(entry, &desc->handlers, node)
        {
            if (entry->attach_id == attach_id)
            {
                if (out_desc != NULL)
                {
                    *out_desc = desc;
                }
                /* 注意：此处持锁返回，调用者负责释放 */
                return entry;
            }
        }
        ticket_lock_release(&desc->lock);
    }

    return NULL;
}

/* ========================================================================
 * 内部辅助：能力校验
 * ======================================================================== */

/**
 * @brief 校验当前线程是否拥有操作指定 IRQ 的能力
 *
 * @details 双路径访问控制（与 endpoint_check_access / thread_check_cap 一致）：
 *          1. 当前线程有 CSpace 时，遍历能力表查找指向该 IRQ 的
 *             KOBJ_INTERRUPT + WRITE 能力。未找到或权限不足返回 -EACCES。
 *          2. 当前线程无 CSpace（内核线程/早期线程）时，直接放行，
 *             保持与内核管理路径的兼容性。
 *
 *          IRQ 对象 ID 约定为中断号本身（kobj_id_t 与 uint32_t 同宽）。
 *
 * @param irq 目标中断号
 *
 * @return KERNEL_OK 有权限
 * @return -ESRCH   无当前线程
 * @return -EACCES  有 CSpace 但无匹配能力或权限不足
 */
static kernel_status_t irq_check_access(uint32_t irq)
{
    KThread_t *current = kthread_get_current();

    if (current == NULL)
    {
        return -(int32_t)ESRCH;
    }

    /* 无 CSpace 的线程（内核线程）放行，兼容内核管理路径 */
    if (current->cspace == NULL)
    {
        return KERNEL_OK;
    }

    /* 有 CSpace：必须持有指向该 IRQ 的 KOBJ_INTERRUPT + WRITE 能力 */
    {
        cspace_t *cs = (cspace_t *)current->cspace;
        cap_slot_t slot;
        bool found = false;

        ticket_lock_acquire(&cs->lock);
        for (slot = 0U; slot < cs->capacity; slot++)
        {
            cap_t *cap = cspace_lookup(cs, slot);
            if ((cap != NULL) &&
                (cap->state == CAP_STATE_VALID) &&
                (cap->kobj_type == KOBJ_INTERRUPT) &&
                (cap->kobj_id == (kobj_id_t)irq))
            {
                /* 找到匹配能力，校验 WRITE 权限 */
                if ((cap->rights & CAP_RIGHT_WRITE) == CAP_RIGHT_WRITE)
                {
                    found = true;
                }
                break;
            }
        }
        ticket_lock_release(&cs->lock);

        if (!found)
        {
            return -(int32_t)EACCES;
        }
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 中断管理 API 实现
 * ======================================================================== */

/**
 * @brief 初始化中断管理子系统
 *
 * @details 初始化所有中断描述符的 handler 链表与锁，初始化 entry 静态池，
 *          重置 attach_id 计数器。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: IN-001
 */
kernel_status_t irq_subsys_init(void)
{
    uint32_t i;

    /* 初始化所有描述符 */
    for (i = 0U; i < IRQ_MAX_HANDLERS; i++)
    {
        irq_desc_t *desc = &s_irq_descs[i];

        ticket_lock_init(&desc->lock);
        irq_desc_reset(desc);
        desc->irq = i;
    }

    /* 初始化 entry 池 */
    irq_entry_pool_init();

    /* 重置 attach_id 计数器 */
    s_next_attach_id = 1U;

    /* 设置初始化标志 */
    s_initialized = true;
    barrier();

    return KERNEL_OK;
}

/**
 * @brief 绑定中断（支持共享中断）
 *
 * @details 为指定中断号新增一个 handler 条目。首次 attach 时配置中断
 *          控制器（优先级/触发模式/亲和性）并使能；后续 attach 仅追加
 *          条目，复用已有硬件配置。
 *
 *          handler 与 notification_id 至少传其一（可同时存在），
 *          dispatch 时两者都会被调用。
 *
 * @param irq             硬件中断号（< IRQ_MAX_HANDLERS）
 * @param handler         内核处理函数（NULL = 纯通知模式）
 * @param arg             handler 参数（可为 NULL）
 * @param notification_id 通知对象 ID（KOBJ_ID_INVALID = 纯 handler 模式）
 * @param trigger         触发模式
 * @param priority        优先级（0-255）
 *
 * @return >0 成功，返回 attach_id
 * @return <=0 失败，返回负错误码
 *
 * @note 对应需求: IN-002
 */
int32_t irq_attach(uint32_t irq,
                   irq_handler_t handler,
                   void *arg,
                   kobj_id_t notification_id,
                   irq_trigger_t trigger,
                   uint8_t priority)
{
    kernel_status_t acc;
    irq_desc_t *desc;
    irq_entry_t *entry;
    uint32_t new_id;
    bool first_attach;

    /* 参数验证 */
    if (irq >= IRQ_MAX_HANDLERS)
    {
        return -(int32_t)EINVAL;
    }

    /* handler 与 notification 至少传其一 */
    if ((handler == NULL) && (notification_id == KOBJ_ID_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    /* 能力校验 */
    acc = irq_check_access(irq);
    if (acc != KERNEL_OK)
    {
        return acc;
    }

    desc = &s_irq_descs[irq];

    ticket_lock_acquire(&desc->lock);

    first_attach = !desc->in_use;

    /* 分配 entry */
    entry = irq_entry_alloc();
    if (entry == NULL)
    {
        ticket_lock_release(&desc->lock);
        return -(int32_t)ENOMEM;
    }

    /* 分配 attach_id（递增、不复用） */
    new_id = s_next_attach_id;
    s_next_attach_id++;

    /* 填充 entry */
    entry->attach_id       = new_id;
    entry->handler         = handler;
    entry->handler_arg     = arg;
    entry->notification_id = notification_id;
    entry->count           = 0U;

    /* 挂到描述符 handler 链表尾部 */
    list_add_tail(&entry->node, &desc->handlers);

    /* 首次 attach：配置硬件并使能 */
    if (first_attach)
    {
        desc->irq          = irq;
        desc->cpu_mask     = IRQ_CPU_MASK_DEFAULT;
        desc->trigger_mode = (uint8_t)trigger;
        desc->priority     = priority;
        desc->in_use       = true;
        desc->masked       = false;
        desc->total_count  = 0ULL;
        barrier();

        /* 配置中断控制器 */
        hal_irq_set_priority(irq, priority);
        hal_irq_set_trigger(irq, trigger);
        if (hal_irq_is_spi(irq))
        {
            hal_irq_set_affinity(irq, desc->cpu_mask);
        }

        hal_irq_enable(irq);
    }

    ticket_lock_release(&desc->lock);

    return (int32_t)new_id;
}

/**
 * @brief 按 attach_id 精确解绑
 *
 * @details 在所有描述符中查找匹配 attach_id 的 entry，移除并归还空闲池。
 *          若所属 IRQ 不再有任何 handler，禁用并清空描述符。
 *
 * @param attach_id 要解绑的 attach ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL attach_id 无效或不存在
 *
 * @note 对应需求: IN-003
 */
kernel_status_t irq_detach_by_id(uint32_t attach_id)
{
    irq_desc_t *desc = NULL;
    irq_entry_t *entry;

    if (attach_id == IRQ_ATTACH_ID_INVALID)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找时会持有 desc->lock，需调用者释放 */
    entry = irq_find_entry_by_id(attach_id, &desc);
    if ((entry == NULL) || (desc == NULL))
    {
        return -(int32_t)EINVAL;
    }

    /* 此时持有 desc->lock */
    irq_entry_free(entry);

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
 * @brief 按 IRQ 号解绑所有 handler
 *
 * @details 解绑指定中断号上的全部 entry，禁用并清空该描述符。
 *
 * @param irq 硬件中断号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效或未绑定
 *
 * @note 对应需求: IN-003
 */
kernel_status_t irq_detach(uint32_t irq)
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

    /* 禁用中断 */
    hal_irq_disable(irq);

    /* 释放所有 entry */
    list_for_each_safe(pos, n, &desc->handlers)
    {
        irq_entry_t *entry = list_entry(pos, irq_entry_t, node);
        irq_entry_free(entry);
    }

    /* 清空描述符 */
    irq_desc_reset(desc);
    barrier();

    ticket_lock_release(&desc->lock);

    return KERNEL_OK;
}

/**
 * @brief 临时屏蔽单个中断（不 detach）
 *
 * @details 调用 hal_irq_disable 屏蔽硬件中断，置 masked 标志，
 *          不修改 handler 链表。可由 irq_unmask 恢复。
 *
 * @param irq 硬件中断号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效或未绑定
 *
 * @note 对应需求: IN-006
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

    /* 屏蔽硬件中断，不修改链表 */
    hal_irq_disable(irq);
    desc->masked = true;
    barrier();

    ticket_lock_release(&desc->lock);

    return KERNEL_OK;
}

/**
 * @brief 恢复被屏蔽的中断
 *
 * @details 调用 hal_irq_enable 恢复硬件中断，清除 masked 标志，
 *          不修改 handler 链表。
 *
 * @param irq 硬件中断号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效或未绑定
 *
 * @note 对应需求: IN-006
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

    /* 恢复硬件中断 */
    hal_irq_enable(irq);
    desc->masked = false;
    barrier();

    ticket_lock_release(&desc->lock);

    return KERNEL_OK;
}

/**
 * @brief 中断分发（遍历 handler 链表）
 *
 * @details 在中断上下文中调用，处理流程：
 *          1. 校验中断号与描述符有效性
 *          2. CPU 亲和性检查：cpu_mask & (1 << cpu_id) 为 0 则跳过
 *          3. mask 状态检查：已 mask 则直接返回
 *          4. 遍历 handler 链表：依次调用 handler（非 NULL）或投递
 *             notification（非 INVALID），并累加计数
 *          5. 累加描述符总计数
 *
 * @param irq 中断号
 *
 * @note 对应需求: IN-004
 * @note 此函数在中断上下文中执行，必须快速返回
 */
void irq_dispatch(uint32_t irq)
{
    irq_desc_t *desc;
    uint32_t cpu_id;
    uint32_t cpu_bit;
    irq_entry_t *entry;

    if (irq >= IRQ_MAX_HANDLERS)
    {
        return;
    }

    desc = &s_irq_descs[irq];

    if (!desc->in_use)
    {
        return;
    }

    /* CPU 亲和性检查：cpu_mask & (1 << cpu_id) */
    cpu_id  = smp_get_cpu_id();
    cpu_bit = 1UL << cpu_id;
    if ((desc->cpu_mask & cpu_bit) == 0U)
    {
        /* 中断路由到其他 CPU，跳过 */
        return;
    }

    /* mask 状态检查 */
    if (desc->masked)
    {
        return;
    }

    /* 累加描述符总计数（中断上下文单核独占，无需原子） */
    desc->total_count++;

    /* 遍历 handler 链表 */
    list_for_each_entry(entry, &desc->handlers, node)
    {
        /* 投递通知（若绑定） */
        if (entry->notification_id != KOBJ_ID_INVALID)
        {
            (void)ipc_notification_signal(entry->notification_id,
                                          (uint64_t)1ULL << (irq & 63U));
        }

        /* 调用内核 handler（若绑定） */
        if (entry->handler != NULL)
        {
            entry->handler(irq, entry->handler_arg);
        }

        /* 累加条目计数 */
        entry->count++;
    }
}

/**
 * @brief 注册内核中断处理函数（简化接口）
 *
 * @details irq_attach 的便捷封装：仅绑定内核 handler，触发模式默认
 *          LEVEL_HIGH，优先级默认 IRQ_PRIORITY_DEFAULT。
 *          支持共享中断（可对同一 IRQ 多次注册）。
 *
 * @param irq      中断号
 * @param handler  处理函数（不得为 NULL）
 * @param arg      用户参数（可为 NULL）
 *
 * @return >0 成功，返回 attach_id
 * @return <=0 失败，返回负错误码
 *
 * @note 对应需求: IN-005
 */
int32_t irq_register_handler(uint32_t irq,
                             irq_handler_t handler,
                             void *arg)
{
    if (handler == NULL)
    {
        return -(int32_t)EINVAL;
    }

    return irq_attach(irq,
                      handler,
                      arg,
                      KOBJ_ID_INVALID,
                      IRQ_TRIGGER_LEVEL_HIGH,
                      (uint8_t)IRQ_PRIORITY_DEFAULT);
}

/**
 * @brief 按 handler 注销内核中断处理函数
 *
 * @details 移除指定 IRQ 上匹配 handler 指针的第一个条目。
 *          若该 IRQ 不再有任何 handler，禁用并清空描述符。
 *
 * @param irq     中断号
 * @param handler 要注销的处理函数指针
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效、未绑定或未找到匹配 handler
 */
kernel_status_t irq_unregister_handler(uint32_t irq,
                                       irq_handler_t handler)
{
    irq_desc_t *desc;
    irq_entry_t *entry;
    bool found = false;

    if (irq >= IRQ_MAX_HANDLERS)
    {
        return -(int32_t)EINVAL;
    }

    if (handler == NULL)
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

    /* 查找匹配 handler 的第一个 entry */
    list_for_each_entry(entry, &desc->handlers, node)
    {
        if (entry->handler == handler)
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        ticket_lock_release(&desc->lock);
        return -(int32_t)EINVAL;
    }

    irq_entry_free(entry);

    /* 若链表空，禁用并清空描述符 */
    if (list_empty(&desc->handlers) != 0)
    {
        hal_irq_disable(irq);
        irq_desc_reset(desc);
    }

    barrier();
    ticket_lock_release(&desc->lock);

    return KERNEL_OK;
}

/**
 * @brief 查询中断统计信息
 *
 * @details 返回指定中断的总触发次数、当前 handler 数量与 mask 状态。
 *
 * @param irq   中断号
 * @param stats 输出统计信息（调用者分配，不得为 NULL）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效或中断未绑定
 *
 * @note 对应需求: IN-006
 */
kernel_status_t irq_get_stats(uint32_t irq, irq_stats_t *stats)
{
    irq_desc_t *desc;

    if (stats == NULL)
    {
        return -(int32_t)EINVAL;
    }

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

    stats->total_count   = desc->total_count;
    stats->handler_count = list_count_nodes(&desc->handlers);
    stats->masked        = desc->masked;

    ticket_lock_release(&desc->lock);

    return KERNEL_OK;
}

/**
 * @brief 获取中断描述符（只读查询）
 *
 * @param irq 中断号
 *
 * @return 中断描述符指针，未注册或参数无效返回 NULL
 *
 * @warning 返回的描述符内部状态受描述符自身锁保护，调用者若需读取
 *          链表内容应自行加锁。
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
