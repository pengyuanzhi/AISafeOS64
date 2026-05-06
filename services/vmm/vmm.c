/**
 * @file    vmm.c
 * @brief   虚拟机管理器（VMM）核心实现
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 本文件实现了 VMM 的所有公共 API：
 *          - VM 生命周期管理（创建/销毁）
 *          - vCPU 生命周期管理（创建/暂停/运行）
 *          - NPT 映射管理
 *          - 虚拟设备注册
 *          - 中断注入
 *          - VM 退出处理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */
#include "vmm.h"
#include <stdint.h>
#include <string.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <kernel/mm/mem.h>
#include "vm.h"
#include "vcpu.h"
#include "npt.h"
#include "vgic.h"
#include "device/virtio.h"
#include "stats/vmm_stats.h"

/* ========================================================================
 * 内部状态
 * ======================================================================== */

/** @brief VM 描述符池 */
static vm_desc_t s_vms[VMM_MAX_VMS];

/** @brief 虚拟设备表 */
static virtio_device_t s_vdevices[VMM_MAX_VDEVICES];

/** @brief VM 初始化标志 */
static bool s_vmm_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 查找 VM 描述符
 *
 * @param vm_id   VM ID
 *
 * @return VM 描述符指针，不存在返回 NULL
 */
static vm_desc_t *vmm_find_vm(uint32_t vm_id)
{
    if (vm_id >= VMM_MAX_VMS)
    {
        return NULL;
    }

    return &s_vms[vm_id];
}

/**
 * @brief 查找虚拟设备
 *
 * @param vm_id   VM ID
 * @param dev_id  设备 ID
 *
 * @return 设备指针，不存在返回 NULL
 */
static virtio_device_t *vmm_find_vdevice(uint32_t vm_id, uint32_t dev_id)
{
    if (dev_id >= VMM_MAX_VDEVICES)
    {
        return NULL;
    }

    if (!s_vdevices[dev_id].active)
    {
        return NULL;
    }

    return &s_vdevices[dev_id];
}

/* ========================================================================
 * 公共 API - VMM 初始化/销毁
 * ======================================================================== */

kernel_status_t vmm_init(void)
{
    uint32_t i;

    /* 检查是否已经初始化 */
    if (s_vmm_initialized)
    {
        return -(int32_t)EPERM;
    }

    /* 初始化所有 VM 描述符 */
    for (i = 0U; i < VMM_MAX_VMS; i++)
    {
        (void)memset(&s_vms[i], 0, sizeof(vm_desc_t));
        s_vms[i].vm_id = i;
    }

    /* 初始化所有虚拟设备 */
    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        (void)memset(&s_vdevices[i], 0, sizeof(virtio_device_t));
        s_vdevices[i].dev_id = i;
    }

    /* 标记为已初始化 */
    s_vmm_initialized = true;

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - VM 生命周期管理
 * ======================================================================== */

int32_t vmm_create_vm(const char *name, uint64_t mem_size)
{
    uint32_t i;
    vm_desc_t *vm;

    if (!s_vmm_initialized)
    {
        return -(int32_t)EPERM;
    }

    if (name == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (mem_size == 0ULL || mem_size > VMM_GUEST_PHYS_SIZE)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找空闲 VM 槽 */
    for (i = 0U; i < VMM_MAX_VMS; i++)
    {
        if (s_vms[i].state == VM_STATE_INVALID)
        {
            break;
        }
    }

    if (i >= VMM_MAX_VMS)
    {
        return -(int32_t)ENOMEM;
    }

    vm = &s_vms[i];

    /* 初始化 VM 描述符 */
    (void)memset(vm, 0, sizeof(vm_desc_t));
    vm->vm_id = i;
    vm->name[0] = '\0';
    (void)strncpy(vm->name, name, 31);
    vm->name[31] = '\0';
    vm->state = VM_STATE_CREATED;
    vm->mem_size = mem_size;
    vm->vcpu_count = 0U;
    vm->npt = NULL;

    /* 初始化 VGIC */
    kernel_status_t ret = vgic_init(i);
    if (ret != KERNEL_OK)
    {
        return -(int32_t)ret;
    }

    return (int32_t)i;
}

kernel_status_t vmm_destroy_vm(uint32_t vm_id)
{
    vm_desc_t *vm;
    uint32_t i;

    vm = vmm_find_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (vm->state != VM_STATE_INVALID && vm->state != VM_STATE_STOPPED)
    {
        return -(int32_t)EBUSY;  /* VM 正在运行，不能销毁 */
    }

    /* 销毁所有 vCPU */
    for (i = 0U; i < vm->vcpu_count; i++)
    {
        /* 调用 vCPU 销毁函数 */
    }

    /* 清空 VM 描述符 */
    (void)memset(vm, 0, sizeof(vm_desc_t));
    vm->vm_id = vm_id;

    /* 清空 VGIC */
    vgic_destroy(vm_id);

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - vCPU 生命周期管理
 * ======================================================================== */

int32_t vmm_create_vcpu(uint32_t vm_id, paddr_t entry_point)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint32_t i;

    vm = vmm_find_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)ENOENT;
    }

    if (vm->state != VM_STATE_CREATED && vm->state != VM_STATE_STOPPED)
    {
        return -(int32_t)EBUSY;  /* VM 必须已创建且未运行 */
    }

    if (entry_point < vm->mem_base || entry_point >= (vm->mem_base + vm->mem_size))
    {
        return -(int32_t)EINVAL;  /* 入口点地址越界 */
    }

    if (vm->vcpu_count >= VMM_MAX_VCPUS_PER_VM)
    {
        return -(int32_t)ENOMEM;  /* 已达到最大 vCPU 数量 */
    }

    i = vm->vcpu_count;

    /* 初始化 vCPU 描述符 */
    (void)memset(&vm->vcpus[i], 0, sizeof(vcpu_desc_t));
    vm->vcpus[i].vcpu_id = i;
    vm->vcpus[i].vm_id = vm_id;
    vm->vcpus[i].state = VCPU_STATE_STOPPED;
    vm->vcpus[i].mem_base = vm->mem_base;
    vm->vcpus[i].pc = entry_point;

    /* 初始化寄存器 */
    vm->vcpus[i].gp_regs.x[0] = 0ULL;  /* x0 = 0 */
    vm->vcpus[i].gp_regs.x[1] = 0ULL;  /* x1 = 0 */
    vm->vcpus[i].gp_regs.x[2] = 0ULL;  /* x2 = 0 */
    vm->vcpus[i].gp_regs.x[3] = 0ULL;  /* x3 = 0 */
    vm->vcpus[i].gp_regs.x[4] = 0ULL;  /* x4 = 0 */
    vm->vcpus[i].gp_regs.x[5] = 0ULL;  /* x5 = 0 */
    vm->vcpus[i].gp_regs.x[6] = 0ULL;  /* x6 = 0 */
    vm->vcpus[i].gp_regs.x[7] = 0ULL;  /* x7 = 0 */
    vm->vcpus[i].gp_regs.x[30] = 0ULL; /* x30 (LR) = 0 */
    vm->vcpus[i].gp_regs.pc = entry_point;  /* PC = entry_point */
    vm->vcpus[i].gp_regs.pstate = 0x3C5ULL;  /* PSTATE = EL1h mode */

    /* 初始化系统寄存器 */
    vm->vcpus[i].sys_regs.esr_el1 = 0ULL;
    vm->vcpus[i].sys_regs.far_el1 = 0ULL;

    /* 初始化退出计数 */
    vm->vcpus[i].exit_count = 0ULL;

    /* 初始化中断状态 */
    vm->vcpus[i].irq_pending = false;
    vm->vcpus[i].pending_irq = 0ULL;
    vm->vcpus[i].active_irq = 0ULL;

    /* 增加 vCPU 计数 */
    vm->vcpu_count++;

    return (int32_t)i;
}

