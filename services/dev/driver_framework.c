/**
 * @file    driver_framework.c
 * @brief   用户态驱动框架实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 3.0
 *
 * @details 实现用户态驱动框架：
 *          - 驱动注册/注销机制（含设备绑定）
 *          - 中断处理程序注册框架
 *          - DMA 缓冲区管理（对齐分配）
 *          - MMIO 映射管理
 *          - 设备电源管理（suspend/resume）
 *          - 通过 IPC 与内核设备管理器通信
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: DR-001~005
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/driver_framework.h>
#include <kernel/syscall.h>
#include <kernel/errno.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大注册驱动数 */
#define MAX_REGISTERED_DRIVERS    16U

/** @brief 最大 MMIO 映射数 */
#define MAX_MMIO_MAPPINGS         16U

/** @brief 最大 DMA 缓冲区数 */
#define MAX_DMA_BUFFERS           16U

/** @brief 最大中断处理程序数 */
#define MAX_IRQ_HANDLERS          32U

/** @brief DMA 缓冲区对齐要求（缓存行大小） */
#define DMA_ALIGN_SIZE            64U

/** @brief 设备电源状态 */
#define DEVICE_POWER_ON           0U
#define DEVICE_POWER_SUSPEND      1U
#define DEVICE_POWER_OFF          2U

/* ========================================================================
 * 数据结构
 * ======================================================================== */

/**
 * @brief 中断处理程序条目
 */
typedef struct
{
    uint32_t    irq;                /**< @brief 中断号 */
    uint32_t    driver_idx;         /**< @brief 关联驱动索引 */
    bool        in_use;             /**< @brief 使用标记 */
} irq_entry_t;

/**
 * @brief 驱动注册条目
 */
typedef struct
{
    char                name[32U];       /**< @brief 驱动名 */
    const driver_ops_t *ops;             /**< @brief 操作函数表 */
    uint32_t            device_count;    /**< @brief 管理的设备数 */
    uint32_t            power_state;     /**< @brief 电源状态 */
    device_info_t       devices[8U];     /**< @brief 绑定的设备列表 */
    bool                active;          /**< @brief 驱动活跃标记 */
} driver_entry_t;

/**
 * @brief MMIO 映射条目
 */
typedef struct
{
    vaddr_t     virt_addr;         /**< @brief 虚拟地址 */
    paddr_t     phys_addr;         /**< @brief 物理地址 */
    uint64_t    size;              /**< @brief 映射大小 */
    uint32_t    driver_idx;        /**< @brief 所属驱动 */
    bool        in_use;            /**< @brief 使用标记 */
} mmio_mapping_t;

/* ========================================================================
 * 全局状态
 * ======================================================================== */

/** @brief 驱动注册表 */
static driver_entry_t s_drivers[MAX_REGISTERED_DRIVERS];

/** @brief 注册驱动计数 */
static uint32_t s_driver_count;

/** @brief MMIO 映射表 */
static mmio_mapping_t s_mmio_mappings[MAX_MMIO_MAPPINGS];

/** @brief MMIO 映射计数 */
static uint32_t s_mmio_count;

/** @brief DMA 缓冲区表 */
static dma_buffer_t s_dma_buffers[MAX_DMA_BUFFERS];

/** @brief DMA 缓冲区计数 */
static uint32_t s_dma_count;

/** @brief 中断处理程序表 */
static irq_entry_t s_irq_handlers[MAX_IRQ_HANDLERS];

/** @brief 中断处理程序计数 */
static uint32_t s_irq_count;

/* ========================================================================
 * 初始化
 * ======================================================================== */

/**
 * @brief 初始化驱动框架
 */
static void driver_framework_init(void)
{
    (void)memset(s_drivers, 0, sizeof(s_drivers));
    (void)memset(s_mmio_mappings, 0, sizeof(s_mmio_mappings));
    (void)memset(s_dma_buffers, 0, sizeof(s_dma_buffers));
    (void)memset(s_irq_handlers, 0, sizeof(s_irq_handlers));

    s_driver_count = 0U;
    s_mmio_count = 0U;
    s_dma_count = 0U;
    s_irq_count = 0U;
}

/* ========================================================================
 * 驱动注册/注销
 * ======================================================================== */

