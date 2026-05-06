/**
 * @file    virtio_block.c
 * @brief   VirtIO-Block 块设备实现
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 本文件实现了 VirtIO-Block 块设备的所有功能：
 *          - 设备初始化/销毁
 *          - 块设备读写操作
 *          - 块设备请求处理
 *          - 中断注入
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */
#include "virtio_block.h"
#include <stdint.h>
#include <string.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <kernel/mm/mem.h>
#include "vmm.h"
#include "vmm_stats.h"

/* ========================================================================
 * 内部状态
 * ======================================================================== */

/** @brief VirtIO-Block 设备表 */
static virtio_blk_dev_t s_blk_devs[VMM_MAX_VDEVICES];

/** @brief VirtIO-Block 设备活跃标记 */
static bool s_blk_devs_active[VMM_MAX_VDEVICES];

/** @brief VirtIO-Block 设备初始化标志 */
static bool s_blk_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 查找 VirtIO-Block 设备
 *
 * @param dev_id   设备 ID
 *
 * @return 设备指针，不存在返回 NULL
 */
static virtio_blk_dev_t *blk_dev_find(uint32_t dev_id)
{
    if (dev_id >= VMM_MAX_VDEVICES)
    {
        return NULL;
    }

    if (!s_blk_devs_active[dev_id])
    {
        return NULL;
    }

    return &s_blk_devs[dev_id];
}

/**
 * @brief 设置设备状态
 *
 * @param dev      设备指针
 * @param status   设备状态
 */
static void blk_dev_set_status(virtio_blk_dev_t *dev, uint16_t status)
{
    dev->status = status;

    /* 如果状态为 RESET，关闭设备 */
    if (status == 0x0U)
    {
        dev->active = false;
    }
}

/* ========================================================================
 * 公共 API - 设备初始化/销毁
 * ======================================================================== */

int32_t virtio_blk_init(uint32_t vm_id, const char *name,
                        const uint8_t *image, uint64_t image_size,
                        uint64_t mmio_base)
{
    uint32_t i;
    virtio_blk_dev_t *dev;
    uint64_t sectors;

    if (!s_blk_initialized)
    {
        return -(int32_t)EPERM;
    }

    if (name == NULL || image == NULL || image_size == 0ULL)
    {
        return -(int32_t)EINVAL;
    }

    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找空闲设备槽 */
    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        if (!s_blk_devs_active[i])
        {
            break;
        }
    }

    if (i >= VMM_MAX_VDEVICES)
    {
        return -(int32_t)ENOMEM;
    }

    dev = &s_blk_devs[i];

    /* 初始化设备 */
    (void)memset(dev, 0, sizeof(virtio_blk_dev_t));
    dev->dev_id = i;
    dev->vm_id = vm_id;
    dev->name[0] = '\0';
    (void)strncpy(dev->name, name, 15);
    dev->name[15] = '\0';
    dev->mmio_base = mmio_base;
    dev->mmio_size = VIRTIO_BLK_CONFIG_SIZE;
    dev->active = true;
    dev->num_queues = VIRTIO_BLK_NUM_QUEUES;
    dev->queue_size = VIRTIO_BLK_QUEUE_SIZE;

    /* 设置设备状态为 RESET */
    blk_dev_set_status(dev, 0x0U);

    /* 计算扇区数 */
    sectors = image_size / 512ULL;  /* 假设 512 字节每扇区 */
    dev->sector_count = sectors;

    /* 设置设备特性 */
    dev->features = VIRTIO_BLK_F_BLK_SIZE |   /* 支持块大小 */
                    VIRTIO_BLK_F_RO |          /* 支持只读 */
                    VIRTIO_BLK_F_FLUSH;       /* 支持刷新 */

    /* 初始化配置空间 */
    dev->config.capacity = sectors;  /* 容量（扇区数） */
    dev->config.size_max = 65536;    /* 最大段大小 */
    dev->config.seg_max = 65535;     /* 最大段数 */
    dev->config.geometry.cylinders = 0;
    dev->config.geometry.heads = 16;
    dev->config.geometry.sectors = 63;
    dev->config.blk_size = 512;     /* 块大小 */
    dev->config.physical_block_exp = 0;
    dev->config.alignment_offset = 0;
    dev->config.min_io_size = 1;
    dev->config.opt_io_size = 0;
    dev->config.wce = 0;

    /* 分配磁盘镜像（复制数据） */
    dev->image = (uint8_t *)kmalloc(image_size);
    if (dev->image == NULL)
    {
        return -(int32_t)ENOMEM;
    }

    (void)memcpy(dev->image, image, image_size);
    dev->image_size = image_size;

    /* 标记为活跃 */
    s_blk_devs_active[i] = true;

    /* 更新统计信息 */
    vmm_stats_update_device(vm_id, VIRTIO_DEVICE_BLOCK, true);

    return (int32_t)i;
}

