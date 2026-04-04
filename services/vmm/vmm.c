/**
 * @file    vmm.c
 * @brief   虚拟机管理器（VMM）实现
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 3.0
 *
 * @details ARMv8-A 虚拟机管理器实现：
 *          - VM/vCPU 生命周期管理
 *          - 嵌套页表管理（简化实现）
 *          - 虚拟中断注入
 *          - VM 退出处理框架
 *          - Guest 物理内存映射
 *          - 虚拟设备模拟框架
 *          - VM 退出处理分发
 *          - hypercall 处理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "vmm.h"
#include <kernel/errno.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * VMM 常量扩展
 * ======================================================================== */

/** @brief 虚拟设备 MMIO 区域基址 */
#define VMM_VDEV_MMIO_BASE      0x09000000ULL

/** @brief 虚拟设备 MMIO 区域大小 */
#define VMM_VDEV_MMIO_SIZE      0x00010000ULL

/** @brief 最大虚拟设备数 */
#define VMM_MAX_VDEVICES        8U

/** @brief Hypercall 最大参数数 */
#define HYP_CALL_MAX_ARGS       4U

/** @brief MMIO 访问最大宽度（字节） */
#define MMIO_ACCESS_MAX_SIZE    8U

/* ========================================================================
 * 虚拟设备类型
 * ======================================================================== */

/**
 * @brief 虚拟设备类型
 */
typedef enum
{
    VDEV_UART = 0U,        /**< @brief 虚拟 UART */
    VDEV_BLOCK,            /**< @brief 虚拟块设备 */
    VDEV_NET,              /**< @brief 虚拟网卡 */
    VDEV_TIMER,            /**< @brief 虚拟定时器 */
    VDEV_RTC,              /**< @brief 虚拟 RTC */
    VDEV_FRAMEBUFFER,      /**< @brief 虚拟帧缓冲 */
    VDEV_GPIO,             /**< @brief 虚拟 GPIO */
    VDEV_CUSTOM            /**< @brief 自定义设备 */
} vdev_type_t;

/**
 * @brief MMIO 访问操作类型
 */
typedef enum
{
    MMIO_READ = 0U,        /**< @brief 读操作 */
    MMIO_WRITE             /**< @brief 写操作 */
} mmio_op_t;

/**
 * @brief 虚拟设备操作回调
 *
 * @param vm_id    VM ID
 * @param vcpu_id  vCPU ID
 * @param offset   MMIO 偏移
 * @param op       操作类型
 * @param value    读/写值
 * @param size     访问宽度（字节）
 *
 * @return KERNEL_OK 成功
 */
typedef kernel_status_t (*vdev_op_fn)(uint32_t vm_id, uint32_t vcpu_id,
                                       uint64_t offset, mmio_op_t op,
                                       uint64_t *value, uint32_t size);

/**
 * @brief 虚拟设备描述符
 */
typedef struct
{
    uint32_t    dev_id;           /**< @brief 设备 ID */
    vdev_type_t type;             /**< @brief 设备类型 */
    char        name[16];         /**< @brief 设备名称 */
    uint32_t    vm_id;            /**< @brief 所属 VM */
    uint64_t    mmio_base;        /**< @brief MMIO 基址 */
    uint64_t    mmio_size;        /**< @brief MMIO 大小 */
    vdev_op_fn  read_fn;          /**< @brief 读回调 */
    vdev_op_fn  write_fn;         /**< @brief 写回调 */
    bool        active;           /**< @brief 活跃标记 */
} vdev_desc_t;

/* ========================================================================
 * VMM 全局状态
 * ======================================================================== */

/** @brief 虚拟机描述符池 */
static vm_desc_t s_vms[VMM_MAX_VMS];

/** @brief VM 使用标记 */
static bool s_vm_used[VMM_MAX_VMS];

/** @brief 虚拟设备表 */
static vdev_desc_t s_vdevs[VMM_MAX_VDEVICES];

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
 * 虚拟设备模拟
 * ======================================================================== */

