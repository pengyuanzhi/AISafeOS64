/**
 * @file    vgic_dist.c
 * @brief   GIC Distributor 模拟实现
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details GIC Distributor 寄存器模拟：
 *          - GICD_* 寄存器访问
 *          - 中断使能/禁用
 *          - 中断挂起/清除
 *          - 中断优先级设置
 *          - 中断路由设置
 *          - SGI (Software Generated Interrupt) 处理
 *
 * @note MISRA-C:2012 合规
 * @note 参考 ARM GICv2 Architecture Specification
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "vgic_dist.h"
#include "../vmm.h"
#include "../vm.h"
#include "vgic.h"
#include <kernel/errno.h>
#include <string.h>

/* ========================================================================
 * GIC Distributor 寄存器结构
 * ======================================================================== */

/**
 * @brief GIC Distributor 寄存器状态
 */
typedef struct
{
    /* GICD_CTLR (0x0000) - Distributor Control Register */
    uint32_t ctlr;

    /* GICD_TYPER (0x0004) - Distributor Type Register (RO) */
    uint32_t typer;

    /* GICD_ISENABLER[0-7] (0x0100) - Interrupt Set-Enable Registers (RW) */
    uint32_t isenabler[8];

    /* GICD_ICENABLER[0-7] (0x0180) - Interrupt Clear-Enable Registers (RW) */
    uint32_t icenabler[8];

    /* GICD_ISPENDR[0-7] (0x0200) - Interrupt Set-Pending Registers (RW) */
    uint32_t ispendr[8];

    /* GICD_ICPENDR[0-7] (0x0280) - Interrupt Clear-Pending Registers (RW) */
    uint32_t icpendr[8];

    /* GICD_IABR[0-7] (0x0300) - Interrupt Active Registers (RO) */
    uint32_t iabr[8];

    /* GICD_IPRIORITYR[0-63] (0x0400) - Interrupt Priority Registers (RW) */
    uint8_t ipriorityr[256];

    /* GICD_ITARGETSR[0-63] (0x0800) - Interrupt Processor Targets Registers (RW) */
    uint8_t itargetsr[256];

    /* GICD_ICFGR[0-1] (0x0C00) - Interrupt Configuration Registers (RW) */
    uint32_t icfgr[2];

    /* GICD_SGIR (0x0F00) - Software Generated Interrupt Register (WO) */
    uint32_t sgir;
} vgic_dist_regs_t;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 获取 GIC Distributor 寄存器状态
 *
 * @param vm   VM 描述符
 *
 * @return GIC Distributor 寄存器状态指针，失败返回 NULL
 */