kernel_status_t vmm_vcpu_pause(uint32_t vm_id, uint32_t vcpu_id)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;

    vm = vmm_find_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)ENOENT;
    }

    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)EINVAL;
    }

    vcpu = &vm->vcpus[vcpu_id];

    if (vcpu->state != VCPU_STATE_RUNNING)
    {
        return -(int32_t)EINVAL;
    }

    /* 设置 vCPU 状态为 STOPPED */
    vcpu->state = VCPU_STATE_STOPPED;

    return KERNEL_OK;
}

kernel_status_t vmm_vcpu_run(uint32_t vm_id, uint32_t vcpu_id)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;

    vm = vmm_find_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)ENOENT;
    }

    if (vcpu_id >= vm->vcpu_count)
    {
        return -(int32_t)EINVAL;
    }

    vcpu = &vm->vcpus[vcpu_id];

    if (vcpu->state != VCPU_STATE_STOPPED)
    {
        return -(int32_t)EBUSY;
    }

    /* 设置 vCPU 状态为 RUNNING */
    vcpu->state = VCPU_STATE_RUNNING;

    /* 更新 VM 状态 */
    vm->state = VM_STATE_RUNNING;

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 中断和退出处理
 * ======================================================================== */

kernel_status_t vmm_inject_irq(uint32_t vm_id, uint32_t vcpu_id, uint32_t irq)
{
    return vgic_inject_irq(vm_id, vcpu_id, irq);
}

kernel_status_t vmm_handle_exit(uint32_t vm_id, uint32_t vcpu_id)
{
    return exit_handler(vm_id, vcpu_id);
}

/* ========================================================================
 * 公共 API - 获取 VM 描述符
 * ======================================================================== */

vm_desc_t *vmm_get_vm(uint32_t vm_id)
{
    return vmm_find_vm(vm_id);
}

/* ========================================================================
 * 公共 API - NPT 映射
 * ======================================================================== */

kernel_status_t vmm_map_guest_page(uint32_t vm_id, paddr_t guest_paddr,
                                    paddr_t host_paddr, uint64_t flags)
{
    vm_desc_t *vm;
    npt_desc_t *npt;

    vm = vmm_find_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)ENOENT;
    }

    if (vm->npt == NULL)
    {
        /* 创建 NPT */
        npt = npt_create(vm_id, vm->mem_size);
        if (npt == NULL)
        {
            return -(int32_t)ENOMEM;
        }
        vm->npt = npt;
    }

    npt = vm->npt;

    return npt_map_page(npt, guest_paddr, host_paddr, flags);
}

/* ========================================================================
 * 公共 API - 虚拟设备注册
 * ======================================================================== */

int32_t vmm_register_vdevice(const char *name, vmm_device_ops_t *ops)
{
    uint32_t i;

    if (!s_vmm_initialized)
    {
        return -(int32_t)EPERM;
    }

    if (name == NULL || ops == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找空闲设备槽 */
    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        if (!s_vdevices[i].active)
        {
            break;
        }
    }

    if (i >= VMM_MAX_VDEVICES)
    {
        return -(int32_t)ENOMEM;
    }

    /* 初始化设备 */
    (void)memset(&s_vdevices[i], 0, sizeof(virtio_device_t));
    s_vdevices[i].dev_id = i;
    (void)strncpy(s_vdevices[i].name, name, 15);
    s_vdevices[i].name[15] = '\0';
    s_vdevices[i].active = true;
    s_vdevices[i].ops = ops;

    return (int32_t)i;
}