/**
 * @brief UART 读操作模拟
 */
static kernel_status_t vdev_uart_read(uint32_t vm_id, uint32_t vcpu_id,
                                        uint64_t offset, mmio_op_t op,
                                        uint64_t *value, uint32_t size)
{
    (void)vm_id;
    (void)vcpu_id;
    (void)op;
    (void)size;

    if (value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 简化 UART 寄存器模拟 */
    switch (offset)
    {
        case 0U:
            /* 数据寄存器 */
            *value = 0ULL;
            break;
        case 4U:
            /* 状态寄存器：TX 空 + RX 空 */
            *value = 0x60ULL;
            break;
        default:
            *value = 0ULL;
            break;
    }

    return KERNEL_OK;
}

/**
 * @brief UART 写操作模拟
 */
static kernel_status_t vdev_uart_write(uint32_t vm_id, uint32_t vcpu_id,
                                         uint64_t offset, mmio_op_t op,
                                         uint64_t *value, uint32_t size)
{
    (void)vm_id;
    (void)vcpu_id;
    (void)op;
    (void)size;

    if (value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (offset == 0U)
    {
        /* 数据寄存器：输出字符（此处为框架） */
        (void)*value;
    }

    return KERNEL_OK;
}

/**
 * @brief 定时器读操作模拟
 */
static kernel_status_t vdev_timer_read(uint32_t vm_id, uint32_t vcpu_id,
                                         uint64_t offset, mmio_op_t op,
                                         uint64_t *value, uint32_t size)
{
    (void)vm_id;
    (void)vcpu_id;
    (void)op;
    (void)size;

    if (value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    switch (offset)
    {
        case 0U:
            /* 当前计数器值 */
            *value = 0ULL;
            break;
        case 8U:
            /* 控制寄存器 */
            *value = 0x03ULL;
            break;
        default:
            *value = 0ULL;
            break;
    }

    return KERNEL_OK;
}

/**
 * @brief 注册虚拟设备
 *
 * @param vm_id    VM ID
 * @param type     设备类型
 * @param name     设备名称
 * @param mmio_base MMIO 基址
 * @param mmio_size MMIO 大小
 *
 * @return 成功返回设备 ID，负数表示错误
 */
int32_t vmm_register_vdevice(uint32_t vm_id, vdev_type_t type,
                               const char *name,
                               uint64_t mmio_base, uint64_t mmio_size)
{
    uint32_t i;
    vdev_desc_t *dev;

    if (!s_initialized)
    {
        return -(int32_t)EPERM;
    }

    if ((name == NULL) || (mmio_size == 0ULL))
    {
        return -(int32_t)EINVAL;
    }

    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        if (!s_vdevs[i].active)
        {
            break;
        }
    }

    if (i >= VMM_MAX_VDEVICES)
    {
        return -(int32_t)ENOMEM;
    }

    dev = &s_vdevs[i];
    dev->dev_id = i;
    dev->type = type;
    dev->vm_id = vm_id;
    dev->mmio_base = mmio_base;
    dev->mmio_size = mmio_size;
    vmm_strcpy(dev->name, name, 16U);

    /* 根据类型设置默认回调 */
    switch (type)
    {
        case VDEV_UART:
            dev->read_fn = vdev_uart_read;
            dev->write_fn = vdev_uart_write;
            break;
        case VDEV_TIMER:
            dev->read_fn = vdev_timer_read;
            dev->write_fn = vdev_timer_read;
            break;
        default:
            dev->read_fn = NULL;
            dev->write_fn = NULL;
            break;
    }

    dev->active = true;

    return (int32_t)i;
}

/**
 * @brief 处理 MMIO 访问
 *
 * @param vm_id     VM ID
 * @param vcpu_id   vCPU ID
 * @param fault_addr 故障地址
 * @param is_write  是否为写操作
 * @param value     读/写值
 * @param size      访问宽度
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_handle_mmio(uint32_t vm_id, uint32_t vcpu_id,
                                  uint64_t fault_addr, bool is_write,
                                  uint64_t *value, uint32_t size)
{
    uint32_t i;
    vdev_desc_t *dev;
    uint64_t offset;

    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        dev = &s_vdevs[i];
        if (!dev->active || (dev->vm_id != vm_id))
        {
            continue;
        }

        if ((fault_addr >= dev->mmio_base) &&
            (fault_addr < (dev->mmio_base + dev->mmio_size)))
        {
            offset = fault_addr - dev->mmio_base;

            if (is_write)
            {
                if (dev->write_fn != NULL)
                {
                    return dev->write_fn(vm_id, vcpu_id, offset,
                                         MMIO_WRITE, value, size);
                }
            }
            else
            {
                if (dev->read_fn != NULL)
                {
                    return dev->read_fn(vm_id, vcpu_id, offset,
                                        MMIO_READ, value, size);
                }
            }

            return KERNEL_OK;
        }
    }

    return -(int32_t)EFAULT;
}

/* ========================================================================
 * Hypercall 处理
 * ======================================================================== */

/**
 * @brief 处理 hypercall
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param call_nr 调用号
 * @param args    参数数组
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t vmm_handle_hypercall(uint32_t vm_id, uint32_t vcpu_id,
                                              uint64_t call_nr,
                                              const uint64_t args[HYP_CALL_MAX_ARGS])
{
    switch (call_nr)
    {
        case 0U:
            /* HYP_CONSOLE_PUTC - 输出字符 */
            (void)args;
            break;

        case 1U:
            /* HYP_GET_TIME - 获取时间 */
            break;

        case 2U:
            /* HYP_SCHEDULE - 主动让出 */
            {
                vm_desc_t *vm = vmm_get_vm(vm_id);
                if (vm != NULL)
                {
                    vcpu_desc_t *vcpu = &vm->vcpus[vcpu_id];
                    vcpu->state = VCPU_STATE_BLOCKED;
                }
            }
            break;

        case 3U:
            /* HYP_SHUTDOWN - 关闭 VM */
            {
                vm_desc_t *vm = vmm_get_vm(vm_id);
                if (vm != NULL)
                {
                    vm->state = VM_STATE_STOPPED;
                }
            }
            break;

        default:
            break;
    }

    (void)vcpu_id;
    return KERNEL_OK;
}

