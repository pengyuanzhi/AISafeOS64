/**
 * @file    exit.c
 * @brief   VM 退出处理实现
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 本文件实现了 VM 退出处理：
 *          - WFI/WFE 退出处理
 *          - Hypercall 退出处理
 *          - MMIO 退出处理
 *          - 系统寄存器退出处理
 *          - 指令中止退出处理
 *          - VM 退出分发器
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */
#include "exit.h"
#include <stdint.h>
#include <string.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include "vmm.h"
#include "vmm_stats.h"

/* ========================================================================
 * VM 退出原因定义
 * ======================================================================== */

/** @brief WFI/WFE 退出原因 (EC=0x01) */
#define EXIT_REASON_WFI_WFE    (0x01ULL)

/** @brief HVC 退出原因 (EC=0x08) */
#define EXIT_REASON_HVC        (0x08ULL)

/** @brief 数据中止退出原因 (EC=0x0A, 0x24) */
#define EXIT_REASON_DATA_ABORT (0x0AULL)

/** @brief 指令中止退出原因 (EC=0x0E) */
#define EXIT_REASON_INST_ABORT (0x0EULL)

/** @brief 系统寄存器退出原因 (EC=0x06) */
#define EXIT_REASON_SYSREG     (0x06ULL)

/* ========================================================================
 * MMIO 访问宽度定义
 * ======================================================================== */

#define MMIO_ACCESS_MAX_SIZE   (8U)

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 处理 WFI/WFE 退出
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t exit_wfi_wfe(uint32_t vm_id, uint32_t vcpu_id)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;

    /* 获取 VM 和 vCPU */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)EINVAL;
    }

    vcpu = &vm->vcpu_id[vcpu_id];

    /* 检查是否有待注入中断 */
    if (vcpu->irq_pending)
    {
        /* 注入中断 */
        vcpu->irq_pending = false;
        return KERNEL_OK;
    }

    /* 没有中断，进入低功耗状态 */
    vcpu->state = VCPU_STATE_BLOCKED;

    /* 更新统计信息 */
    vmm_stats_update_exit(EXIT_REASON_WFI_WFE);

    return KERNEL_OK;
}

/**
 * @brief 处理 HVC 退出（Hypercall）
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t exit_hypercall(uint32_t vm_id, uint32_t vcpu_id)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint64_t call_nr;
    uint64_t args[HYPERCALL_MAX_ARGS];

    /* 获取 VM 和 vCPU */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)EINVAL;
    }

    vcpu = &vm->vcpus[vcpu_id];

    /* 读取 Hypercall 号 */
    call_nr = vcpu->gp_regs.x[0];

    /* 读取参数（简化：仅读取前 4 个参数） */
    args[0] = vcpu->gp_regs.x[1];
    args[1] = vcpu->gp_regs.x[2];
    args[2] = vcpu->gp_regs.x[3];
    args[3] = vcpu->gp_regs.x[4];

    /* 处理 Hypercall */
    return vmm_handle_hypercall(vm_id, vcpu_id, call_nr, args);
}

/**
 * @brief 处理数据中止退出（MMIO）
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t exit_data_abort(uint32_t vm_id, uint32_t vcpu_id)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint64_t esr;
    uint64_t far;
    bool is_write;
    uint64_t offset;
    uint64_t value;
    uint32_t size;
    kernel_status_t ret;

    /* 获取 VM 和 vCPU */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)EINVAL;
    }

    vcpu = &vm->vcpus[vcpu_id];

    /* 读取 ESR_EL2 和 FAR_EL2 */
    esr = vcpu->sys_regs.esr_el2;
    far = vcpu->sys_regs.far_el2;

    /* 提取信息 */
    is_write = ((esr & 0x40ULL) != 0ULL);  /* 位 6 = 写操作 */
    size = 1U << ((esr >> 22ULL) & 0x3ULL);  /* 访问宽度 */

    /* 检查是否为 MMIO 访问 */
    if ((esr & 0x3FULL) != EXIT_REASON_DATA_ABORT)
    {
        return KERNEL_OK;
    }

    /* 更新统计信息 */
    vmm_stats_update_exit(EXIT_REASON_DATA_ABORT);

    /* 处理 MMIO 访问 */
    ret = vmm_handle_mmio(vm_id, vcpu_id, far, is_write, &value, size);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    /* 如果是读操作，将返回值写入寄存器 */
    if (!is_write)
    {
        uint64_t reg_idx = (esr >> 16ULL) & 0x1FULL;
        vcpu->gp_regs.x[reg_idx] = value;
    }

    /* 恢复 PC（自增加 4 字节） */
    vcpu->gp_regs.pc += 4ULL;

    return KERNEL_OK;
}

/**
 * @brief 处理指令中止退出
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t exit_inst_abort(uint32_t vm_id, uint32_t vcpu_id)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;

    /* 获取 VM 和 vCPU */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)EINVAL;
    }

    vcpu = &vm->vcpus[vcpu_id];

    /* 更新统计信息 */
    vmm_stats_update_exit(EXIT_REASON_INST_ABORT);

    /* 简化实现：不处理，直接返回 */
    return KERNEL_OK;
}

/**
 * @brief 处理系统寄存器退出
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t exit_sysreg(uint32_t vm_id, uint32_t vcpu_id)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;

    /* 获取 VM 和 vCPU */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)EINVAL;
    }

    vcpu = &vm->vcpu_id[vcpu_id];

    /* 更新统计信息 */
    vmm_stats_update_exit(EXIT_REASON_SYSREG);

    /* 简化实现：不处理，直接返回 */
    return KERNEL_OK;
}

/* ========================================================================
 * 内部 API - VM 退出处理
 * ======================================================================== */

kernel_status_t exit_handler(uint32_t vm_id, uint32_t vcpu_id)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint64_t esr;
    uint64_t ec;
    kernel_status_t ret;

    /* 获取 VM 和 vCPU */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)EINVAL;
    }

    vcpu = &vm->vcpus[vcpu_id];

    /* 读取 ESR_EL2 */
    esr = vcpu->sys_regs.esr_el2;

    /* 提取 EC（异常类） */
    ec = (esr >> 26ULL) & 0x3FULL;

    /* 根据 EC 分发到对应的处理函数 */
    switch (ec)
    {
        case EXIT_REASON_WFI_WFE:
            ret = exit_wfi_wfe(vm_id, vcpu_id);
            break;

        case EXIT_REASON_HVC:
            ret = exit_hypercall(vm_id, vcpu_id);
            break;

        case EXIT_REASON_SYSREG:
            ret = exit_sysreg(vm_id, vcpu_id);
            break;

        case EXIT_REASON_INST_ABORT:
            ret = exit_inst_abort(vm_id, vcpu_id);
            break;

        case EXIT_REASON_DATA_ABORT:
        case 0x24ULL:  /* 数据中止（64 位） */
            ret = exit_data_abort(vm_id, vcpu_id);
            break;

        default:
            ret = KERNEL_OK;  /* 未知退出，忽略 */
            break;
    }

    /* 更新统计信息 */
    if (ret == KERNEL_OK)
    {
        vcpu->exit_count++;
    }

    return ret;
}

/* ========================================================================
 * 公共 API - VM 退出处理
 * ======================================================================== */

kernel_status_t vmm_handle_exit(uint32_t vm_id, uint32_t vcpu_id)
{
    return exit_handler(vm_id, vcpu_id);
}
