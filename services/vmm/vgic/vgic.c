/**
 * @file    vgic.c
 * @brief   虚拟 GIC（VGIC）实现
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 本文件实现了虚拟 GIC（VGIC）的所有功能：
 *          - VGIC 初始化/销毁
 *          - 中断注入/清除
 *          - 优先级设置
 *          - 中断路由
 *          - 使能/禁用
 *          - 状态检查
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */
#include "vgic.h"
#include <stdint.h>
#include <string.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include "vmm.h"
#include "vm.h"

/* ========================================================================
 * 内部状态
 * ======================================================================== */

/** @brief VGIC 描述符表 */
static vgic_desc_t s_vgic[VMM_MAX_VMS];

/** @brief VGIC 初始化标志 */
static bool s_vgic_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 查找 VGIC 描述符
 *
 * @param vm_id   VM ID
 *
 * @return VGIC 描述符指针，不存在返回 NULL
 */
static vgic_desc_t *vgic_find(uint32_t vm_id)
{
    if (vm_id >= VMM_MAX_VMS)
    {
        return NULL;
    }

    return &s_vgic[vm_id];
}

/**
 * @brief 检查 VM 是否存在
 *
 * @param vm_id   VM ID
 *
 * @return true=存在, false=不存在
 */
static bool vgic_vm_exists(uint32_t vm_id)
{
    vm_desc_t *vm;

    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return false;
    }

    return true;
}

/**
 * @brief 检查 vCPU 是否存在
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 *
 * @return true=存在, false=不存在
 */
static bool vgic_vcpu_exists(uint32_t vm_id, uint32_t vcpu_id)
{
    vm_desc_t *vm;

    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return false;
    }

    if (vcpu_id >= vm->vcpu_count)
    {
        return false;
    }

    return true;
}

/**
 * @brief 设置中断状态
 *
 * @param vgic     VGIC 描述符指针
 * @param irq      中断号
 * @param state    中断状态
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
static kernel_status_t vgic_set_irq_state(vgic_desc_t *vgic,
                                           uint32_t irq,
                                           vgic_irq_state_t state)
{
    if (vgic == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return -(int32_t)EINVAL;
    }

    if (state >= VGIC_IRQ_ACTIVE_PENDING)
    {
        return -(int32_t)EINVAL;
    }

    vgic->irq_state[irq] = state;

    return KERNEL_OK;
}

/**
 * @brief 更新中断挂起位图
 *
 * @param vgic     VGIC 描述符指针
 * @param irq      中断号
 * @param pending  是否挂起
 */
static void vgic_update_pending_map(vgic_desc_t *vgic,
                                      uint32_t irq,
                                      bool pending)
{
    uint32_t idx;
    uint32_t bit;
    uint32_t mask;

    if (vgic == NULL || irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return;
    }

    /* 计算索引和位偏移 */
    idx = irq / 32U;
    bit = irq % 32U;

    /* 设置/清除位 */
    mask = 1U << bit;
    if (pending)
    {
        vgic->irq_pending[idx] |= mask;
    }
    else
    {
        vgic->irq_pending[idx] &= ~mask;
    }
}

/**
 * @brief 检查中断是否使能
 *
 * @param vgic     VGIC 描述符指针
 * @param irq      中断号
 *
 * @return true=使能, false=禁用
 */
static bool vgic_irq_is_enabled(vgic_desc_t *vgic, uint32_t irq)
{
    uint32_t idx;
    uint32_t bit;
    uint32_t mask;

    if (vgic == NULL || irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return false;
    }

    /* 计算索引和位偏移 */
    idx = irq / 32U;
    bit = irq % 32U;
    mask = 1U << bit;

    return ((vgic->irq_enabled[idx] & mask) != 0U);
}

/* ========================================================================
 * 公共 API - VGIC 初始化/销毁
 * ======================================================================== */