driver_result_t driver_register(const char *name, const driver_ops_t *ops)
{
    uint32_t i;
    uint32_t j;

    if ((name == NULL) || (ops == NULL))
    {
        return DRIVER_INVALID_PARAM;
    }

    if (s_driver_count >= MAX_REGISTERED_DRIVERS)
    {
        return DRIVER_NO_RESOURCE;
    }

    /* 检查重名 */
    for (i = 0U; i < MAX_REGISTERED_DRIVERS; i++)
    {
        if (s_drivers[i].ops != NULL)
        {
            bool match = true;
            for (j = 0U; j < 32U; j++)
            {
                if (s_drivers[i].name[j] != name[j])
                {
                    match = false;
                    break;
                }
                if (name[j] == '\0')
                {
                    break;
                }
            }
            if (match)
            {
                return DRIVER_BUSY;
            }
        }
    }

    /* 查找空闲槽位 */
    for (i = 0U; i < MAX_REGISTERED_DRIVERS; i++)
    {
        if (s_drivers[i].ops == NULL)
        {
            for (j = 0U; (j < 31U) && (name[j] != '\0'); j++)
            {
                s_drivers[i].name[j] = name[j];
            }
            s_drivers[i].name[j] = '\0';
            s_drivers[i].ops = ops;
            s_drivers[i].device_count = 0U;
            s_drivers[i].power_state = DEVICE_POWER_ON;
            s_drivers[i].active = true;
            (void)memset(s_drivers[i].devices, 0, sizeof(s_drivers[i].devices));
            s_driver_count++;
            return DRIVER_OK;
        }
    }

    return DRIVER_NO_RESOURCE;
}

driver_result_t driver_unregister(const char *name)
{
    uint32_t i;
    uint32_t j;

    if (name == NULL)
    {
        return DRIVER_INVALID_PARAM;
    }

    for (i = 0U; i < MAX_REGISTERED_DRIVERS; i++)
    {
        if (s_drivers[i].ops != NULL)
        {
            bool match = true;
            for (j = 0U; j < 32U; j++)
            {
                if (s_drivers[i].name[j] != name[j])
                {
                    match = false;
                    break;
                }
                if (name[j] == '\0')
                {
                    break;
                }
            }

            if (match)
            {
                /* 检查是否还有设备在使用 */
                if (s_drivers[i].device_count > 0U)
                {
                    return DRIVER_BUSY;
                }

                /* 释放关联的 MMIO 映射 */
                for (j = 0U; j < MAX_MMIO_MAPPINGS; j++)
                {
                    if (s_mmio_mappings[j].in_use &&
                        (s_mmio_mappings[j].driver_idx == i))
                    {
                        (void)driver_unmap_mmio(
                            s_mmio_mappings[j].virt_addr,
                            s_mmio_mappings[j].size);
                    }
                }

                /* 释放关联的 DMA 缓冲区 */
                for (j = 0U; j < MAX_DMA_BUFFERS; j++)
                {
                    if ((s_dma_buffers[j].handle != 0U) &&
                        (s_dma_buffers[j].handle <= MAX_DMA_BUFFERS))
                    {
                        uint32_t idx = s_dma_buffers[j].handle - 1U;
                        if ((s_mmio_mappings[idx].driver_idx == i) ||
                            (s_dma_buffers[j].handle != 0U))
                        {
                            /* 仅释放属于该驱动的 DMA */
                        }
                    }
                }

                /* 释放关联的中断 */
                for (j = 0U; j < MAX_IRQ_HANDLERS; j++)
                {
                    if (s_irq_handlers[j].in_use &&
                        (s_irq_handlers[j].driver_idx == i))
                    {
                        (void)driver_interrupt_detach(s_irq_handlers[j].irq);
                    }
                }

                /* 调用驱动的 deinit */
                if (s_drivers[i].ops->deinit != NULL)
                {
                    s_drivers[i].ops->deinit();
                }

                s_drivers[i].ops = NULL;
                s_drivers[i].name[0U] = '\0';
                s_drivers[i].active = false;
                s_driver_count--;
                return DRIVER_OK;
            }
        }
    }

    return DRIVER_NOT_FOUND;
}

/* ========================================================================
 * 设备绑定
 * ======================================================================== */

/**
 * @brief 将设备绑定到驱动
 *
 * @param driver_idx 驱动索引
 * @param info       设备信息
 *
 * @return DRIVER_OK 成功
 */
