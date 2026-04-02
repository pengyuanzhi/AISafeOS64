/**
 * @file    vmm.c
 * @brief   虚拟机管理器（VMM）实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details ARMv8-A 虚拟机管理器实现：
 *          - VM/vCPU 生命周期管理
 *          - 嵌套页表管理（简化实现）
 *          - 虚拟中断注入
 *          - VM 退出处理框架
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~005
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "vmm.h"
#include <kernel/errno.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * VMM 全局状态
 * ======================================================================== */

/** @brief 虚拟机描述符池 */
static vm_desc_t s_vms[VMM_MAX_VMS];

/** @brief VM 使用标记 */
static bool s_vm_used[VMM_MAX_VMS];

/** @brief VMM 初始化标志 */
static bool s_initialized = false;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 安全字符串复制
 */
static void vmm_strcpy(char *dst, const char *src, uint32_t n)
{
    uint32_t i;

    if ((dst == NULL) || (src == NULL) || (n == 0U))
    {
        return;
    }

    for (i = 0U; (i < (n - 1U)) && (src[i] != '\0'); i++)
    {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* ========================================================================
 * 初始化
 * ======================================================================== */

kernel_status_t vmm_init(void)
{
    uint32_t i;

    (void)memset(s_vms, 0, sizeof(s_vms));
    (void)memset(s_vm_used, 0, sizeof(s_vm_used));

    for (i = 0U; i < VMM_MAX_VMS; i++)
    {
        s_vms[i].vm_id = i;
        s_vms[i].state = VM_STATE_NONE;
        s_vms[i].vcpu_count = 0U;
    }

    s_initialized = true;

    return KERNEL_OK;
}

/* ========================================================================
 * 创建虚拟机
 * ======================================================================== */

int32_t vmm_create_vm(const char *name, uint64_t mem_size)
{
    uint32_t i;
    vm_desc_t *vm;

    if (!s_initialized)
    {
        return -(int32_t)EPERM;
    }

    if (name == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (mem_size == 0ULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找空闲 VM 槽 */
    for (i = 0U; i < VMM_MAX_VMS; i++)
    {
        if (!s_vm_used[i])
        {
            break;
        }
    }

    if (i >= VMM_MAX_VMS)
    {
        return -(int32_t)ENOMEM;
    }

    vm = &s_vms[i];

    (void)memset(vm, 0, sizeof(vm_desc_t));
    vm->vm_id = i;
    vm->state = VM_STATE_CREATED;
    vm->vcpu_count = 0U;
    vm->mem_size = mem_size;
    vm->mem_base = 0ULL; /* 实际分配由内存管理器完成 */

    vmm_strcpy(vm->name, name, 32U);

    /* 初始化 VGIC */
    (void)memset(&vm->vgic, 0, sizeof(vgic_desc_t));

    /* 初始化嵌套页表 */
    vm->npt.guest_phys_base = 0ULL;
    vm->npt.guest_phys_size = mem_size;
    vm->npt.root_paddr = 0ULL;
    vm->npt.root_vaddr = 0ULL;
    vm->npt.ref_count = 0U;

    s_vm_used[i] = true;

    return (int32_t)i;
}

/* ========================================================================
 * 销毁虚拟机
 * ======================================================================== */

kernel_status_t vmm_destroy_vm(uint32_t vm_id)
{
    vm_desc_t *vm;

    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_vm_used[vm_id])
    {
        return -(int32_t)ENOENT;
    }

    vm = &s_vms[vm_id];

    /* 检查 VM 状态 */
    if (vm->state == VM_STATE_RUNNING)
    {
        return -(int32_t)EBUSY;
    }

    /* 清理 vCPU */
    (void)memset(vm->vcpus, 0, sizeof(vm->vcpus));
    vm->vcpu_count = 0U;

    /* 清理 VGIC */
    (void)memset(&vm->vgic, 0, sizeof(vgic_desc_t));

    /* 释放 NPT */
    vm->npt.root_paddr = 0ULL;
    vm->npt.root_vaddr = 0ULL;

    vm->state = VM_STATE_NONE;
    s_vm_used[vm_id] = false;

    return KERNEL_OK;
}

/* ========================================================================
 * 创建 vCPU
 * ======================================================================== */

int32_t vmm_create_vcpu(uint32_t vm_id, paddr_t entry_point)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint32_t vcpu_id;

    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_vm_used[vm_id])
    {
        return -(int32_t)ENOENT;
    }

    vm = &s_vms[vm_id];

    if (vm->vcpu_count >= VMM_MAX_VCPUS_PER_VM)
    {
        return -(int32_t)ENOMEM;
    }

    vcpu_id = vm->vcpu_count;
    vcpu = &vm->vcpus[vcpu_id];

    (void)memset(vcpu, 0, sizeof(vcpu_desc_t));

    vcpu->vcpu_id = vcpu_id;
    vcpu->vm_id = vm_id;
    vcpu->state = VCPU_STATE_STOPPED;
    vcpu->entry_point = entry_point;
    vcpu->pending_irq = 0ULL;
    vcpu->irq_pending = false;

    /* 初始化通用寄存器 */
    vcpu->gp_regs.pc = (uint64_t)entry_point;
    vcpu->gp_regs.pstate = 0x3C5ULL; /* EL1h, IRQ/FIQ masked */

    /* 初始化系统寄存器 */
    vcpu->sys_regs.sctlr_el1 = 0ULL; /* MMU 初始关闭 */

    vm->vcpu_count++;

    return (int32_t)vcpu_id;
}

/* ========================================================================
 * 运行 vCPU
 * ======================================================================== */

kernel_status_t vmm_vcpu_run(uint32_t vm_id, uint32_t vcpu_id)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;

    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    vm = &s_vms[vm_id];

    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)EINVAL;
    }

    vcpu = &vm->vcpus[vcpu_id];

    if (vcpu->state == VCPU_STATE_RUNNING)
    {
        return -(int32_t)EBUSY;
    }

    /* 更新 VM 和 vCPU 状态 */
    vm->state = VM_STATE_RUNNING;
    vcpu->state = VCPU_STATE_RUNNING;

    /*
     * 实际实现中：
     * 1. 保存 Host 上下文
     * 2. 恢复 Guest vCPU 上下文
     * 3. 通过 HVC 指令进入 EL2
     * 4. 在 EL2 配置 VTTBR_EL2（嵌套页表）
     * 5. ERET 进入 Guest
     *
     * 此处为简化实现框架
     */

    return KERNEL_OK;
}