/* ========================================================================
 * 初始化
 * ======================================================================== */

kernel_status_t vmm_init(void)
{
    uint32_t i;

    (void)memset(s_vms, 0, sizeof(s_vms));
    (void)memset(s_vm_used, 0, sizeof(s_vm_used));
    (void)memset(s_vdevs, 0, sizeof(s_vdevs));

    for (i = 0U; i < VMM_MAX_VMS; i++)
    {
        s_vms[i].vm_id = i;
        s_vms[i].state = VM_STATE_NONE;
        s_vms[i].vcpu_count = 0U;
    }

    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        s_vdevs[i].dev_id = i;
        s_vdevs[i].active = false;
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
    vm->mem_base = 0ULL;

    vmm_strcpy(vm->name, name, 32U);

    (void)memset(&vm->vgic, 0, sizeof(vgic_desc_t));

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
    uint32_t i;

    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_vm_used[vm_id])
    {
        return -(int32_t)ENOENT;
    }

    vm = &s_vms[vm_id];

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

    /* 移除关联的虚拟设备 */
    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        if (s_vdevs[i].active && (s_vdevs[i].vm_id == vm_id))
        {
            s_vdevs[i].active = false;
        }
    }

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

    vcpu->gp_regs.pc = (uint64_t)entry_point;
    vcpu->gp_regs.pstate = 0x3C5ULL;

    vcpu->sys_regs.sctlr_el1 = 0ULL;

    vm->vcpu_count++;

    return (int32_t)vcpu_id;
}

/* ========================================================================
 * 暂停 vCPU
 * ======================================================================== */