kernel_status_t vgic_init(uint32_t vm_id)
{
    vgic_desc_t *vgic;

    if (!s_vgic_initialized)
    {
        return -(int32_t)EPERM;
    }

    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    vgic = &s_vgic[vm_id];

    /* 初始化 VGIC 描述符 */
    (void)memset(vgic, 0, sizeof(vgic_desc_t));

    /* 设置默认状态 */
    for (uint32_t i = 0U; i < VMM_VGIC_MAX_INTERRUPTS; i++)
    {
        vgic->irq_state[i] = VGIC_IRQ_INACTIVE;
        vgic->irq_priority[i] = 7U;  /* 默认最低优先级 */
        vgic->irq_config[i] = 0U;    /* 默认电平触发 */
        vgic->irq_target[i] = 0x1U;  /* 默认 CPU 0 */
    }

    return KERNEL_OK;
}

kernel_status_t vgic_destroy(uint32_t vm_id)
{
    vgic_desc_t *vgic;

    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    vgic = &s_vgic[vm_id];

    /* 清空 VGIC 描述符 */
    (void)memset(vgic, 0, sizeof(vgic_desc_t));

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 中断注入/清除
 * ======================================================================== */

kernel_status_t vgic_inject_irq(uint32_t vm_id, uint32_t vcpu_id,
                                 uint32_t irq)
{
    vgic_desc_t *vgic;
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;

    /* 检查 VM 和 vCPU 是否存在 */
    if (!vgic_vm_exists(vm_id))
    {
        return -(int32_t)ENOENT;
    }

    if (!vgic_vcpu_exists(vm_id, vcpu_id))
    {
        return -(int32_t)ENOENT;
    }

    /* 检查中断号是否有效 */
    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 VGIC 描述符 */
    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 检查中断是否使能 */
    if (!vgic_irq_is_enabled(vgic, irq))
    {
        return -(int32_t)EPERM;
    }

    /* 获取 vCPU */
    vm = vmm_get_vm(vm_id);
    vcpu = &vm->vcpus[vcpu_id];

    /* 设置中断状态为 PENDING */
    kernel_status_t ret = vgic_set_irq_state(vgic, irq, VGIC_IRQ_PENDING);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    /* 更新挂起位图 */
    vgic_update_pending_map(vgic, irq, true);

    /* 标记 vCPU 有待注入中断 */
    vcpu->irq_pending = true;

    /* 如果 vCPU 处于 BLOCKED 状态，唤醒它 */
    if (vcpu->state == VCPU_STATE_BLOCKED)
    {
        vcpu->state = VCPU_STATE_RUNNING;
    }

    return KERNEL_OK;
}

kernel_status_t vgic_clear_irq(uint32_t vm_id, uint32_t vcpu_id,
                                 uint32_t irq)
{
    vgic_desc_t *vgic;

    /* 检查 VM 和 vCPU 是否存在 */
    if (!vgic_vm_exists(vm_id))
    {
        return -(int32_t)ENOENT;
    }

    if (!vgic_vcpu_exists(vm_id, vcpu_id))
    {
        return -(int32_t)ENOENT;
    }

    /* 检查中断号是否有效 */
    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 VGIC 描述符 */
    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 设置中断状态为 INACTIVE */
    kernel_status_t ret = vgic_set_irq_state(vgic, irq, VGIC_IRQ_INACTIVE);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    /* 清除挂起位图 */
    vgic_update_pending_map(vgic, irq, false);

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 优先级/路由
 * ======================================================================== */

kernel_status_t vgic_set_priority(uint32_t vm_id, uint32_t irq,
                                   uint8_t priority)
{
    vgic_desc_t *vgic;

    /* 检查 VM 是否存在 */
    if (!vgic_vm_exists(vm_id))
    {
        return -(int32_t)ENOENT;
    }

    /* 检查中断号是否有效 */
    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查优先级是否有效 (0~7) */
    if (priority > 7U)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 VGIC 描述符 */
    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 设置中断优先级 */
    vgic->irq_priority[irq] = priority;

    return KERNEL_OK;
}

kernel_status_t vgic_set_target(uint32_t vm_id, uint32_t irq,
                                  uint8_t cpu_mask)
{
    vgic_desc_t *vgic;

    /* 检查 VM 是否存在 */
    if (!vgic_vm_exists(vm_id))
    {
        return -(int32_t)ENOENT;
    }

    /* 检查中断号是否有效 */
    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 VGIC 描述符 */
    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 设置中断路由 */
    vgic->irq_target[irq] = cpu_mask;

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 使能/禁用
 * ======================================================================== */

kernel_status_t vgic_enable_irq(uint32_t vm_id, uint32_t irq,
                                 bool enable)
{
    vgic_desc_t *vgic;
    uint32_t idx;
    uint32_t bit;
    uint32_t mask;

    /* 检查 VM 是否存在 */
    if (!vgic_vm_exists(vm_id))
    {
        return -(int32_t)ENOENT;
    }

    /* 检查中断号是否有效 */
    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 VGIC 描述符 */
    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 计算索引和位偏移 */
    idx = irq / 32U;
    bit = irq % 32U;
    mask = 1U << bit;

    /* 设置/清除使能位 */
    if (enable)
    {
        vgic->irq_enabled[idx] |= mask;
    }
    else
    {
        vgic->irq_enabled[idx] &= ~mask;
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 状态检查
 * ======================================================================== */

bool vgic_irq_is_pending(uint32_t vm_id, uint32_t vcpu_id,
                         uint32_t irq)
{
    vgic_desc_t *vgic;
    uint32_t idx;
    uint32_t bit;
    uint32_t mask;

    /* 检查 VM 和 vCPU 是否存在 */
    if (!vgic_vm_exists(vm_id))
    {
        return false;
    }

    if (!vgic_vcpu_exists(vm_id, vcpu_id))
    {
        return false;
    }

    /* 检查中断号是否有效 */
    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return false;
    }

    /* 获取 VGIC 描述符 */
    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return false;
    }

    /* 计算索引和位偏移 */
    idx = irq / 32U;
    bit = irq % 32U;
    mask = 1U << bit;

    /* 检查是否挂起 */
    return ((vgic->irq_pending[idx] & mask) != 0U);
}

vgic_irq_state_t vgic_get_irq_state(uint32_t vm_id, uint32_t vcpu_id,
                                     uint32_t irq)
{
    vgic_desc_t *vgic;

    /* 检查 VM 和 vCPU 是否存在 */
    if (!vgic_vm_exists(vm_id))
    {
        return VGIC_IRQ_INACTIVE;
    }

    if (!vgic_vcpu_exists(vm_id, vcpu_id))
    {
        return VGIC_IRQ_INACTIVE;
    }

    /* 检查中断号是否有效 */
    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return VGIC_IRQ_INACTIVE;
    }

    /* 获取 VGIC 描述符 */
    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return VGIC_IRQ_INACTIVE;
    }

    /* 返回中断状态 */
    return vgic->irq_state[irq];
}

/* ========================================================================
 * 公共 API - 清空所有中断
 * ======================================================================== */

void vgic_clear_all_irqs(uint32_t vm_id)
{
    vgic_desc_t *vgic;

    /* 检查 VM 是否存在 */
    if (!vgic_vm_exists(vm_id))
    {
        return;
    }

    /* 获取 VGIC 描述符 */
    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return;
    }

    /* 清空所有中断 */
    (void)memset(vgic->irq_state, 0, sizeof(vgic->irq_state));
    (void)memset(vgic->irq_pending, 0, sizeof(vgic->irq_pending));
}

/* ========================================================================
 * 全局初始化
 * ======================================================================== */

void vgic_global_init(void)
{
    /* 初始化 VGIC 描述符表 */
    (void)memset(s_vgic, 0, sizeof(s_vgic));

    /* 标记为已初始化 */
    s_vgic_initialized = true;
}