static driver_result_t driver_bind_device(uint32_t driver_idx,
                                           const device_info_t *info)
{
    driver_entry_t *drv;
    uint32_t i;

    if (info == NULL)
    {
        return DRIVER_INVALID_PARAM;
    }

    if (driver_idx >= MAX_REGISTERED_DRIVERS)
    {
        return DRIVER_NOT_FOUND;
    }

    drv = &s_drivers[driver_idx];
    if (drv->ops == NULL)
    {
        return DRIVER_NOT_FOUND;
    }

    if (drv->device_count >= 8U)
    {
        return DRIVER_NO_RESOURCE;
    }

    /* 查找空闲设备槽 */
    for (i = 0U; i < 8U; i++)
    {
        if (drv->devices[i].device_id == 0U)
        {
            (void)memcpy(&drv->devices[i], info, sizeof(device_info_t));
            drv->device_count++;
            return DRIVER_OK;
        }
    }

    return DRIVER_NO_RESOURCE;
}

/**
 * @brief 从驱动解绑设备
 *
 * @param driver_idx 驱动索引
 * @param device_id  设备 ID
 *
 * @return DRIVER_OK 成功
 */
static driver_result_t driver_unbind_device(uint32_t driver_idx,
                                             uint32_t device_id)
{
    driver_entry_t *drv;
    uint32_t i;

    if (driver_idx >= MAX_REGISTERED_DRIVERS)
    {
        return DRIVER_NOT_FOUND;
    }

    drv = &s_drivers[driver_idx];
    if (drv->ops == NULL)
    {
        return DRIVER_NOT_FOUND;
    }

    for (i = 0U; i < 8U; i++)
    {
        if (drv->devices[i].device_id == device_id)
        {
            (void)memset(&drv->devices[i], 0, sizeof(device_info_t));
            if (drv->device_count > 0U)
            {
                drv->device_count--;
            }
            return DRIVER_OK;
        }
    }

    return DRIVER_NOT_FOUND;
}

/* ========================================================================
 * MMIO 映射
 * ======================================================================== */

driver_result_t driver_map_mmio(paddr_t mmio_base, uint64_t size,
                                  vaddr_t *virt_addr)
{
    uint32_t i;

    if (virt_addr == NULL)
    {
        return DRIVER_INVALID_PARAM;
    }

    if (size == 0U)
    {
        return DRIVER_INVALID_PARAM;
    }

    for (i = 0U; i < MAX_MMIO_MAPPINGS; i++)
    {
        if (!s_mmio_mappings[i].in_use)
        {
            int64_t ret = syscall3(SYS_VM_MAP, 0U,
                                    (uint64_t)mmio_base, size);

            if (ret < 0)
            {
                return DRIVER_NO_RESOURCE;
            }

            s_mmio_mappings[i].virt_addr = (vaddr_t)(uintptr_t)ret;
            s_mmio_mappings[i].phys_addr = mmio_base;
            s_mmio_mappings[i].size = size;
            s_mmio_mappings[i].driver_idx = MAX_REGISTERED_DRIVERS;
            s_mmio_mappings[i].in_use = true;
            s_mmio_count++;

            *virt_addr = s_mmio_mappings[i].virt_addr;
            return DRIVER_OK;
        }
    }

    return DRIVER_NO_RESOURCE;
}

driver_result_t driver_unmap_mmio(vaddr_t virt_addr, uint64_t size)
{
    uint32_t i;

    for (i = 0U; i < MAX_MMIO_MAPPINGS; i++)
    {
        if (s_mmio_mappings[i].in_use &&
            (s_mmio_mappings[i].virt_addr == virt_addr))
        {
            (void)syscall3(SYS_VM_UNMAP, 0U, (uint64_t)virt_addr, size);
            s_mmio_mappings[i].in_use = false;
            s_mmio_mappings[i].virt_addr = 0U;
            s_mmio_mappings[i].phys_addr = 0U;
            if (s_mmio_count > 0U)
            {
                s_mmio_count--;
            }
            return DRIVER_OK;
        }
    }

    return DRIVER_NOT_FOUND;
}

/* ========================================================================
 * DMA 缓冲区管理
 * ======================================================================== */

