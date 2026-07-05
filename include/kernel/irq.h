/**
 * @file    irq.h
 * @brief   中断管理子系统接口
 * @author  AISafe64 Team
 * @date    2026-07-05
 * @version 4.0
 *
 * @details 本文件定义中断管理子系统的对外接口：
 *          - 同一 IRQ 可挂载多个 handler（链表管理）
 *          - attach 时校验 IRQF_SHARED 标志与 handler_arg 唯一性
 *          - handler 返回 irq_return_t，dispatch 统计伪中断
 *          - mask/unmask 独立于 attach/detach
 *          - CPU 亲和性路由
 *          - 触发计数与诊断统计
 *          - 能力校验
 *
 * @note MISRA-C:2012 合规
 *
 * @revision history
 * v1.0 2026-04-01 初始版本（单 handler）
 * v2.0 2026-07-04 HAL 抽象 + irq_ 前缀统一
 * v3.0 2026-07-05 多 handler 链表 + attach_id + mask/unmask + 亲和性 + 统计
 * v4.0 2026-07-05 O(1) detach + 伪中断 + IRQF_SHARED + 失败回滚（当前版本）
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
 * 中断处理返回值
 * ======================================================================== */

/**
 * @brief 中断处理函数返回值
 *
 * @details handler 必须返回 IRQ_HANDLED 或 IRQ_NONE。
 *          dispatch 遍历全部 handler 后若无任何 IRQ_HANDLED，
 *          计为伪中断（spurious），用于诊断硬件故障或驱动问题。
 */
typedef enum
{
    IRQ_NONE    = 0U,  /**< @brief 非本设备产生的中断 */
    IRQ_HANDLED = 1U   /**< @brief 已处理 */
} irq_return_t;

/* ========================================================================
 * 中断处理函数类型
 * ======================================================================== */

/**
 * @brief 中断处理函数指针
 *
 * @param irq 中断号
 * @param arg 注册时传入的用户参数
 * @return IRQ_HANDLED 已处理，IRQ_NONE 非本设备
 */
typedef irq_return_t (*irq_handler_t)(uint32_t irq, void *arg);

/* ========================================================================
 * attach 标志
 * ======================================================================== */

/**
 * @brief 允许同一 IRQ 多 handler
 *
 * @details attach 时传入此标志表示允许与其他 handler 共用同一中断号。
 *          若已有 handler 未声明此标志，新 attach 返回 -EBUSY。
 */
#define IRQF_SHARED     (1U << 0U)

/* ========================================================================
 * 中断配置常量
 * ======================================================================== */

/** @brief 中断描述符表容量（索引即硬件中断号） */
#define IRQ_MAX_HANDLERS        256U

/** @brief handler 条目池容量（所有 IRQ 共享） */
#define IRQ_MAX_ENTRIES         256U

/** @brief 无效 attach_id（0 保留） */
#define IRQ_ATTACH_ID_INVALID   0U

/** @brief 中断优先级最小值（最高优先级） */
#define IRQ_PRIORITY_MIN        0U

/** @brief 中断优先级最大值（最低优先级） */
#define IRQ_PRIORITY_MAX        255U

/** @brief 默认中断优先级 */
#define IRQ_PRIORITY_DEFAULT    128U

/** @brief 默认 CPU 亲和性掩码（CPU 0） */
#define IRQ_CPU_MASK_DEFAULT    0x01U

/** @brief 全 CPU 亲和性掩码 */
#define IRQ_CPU_MASK_ALL        ((uint32_t)0xFFFFFFFFU)

/* ========================================================================
 * 中断绑定条目
 * ======================================================================== */

/**
 * @brief 中断绑定条目
 *
 * @details 每次 irq_attach 分配一个条目，挂到对应 IRQ 描述符的链表上。
 *          attach_id 全局递增、永不复用。
 *          handler_arg 在 IRQF_SHARED 模式下必须非 NULL 且唯一，
 *          用于精确区分同一 IRQ 上的不同设备。
 */
typedef struct irq_entry
{
    struct list_head node;            /**< @brief 链表节点 */
    uint32_t         attach_id;       /**< @brief 唯一 attach ID */
    uint32_t         flags;           /**< @brief attach 标志（如 IRQF_SHARED） */
    irq_handler_t    handler;         /**< @brief 内核处理函数（NULL = 纯通知模式） */
    void            *handler_arg;     /**< @brief 处理函数参数（IRQF_SHARED 时必须唯一） */
    kobj_id_t        notification_id; /**< @brief 通知对象（KOBJ_ID_INVALID = 纯 handler） */
    uint32_t         count;           /**< @brief 此条目的触发次数 */
    uint32_t         spurious_count;  /**< @brief 此条目返回 IRQ_NONE 的次数 */
} irq_entry_t;

/* ========================================================================
 * 中断描述符
 * ======================================================================== */

/**
 * @brief 中断描述符（每个硬件中断号一个）
 *
 * @details 维护该中断的配置、handler 链表与统计信息。
 *          由独立 TicketLock 保护链表操作。
 */
typedef struct
{
    struct list_head handlers;     /**< @brief handler 链表头 */
    uint32_t         irq;          /**< @brief 中断号 */
    uint32_t         cpu_mask;     /**< @brief CPU 亲和性掩码（bit i = CPU i） */
    uint8_t          trigger_mode; /**< @brief 触发模式（irq_trigger_t） */
    uint8_t          priority;     /**< @brief 优先级 */
    bool             in_use;       /**< @brief 是否有至少一个 handler */
    bool             masked;       /**< @brief 是否被 mask（临时屏蔽） */
    uint64_t         total_count;  /**< @brief 总触发次数 */
    uint64_t         spurious_count; /**< @brief 伪中断次数（全部 handler 返回 IRQ_NONE） */
    TicketLock_t     lock;         /**< @brief 保护 handler 链表 */
} irq_desc_t;

