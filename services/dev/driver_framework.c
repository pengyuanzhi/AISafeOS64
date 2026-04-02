/**
 * @file    driver_framework.c
 * @brief   用户态驱动框架实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 实现用户态驱动框架：MMIO 映射、DMA 缓冲区管理、中断绑定
 *
 * @note 对应需求: DR-001~005
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/driver_framework.h>
#include <kernel/syscall.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 驱动注册表
 * ======================================================================== */

/** @brief 最大注册驱动数 */
#define MAX_REGISTERED_DRIVERS    16U

/** @brief 驱动注册条目 */
typedef struct
{
    char                name[32U];       /**< @brief 驱动名 */
    const driver_ops_t *ops;             /**< @brief 操作函数表 */
    uint32_t            device_count;    /**< @brief 管理的设备数 */
} driver_entry_t;

static driver_entry_t s_drivers[MAX_REGISTERED_DRIVERS];
static uint32_t s_driver_count;

/* ========================================================================
 * MMIO 映射表
 * ======================================================================== */

#define MAX_MMIO_MAPPINGS    16U

typedef struct
{
    vaddr_t     virt_addr;
    paddr_t     phys_addr;
    uint64_t    size;
    uint32_t    in_use;
} mmio_mapping_t;

static mmio_mapping_t s_mmio_mappings[MAX_MMIO_MAPPINGS];
static uint32_t s_mmio_count;

/* ========================================================================
 * DMA 缓冲区表
 * ======================================================================== */

#define MAX_DMA_BUFFERS      16U

static dma_buffer_t s_dma_buffers[MAX_DMA_BUFFERS];
static uint32_t s_dma_count;

/* ========================================================================
 * 驱动注册
 * ======================================================================== */

driver_result_t driver_register(const char *name, const driver_ops_t *ops)
{
    uint32_t i;

    if ((name == NULL) || (ops == NULL))
    {
        return DRIVER_INVALID_PARAM;
    }

    if (s_driver_count >= MAX_REGISTERED_DRIVERS)
    {
        return DRIVER_NO_RESOURCE;
    }

    for (i = 0U; i < MAX_REGISTERED_DRIVERS; i++)
    {
        if (s_drivers[i].ops == NULL)
        {
            uint32_t j;
            for (j = 0U; (j < 31U) && (name[j] != '\0'); j++)
            {
                s_drivers[i].name[j] = name[j];
            }
            s_drivers[i].name[j] = '\0';
            s_drivers[i].ops = ops;
            s_drivers[i].device_count = 0U;
            s_driver_count++;
            return DRIVER_OK;
        }
    }

    return DRIVER_NO_RESOURCE;
}

driver_result_t driver_unregister(const char *name)
{
    uint32_t i;

    if (name == NULL)
    {
        return DRIVER_INVALID_PARAM;
    }

    for (i = 0U; i < MAX_REGISTERED_DRIVERS; i++)
    {
        if (s_drivers[i].ops != NULL)
        {
            uint32_t match = 1U;
            uint32_t j;
            for (j = 0U; j < 32U; j++)
            {
                if (s_drivers[i].name[j] != name[j])
                {
                    match = 0U;
                    break;
                }
                if (name[j] == '\0')
                {
                    break;
                }
            }
            if (match != 0U)
            {
                s_drivers[i].ops = NULL;
                s_drivers[i].name[0U] = '\0';
                s_driver_count--;
                return DRIVER_OK;
            }
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

    for (i = 0U; i < MAX_MMIO_MAPPINGS; i++)
    {
        if (s_mmio_mappings[i].in_use == 0U)
        {
            /* 通过系统调用映射 */
            int64_t ret = syscall3(SYS_VM_MAP, 0U,
                                    (uint64_t)mmio_base, size);

            if (ret < 0)
            {
                return DRIVER_NO_RESOURCE;
            }

            s_mmio_mappings[i].virt_addr = (vaddr_t)(uintptr_t)ret;
            s_mmio_mappings[i].phys_addr = mmio_base;
            s_mmio_mappings[i].size = size;
            s_mmio_mappings[i].in_use = 1U;
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
        if ((s_mmio_mappings[i].in_use != 0U) &&
            (s_mmio_mappings[i].virt_addr == virt_addr))
        {
            (void)syscall3(SYS_VM_UNMAP, 0U, (uint64_t)virt_addr, size);
            s_mmio_mappings[i].in_use = 0U;
            s_mmio_count--;
            return DRIVER_OK;
        }
    }

    return DRIVER_NOT_FOUND;
}

/* ========================================================================
 * DMA 缓冲区
 * ======================================================================== */

driver_result_t driver_dma_alloc(uint64_t size, dma_buffer_t *buffer)
{
    uint32_t i;

    if (buffer == NULL)
    {
        return DRIVER_INVALID_PARAM;
    }

    for (i = 0U; i < MAX_DMA_BUFFERS; i++)
    {
        if (s_dma_buffers[i].handle == 0U)
        {
            /* 分配连续页 */
            int64_t ret = syscall3(SYS_VM_MAP, 0U, 0U, size);

            if (ret < 0)
            {
                return DRIVER_NO_RESOURCE;
            }

            buffer->virt_addr = (vaddr_t)(uintptr_t)ret;
            buffer->phys_addr = 0U; /* 需要查询物理地址 */
            buffer->size = size;
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
    if ((buffer == NULL) || (buffer->handle == 0U))
    {
        return DRIVER_INVALID_PARAM;
    }

    if (buffer->handle <= MAX_DMA_BUFFERS)
    {
        uint32_t idx = buffer->handle - 1U;
        (void)syscall3(SYS_VM_UNMAP, 0U,
                         (uint64_t)buffer->virt_addr, buffer->size);
        s_dma_buffers[idx].handle = 0U;
        s_dma_count--;
        return DRIVER_OK;
    }

    return DRIVER_NOT_FOUND;
}

/* ========================================================================
 * 中断绑定
 * ======================================================================== */

driver_result_t driver_interrupt_attach(uint32_t irq)
{
    int64_t ret = syscall2(SYS_INTERRUPT_ATTACH,
                             (uint64_t)irq, 0U);

    if (ret < 0)
    {
        return DRIVER_ERROR;
    }

    return DRIVER_OK;
}

driver_result_t driver_interrupt_detach(uint32_t irq)
{
    int64_t ret = syscall1(SYS_INTERRUPT_DETACH, (uint64_t)irq);

    if (ret < 0)
    {
        return DRIVER_ERROR;
    }

    return DRIVER_OK;
}
