/**
 * @file    vmm_events.h
 * @brief   VM 事件管理接口
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 本文件定义了 VM 事件管理的数据结构和接口：
 *          - 事件类型定义
 *          - 事件描述符
 *          - 事件队列
 *          - 事件回调
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef VMM_EVENTS_H
#define VMM_EVENTS_H

#include <stdint.h>
#include <stdbool.h>
#include <kernel/types.h>

/* ========================================================================
 * 事件类型定义
 * ======================================================================== */

/**
 * @brief VM 事件类型
 */
typedef enum
{
    VMM_EVENT_VM_CREATED = 0U,       /**< @brief VM 创建 */
    VMM_EVENT_VM_DESTROYED,          /**< @brief VM 销毁 */
    VMM_EVENT_VCPU_CREATED,          /**< @brief vCPU 创建 */
    VMM_EVENT_VCPU_DESTROYED,        /**< @brief vCPU 销毁 */
    VMM_EVENT_EXIT,                  /**< @brief VM 退出 */
    VMM_EVENT_IRQ,                   /**< @brief 中断注入 */
    VMM_EVENT_MMIO,                  /**< @brief MMIO 访问 */
    VMM_EVENT_MAX                   /**< @brief 最大事件类型 */
} vmm_event_type_t;

/* ========================================================================
 * 事件数据结构
 * ======================================================================== */

/**
 * @brief VM 创建事件数据
 */
typedef struct
{
    uint32_t vm_id;          /**< @brief VM ID */
    char name[32];           /**< @brief VM 名称 */
    uint64_t mem_size;       /**< @brief 内存大小 */
} vmm_event_vm_created_t;

/**
 * @brief VM 销毁事件数据
 */
typedef struct
{
    uint32_t vm_id;          /**< @brief VM ID */
} vmm_event_vm_destroyed_t;

/**
 * @brief vCPU 创建事件数据
 */
typedef struct
{
    uint32_t vm_id;          /**< @brief VM ID */
    uint32_t vcpu_id;        /**< @brief vCPU ID */
} vmm_event_vcpu_created_t;

/**
 * @brief vCPU 销毁事件数据
 */
typedef struct
{
    uint32_t vm_id;          /**< @brief VM ID */
    uint32_t vcpu_id;        /**< @brief vCPU ID */
} vmm_event_vcpu_destroyed_t;

/**
 * @brief VM 退出事件数据
 */
typedef struct
{
    uint32_t vm_id;          /**< @brief VM ID */
    uint32_t vcpu_id;        /**< @brief vCPU ID */
    uint32_t exit_reason;    /**< @brief 退出原因 */
    uint64_t esr;            /**< @brief ESR_EL2 */
    uint64_t far;            /**< @brief FAR_EL2 */
} vmm_event_exit_t;

/**
 * @brief 中断事件数据
 */
typedef struct
{
    uint32_t vm_id;          /**< @brief VM ID */
    uint32_t vcpu_id;        /**< @brief vCPU ID */
    uint32_t irq;            /**< @brief 中断号 */
} vmm_event_irq_t;

/**
 * @brief MMIO 事件数据
 */
typedef struct
{
    uint32_t vm_id;          /**< @brief VM ID */
    uint32_t vcpu_id;        /**< @brief vCPU ID */
    uint64_t addr;           /**< @brief 访问地址 */
    bool is_write;           /**< @brief 是否为写操作 */
    uint32_t size;           /**< @brief 访问大小 */
    uint64_t value;          /**< @brief 访问值 */
} vmm_event_mmio_t;

/* ========================================================================
 * 事件描述符
 * ======================================================================== */

/**
 * @brief 事件描述符
 */
typedef struct vmm_event_desc
{
    vmm_event_type_t type;         /**< @brief 事件类型 */
    uint32_t vm_id;                /**< @brief VM ID */
    uint32_t vcpu_id;              /**< @brief vCPU ID */
    
    // 联合体，根据 type 存储不同的事件数据
    union
    {
        vmm_event_vm_created_t vm_created;
        vmm_event_vm_destroyed_t vm_destroyed;
        vmm_event_vcpu_created_t vcpu_created;
        vmm_event_vcpu_destroyed_t vcpu_destroyed;
        vmm_event_exit_t exit;
        vmm_event_irq_t irq;
        vmm_event_mmio_t mmio;
    } data;
    
    bool is_pending;              /**< @brief 是否待处理 */
    void *user_data;               /**< @brief 用户数据 */
    void (*callback)(struct vmm_event_desc *event);  /**< @brief 回调函数 */
} vmm_event_desc_t;

/* ========================================================================
 * 事件队列结构
 * ======================================================================== */

/**
 * @brief 事件队列结构
 */
typedef struct
{
    vmm_event_desc_t *events;      /**< @brief 事件数组 */
    uint32_t capacity;             /**< @brief 队列容量 */
    uint32_t size;                 /**< @brief 当前大小 */
    uint32_t head;                 /**< @brief 队列头 */
    uint32_t tail;                 /**< @brief 队列尾 */
} vmm_event_queue_t;

/* ========================================================================
 * 公共 API 接口
 * ======================================================================== */

/**
 * @brief 初始化事件管理系统
 *
 * @param capacity 事件队列容量
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t vmm_events_init(uint32_t capacity);

/**
 * @brief 销毁事件管理系统
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_events_destroy(void);

/**
 * @brief 创建事件
 *
 * @param type 事件类型
 * @param vm_id VM ID
 * @param vcpu_id vCPU ID
 * @param event 返回事件描述符指针
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOMEM 无可用事件槽
 */
kernel_status_t vmm_events_create(vmm_event_type_t type,
                                    uint32_t vm_id,
                                    uint32_t vcpu_id,
                                    vmm_event_desc_t **event);

/**
 * @brief 销毁事件
 *
 * @param event 事件描述符指针
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_events_destroy_event(vmm_event_desc_t *event);

/**
 * @brief 添加事件到队列
 *
 * @param event 事件描述符指针
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EBUSY 队列已满
 */
kernel_status_t vmm_events_add(vmm_event_desc_t *event);

/**
 * @brief 从队列取出事件
 *
 * @param event 返回事件描述符指针
 *
 * @return KERNEL_OK 成功
 * @return -ENODATA 队列为空
 */
kernel_status_t vmm_events_remove(vmm_event_desc_t **event);

/**
 * @brief 等待事件
 *
 * @param timeout 超时时间（毫秒）
 *
 * @return KERNEL_OK 成功
 * @return -ENODATA 超时或队列为空
 */
kernel_status_t vmm_events_wait(uint32_t timeout);

/**
 * @brief 通知事件（触发回调）
 *
 * @param event 事件描述符指针
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t vmm_events_notify(vmm_event_desc_t *event);

/**
 * @brief 等待并通知事件
 *
 * @param timeout 超时时间（毫秒）
 *
 * @return KERNEL_OK 成功
 * @return -ENODATA 超时或队列为空
 */
kernel_status_t vmm_events_wait_and_notify(uint32_t timeout);

/**
 * @brief 清空所有事件
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_events_clear(void);

/**
 * @brief 注册事件回调
 *
 * @param callback 回调函数指针
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_events_register_callback(void (*callback)(vmm_event_desc_t *event));

/**
 * @brief 取消注册事件回调
 *
 * @param callback 回调函数指针
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_events_unregister_callback(void (*callback)(vmm_event_desc_t *event));

#endif /* VMM_EVENTS_H */
