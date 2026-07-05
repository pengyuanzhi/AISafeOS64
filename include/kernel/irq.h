/**
 * @file    irq.h
 * @brief   中断管理子系统接口
 * @author  AISafe64 Team
 * @date    2026-07-04
 * @version 3.0
 *
 * @details 本文件定义了中断管理子系统的对外接口：
 *          - 同一 IRQ 可挂载多个 handler（链表管理）
 *          - 每个 attach 拥有唯一 attach_id，支持精确 detach
 *          - mask/unmask 独立于 attach/detach 的临时屏蔽
 *          - CPU 亲和性（cpu_mask）路由
 *          - 中断触发计数与诊断统计
 *          - 能力（CSpace）权限校验，无 CSpace 的内核线程放行
 *
 *          中断处理流程：
 *          1. 硬件中断触发，架构层调用 irq_dispatch(irq)
 *          2. 检查 CPU 亲和性与 mask 状态
 *          3. 遍历 handler 链表，依次调用 handler 或投递 notification
 *          4. 更新计数统计
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: IN-001~006
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_IRQ_H
#define KERNEL_IRQ_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/hal_irq.h>
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
 * @details 内核态中断处理回调。在中断上下文中被 irq_dispatch 调用。
 *
 * @param irq 中断号
 * @param arg 注册时传入的用户参数
 */
typedef void (*irq_handler_t)(uint32_t irq, void *arg);

/* ========================================================================
 * 中断配置常量
 * ======================================================================== */

/**
 * @brief 中断描述符表容量
 *
 * @details 静态中断描述符数组的大小，索引即硬件中断号。
 *          覆盖 SGI/PPI/SPI，与 GICv2 中断号空间对齐。
 */
#define IRQ_MAX_HANDLERS        256U

/**
 * @brief 中断绑定条目池容量
 *
 * @details 静态 irq_entry_t 池的大小，所有 IRQ 共享该池。
 *          支持 handler 总数（）上限。
 */
#define IRQ_MAX_ENTRIES         256U

/**
 * @brief 无效 attach_id
 *
 * @details 用于标识尚未分配或已失效的 attach 条目。
 *          irq_attach 返回 > 0 表示成功。
 */
#define IRQ_ATTACH_ID_INVALID   0U

/** @brief 中断优先级最小值（最高优先级） */
#define IRQ_PRIORITY_MIN        0U

/** @brief 中断优先级最大值（最低优先级） */
#define IRQ_PRIORITY_MAX        255U

/** @brief 默认中断优先级 */
#define IRQ_PRIORITY_DEFAULT    128U

/** @brief 默认 CPU 亲和性掩码（CPU 0） */
#define IRQ_CPU_MASK_DEFAULT    0x01U

/** @brief 全 CPU 亲和性掩码（所有 CONFIG_MAX_CPUS 位） */
#define IRQ_CPU_MASK_ALL        ((uint32_t)0xFFFFFFFFU)

/* ========================================================================
 * 中断绑定条目（）
 * ======================================================================== */

/**
 * @brief 中断绑定条目
 *
 * @details 每一次 irq_attach 分配一个独立条目，挂到对应 IRQ 描述符的
 *          handler 链表上。同一 IRQ 可挂载多个条目以线。
 *          attach_id 全局递增、永不复用，用于精确 detach 与防 UAF。
 */
typedef struct irq_entry
{
    struct list_head node;            /**< @brief 链表节点（挂在 irq_desc_t.handlers 上） */
    uint32_t         attach_id;       /**< @brief 唯一 attach ID（用于精确 detach） */
    irq_handler_t    handler;         /**< @brief 内核处理函数（NULL = 纯通知模式） */
    void            *handler_arg;     /**< @brief 处理函数参数 */
    kobj_id_t        notification_id; /**< @brief 通知对象（KOBJ_ID_INVALID = 纯 handler 模式） */
    uint32_t         count;           /**< @brief 此条目的触发次数 */
} irq_entry_t;

/* ========================================================================
 * 中断描述符
 * ======================================================================== */

/**
 * @brief 中断描述符
 *
 * @details 每个硬件中断号对应一个描述符，维护该中断的配置、handler 链表
 *          与统计信息。handler 链表，由独立 TicketLock 保护。
 */
typedef struct
{
    struct list_head handlers;     /**< @brief handler 链表头（） */
    uint32_t         irq;          /**< @brief 中断号 */
    uint32_t         cpu_mask;     /**< @brief CPU 亲和性掩码（bit i = CPU i） */
    uint8_t          trigger_mode; /**< @brief 触发模式（@ref irq_trigger_t） */
    uint8_t          priority;     /**< @brief 优先级 */
    bool             in_use;       /**< @brief 是否有至少一个 handler */
    bool             masked;       /**< @brief 是否被 mask（临时屏蔽） */
    uint64_t         total_count;  /**< @brief 总触发次数 */
    TicketLock_t     lock;         /**< @brief 保护 handler 链表 */
} irq_desc_t;