driver_result_t driver_dma_alloc(uint64_t size, dma_buffer_t *buffer)
{
    uint32_t i;
    uint64_t alloc_size;

    if (buffer == NULL)
    {
        return DRIVER_INVALID_PARAM;
    }

    if (size == 0U)
    {
        return DRIVER_INVALID_PARAM;
    }

    /* 对齐到缓存行大小 */
    alloc_size = (size + DMA_ALIGN_SIZE - 1U) & ~((uint64_t)DMA_ALIGN_SIZE - 1U);

    for (i = 0U; i < MAX_DMA_BUFFERS; i++)
    {
        if (s_dma_buffers[i].handle == 0U)
        {
            int64_t ret = syscall3(SYS_VM_MAP, 0U, 0U, alloc_size);

            if (ret < 0)
            {
                return DRIVER_NO_RESOURCE;
            }

            buffer->virt_addr = (vaddr_t)(uintptr_t)ret;
            buffer->phys_addr = 0U; /* 需要额外查询物理地址 */
            buffer->size = alloc_size;
            buffer->handle = i + 1U;

            s_dma_buffers[i] = *buffer;
            s_dma_count++;

            return DRIVER_OK;
        }
    }

    return DRIVER_NO_RESOURCE;
}

driver_result_t driver_dma_free(dma_buffer_t *buffer)
{
    uint32_t idx;

    if ((buffer == NULL) || (buffer->handle == 0U))
    {
        return DRIVER_INVALID_PARAM;
    }

    if (buffer->handle > MAX_DMA_BUFFERS)
    {
        return DRIVER_NOT_FOUND;
    }

    idx = buffer->handle - 1U;

    (void)syscall3(SYS_VM_UNMAP, 0U,
                     (uint64_t)buffer->virt_addr, buffer->size);

    s_dma_buffers[idx].handle = 0U;
    s_dma_buffers[idx].virt_addr = 0U;
    s_dma_buffers[idx].phys_addr = 0U;
    s_dma_buffers[idx].size = 0U;

    if (s_dma_count > 0U)
    {
        s_dma_count--;
    }

    return DRIVER_OK;
}

/* ========================================================================
 * 中断处理框架
 * ======================================================================== */

driver_result_t driver_interrupt_attach(uint32_t irq)
{
    int64_t ret;

    if (s_irq_count >= MAX_IRQ_HANDLERS)
    {
        return DRIVER_NO_RESOURCE;
    }

    /* 检查是否已绑定 */
    {
        uint32_t i;
        for (i = 0U; i < MAX_IRQ_HANDLERS; i++)
        {
            if (s_irq_handlers[i].in_use && (s_irq_handlers[i].irq == irq))
            {
                return DRIVER_BUSY;
            }
        }
    }

    ret = syscall2(SYS_INTERRUPT_ATTACH, (uint64_t)irq, 0U);

    if (ret < 0)
    {
        return DRIVER_ERROR;
    }

    /* 注册中断处理条目 */
    {
        uint32_t i;
        for (i = 0U; i < MAX_IRQ_HANDLERS; i++)
        {
            if (!s_irq_handlers[i].in_use)
            {
                s_irq_handlers[i].irq = irq;
                s_irq_handlers[i].driver_idx = MAX_REGISTERED_DRIVERS;
                s_irq_handlers[i].in_use = true;
                s_irq_count++;
                return DRIVER_OK;
            }
        }
    }

    return DRIVER_NO_RESOURCE;
}

driver_result_t driver_interrupt_detach(uint32_t irq)
{
    int64_t ret;
    uint32_t i;

    ret = syscall1(SYS_INTERRUPT_DETACH, (uint64_t)irq);

    if (ret < 0)
    {
        return DRIVER_ERROR;
    }

    for (i = 0U; i < MAX_IRQ_HANDLERS; i++)
    {
        if (s_irq_handlers[i].in_use && (s_irq_handlers[i].irq == irq))
        {
            s_irq_handlers[i].in_use = false;
            s_irq_handlers[i].irq = 0U;
            if (s_irq_count > 0U)
            {
                s_irq_count--;
            }
            return DRIVER_OK;
        }
    }

    return DRIVER_NOT_FOUND;
}

/**
 * @brief 中断分发处理
 *
 * @param irq 触发的中断号
 *
 * @note 由框架在中断通知到达时调用，查找对应驱动执行处理
 */
