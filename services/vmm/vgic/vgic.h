/**
 * @file    vgic.h
 * @brief   虚拟 GIC（VGIC）接口
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 本文件定义了虚拟 GIC（VGIC）相关数据结构和接口：
 *          - 中断状态枚举
 *          - VGIC 描述符
 *          - 公共 API 接口
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_VMM_VGIC_VGIC_H
#define SERVICES_VMM_VGIC_VGIC_H

#include <stdint.h>
#include <stdbool.h>
#include <kernel/types.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大虚拟中断数 */
#define VMM_VGIC_MAX_INTERRUPTS    (256U)

/* ========================================================================
 * 中断状态
 * ======================================================================== */

/**
 * @brief 虚拟 GIC 中断状态
 */
typedef enum
{
    VGIC_IRQ_INACTIVE = 0U,   /**< @brief 未激活 */
    VGIC_IRQ_PENDING,         /**< @brief 待处理 */
    VGIC_IRQ_ACTIVE,          /**< @brief 活跃 */
    VGIC_IRQ_ACTIVE_PENDING   /**< @brief 活跃且待处理 */
} vgic_irq_state_t;

/* ========================================================================
 * VGIC 描述符
 * ======================================================================== */

/**
 * @brief 虚拟 GIC 描述符
 *
 * @details 虚拟 GIC 用于模拟 Guest 中的 GICv2 控制器
 *
 * @details 支持的中断：
 *          - IPI (Inter-Processor Interrupt) - 核间中断
 *          - SPI (Shared Peripheral Interrupt) - 共享外设中断
 *          - PPI (Private Peripheral Interrupt) - 私有外设中断
 */
typedef struct
{
    /** @brief 中断状态 */
    vgic_irq_state_t irq_state[VMM_VGIC_MAX_INTERRUPTS];

    /** @brief 中断优先级（8 级优先级，0 最高） */
    uint8_t irq_priority[VMM_VGIC_MAX_INTERRUPTS];

    /** @brief 中断使能位图 */
    uint32_t irq_enabled[VMM_VGIC_MAX_INTERRUPTS / 32U + 1U];

    /** @brief 中断配置（Edge/Level） */
    uint8_t irq_config[VMM_VGIC_MAX_INTERRUPTS];

    /** @brief 中断路由（CPU 模式） */
    uint8_t irq_target[VMM_VGIC_MAX_INTERRUPTS];

    /** @brief 中断挂起位图 */
    uint32_t irq_pending[VMM_VGIC_MAX_INTERRUPTS / 32U + 1U];
} vgic_desc_t;

/* ========================================================================
 * 公共 API 接口
 * ======================================================================== */

/**
 * @brief VGIC 全局初始化
 *
 * @details 初始化 VGIC 描述符表
 */
void vgic_global_init(void);

/**
 * @brief VGIC 初始化
 *
 * @param vm_id   VM ID
 *
 * @return KERNEL_OK 成功
 * @return -EPERM  VGIC 未初始化
 * @return -EINVAL 参数无效
 */
kernel_status_t vgic_init(uint32_t vm_id);

/**
 * @brief VGIC 销毁
 *
 * @param vm_id   VM ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t vgic_destroy(uint32_t vm_id);

/**
 * @brief 注入虚拟中断到 vCPU
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param irq     中断号 (0~255)
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOENT VM 或 vCPU 不存在
 */
kernel_status_t vgic_inject_irq(uint32_t vm_id, uint32_t vcpu_id,
                                uint32_t irq);

/**
 * @brief 清除虚拟中断
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param irq     中断号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t vgic_clear_irq(uint32_t vm_id, uint32_t vcpu_id,
                               uint32_t irq);

/**
 * @brief 设置中断优先级
 *
 * @param vm_id       VM ID
 * @param irq         中断号
 * @param priority    优先级 (0~7, 0 最高)
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t vgic_set_priority(uint32_t vm_id, uint32_t irq,
                                  uint8_t priority);

/**
 * @brief 设置中断路由
 *
 * @param vm_id       VM ID
 * @param irq         中断号
 * @param cpu_mask    CPU 位图 (bit 0 = CPU0, bit 1 = CPU1, ...)
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t vgic_set_target(uint32_t vm_id, uint32_t irq,
                                uint8_t cpu_mask);

/**
 * @brief 使能/禁用中断
 *
 * @param vm_id   VM ID
 * @param irq     中断号
 * @param enable  true=使能, false=禁用
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t vgic_enable_irq(uint32_t vm_id, uint32_t irq,
                                bool enable);

/**
 * @brief 检查中断是否挂起
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param irq     中断号
 *
 * @return true=挂起, false=未挂起
 */
bool vgic_irq_is_pending(uint32_t vm_id, uint32_t vcpu_id,
                         uint32_t irq);

/**
 * @brief 获取中断状态
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param irq     中断号
 *
 * @return 中断状态，失败返回 VGIC_IRQ_INACTIVE
 */
vgic_irq_state_t vgic_get_irq_state(uint32_t vm_id, uint32_t vcpu_id,
                                     uint32_t irq);

/**
 * @brief 清空所有中断
 *
 * @param vm_id   VM ID
 */
void vgic_clear_all_irqs(uint32_t vm_id);

#endif /* SERVICES_VMM_VGIC_VGIC_H */