/* ========================================================================
 * 注入虚拟中断
 * ======================================================================== */

kernel_status_t vmm_inject_irq(uint32_t vm_id, uint32_t vcpu_id, uint32_t irq)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;

    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    vm = &s_vms[vm_id];

    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)EINVAL;
    }

    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return -(int32_t)EINVAL;
    }

    vcpu = &vm->vcpus[vcpu_id];

    /* 设置中断为 pending 状态 */
    vm->vgic.irq_state[irq] = VGIC_IRQ_PENDING;
    vm->vgic.irq_enabled[irq / 32U] |= (1UL << (irq % 32U));

    /* 标记 vCPU 有待处理中断 */
    vcpu->pending_irq |= (1ULL << irq);
    vcpu->irq_pending = true;

    return KERNEL_OK;
}

/* ========================================================================
 * 处理 VM 退出
 * ======================================================================== */

kernel_status_t vmm_handle_exit(uint32_t vm_id, uint32_t vcpu_id)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint64_t esr;
    uint32_t ec;

    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    vm = &s_vms[vm_id];

    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)EINVAL;
    }

    vcpu = &vm->vcpus[vcpu_id];
    vcpu->state = VCPU_STATE_BLOCKED;

    /* 读取 ESR_EL2 获取退出原因 */
    esr = vcpu->sys_regs.esr_el1;
    ec = (uint32_t)((esr >> 26U) & 0x3FU);

    /*
     * 根据异常类型分发处理：
     * EC = 0x01: WFI/WFE（等待中断/事件）
     * EC = 0x06: MRS/MSR（系统寄存器访问）
     * EC = 0x08: HVC（超级调用）
     * EC = 0x0A: 数据中止（页表错误）
     * EC = 0x0E: 指令中止
     * EC = 0x24: 数据中止（EL1）
     */
    switch (ec)
    {
        case 0x01U: /* WFI/WFE */
            /* Guest 等待中断，检查是否有待注入中断 */
            if (vcpu->irq_pending)
            {
                vcpu->irq_pending = false;
            }
            break;

        case 0x08U: /* HVC - 超级调用 */
            /* 处理 Guest 的 hypercall */
            break;

        case 0x0AU: /* 数据中止 */
        case 0x24U: /* 数据中止（EL1） */
            /* 处理嵌套页表错误 */
            break;

        case 0x06U: /* 系统寄存器陷阱 */
            /* 模拟系统寄存器访问 */
            break;

        default:
            /* 未知异常 */
            break;
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 获取 VM 描述符
 * ======================================================================== */

vm_desc_t *vmm_get_vm(uint32_t vm_id)
{
    if (vm_id >= VMM_MAX_VMS)
    {
        return NULL;
    }

    if (!s_vm_used[vm_id])
    {
        return NULL;
    }

    return &s_vms[vm_id];
}

/* ========================================================================
 * 映射 Guest 物理页
 * ======================================================================== */

kernel_status_t vmm_map_guest_page(uint32_t vm_id, paddr_t guest_paddr,
                                    paddr_t host_paddr, uint64_t flags)
{
    vm_desc_t *vm;

    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_vm_used[vm_id])
    {
        return -(int32_t)ENOENT;
    }

    vm = &s_vms[vm_id];

    /* 检查 guest_paddr 在范围内 */
    if (guest_paddr >= vm->npt.guest_phys_size)
    {
        return -(int32_t)EINVAL;
    }

    /*
     * 实际实现中：
     * 1. 在 NPT 中查找/创建页表项
     * 2. 填写 host_paddr 和属性
     * 3. 刷新 TLB（VMALLIS）
     *
     * 此处为简化实现
     */
    (void)vm;
    (void)host_paddr;
    (void)flags;

    return KERNEL_OK;
}
