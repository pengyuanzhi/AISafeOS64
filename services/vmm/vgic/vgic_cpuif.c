/**
 * @file    vgic_cpuif.c
 * @brief   GIC CPU Interface 模拟实现
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details GIC CPU Interface 寄存器模拟：
 *          - GICC_* 寄存器访问
 *          - 中断优先级屏蔽（PMR）
 *          - 中断二进制点（BPR）
 *          - 中断确认（IAR）
 *          - 中断结束（EOIR）
 *          - 运行优先级（RPR）
 *          - 最高优先级待处理中断（HPPIR）
 *
 * @note MISRA-C:2012 合规
 * @note 参考 ARM GICv2 Architecture Specification
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "vgic_cpuif.h"
#include "../vmm.h"
#include "../vm.h"
#include "vgic.h"
#include <kernel/errno.h>
#include <string.h>

/* ========================================================================
 * GIC CPU Interface 寄存器结构
 * ======================================================================== */

/**
 * @brief GIC CPU Interface 寄存器状态（每个 vCPU 一个）
 */
typedef struct
{
    /* GICC_CTLR (0x0000) - CPU Interface Control Register (RW) */
    uint32_t ctlr;

    /* GICC_PMR (0x0004) - Interrupt Priority Mask Register (RW) */
    uint8_t pmr;

    /* GICC_BPR (0x0008) - Binary Point Register (RW) */
    uint8_t bpr;

    /* GICC_RPR (0x0014) - Running Priority Register (RO) */
    uint8_t rpr;

    /* 当前活跃中断 */
    uint32_t active_irq;

    /* 当前优先级 */
    uint8_t active_prio;

    /* 最高优先级待处理中断 */
    uint32_t highest_irq;

    /* 最高优先级 */
    uint8_t highest_prio;
} vgic_cpuif_regs_t;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 获取 GIC CPU Interface 寄存器状态
 *
 * @param vm      VM 描述符
 * @param vcpu_id vCPU ID
 *
 * @return GIC CPU Interface 寄存器状态指针，失败返回 NULL
 */
static vgic_cpuif_regs_t *vgic_cpuif_get_regs(vm_desc_t *vm, uint32_t vcpu_id)
{
    if (vm == NULL || vcpu_id >= vm->vcpu_count)
    {
        return NULL;
    }

    /* 从 vCPU 描述符中获取 GIC CPU Interface 寄存器 */
    return &vm->vcpus[vcpu_id].vgic_cpuif;
}

/**
 * @brief 检查寄存器偏移是否有效
 *
 * @param offset  寄存器偏移
 * @param size    访问大小
 *
 * @return true=有效, false=无效
 */
static bool is_reg_valid(uint32_t offset, uint32_t size)
{
    /* 检查是否在有效范围内 */
    if (offset > 0x1000U)
    {
        return false;
    }

    /* 检查访问大小是否有效 */
    if (size != 1U && size != 2U && size != 4U)
    {
        return false;
    }

    /* 检查是否对齐 */
    if ((offset % size) != 0U)
    {
        return false;
    }

    return true;
}

/**
 * @brief 查找最高优先级待处理中断
 *
 * @param vm      VM 描述符
 * @param vcpu    vCPU 描述符
 * @param regs    CPU Interface 寄存器
 * @param irq     输出中断号
 * @param prio    输出中断优先级
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOENT 无待处理中断
 */