/* ========================================================================
 * 中断统计信息
 * ======================================================================== */

/**
 * @brief 中断统计信息（由 irq_get_stats 返回）
 */
typedef struct
{
    uint64_t total_count;     /**< @brief 总触发次数 */
    uint64_t spurious_count;  /**< @brief 伪中断次数 */
    uint32_t handler_count;   /**< @brief 当前 handler 数量 */
    bool     masked;          /**< @brief 是否处于 mask 状态 */
} irq_stats_t;

/* ========================================================================
 * 中断管理 API
 * ======================================================================== */

/**
 * @brief 初始化中断管理子系统
 *
 * @details 初始化描述符表、handler 条目池与自旋锁。
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t irq_subsys_init(void);

/**
 * @brief 绑定中断
 *
 * @details 为指定中断号新增一个 handler 条目。
 *
 *          契约约束：
 *          - 首次 attach 不需要 IRQF_SHARED
 *          - 后续 attach 必须传入 IRQF_SHARED，且已有 handler 也必须声明了 IRQF_SHARED
 *          - IRQF_SHARED 模式下 handler_arg 必须非 NULL 且在该 IRQ 上唯一
 *
 *          handler 与 notification_id 至少传一个（可同时存在）。
 *          首次 attach 时配置中断控制器并使能。
 *
 * @param irq             硬件中断号
 * @param handler         内核处理函数（NULL = 纯通知模式）
 * @param arg             handler 参数（IRQF_SHARED 时必须非 NULL 且唯一）
 * @param notification_id 通知对象（KOBJ_ID_INVALID = 纯 handler 模式）
 * @param trigger         触发模式
 * @param priority        优先级
 * @param flags           attach 标志（如 IRQF_SHARED）
 *
 * @return >0 成功，返回 attach_id
 * @return <=0 失败，返回负错误码
 */
int32_t irq_attach(uint32_t irq,
                   irq_handler_t handler,
                   void *arg,
                   kobj_id_t notification_id,
                   irq_trigger_t trigger,
                   uint8_t priority,
                   uint32_t flags);

/**
 * @brief 按 IRQ 号 + attach_id 精确解绑
 *
 * @details 移除指定 IRQ 上匹配 attach_id 的条目。
 *          当最后一个条目被移除时，禁用该中断并清空描述符。
 *
 * @param irq       硬件中断号
 * @param attach_id 要解绑的 attach ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效或未找到
 */
kernel_status_t irq_detach_by_id(uint32_t irq, uint32_t attach_id);

/**
 * @brief 按 IRQ 号 + handler + arg 精确解绑
 *
 * @details 移除指定 IRQ 上匹配 handler 和 handler_arg 的第一个条目。
 *          当最后一个条目被移除时，禁用该中断并清空描述符。
 *
 * @param irq     硬件中断号
 * @param handler 要注销的处理函数指针
 * @param arg     注册时传入的参数（用于区分同一 handler 的不同实例）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 未找到匹配
 */
kernel_status_t irq_detach_by_handler(uint32_t irq, irq_handler_t handler, void *arg);

/**
 * @brief 按 IRQ 号解绑所有 handler
 *
 * @param irq 硬件中断号
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效或未绑定
 */
kernel_status_t irq_detach_all(uint32_t irq);

/**
 * @brief 临时屏蔽单个中断（不 detach）
 *
 * @param irq 硬件中断号
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效或未绑定
 */
kernel_status_t irq_mask(uint32_t irq);

/**
 * @brief 恢复被屏蔽的中断
 *
 * @param irq 硬件中断号
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效或未绑定
 */
kernel_status_t irq_unmask(uint32_t irq);

/**
 * @brief 中断分发
 *
 * @details 遍历 handler 链表，依次调用 handler 或投递 notification。
 *          若全部 handler 返回 IRQ_NONE，计为伪中断。
 *
 * @param irq 中断号
 * @note 在中断上下文中执行，必须快速返回
 */
void irq_dispatch(uint32_t irq);

/**
 * @brief 注册内核中断处理函数（简化接口）
 *
 * @details irq_attach 的便捷封装，触发模式默认 LEVEL_HIGH，
 *          优先级默认 IRQ_PRIORITY_DEFAULT。
 *
 * @param irq      中断号
 * @param handler  处理函数（不得为 NULL）
 * @param arg      处理函数参数（可为 NULL，IRQF_SHARED 时必须非 NULL）
 *
 * @return >0 成功，返回 attach_id
 * @return <=0 失败，返回负错误码
 */
int32_t irq_register_handler(uint32_t irq,
                             irq_handler_t handler,
                             void *arg);

/**
 * @brief 按 handler 注销（封装 irq_detach_by_handler）
 *
 * @param irq     中断号
 * @param handler 要注销的处理函数指针
 * @return KERNEL_OK 成功
 * @return -EINVAL 未找到
 */
kernel_status_t irq_unregister_handler(uint32_t irq, irq_handler_t handler);

/**
 * @brief 查询中断统计信息
 *
 * @param irq   中断号
 * @param stats 输出统计信息（不得为 NULL）
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效或未绑定
 */
kernel_status_t irq_get_stats(uint32_t irq, irq_stats_t *stats);

/**
 * @brief 获取中断描述符（只读查询）
 *
 * @param irq 中断号
 * @return 描述符指针，未注册返回 NULL
 * @warning 调用者若需读取链表内容应自行加锁
 */
irq_desc_t *irq_get_desc(uint32_t irq);

#endif /* KERNEL_IRQ_H */
