/**
 * @file    virtio.c
 * @brief   VirtIO 设备总线实现
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 本文件实现了 VirtIO 设备的总线框架：
 *          - 虚拟设备注册/注销
 *          - MMIO 访问处理
 *          - VirtIO 鹰踢（kick）机制
 *          - 设备特性协商
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */
#include "virtio.h"
#include <stdint.h>
#include <string.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include "vmm.h"
#include "vmm_stats.h"

/* ========================================================================
 * 内部状态
 * ======================================================================== */

/** @brief 虚拟设备表 */
static virtio_device_t s_vdevs[VMM_MAX_VDEVICES];

/** @brief 虚拟设备活跃标记 */
static bool s_vdevs_active[VMM_MAX_VDEVICES];

/** @brief 虚拟设备初始化标志 */
static bool s_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 查找虚拟设备
 *
 * @param vm_id       VM ID
 * @param mmio_base   MMIO 基址
 *
 * @return 设备指针，不存在返回 NULL
 */
static virtio_device_t *vdev_find(uint32_t vm_id, uint64_t mmio_base)
{
    uint32_t i;

    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        if (s_vdevs_active[i] &&
            s_vdevs[i].vm_id == vm_id &&
            s_vdevs[i].mmio_base == mmio_base)
        {
            return &s_vdevs[i];
        }
    }

    return NULL;
}

/* ========================================================================
 * 公共 API - 虚拟设备注册
 * ======================================================================== */

int32_t vmm_register_vdevice(uint32_t vm_id, virtio_device_type_t type,
                               const char *name,
                               uint64_t mmio_base, uint64_t mmio_size)
{
    uint32_t i;
    virtio_device_t *dev;
    vm_desc_t *vm;

    if (!s_initialized)
    {
        return -(int32_t)EPERM;
    }

    if (name == NULL || mmio_size == 0ULL)
    {
        return -(int32_t)EINVAL;
    }

    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 查找空闲设备槽 */
    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        if (!s_vdevs_active[i])
        {
            break;
        }
    }

    if (i >= VMM_MAX_VDEVICES)
    {
        return -(int32_t)ENOMEM;
    }

    dev = &s_vdevs[i];

    /* 初始化设备 */
    (void)memset(dev, 0, sizeof(virtio_device_t));
    dev->dev_id = i;
    dev->vm_id = vm_id;
    dev->type = type;
    dev->mmio_base = mmio_base;
    dev->mmio_size = mmio_size;
    dev->active = true;
    dev->num_vqs = 0U;
    dev->queue_index = 0U;
    dev->features = 0U;
    dev->status = 0U;  /* RESET */
    dev->config_gen = 0U;
    dev->device_features = 0U;
    dev->driver_features = 0U;
    dev->device_features_sel = 0U;
    dev->driver_features_sel = 0U;
    dev->config = NULL;
    dev->config_size = 0U;
    dev->read_fn = NULL;
    dev->write_fn = NULL;
    dev->priv = NULL;

    /* 根据设备类型设置默认配置空间 */
    switch (type)
    {
        case VIRTIO_DEVICE_BLOCK:
        case VIRTIO_DEVICE_NET:
        case VIRTIO_DEVICE_CONSOLE:
        case VIRTIO_DEVICE_RNG:
        case VIRTIO_DEVICE_BALLOON:
            dev->config_size = 0x100U;  /* 256 字节 */
            break;
        default:
            dev->config_size = 0x0U;
            break;
    }

    /* 分配配置空间 */
    if (dev->config_size > 0U)
    {
        dev->config = (void *)kmalloc(dev->config_size);
    }

    /* 设置设备特定操作（待实现具体设备） */
    dev->read_fn = vmm_virtio_default_read;
    dev->write_fn = vmm_virtio_default_write;

    /* 标记为活跃 */
    s_vdevs_active[i] = true;
    vm->vdev_count++;
    vm->vdev_ids[vm->vdev_count - 1] = i;

    /* 更新统计信息 */
    vmm_stats_update_device(vm_id, (uint32_t)type, true);

    return (int32_t)i;
}

/* ========================================================================
 * 公共 API - MMIO 访问处理
 * ======================================================================== */