static kernel_status_t find_highest_priority_irq(vm_desc_t *vm,
                                                vcpu_desc_t *vcpu,
                                                vgic_cpuif_regs_t *regs,
                                                uint32_t *irq,
                                                uint8_t *prio)
{
    vgic_desc_t *vgic;
    uint32_t i;
    uint32_t best_irq;
    uint8_t best_prio;
    bool found;

    if (vm == NULL || vcpu == NULL || regs == NULL || irq == NULL || prio == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 VGIC 描述符 */
    vgic = (vgic_desc_t *)&vm->vgic;
    if (vgic == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 遍历所有中断，找到最高优先级的待处理中断 */
    best_irq = 0U;
    best_prio = 0xFFU;
    found = false;

    for (i = 0U; i < 256U; i++)
    {
        /* 检查中断是否使能 */
        if (!vgic_irq_is_enabled(vm->vm_id, vcpu->vcpu_id, i))
        {
            continue;
        }

        /* 检查中断是否待处理 */
        if (!vgic_irq_is_pending(vm->vm_id, vcpu->vcpu_id, i))
        {
            continue;
        }

        /* 检查中断是否被 PMR 屏蔽 */
        if (vgic->irq_priority[i] >= regs->pmr)
        {
            continue;
        }

        /* 找到更高优先级的中断 */
        if (vgic->irq_priority[i] < best_prio)
        {
            best_irq = i;
            best_prio = vgic->irq_priority[i];
            found = true;
        }
    }

    /* 如果没有找到待处理中断，返回 1023 */
    if (!found)
    {
        *irq = 1023U;
        *prio = 0xFFU;
        return -(int32_t)ENOENT;
    }

    *irq = best_irq;
    *prio = best_prio;
    return KERNEL_OK;
}

/* ========================================================================
 * GIC CPU Interface 寄存器访问实现
 * ======================================================================== */

/**
 * @brief 读取 GICC_CTLR (CPU Interface Control Register)
 */
static kernel_status_t read_gicc_ctlr(vgic_cpuif_regs_t *regs, uint32_t *value)
{
    if (regs == NULL || value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    *value = regs->ctlr;
    return KERNEL_OK;
}

/**
 * @brief 写入 GICC_CTLR (CPU Interface Control Register)
 */
static kernel_status_t write_gicc_ctlr(vgic_cpuif_regs_t *regs, uint32_t value)
{
    if (regs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    regs->ctlr = value;
    return KERNEL_OK;
}

/**
 * @brief 读取 GICC_PMR (Interrupt Priority Mask Register)
 */
static kernel_status_t read_gicc_pmr(vgic_cpuif_regs_t *regs, uint32_t *value)
{
    if (regs == NULL || value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    *value = (uint32_t)regs->pmr;
    return KERNEL_OK;
}

/**
 * @brief 写入 GICC_PMR (Interrupt Priority Mask Register)
 */
static kernel_status_t write_gicc_pmr(vgic_cpuif_regs_t *regs, uint32_t value)
{
    if (regs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* PMR 只有 8 位 */
    regs->pmr = (uint8_t)value;
    return KERNEL_OK;
}

/**
 * @brief 读取 GICC_BPR (Binary Point Register)
 */
static kernel_status_t read_gicc_bpr(vgic_cpuif_regs_t *regs, uint32_t *value)
{
    if (regs == NULL || value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    *value = (uint32_t)regs->bpr;
    return KERNEL_OK;
}

/**
 * @brief 写入 GICC_BPR (Binary Point Register)
 */
static kernel_status_t write_gicc_bpr(vgic_cpuif_regs_t *regs, uint32_t value)
{
    if (regs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* BPR 只有 8 位，范围 0-7 */
    if (value > 7U)
    {
        return -(int32_t)EINVAL;
    }

    regs->bpr = (uint8_t)value;
    return KERNEL_OK;
}

/**
 * @brief 读取 GICC_HPPIR (Highest Priority Pending Interrupt Register)
 */
static kernel_status_t read_gicc_hppir(vm_desc_t *vm,
                                       vcpu_desc_t *vcpu,
                                       vgic_cpuif_regs_t *regs,
                                       uint32_t *value)
{
    uint32_t irq;
    uint8_t prio;
    kernel_status_t ret;

    if (vm == NULL || vcpu == NULL || regs == NULL || value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找最高优先级待处理中断 */
    ret = find_highest_priority_irq(vm, vcpu, regs, &irq, &prio);
    if (ret != KERNEL_OK)
    {
        /* 没有待处理中断，返回 1023 */
        *value = (1023U << 0);
        return KERNEL_OK;
    }

    /* 返回中断号（CPUID = 0） */
    *value = (0U << 10) | (irq << 0);

    return KERNEL_OK;
}

/**
 * @brief 读取 GICC_IAR (Interrupt Acknowledge Register)
 */
static kernel_status_t read_gicc_iar(vm_desc_t *vm,
                                   vcpu_desc_t *vcpu,
                                   vgic_cpuif_regs_t *regs,
                                   uint32_t *value)
{
    uint32_t irq;
    uint8_t prio;
    kernel_status_t ret;

    if (vm == NULL || vcpu == NULL || regs == NULL || value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找最高优先级待处理中断 */
    ret = find_highest_priority_irq(vm, vcpu, regs, &irq, &prio);
    if (ret != KERNEL_OK)
    {
        /* 没有待处理中断，返回 1023 */
        *value = (1023U << 0);
        return KERNEL_OK;
    }

    /* 更新活跃中断和优先级 */
    regs->active_irq = irq;
    regs->active_prio = prio;
    regs->rpr = prio;

    /* 返回中断号（CPUID = 0） */
    *value = (0U << 10) | (irq << 0);

    /* 清除待处理标志（TODO：需要更新 VGIC 状态） */

    return KERNEL_OK;
}

/**
 * @brief 写入 GICC_EOIR (End of Interrupt Register)
 */
static kernel_status_t write_gicc_eoir(uint32_t vm_id,
                                     uint32_t vcpu_id,
                                     vgic_cpuif_regs_t *regs,
                                     uint32_t value)
{
    uint32_t irq;

    if (regs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 提取中断号 */
    irq = value & GICC_EOIR_INTID_MASK;

    /* 忽略无效中断号 */
    if (irq >= 1020U)
    {
        return KERNEL_OK;
    }

    /* 清除活跃中断 */
    regs->active_irq = 0U;
    regs->active_prio = 0xFFU;
    regs->rpr = 0xFFU;

    /* 清除 VGIC 状态（TODO：需要更新 VGIC 状态） */
    (void)vgic_clear_irq(vm_id, vcpu_id, irq);

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API 实现
 * ======================================================================== */

/**
 * @brief 读取 GIC CPU Interface 寄存器
 */
kernel_status_t vgic_cpuif_read(uint32_t vm_id, uint32_t vcpu_id,
                                  uint32_t offset, uint32_t size,
                                  uint32_t *value)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    vgic_cpuif_regs_t *regs;

    /* 参数检查 */
    if (value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查寄存器是否有效 */
    if (!is_reg_valid(offset, size))
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 VM 描述符 */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 检查 vCPU 是否存在 */
    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)ENOENT;
    }

    /* 获取 vCPU 描述符 */
    vcpu = &vm->vcpus[vcpu_id];

    /* 获取 GIC CPU Interface 寄存器 */
    regs = vgic_cpuif_get_regs(vm, vcpu_id);
    if (regs == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 根据寄存器偏移分发 */
    if (offset == GICC_CTLR_OFFSET)
    {
        return read_gicc_ctlr(regs, value);
    }
    else if (offset == GICC_PMR_OFFSET)
    {
        return read_gicc_pmr(regs, value);
    }
    else if (offset == GICC_BPR_OFFSET)
    {
        return read_gicc_bpr(regs, value);
    }
    else if (offset == GICC_IAR_OFFSET)
    {
        return read_gicc_iar(vm, vcpu, regs, value);
    }
    else if (offset == GICC_RPR_OFFSET)
    {
        *value = (uint32_t)regs->rpr;
        return KERNEL_OK;
    }
    else if (offset == GICC_HPPIR_OFFSET)
    {
        return read_gicc_hppir(vm, vcpu, regs, value);
    }
    else
    {
        return -(int32_t)EINVAL;
    }
}

/**
 * @brief 写入 GIC CPU Interface 寄存器
 */
kernel_status_t vgic_cpuif_write(uint32_t vm_id, uint32_t vcpu_id,
                                   uint32_t offset, uint32_t size,
                                   uint32_t value)
{
    vm_desc_t *vm;
    vgic_cpuif_regs_t *regs;

    /* 检查寄存器是否有效 */
    if (!is_reg_valid(offset, size))
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 VM 描述符 */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 检查 vCPU 是否存在 */
    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)ENOENT;
    }

    /* 获取 GIC CPU Interface 寄存器 */
    regs = vgic_cpuif_get_regs(vm, vcpu_id);
    if (regs == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 根据寄存器偏移分发 */
    if (offset == GICC_CTLR_OFFSET)
    {
        return write_gicc_ctlr(regs, value);
    }
    else if (offset == GICC_PMR_OFFSET)
    {
        return write_gicc_pmr(regs, value);
    }
    else if (offset == GICC_BPR_OFFSET)
    {
        return write_gicc_bpr(regs, value);
    }
    else if (offset == GICC_EOIR_OFFSET)
    {
        return write_gicc_eoir(vm_id, vcpu_id, regs, value);
    }
    else
    {
        return -(int32_t)EINVAL;
    }
}

/**
 * @brief 获取最高优先级待处理中断
 */
kernel_status_t vgic_get_highest_priority_irq(uint32_t vm_id,
                                              uint32_t vcpu_id,
                                              uint32_t *irq)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    vgic_cpuif_regs_t *regs;
    uint8_t prio;
    kernel_status_t ret;

    /* 参数检查 */
    if (irq == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 VM 描述符 */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 检查 vCPU 是否存在 */
    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)ENOENT;
    }

    /* 获取 vCPU 描述符 */
    vcpu = &vm->vcpus[vcpu_id];

    /* 获取 GIC CPU Interface 寄存器 */
    regs = vgic_cpuif_get_regs(vm, vcpu_id);
    if (regs == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 查找最高优先级待处理中断 */
    ret = find_highest_priority_irq(vm, vcpu, regs, irq, &prio);
    if (ret == KERNEL_OK)
    {
        regs->highest_irq = *irq;
        regs->highest_prio = prio;
    }

    return ret;
}

/**
 * @brief 中断确认（ACK）
 */
kernel_status_t vgic_ack_irq(uint32_t vm_id, uint32_t vcpu_id,
                             uint32_t irq)
{
    vm_desc_t *vm;
    vgic_cpuif_regs_t *regs;

    /* 获取 VM 描述符 */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 检查 vCPU 是否存在 */
    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)ENOENT;
    }

    /* 获取 GIC CPU Interface 寄存器 */
    regs = vgic_cpuif_get_regs(vm, vcpu_id);
    if (regs == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 设置活跃中断（TODO：需要获取中断优先级） */
    regs->active_irq = irq;
    regs->active_prio = 0U;
    regs->rpr = 0U;

    /* 清除 VGIC 待处理标志（TODO：需要更新 VGIC 状态） */

    return KERNEL_OK;
}

/**
 * @brief 中断结束（EOI）
 */
kernel_status_t vgic_end_irq(uint32_t vm_id, uint32_t vcpu_id,
                             uint32_t irq)
{
    vm_desc_t *vm;
    vgic_cpuif_regs_t *regs;

    /* 获取 VM 描述符 */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 检查 vCPU 是否存在 */
    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)ENOENT;
    }

    /* 获取 GIC CPU Interface 寄存器 */
    regs = vgic_cpuif_get_regs(vm, vcpu_id);
    if (regs == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 清除活跃中断 */
    regs->active_irq = 0U;
    regs->active_prio = 0xFFU;
    regs->rpr = 0xFFU;

    /* 清除 VGIC 状态 */
    (void)vgic_clear_irq(vm_id, vcpu_id, irq);

    return KERNEL_OK;
}