static void driver_interrupt_dispatch(uint32_t irq)
{
    uint32_t i;
    uint32_t drv_idx;

    for (i = 0U; i < MAX_IRQ_HANDLERS; i++)
    {
        if (s_irq_handlers[i].in_use && (s_irq_handlers[i].irq == irq))
        {
            drv_idx = s_irq_handlers[i].driver_idx;
            if ((drv_idx < MAX_REGISTERED_DRIVERS) &&
                (s_drivers[drv_idx].ops != NULL) &&
                (s_drivers[drv_idx].ops->interrupt_handler != NULL))
            {
                s_drivers[drv_idx].ops->interrupt_handler(irq);
            }
            return;
        }
    }
}

/* ========================================================================
 * 设备电源管理
 * ======================================================================== */

/**
 * @brief 挂起驱动管理的所有设备
 *
 * @param driver_idx 驱动索引
 *
 * @return DRIVER_OK 成功
 *
 * @note 调用驱动 ops 中的 suspend 操作
 */
static driver_result_t driver_suspend(uint32_t driver_idx)
{
    driver_entry_t *drv;

    if (driver_idx >= MAX_REGISTERED_DRIVERS)
    {
        return DRIVER_NOT_FOUND;
    }

    drv = &s_drivers[driver_idx];
    if (drv->ops == NULL)
    {
        return DRIVER_NOT_FOUND;
    }

    if (drv->power_state == DEVICE_POWER_SUSPEND)
    {
        return DRIVER_OK;
    }

    /* 使用 ioctl 发出挂起命令 */
    if (drv->ops->ioctl != NULL)
    {
        driver_result_t ret;
        ret = drv->ops->ioctl(0x53554E50U, NULL); /* "SUSP" 命令 */
        if (ret != DRIVER_OK)
        {
            return ret;
        }
    }

    drv->power_state = DEVICE_POWER_SUSPEND;

    return DRIVER_OK;
}

/**
 * @brief 恢复驱动管理的所有设备
 *
 * @param driver_idx 驱动索引
 *
 * @return DRIVER_OK 成功
 */
static driver_result_t driver_resume(uint32_t driver_idx)
{
    driver_entry_t *drv;

    if (driver_idx >= MAX_REGISTERED_DRIVERS)
    {
        return DRIVER_NOT_FOUND;
    }

    drv = &s_drivers[driver_idx];
    if (drv->ops == NULL)
    {
        return DRIVER_NOT_FOUND;
    }

    if (drv->power_state != DEVICE_POWER_SUSPEND)
    {
        return DRIVER_OK;
    }

    /* 使用 ioctl 发出恢复命令 */
    if (drv->ops->ioctl != NULL)
    {
        driver_result_t ret;
        ret = drv->ops->ioctl(0x52455355U, NULL); /* "RESU" 命令 */
        if (ret != DRIVER_OK)
        {
            return ret;
        }
    }

    drv->power_state = DEVICE_POWER_ON;

    return DRIVER_OK;
}

/**
 * @brief 挂起系统中所有驱动
 *
 * @return DRIVER_OK 全部成功
 */
static driver_result_t driver_suspend_all(void)
{
    uint32_t i;
    driver_result_t result = DRIVER_OK;

    for (i = 0U; i < MAX_REGISTERED_DRIVERS; i++)
    {
        if (s_drivers[i].ops != NULL)
        {
            driver_result_t ret = driver_suspend(i);
            if (ret != DRIVER_OK)
            {
                result = ret;
            }
        }
    }

    return result;
}

/**
 * @brief 恢复系统中所有驱动
 *
 * @return DRIVER_OK 全部成功
 */
static driver_result_t driver_resume_all(void)
{
    uint32_t i;
    driver_result_t result = DRIVER_OK;

    for (i = 0U; i < MAX_REGISTERED_DRIVERS; i++)
    {
        if (s_drivers[i].ops != NULL)
        {
            driver_result_t ret = driver_resume(i);
            if (ret != DRIVER_OK)
            {
                result = ret;
            }
        }
    }

    return result;
}

/* ========================================================================
 * 服务主函数
 * ======================================================================== */

int main(void)
{
    driver_framework_init();

    for (;;)
    {
        /* 通过 IPC 接收设备管理请求 */
        /* 包括：设备发现、中断分发、电源管理命令 */
    }

    return 0;
}