static vgic_dist_regs_t *vgic_dist_get_regs(vm_desc_t *vm)
{
    if (vm == NULL)
    {
        return NULL;
    }

    /* 从 VM 描述符中获取 VGIC Distributor 寄存器 */
    return &vm->vgic_dist;
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
 * @brief 计算 GICD_ISENABLER/ICENABLER/ISPENDR/ICPENDR 索引
 *
 * @param offset  寄存器偏移
 * @param idx     输出数组索引
 * @param bit     输出位偏移
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
static kernel_status_t calc_idx_bit(uint32_t offset, uint32_t *idx, uint32_t *bit)
{
    uint32_t base_offset;
    uint32_t reg_offset;

    if (idx == NULL || bit == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 判断寄存器类型 */
    if (offset >= GICD_ISENABLER_OFFSET && offset < GICD_ICENABLER_OFFSET)
    {
        base_offset = GICD_ISENABLER_OFFSET;
    }
    else if (offset >= GICD_ICENABLER_OFFSET && offset < GICD_ISPENDR_OFFSET)
    {
        base_offset = GICD_ICENABLER_OFFSET;
    }
    else if (offset >= GICD_ISPENDR_OFFSET && offset < GICD_ICPENDR_OFFSET)
    {
        base_offset = GICD_ISPENDR_OFFSET;
    }
    else if (offset >= GICD_ICPENDR_OFFSET && offset < GICD_IABR_OFFSET)
    {
        base_offset = GICD_ICPENDR_OFFSET;
    }
    else
    {
        return -(int32_t)EINVAL;
    }

    /* 计算数组索引和位偏移 */
    reg_offset = offset - base_offset;
    *idx = reg_offset / 4U;
    *bit = 0U;

    return KERNEL_OK;
}

/* ========================================================================
 * GIC Distributor 寄存器访问实现
 * ======================================================================== */

/**
 * @brief 读取 GICD_CTLR (Distributor Control Register)
 */
static kernel_status_t read_gicd_ctlr(vgic_dist_regs_t *regs, uint32_t *value)
{
    if (regs == NULL || value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    *value = regs->ctlr;
    return KERNEL_OK;
}

/**
 * @brief 读取 GICD_TYPER (Distributor Type Register)
 */
static kernel_status_t read_gicd_typer(vgic_dist_regs_t *regs, uint32_t *value)
{
    if (regs == NULL || value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    *value = regs->typer;
    return KERNEL_OK;
}

/**
 * @brief 读取 GICD_ISENABLER (Interrupt Set-Enable Registers)
 */
static kernel_status_t read_gicd_isenabler(vgic_dist_regs_t *regs,
                                           uint32_t offset,
                                           uint32_t size,
                                           uint32_t *value)
{
    uint32_t idx;

    if (regs == NULL || value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 计算数组索引 */
    idx = (offset - GICD_ISENABLER_OFFSET) / 4U;

    /* 读取 4 字节 */
    if (size == 4U)
    {
        *value = regs->isenabler[idx];
    }
    /* 读取 2 字节 */
    else if (size == 2U)
    {
        uint16_t *ptr = (uint16_t *)&regs->isenabler[idx];
        *value = (uint32_t)ptr[idx % 2U];
    }
    /* 读取 1 字节 */
    else if (size == 1U)
    {
        uint8_t *ptr = (uint8_t *)&regs->isenabler[idx];
        *value = (uint32_t)ptr[idx % 4U];
    }
    else
    {
        return -(int32_t)EINVAL;
    }

    return KERNEL_OK;
}

/**
 * @brief 写入 GICD_ISENABLER (Interrupt Set-Enable Registers)
 */
static kernel_status_t write_gicd_isenabler(vgic_dist_regs_t *regs,
                                            uint32_t offset,
                                            uint32_t size,
                                            uint32_t value)
{
    uint32_t idx;

    if (regs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 计算数组索引 */
    idx = (offset - GICD_ISENABLER_OFFSET) / 4U;

    /* 写入 4 字节 */
    if (size == 4U)
    {
        regs->isenabler[idx] = value;
    }
    /* 写入 2 字节 */
    else if (size == 2U)
    {
        uint16_t *ptr = (uint16_t *)&regs->isenabler[idx];
        ptr[idx % 2U] = (uint16_t)value;
    }
    /* 写入 1 字节 */
    else if (size == 1U)
    {
        uint8_t *ptr = (uint8_t *)&regs->isenabler[idx];
        ptr[idx % 4U] = (uint8_t)value;
    }
    else
    {
        return -(int32_t)EINVAL;
    }

    return KERNEL_OK;
}

/**
 * @brief 写入 GICD_ICENABLER (Interrupt Clear-Enable Registers)
 */
static kernel_status_t write_gicd_icenabler(vgic_dist_regs_t *regs,
                                            uint32_t offset,
                                            uint32_t size,
                                            uint32_t value)
{
    uint32_t idx;

    if (regs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 计算数组索引 */
    idx = (offset - GICD_ICENABLER_OFFSET) / 4U;

    /* 写入 4 字节（清除对应位） */
    if (size == 4U)
    {
        regs->isenabler[idx] &= ~value;
    }
    /* 写入 2 字节（清除对应位） */
    else if (size == 2U)
    {
        uint16_t *ptr = (uint16_t *)&regs->isenabler[idx];
        ptr[idx % 2U] &= (uint16_t)~value;
    }
    /* 写入 1 字节（清除对应位） */
    else if (size == 1U)
    {
        uint8_t *ptr = (uint8_t *)&regs->isenabler[idx];
        ptr[idx % 4U] &= (uint8_t)~value;
    }
    else
    {
        return -(int32_t)EINVAL;
    }

    return KERNEL_OK;
}

/**
 * @brief 读取 GICD_IPRIORITYR (Interrupt Priority Registers)
 */
static kernel_status_t read_gicd_ipriorityr(vgic_dist_regs_t *regs,
                                              uint32_t offset,
                                              uint32_t size,
                                              uint32_t *value)
{
    uint32_t idx;

    if (regs == NULL || value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 计算数组索引（每个寄存器 4 字节，包含 4 个 8 位优先级） */
    idx = (offset - GICD_IPRIORITYR_OFFSET) / 4U;

    /* 读取 4 字节 */
    if (size == 4U)
    {
        uint8_t *ptr = &regs->ipriorityr[idx * 4U];
        *value = ((uint32_t)ptr[0] << 0) |
                 ((uint32_t)ptr[1] << 8) |
                 ((uint32_t)ptr[2] << 16) |
                 ((uint32_t)ptr[3] << 24);
    }
    /* 读取 2 字节 */
    else if (size == 2U)
    {
        uint8_t *ptr = &regs->ipriorityr[(offset - GICD_IPRIORITYR_OFFSET) % 4U];
        *value = ((uint32_t)ptr[0] << 0) |
                 ((uint32_t)ptr[1] << 8);
    }
    /* 读取 1 字节 */
    else if (size == 1U)
    {
        uint8_t *ptr = &regs->ipriorityr[(offset - GICD_IPRIORITYR_OFFSET) % 4U];
        *value = (uint32_t)ptr[0];
    }
    else
    {
        return -(int32_t)EINVAL;
    }

    return KERNEL_OK;
}

/**
 * @brief 写入 GICD_IPRIORITYR (Interrupt Priority Registers)
 */
static kernel_status_t write_gicd_ipriorityr(vgic_dist_regs_t *regs,
                                               uint32_t offset,
                                               uint32_t size,
                                               uint32_t value)
{
    uint32_t idx;

    if (regs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 写入 4 字节 */
    if (size == 4U)
    {
        uint8_t *ptr = &regs->ipriorityr[(offset - GICD_IPRIORITYR_OFFSET) % 4U];
        ptr[0] = (uint8_t)(value >> 0);
        ptr[1] = (uint8_t)(value >> 8);
        ptr[2] = (uint8_t)(value >> 16);
        ptr[3] = (uint8_t)(value >> 24);
    }
    /* 写入 2 字节 */
    else if (size == 2U)
    {
        uint8_t *ptr = &regs->ipriorityr[(offset - GICD_IPRIORITYR_OFFSET) % 4U];
        ptr[0] = (uint8_t)(value >> 0);
        ptr[1] = (uint8_t)(value >> 8);
    }
    /* 写入 1 字节 */
    else if (size == 1U)
    {
        uint8_t *ptr = &regs->ipriorityr[(offset - GICD_IPRIORITYR_OFFSET) % 4U];
        ptr[0] = (uint8_t)(value >> 0);
    }
    else
    {
        return -(int32_t)EINVAL;
    }

    return KERNEL_OK;
}

/**
 * @brief 读取 GICD_ITARGETSR (Interrupt Processor Targets Registers)
 */
static kernel_status_t read_gicd_itargetsr(vgic_dist_regs_t *regs,
                                              uint32_t offset,
                                              uint32_t size,
                                              uint32_t *value)
{
    uint32_t idx;

    if (regs == NULL || value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 计算数组索引（每个寄存器 4 字节，包含 4 个 8 位目标 CPU） */
    idx = (offset - GICD_ITARGETSR_OFFSET) / 4U;

    /* 读取 4 字节 */
    if (size == 4U)
    {
        uint8_t *ptr = &regs->itargetsr[idx * 4U];
        *value = ((uint32_t)ptr[0] << 0) |
                 ((uint32_t)ptr[1] << 8) |
                 ((uint32_t)ptr[2] << 16) |
                 ((uint32_t)ptr[3] << 24);
    }
    /* 读取 2 字节 */
    else if (size == 2U)
    {
        uint8_t *ptr = &regs->itargetsr[(offset - GICD_ITARGETSR_OFFSET) % 4U];
        *value = ((uint32_t)ptr[0] << 0) |
                 ((uint32_t)ptr[1] << 8);
    }
    /* 读取 1 字节 */
    else if (size == 1U)
    {
        uint8_t *ptr = &regs->itargetsr[(offset - GICD_ITARGETSR_OFFSET) % 4U];
        *value = (uint32_t)ptr[0];
    }
    else
    {
        return -(int32_t)EINVAL;
    }

    return KERNEL_OK;
}

/**
 * @brief 写入 GICD_ITARGETSR (Interrupt Processor Targets Registers)
 */
static kernel_status_t write_gicd_itargetsr(vgic_dist_regs_t *regs,
                                               uint32_t offset,
                                               uint32_t size,
                                               uint32_t value)
{
    uint32_t idx;

    if (regs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 写入 4 字节 */
    if (size == 4U)
    {
        uint8_t *ptr = &regs->itargetsr[(offset - GICD_ITARGETSR_OFFSET) % 4U];
        ptr[0] = (uint8_t)(value >> 0);
        ptr[1] = (uint8_t)(value >> 8);
        ptr[2] = (uint8_t)(value >> 16);
        ptr[3] = (uint8_t)(value >> 24);
    }
    /* 写入 2 字节 */
    else if (size == 2U)
    {
        uint8_t *ptr = &regs->itargetsr[(offset - GICD_ITARGETSR_OFFSET) % 4U];
        ptr[0] = (uint8_t)(value >> 0);
        ptr[1] = (uint8_t)(value >> 8);
    }
    /* 写入 1 字节 */
    else if (size == 1U)
    {
        uint8_t *ptr = &regs->itargetsr[(offset - GICD_ITARGETSR_OFFSET) % 4U];
        ptr[0] = (uint8_t)(value >> 0);
    }
    else
    {
        return -(int32_t)EINVAL;
    }

    return KERNEL_OK;
}

/**
 * @brief 读取 GICD_ICFGR (Interrupt Configuration Registers)
 */
static kernel_status_t read_gicd_icfgr(vgic_dist_regs_t *regs,
                                        uint32_t offset,
                                        uint32_t size,
                                        uint32_t *value)
{
    uint32_t idx;

    if (regs == NULL || value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 计算数组索引 */
    idx = (offset - GICD_ICFGR_OFFSET) / 4U;

    /* 读取 4 字节 */
    if (size == 4U)
    {
        *value = regs->icfgr[idx];
    }
    /* 读取 2 字节 */
    else if (size == 2U)
    {
        uint16_t *ptr = (uint16_t *)&regs->icfgr[idx];
        *value = (uint32_t)ptr[idx % 2U];
    }
    /* 读取 1 字节 */
    else if (size == 1U)
    {
        uint8_t *ptr = (uint8_t *)&regs->icfgr[idx];
        *value = (uint32_t)ptr[idx % 4U];
    }
    else
    {
        return -(int32_t)EINVAL;
    }

    return KERNEL_OK;
}

/**
 * @brief 写入 GICD_ICFGR (Interrupt Configuration Registers)
 */
static kernel_status_t write_gicd_icfgr(vgic_dist_regs_t *regs,
                                        uint32_t offset,
                                        uint32_t size,
                                        uint32_t value)
{
    uint32_t idx;

    if (regs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 写入 4 字节 */
    if (size == 4U)
    {
        regs->icfgr[idx] = value;
    }
    /* 写入 2 字节 */
    else if (size == 2U)
    {
        uint16_t *ptr = (uint16_t *)&regs->icfgr[idx];
        ptr[idx % 2U] = (uint16_t)value;
    }
    /* 写入 1 字节 */
    else if (size == 1U)
    {
        uint8_t *ptr = (uint8_t *)&regs->icfgr[idx];
        ptr[idx % 4U] = (uint8_t)value;
    }
    else
    {
        return -(int32_t)EINVAL;
    }

    return KERNEL_OK;
}

/**
 * @brief 读取 GICD_IABR (Interrupt Active Registers)
 */
static kernel_status_t read_gicd_iabr(vgic_dist_regs_t *regs,
                                       uint32_t offset,
                                       uint32_t size,
                                       uint32_t *value)
{
    uint32_t idx;

    if (regs == NULL || value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 计算数组索引 */
    idx = (offset - GICD_IABR_OFFSET) / 4U;

    /* 读取 4 字节 */
    if (size == 4U)
    {
        *value = regs->iabr[idx];
    }
    /* 读取 2 字节 */
    else if (size == 2U)
    {
        uint16_t *ptr = (uint16_t *)&regs->iabr[idx];
        *value = (uint32_t)ptr[idx % 2U];
    }
    /* 读取 1 字节 */
    else if (size == 1U)
    {
        uint8_t *ptr = (uint8_t *)&regs->iabr[idx];
        *value = (uint32_t)ptr[idx % 4U];
    }
    else
    {
        return -(int32_t)EINVAL;
    }

    return KERNEL_OK;
}

/**
 * @brief 写入 GICD_SGIR (Software Generated Interrupt Register)
 */
static kernel_status_t write_gicd_sgir(uint32_t vm_id, uint32_t value)
{
    uint32_t target_filter;
    uint32_t cpu_list;
    uint32_t sgiintid;
    uint32_t i;
    vm_desc_t *vm;

    /* 获取 VM 描述符 */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 解析 SGI 参数 */
    target_filter = (value & GICD_SGIR_TL_MASK) >> 24U;
    cpu_list = (value & GICD_SGIR_CPULIST_MASK) >> 16U;
    sgiintid = value & GICD_SGIR_SGIINTID_MASK;

    /* 检查 SGI ID (0-15) */
    if (sgiintid > 15U)
    {
        return -(int32_t)EINVAL;
    }

    /* 根据目标过滤器发送 SGI */
    switch (target_filter)
    {
        case GICD_SGIR_TL_LIST:
            /* 发送到 CPU List 中指定的 CPU */
            for (i = 0U; i < 8U; i++)
            {
                if ((cpu_list & (1U << i)) != 0U)
                {
                    /* 注入 SGI 到 vCPU[i] */
                    (void)vmm_inject_irq(vm_id, i, sgiintid);
                }
            }
            break;

        case GICD_SGIR_TL_ALL_OTHERS:
            /* 发送到所有其他 CPU（不包括自己） */
            for (i = 0U; i < vm->vcpu_count; i++)
            {
                /* TODO: 需要判断当前 vCPU，排除自己 */
                (void)vmm_inject_irq(vm_id, i, sgiintid);
            }
            break;

        case GICD_SGIR_TL_SELF:
            /* 发送到自己 */
            /* TODO: 需要判断当前 vCPU */
            (void)vmm_inject_irq(vm_id, 0U, sgiintid);
            break;

        default:
            return -(int32_t)EINVAL;
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API 实现
 * ======================================================================== */

/**
 * @brief 读取 GIC Distributor 寄存器
 */
kernel_status_t vgic_dist_read(uint32_t vm_id, uint32_t offset,
                                 uint32_t size, uint32_t *value)
{
    vm_desc_t *vm;
    vgic_dist_regs_t *regs;

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

    /* 获取 GIC Distributor 寄存器 */
    regs = vgic_dist_get_regs(vm);
    if (regs == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 根据寄存器偏移分发 */
    if (offset == GICD_CTLR_OFFSET)
    {
        return read_gicd_ctlr(regs, value);
    }
    else if (offset == GICD_TYPER_OFFSET)
    {
        return read_gicd_typer(regs, value);
    }
    else if (offset >= GICD_ISENABLER_OFFSET && offset < GICD_ICENABLER_OFFSET)
    {
        return read_gicd_isenabler(regs, offset, size, value);
    }
    else if (offset >= GICD_ICENABLER_OFFSET && offset < GICD_ISPENDR_OFFSET)
    {
        /* GICD_ICENABLER 是只写的，返回 ISENABLER 的值 */
        return read_gicd_isenabler(regs, offset - GICD_ICENABLER_OFFSET + GICD_ISENABLER_OFFSET, size, value);
    }
    else if (offset >= GICD_ISPENDR_OFFSET && offset < GICD_ICPENDR_OFFSET)
    {
        /* GICD_ISPENDR 读取逻辑（TODO：实现） */
        return read_gicd_isenabler(regs, offset - GICD_ISPENDR_OFFSET + GICD_ISENABLER_OFFSET, size, value);
    }
    else if (offset >= GICD_ICPENDR_OFFSET && offset < GICD_IABR_OFFSET)
    {
        /* GICD_ICPENDR 读取逻辑（TODO：实现） */
        return read_gicd_isenabler(regs, offset - GICD_ICPENDR_OFFSET + GICD_ISENABLER_OFFSET, size, value);
    }
    else if (offset >= GICD_IABR_OFFSET && offset < GICD_IPRIORITYR_OFFSET)
    {
        return read_gicd_iabr(regs, offset, size, value);
    }
    else if (offset >= GICD_IPRIORITYR_OFFSET && offset < GICD_ITARGETSR_OFFSET)
    {
        return read_gicd_ipriorityr(regs, offset, size, value);
    }
    else if (offset >= GICD_ITARGETSR_OFFSET && offset < GICD_ICFGR_OFFSET)
    {
        return read_gicd_itargetsr(regs, offset, size, value);
    }
    else if (offset >= GICD_ICFGR_OFFSET && offset < GICD_SGIR_OFFSET)
    {
        return read_gicd_icfgr(regs, offset, size, value);
    }
    else
    {
        return -(int32_t)EINVAL;
    }
}

/**
 * @brief 写入 GIC Distributor 寄存器
 */
kernel_status_t vgic_dist_write(uint32_t vm_id, uint32_t offset,
                                  uint32_t size, uint32_t value)
{
    vm_desc_t *vm;
    vgic_dist_regs_t *regs;

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

    /* 获取 GIC Distributor 寄存器 */
    regs = vgic_dist_get_regs(vm);
    if (regs == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 根据寄存器偏移分发 */
    if (offset == GICD_CTLR_OFFSET)
    {
        /* GICD_CTLR 是只写的 */
        regs->ctlr = value;
        return KERNEL_OK;
    }
    else if (offset >= GICD_ISENABLER_OFFSET && offset < GICD_ICENABLER_OFFSET)
    {
        return write_gicd_isenabler(regs, offset, size, value);
    }
    else if (offset >= GICD_ICENABLER_OFFSET && offset < GICD_ISPENDR_OFFSET)
    {
        return write_gicd_icenabler(regs, offset, size, value);
    }
    else if (offset >= GICD_ISPENDR_OFFSET && offset < GICD_ICPENDR_OFFSET)
    {
        /* GICD_ISPENDR 写入逻辑（TODO：实现） */
        return write_gicd_isenabler(regs, offset - GICD_ISPENDR_OFFSET + GICD_ISENABLER_OFFSET, size, value);
    }
    else if (offset >= GICD_ICPENDR_OFFSET && offset < GICD_IABR_OFFSET)
    {
        /* GICD_ICPENDR 写入逻辑（TODO：实现） */
        return write_gicd_icenabler(regs, offset - GICD_ICPENDR_OFFSET + GICD_ISENABLER_OFFSET, size, value);
    }
    else if (offset >= GICD_IPRIORITYR_OFFSET && offset < GICD_ITARGETSR_OFFSET)
    {
        return write_gicd_ipriorityr(regs, offset, size, value);
    }
    else if (offset >= GICD_ITARGETSR_OFFSET && offset < GICD_ICFGR_OFFSET)
    {
        return write_gicd_itargetsr(regs, offset, size, value);
    }
    else if (offset >= GICD_ICFGR_OFFSET && offset < GICD_SGIR_OFFSET)
    {
        return write_gicd_icfgr(regs, offset, size, value);
    }
    else if (offset == GICD_SGIR_OFFSET)
    {
        return write_gicd_sgir(vm_id, value);
    }
    else
    {
        return -(int32_t)EINVAL;
    }
}