kernel_status_t vmm_vcpu_pause(uint32_t vm_id, uint32_t vcpu_id)
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

    if (vcpu->state != VCPU_STATE_RUNNING)
    {
        return -(int32_t)EINVAL;
    }

    vcpu->state = VCPU_STATE_STOPPED;

    return KERNEL_OK;
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

    vm->state = VM_STATE_RUNNING;
    vcpu->state = VCPU_STATE_RUNNING;

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

    vm->vgic.irq_state[irq] = VGIC_IRQ_PENDING;
    vm->vgic.irq_enabled[irq / 32U] |= (1UL << (irq % 32U));

    vcpu->pending_irq |= (1ULL << irq);
    vcpu->irq_pending = true;

    return KERNEL_OK;
}

/* ========================================================================
 * VM 退出处理分发
 * ======================================================================== */

kernel_status_t vmm_handle_exit(uint32_t vm_id, uint32_t vcpu_id)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint64_t esr;
    uint32_t ec;
    uint64_t far;

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

    esr = vcpu->sys_regs.esr_el1;
    ec = (uint32_t)((esr >> 26U) & 0x3FU);
    far = vcpu->sys_regs.far_el1;

    switch (ec)
    {
        case 0x01U: /* WFI/WFE */
            if (vcpu->irq_pending)
            {
                vcpu->irq_pending = false;
            }
            break;

        case 0x06U: /* 系统寄存器陷阱 */
            break;

        case 0x08U: /* HVC - 超级调用 */
            {
                uint64_t args[HYP_CALL_MAX_ARGS];
                args[0U] = vcpu->gp_regs.x[0U];
                args[1U] = vcpu->gp_regs.x[1U];
                args[2U] = vcpu->gp_regs.x[2U];
                args[3U] = vcpu->gp_regs.x[3U];
                (void)vmm_handle_hypercall(vm_id, vcpu_id,
                                           vcpu->gp_regs.x[0U], args);
            }
            break;

        case 0x0AU: /* 数据中止 */
        case 0x24U:
            /* 检查是否为 MMIO 访问 */
            {
                bool is_write = ((esr & 0x40U) != 0U);
                uint32_t sas = (uint32_t)((esr >> 22U) & 0x3U);
                uint32_t access_size = 1U << sas;
                uint64_t value = 0ULL;

                (void)vmm_handle_mmio(vm_id, vcpu_id, far,
                                       is_write, &value, access_size);

                if (!is_write)
                {
                    vcpu->gp_regs.x[(esr >> 16U) & 0x1FU] = value;
                }

                vcpu->gp_regs.pc += 4U;
            }
            break;

        case 0x0EU: /* 指令中止 */
            break;

        default:
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

    if (guest_paddr >= vm->npt.guest_phys_size)
    {
        return -(int32_t)EINVAL;
    }

    (void)vm;
    (void)host_paddr;
    (void)flags;

    return KERNEL_OK;
}

/* ========================================================================
 * 获取 VMM 统计
 * ======================================================================== */

/**
 * @brief 获取 VMM 统计信息
 *
 * @param vm_count   活跃 VM 数输出
 * @param vcpu_count 活跃 vCPU 数输出
 * @param vdev_count 活跃虚拟设备数输出
 */
void vmm_get_stats(uint32_t *vm_count, uint32_t *vcpu_count,
                     uint32_t *vdev_count)
{
    uint32_t vms = 0U;
    uint32_t vcpus = 0U;
    uint32_t vdevs = 0U;
    uint32_t i;

    for (i = 0U; i < VMM_MAX_VMS; i++)
    {
        if (s_vm_used[i])
        {
            vms++;
            vcpus += s_vms[i].vcpu_count;
        }
    }

    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        if (s_vdevs[i].active)
        {
            vdevs++;
        }
    }

    if (vm_count != NULL)
    {
        *vm_count = vms;
    }
    if (vcpu_count != NULL)
    {
        *vcpu_count = vcpus;
    }
    if (vdev_count != NULL)
    {
        *vdev_count = vdevs;
    }
}