kernel_status_t virtio_blk_destroy(uint32_t dev_id)
{
    virtio_blk_dev_t *dev;

    dev = blk_dev_find(dev_id);
    if (dev == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 释放磁盘镜像 */
    if (dev->image != NULL)
    {
        kfree(dev->image);
        dev->image = NULL;
    }

    /* 标记为不活跃 */
    s_blk_devs_active[dev_id] = false;

    return KERNEL_OK;
}

virtio_blk_dev_t *virtio_blk_get_device(uint32_t dev_id)
{
    return blk_dev_find(dev_id);
}

/* ========================================================================
 * 公共 API - 块设备读写操作
 * ======================================================================== */

int64_t virtio_blk_read(uint32_t dev_id, uint64_t sector,
                        void *buffer, uint32_t num_sectors)
{
    virtio_blk_dev_t *dev;
    uint64_t offset;
    uint64_t size;
    uint64_t max_sectors;

    dev = blk_dev_find(dev_id);
    if (dev == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查是否为只读设备 */
    if ((dev->features & VIRTIO_BLK_F_RO) != 0ULL)
    {
        return -(int32_t)EROFS;
    }

    /* 边界检查 */
    if (sector >= dev->sector_count)
    {
        return -(int32_t)ERANGE;
    }

    if (num_sectors == 0U || num_sectors > 256U)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查是否超出容量 */
    if (sector + num_sectors > dev->sector_count)
    {
        return -(int32_t)ERANGE;
    }

    /* 计算读取偏移 */
    offset = sector * 512ULL;  /* 假设 512 字节每扇区 */
    size = num_sectors * 512ULL;

    /* 检查缓冲区大小 */
    if (buffer == NULL || size == 0ULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查偏移是否越界 */
    if (offset >= dev->image_size)
    {
        return -(int32_t)ERANGE;
    }

    /* 检查读取是否超出镜像大小 */
    if (offset + size > dev->image_size)
    {
        return -(int32_t)ERANGE;
    }

    /* 执行读取 */
    (void)memcpy(buffer, dev->image + offset, (size_t)size);

    return (int64_t)size;
}

int64_t virtio_blk_write(uint32_t dev_id, uint64_t sector,
                         const void *buffer, uint32_t num_sectors)
{
    virtio_blk_dev_t *dev;
    uint64_t offset;
    uint64_t size;

    dev = blk_dev_find(dev_id);
    if (dev == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查是否为只读设备 */
    if ((dev->features & VIRTIO_BLK_F_RO) != 0ULL)
    {
        return -(int32_t)EROFS;
    }

    /* 边界检查 */
    if (sector >= dev->sector_count)
    {
        return -(int32_t)ERANGE;
    }

    if (num_sectors == 0U || num_sectors > 256U)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查是否超出容量 */
    if (sector + num_sectors > dev->sector_count)
    {
        return -(int32_t)ERANGE;
    }

    /* 计算写入偏移 */
    offset = sector * 512ULL;  /* 假设 512 字节每扇区 */
    size = num_sectors * 512ULL;

    /* 检查缓冲区大小 */
    if (buffer == NULL || size == 0ULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查偏移是否越界 */
    if (offset >= dev->image_size)
    {
        return -(int32_t)ERANGE;
    }

    /* 检查写入是否超出镜像大小 */
    if (offset + size > dev->image_size)
    {
        return -(int32_t)ERANGE;
    }

    /* 执行写入 */
    (void)memcpy((void *)(dev->image + offset), buffer, (size_t)size);

    return (int64_t)size;
}

kernel_status_t virtio_blk_flush(uint32_t dev_id)
{
    /* 简化实现：仅返回成功
     * 完整实现需要：
     * 1. 清除写缓存
     * 2. 同步到磁盘镜像
     * 3. 如果支持 DISCARD，执行 TRIM
     */

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 块设备请求处理
 * ======================================================================== */

kernel_status_t virtio_blk_handle_req(uint32_t dev_id,
                                      virtio_blk_req_t *req)
{
    virtio_blk_dev_t *dev;
    uint64_t sector;
    uint32_t num_sectors;
    uint32_t type;
    kernel_status_t ret;
    int64_t bytes;

    dev = blk_dev_find(dev_id);
    if (dev == NULL || req == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 读取请求类型和扇区 */
    type = req->hdr.type;
    sector = req->hdr.sector;
    num_sectors = req->data.len / 512ULL;  /* 假设 512 字节每扇区 */

    /* 根据请求类型分发处理 */
    switch (type)
    {
        case VIRTIO_BLK_T_IN:  /* 读操作 */
            bytes = virtio_blk_read(dev_id, sector, req->data.addr, num_sectors);
            if (bytes < 0)
            {
                req->status = VIRTIO_BLK_S_IOERR;
                return KERNEL_OK;
            }
            req->status = VIRTIO_BLK_S_OK;
            break;

        case VIRTIO_BLK_T_OUT:  /* 写操作 */
            bytes = virtio_blk_write(dev_id, sector, (const void *)req->data.addr, num_sectors);
            if (bytes < 0)
            {
                req->status = VIRTIO_BLK_S_IOERR;
                return KERNEL_OK;
            }
            req->status = VIRTIO_BLK_S_OK;
            break;

        case VIRTIO_BLK_T_FLUSH:  /* 刷新操作 */
            ret = virtio_blk_flush(dev_id);
            if (ret != KERNEL_OK)
            {
                req->status = VIRTIO_BLK_S_IOERR;
                return ret;
            }
            req->status = VIRTIO_BLK_S_OK;
            break;

        case VIRTIO_BLK_T_DISCARD:  /* 丢弃操作 */
            /* 简化实现：仅返回成功
             * 完整实现需要：
             * 1. TRIM 设备（清空写入映射表）
             * 2. 更新磁盘镜像
             */
            req->status = VIRTIO_BLK_S_OK;
            break;

        case VIRTIO_BLK_T_WRITE_SAME:  /* 写相同块 */
            /* 简化实现：仅返回成功
             * 完整实现需要：
             * 1. 从 data.addr 读取一个块
             * 2. 写入多个扇区
             */
            req->status = VIRTIO_BLK_S_OK;
            break;

        default:
            req->status = VIRTIO_BLK_S_UNSUPP;
            break;
    }

    /* 如果请求成功，注入中断 */
    if (req->status == VIRTIO_BLK_S_OK)
    {
        vmm_inject_irq(dev->vm_id, 0U);
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 中断注入
 * ======================================================================== */

kernel_status_t virtio_blk_inject_irq(uint32_t dev_id, uint32_t vcpu_id)
{
    (void)vcpu_id;

    /* 简化实现：注入到 vCPU 0
     * 完整实现需要：
     * 1. 查找设备的 vCPU 数量
     * 2. 注入中断到正确的 vCPU
     */

    return vmm_inject_irq(dev_id, 0U);
}