/* ========================================================================
 * 中断统计信息
 * ======================================================================== */

/**
 * @brief 中断统计信息
 *
 * @details 由 irq_get_stats 返回，用于诊断与监控。
 */
typedef struct
{
    uint64_t total_count;   /**< @brief 总触发次数 */
    uint32_t handler_count; /**< @brief 当前 handler 数量（含共享） */
    bool     masked;        /**< @brief 是否处于 mask 状态 */
} irq_stats_t;

/* ========================================================================
 * 中断管理 API
 * ======================================================================== */

/**
 * @brief 初始化中断管理子系统
 *
 * @details 初始化中断描述符表、handler 条目池与自旋锁。
 *          应在架构层中断控制器初始化后、调度器启动前调用。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: IN-001
 */
kernel_status_t irq_subsys_init(void);

/**
 * @brief 绑定中断（）
 *
 * @details 为指定中断号新增一个 handler 条目。同一 IRQ 可被多次 attach，
 *          形成 handler 链表。每个 attach 返回唯一的 attach_id，用于
 *          精确 detach。
 *
 *          - handler 与 notification_id 至少传一个（可同时存在）
 *          - 首次 attach 时配置中断控制器（优先级/触发模式/亲和性）并使能
 *          - 后续 attach 仅追加 handler 条目
 *
 * @param irq             硬件中断号（< IRQ_MAX_HANDLERS）
 * @param handler         内核处理函数（NULL = 纯通知模式）
 * @param arg             handler 参数（可为 NULL）
 * @param notification_id 通知对象 ID（KOBJ_ID_INVALID = 纯 handler 模式）
 * @param trigger         触发模式
 * @param priority        优先级（0-255）
 *
 * @return >0 成功，返回 attach_id
 * @return <=0 失败，返回负错误码（-EINVAL/-ENOMEM/-EACCES 等）
 *
 * @note 对应需求: IN-002
 */
int32_t irq_attach(uint32_t irq,
                   irq_handler_t handler,
                   void *arg,
                   kobj_id_t notification_id,
                   irq_trigger_t trigger,
                   uint8_t priority);

/**
 * @brief 按 attach_id 精确解绑
 *
 * @details 移除指定 attach_id 对应的 handler 条目。当某 IRQ 的最后一个
 *          条目被移除时，禁用该中断并清空描述符。
 *
 * @param attach_id 要解绑的 attach ID（由 irq_attach 返回）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL attach_id 无效或不存在
 *
 * @note 对应需求: IN-003
 */
kernel_status_t irq_detach_by_id(uint32_t attach_id);

/**
 * @brief 按 IRQ 号解绑所有 handler
 *
 * @details 解绑指定中断号上的所有 handler 条目，禁用并清空该中断描述符。
 *
 * @param irq 硬件中断号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效或未绑定
 *
 * @note 对应需求: IN-003
 */
kernel_status_t irq_detach(uint32_t irq);

/**
 * @brief 临时屏蔽单个中断（不 detach）
 *
 * @details 调用 hal_irq_disable 屏蔽硬件中断，但保留所有 handler 条目。
 *          用于驱动中需要临时屏蔽中断做原子操作的场景。
 *
 * @param irq 硬件中断号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效或未绑定
 *
 * @note 对应需求: IN-006
 */
kernel_status_t irq_mask(uint32_t irq);

/**
 * @brief 恢复被屏蔽的中断
 *
 * @details 调用 hal_irq_enable 恢复硬件中断，不修改 handler 链表。
 *
 * @param irq 硬件中断号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效或未绑定
 *
 * @note 对应需求: IN-006
 */
kernel_status_t irq_unmask(uint32_t irq);

/**
 * @brief 中断分发（遍历 handler 链表）
 *
 * @details 在中断上下文中调用。根据中断号查找描述符，进行：
 *          - CPU 亲和性检查：cpu_mask 不含当前 CPU 则跳过
 *          - mask 状态检查：已 mask 则跳过
 *          - 遍历 handler 链表，依次调用 handler 或投递 notification
 *          - 更新触发计数
 *
 * @param irq 中断号
 *
 * @note 对应需求: IN-004
 * @note 此函数在中断上下文中执行，必须快速返回
 */
void irq_dispatch(uint32_t irq);

/**
 * @brief 注册内核中断处理函数（简化接口）
 *
 * @details irq_attach 的便捷封装：仅绑定内核 handler，触发模式默认
 *          LEVEL_HIGH，优先级默认 IRQ_PRIORITY_DEFAULT，CPU 亲和性默认
 *          CPU 0。。
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
                             void *arg);

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
                                       irq_handler_t handler);

/**
 * @brief 查询中断统计信息
 *
 * @param irq   中断号
 * @param stats 输出统计信息（调用者分配，不得为 NULL）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效或中断未绑定
 *
 * @note 对应需求: IN-006
 */
kernel_status_t irq_get_stats(uint32_t irq, irq_stats_t *stats);

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
irq_desc_t *irq_get_desc(uint32_t irq);

#endif /* KERNEL_IRQ_H */