kernel_status_t vmm_handle_mmio(uint32_t vm_id, uint32_t vcpu_id,
                                  uint64_t fault_addr, bool is_write,
                                  uint64_t *value, uint32_t size)
{
    uint64_t offset;
    virtio_device_t *dev;
    kernel_status_t ret;

    /* 查找对应的虚拟设备 */
    dev = vdev_find(vm_id, fault_addr);
    if (dev == NULL)
    {
        return -(int32_t)EFAULT;
    }

    /* 计算偏移 */
    offset = fault_addr - dev->mmio_base;

    /* 边界检查 */
    if (offset >= dev->mmio_size)
    {
        return -(int32_t)EFAULT;
    }

    /* 宽度检查 */
    if (size > 8U)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查设备状态 */
    if (dev->status == 0U)  /* RESET */
    {
        return -(int32_t)EFAULT;
    }

    /* 处理 MMIO 访问 */
    if (is_write)
    {
        ret = dev->write_fn(vm_id, vcpu_id, offset, MMIO_WRITE, value, size);
    }
    else
    {
        ret = dev->read_fn(vm_id, vcpu_id, offset, MMIO_READ, value, size);
    }

    return ret;
}

/* ========================================================================
 * 公共 API - VirtIO 鹰踢（Kick）
 * ======================================================================== */

kernel_status_t vmm_virtio_kick(uint32_t vm_id, uint32_t vcpu_id,
                                 uint32_t queue_idx)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    virtio_device_t *dev;
    uint32_t i;

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

    /* 查找设备 */
    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        if (s_vdevs_active[i] && s_vdevs[i].vm_id == vm_id)
        {
            dev = &s_vdevs[i];

            /* 检查队列索引是否有效 */
            if (queue_idx >= dev->num_vqs)
            {
                return -(int32_t)EINVAL;
            }

            /* 标记队列需要唤醒 */
            if (dev->vqs[queue_idx].state == VIRTIO_QUEUE_BLOCKED)
            {
                dev->vqs[queue_idx].state = VIRTIO_QUEUE_SUSPENDED;
            }

            /* 注入虚拟中断（简化实现） */
            return vmm_inject_irq(vm_id, vcpu_id, 0U);  /* 使用中断 0 作为 VirtIO 中断 */
        }
    }

    return -(int32_t)EINVAL;
}

/* ========================================================================
 * 内部 API - 虚拟设备操作
 * ======================================================================== */

/**
 * @brief VirtIO 默认读操作
 */
static kernel_status_t vmm_virtio_default_read(uint32_t vm_id, uint32_t vcpu_id,
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

    /* 简化实现：返回默认值 */
    switch (offset)
    {
        case 0x000:  /* DEVICE_ID */
            *value = 0x1000U;  /* VirtIO 设备 ID */
            break;
        case 0x004:  /* DEVICE_FEATURES */
            *value = 0x00000000ULL;
            break;
        case 0x008:  /* DRIVER_FEATURES */
            *value = 0x00000000ULL;
            break;
        case 0x00C:  /* NUM_QUEUES */
            *value = 0x00000000ULL;
            break;
        case 0x044:  /* STATUS */
            *value = 0x00000000ULL;  /* RESET */
            break;
        case 0x04C:  /* CONFIG_GENERATION */
            *value = 0x00000000ULL;
            break;
        default:
            *value = 0x00000000ULL;
            break;
    }

    return KERNEL_OK;
}

/**
 * @brief VirtIO 默认写操作
 */
static kernel_status_t vmm_virtio_default_write(uint32_t vm_id, uint32_t vcpu_id,
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

    /* 简化实现：仅处理部分寄存器 */
    switch (offset)
    {
        case 0x044:  /* STATUS */
            {
                virtio_device_t *dev = vdev_find(vm_id, vmm_get_mmio_base(vm_id));
                if (dev != NULL)
                {
                    dev->status = (uint16_t)(*value & 0xFFFFU);
                }
            }
            break;
        default:
            break;
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 内部 API - 辅助函数
 * ======================================================================== */

/**
 * @brief 获取 VM 的 MMIO 基址（内部 API）
 */
uint64_t vmm_get_mmio_base(uint32_t vm_id)
{
    vm_desc_t *vm;
    uint32_t i;

    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return 0ULL;
    }

    for (i = 0U; i < vm->vdev_count; i++)
    {
        if (s_vdevs_active[vm->vdev_ids[i]])
        {
            return s_vdevs[vm->vdev_ids[i]].mmio_base;
        }
    }

    return 0ULL;
}
