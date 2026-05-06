/**
 * @file    virtio_rng.c
 * @brief   VirtIO-RNG 随机数设备实现
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details 本文件实现了 VirtIO-RNG 随机数设备的所有功能：
 *          - 设备初始化/销毁
 *          - MMIO 读/写操作
 *          - 随机数生成
 *          - 统计信息管理
 *          - 随机数生成器重置
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */
#include "virtio_rng.h"
#include <stdint.h>
#include <string.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include "vmm.h"
#include "vmm_stats.h"

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 线性同余生成器参数 */
#define RNG_LCG_MULTIPLIER     1664525U
#define RNG_LCG_INCREMENT      1013904223U
#define RNG_LCG_MODULO         0xFFFFFFFFU

/* ========================================================================
 * 内部状态
 * ======================================================================== */

/** @brief VirtIO-RNG 设备表 */
static virtio_rng_dev_t s_rng_devs[VMM_MAX_VDEVICES];

/** @brief VirtIO-RNG 设备活跃标记 */
static bool s_rng_devs_active[VMM_MAX_VDEVICES];

/** @brief VirtIO-RNG 设备初始化标志 */
static bool s_rng_initialized = false;

/** @brief 全局随机数种子 */
static uint32_t s_global_rng_seed = 0x12345678U;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 查找 VirtIO-RNG 设备
 *
 * @param dev_id   设备 ID
 *
 * @return 设备指针，不存在返回 NULL
 */
static virtio_rng_dev_t *rng_dev_find(uint32_t dev_id)
{
    if (dev_id >= VMM_MAX_VDEVICES)
    {
        return NULL;
    }

    if (!s_rng_devs_active[dev_id])
    {
        return NULL;
    }

    return &s_rng_devs[dev_id];
}

/**
 * @brief 设置设备状态
 *
 * @param dev      设备指针
 * @param status   设备状态
 */
static void rng_dev_set_status(virtio_rng_dev_t *dev, uint16_t status)
{
    dev->status = status;

    /* 如果状态为 RESET，关闭设备 */
    if (status == 0x0U)
    {
        dev->active = false;
    }
}

/**
 * @brief MMIO 读操作（VirtIO-RNG）
 *
 * @param dev      设备指针
 * @param offset   MMIO 偏移
 * @param value    输出值
 * @param size     访问大小
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t rng_dev_mmio_read(virtio_rng_dev_t *dev,
                                           uint64_t offset,
                                           uint64_t *value,
                                           uint32_t size)
{
    uint8_t *config_ptr;
    uint32_t i;

    if (dev == NULL || value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* VirtIO MMIO 寄存器读取 */
    /* 注意：这里只处理 VirtIO-RNG 特有的寄存器，通用 VirtIO 寄存器由 virtio.c 处理 */

    /* 配置空间读取 */
    if (offset >= 0x100U && offset < 0x100U + VIRTIO_RNG_CONFIG_SIZE)
    {
        config_ptr = (uint8_t *)&dev->config;
        offset -= 0x100U;

        if (offset + size > VIRTIO_RNG_CONFIG_SIZE)
        {
            return -(int32_t)EINVAL;
        }

        *value = 0ULL;
        for (i = 0U; i < size; i++)
        {
            *value |= (uint64_t)config_ptr[offset + i] << (i * 8U);
        }

        return KERNEL_OK;
    }

    return -(int32_t)EINVAL;
}

/**
 * @brief MMIO 写操作（VirtIO-RNG）
 *
 * @param dev      设备指针
 * @param offset   MMIO 偏移
 * @param value    写入值
 * @param size     访问大小
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t rng_dev_mmio_write(virtio_rng_dev_t *dev,
                                            uint64_t offset,
                                            uint64_t value,
                                            uint32_t size)
{
    uint8_t *config_ptr;
    uint32_t i;

    if (dev == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* VirtIO MMIO 寄存器写入 */
    /* 注意：这里只处理 VirtIO-RNG 特有的寄存器，通用 VirtIO 寄存器由 virtio.c 处理 */

    /* 配置空间写入 */
    if (offset >= 0x100U && offset < 0x100U + VIRTIO_RNG_CONFIG_SIZE)
    {
        config_ptr = (uint8_t *)&dev->config;
        offset -= 0x100U;

        if (offset + size > VIRTIO_RNG_CONFIG_SIZE)
        {
            return -(int32_t)EINVAL;
        }

        for (i = 0U; i < size; i++)
        {
            config_ptr[offset + i] = (uint8_t)(value >> (i * 8U));
        }

        return KERNEL_OK;
    }

    return -(int32_t)EINVAL;
}

/**
 * @brief VirtIO-RNG 读回调
 *
 * @param vm_id       VM ID
 * @param vcpu_id     vCPU ID
 * @param offset      MMIO 偏移
 * @param op          操作类型
 * @param value       读/写值
 * @param size        访问宽度（字节）
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t virtio_rng_read_cb(uint32_t vm_id, uint32_t vcpu_id,
                                          uint64_t offset, mmio_op_t op,
                                          uint64_t *value, uint32_t size)
{
    virtio_rng_dev_t *dev;
    virtio_device_t *vdev;

    (void)vcpu_id;

    if (op != MMIO_READ)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找 VirtIO 设备 */
    vdev = vmm_get_vdevice(vm_id, VMM_VDEV_MMIO_BASE);
    if (vdev == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 查找 VirtIO-RNG 设备 */
    dev = (virtio_rng_dev_t *)vdev->priv;
    if (dev == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* MMIO 读操作 */
    return rng_dev_mmio_read(dev, offset, value, size);
}

/**
 * @brief VirtIO-RNG 写回调
 *
 * @param vm_id       VM ID
 * @param vcpu_id     vCPU ID
 * @param offset      MMIO 偏移
 * @param op          操作类型
 * @param value       读/写值
 * @param size        访问宽度（字节）
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t virtio_rng_write_cb(uint32_t vm_id, uint32_t vcpu_id,
                                             uint64_t offset, mmio_op_t op,
                                             uint64_t *value, uint32_t size)
{
    virtio_rng_dev_t *dev;
    virtio_device_t *vdev;

    (void)vcpu_id;

    if (op != MMIO_WRITE)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找 VirtIO 设备 */
    vdev = vmm_get_vdevice(vm_id, VMM_VDEV_MMIO_BASE);
    if (vdev == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 查找 VirtIO-RNG 设备 */
    dev = (virtio_rng_dev_t *)vdev->priv;
    if (dev == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* MMIO 写操作 */
    return rng_dev_mmio_write(dev, offset, *value, size);
}

/**
 * @brief 生成随机数（线性同余生成器）
 *
 * @param dev       设备指针
 * @param data      数据指针
 * @param len       数据长度
 *
 * @return 实际生成的字节数
 */
static uint32_t rng_generate_random(virtio_rng_dev_t *dev,
                                     uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint32_t *word_ptr;

    if (dev == NULL || data == NULL || len == 0U)
    {
        return 0U;
    }

    word_ptr = (uint32_t *)data;

    /* 按字生成随机数 */
    for (i = 0U; i < len / 4U; i++)
    {
        /* 线性同余生成器：state = (state * a + c) % m */
        dev->state = (dev->state * RNG_LCG_MULTIPLIER) + RNG_LCG_INCREMENT;
        word_ptr[i] = dev->state;
    }

    /* 处理剩余的字节 */
    for (i = 0U; i < len % 4U; i++)
    {
        dev->state = (dev->state * RNG_LCG_MULTIPLIER) + RNG_LCG_INCREMENT;
        data[(len / 4U) * 4U + i] = (uint8_t)(dev->state >> (i * 8U));
    }

    return len;
}

/**
 * @brief 获取当前时间（作为随机数种子）
 *
 * @return 当前时间戳
 */
static uint32_t rng_get_time(void)
{
    /* 简化实现：使用全局种子递增 */
    s_global_rng_seed += 0x1234U;
    return s_global_rng_seed;
}

/* ========================================================================
 * 公共 API - 全局初始化/销毁
 * ======================================================================== */

kernel_status_t virtio_rng_global_init(void)
{
    uint32_t i;

    if (s_rng_initialized)
    {
        return KERNEL_OK;
    }

    /* 初始化全局随机数种子 */
    s_global_rng_seed = 0x12345678U;

    /* 初始化设备表 */
    (void)memset(s_rng_devs, 0, sizeof(s_rng_devs));
    (void)memset(s_rng_devs_active, false, sizeof(s_rng_devs_active));

    /* 初始化每个设备 */
    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        s_rng_devs[i].dev_id = i;
        s_rng_devs[i].vm_id = VMM_MAX_VMS;
        s_rng_devs[i].active = false;
    }

    s_rng_initialized = true;
    return KERNEL_OK;
}

kernel_status_t virtio_rng_global_destroy(void)
{
    uint32_t i;

    if (!s_rng_initialized)
    {
        return KERNEL_OK;
    }

    /* 销毁所有活跃设备 */
    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        if (s_rng_devs_active[i])
        {
            (void)virtio_rng_destroy(i);
        }
    }

    s_rng_initialized = false;
    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 设备初始化/销毁
 * ======================================================================== */

int32_t virtio_rng_create(uint32_t vm_id, const char *name,
                          uint64_t entropy, uint64_t mmio_base)
{
    uint32_t i;
    virtio_rng_dev_t *dev;
    virtio_device_t *vdev;
    int32_t ret;

    if (!s_rng_initialized)
    {
        return -(int32_t)EPERM;
    }

    if (name == NULL)
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
        if (!s_rng_devs_active[i])
        {
            break;
        }
    }

    if (i >= VMM_MAX_VDEVICES)
    {
        return -(int32_t)ENOMEM;
    }

    dev = &s_rng_devs[i];

    /* 初始化设备 */
    (void)memset(dev, 0, sizeof(virtio_rng_dev_t));
    dev->dev_id = i;
    dev->vm_id = vm_id;
    dev->name[0] = '\0';
    (void)strncpy(dev->name, name, 15);
    dev->name[15] = '\0';
    dev->mmio_base = mmio_base;
    dev->mmio_size = VIRTIO_RNG_CONFIG_SIZE;
    dev->active = true;
    dev->num_vqs = VIRTIO_RNG_NUM_QUEUES;
    dev->config_size = VIRTIO_RNG_CONFIG_SIZE;

    /* 设置设备状态为 RESET */
    rng_dev_set_status(dev, 0x0U);

    /* 设置配置空间 */
    dev->config.entropy = entropy;

    /* 设置设备特性 */
    dev->features = 0U;  /* VirtIO-RNG 目前没有定义特性位 */

    /* 初始化队列 */
    for (i = 0U; i < VIRTIO_RNG_NUM_QUEUES; i++)
    {
        dev->vqs[i].state = VIRTIO_QUEUE_UNUSED;
        dev->vqs[i].size = VIRTIO_RNG_QUEUE_SIZE;
        dev->vqs[i].index = i;
    }

    /* 初始化随机数生成器 */
    dev->seed = rng_get_time();
    dev->state = dev->seed;

    /* 初始化熵缓冲区 */
    (void)memset(dev->entropy.buffer, 0, sizeof(dev->entropy.buffer));
    dev->entropy.count = 0U;

    /* 初始化统计信息 */
    dev->total_bytes = 0ULL;
    dev->requests = 0ULL;

    /* 设置回调函数 */
    dev->read_fn = virtio_rng_read_cb;
    dev->write_fn = virtio_rng_write_cb;

    /* 注册 VirtIO 设备 */
    ret = vmm_register_vdevice(vm_id, dev->dev_id, VIRTIO_DEVICE_RNG,
                                mmio_base, VIRTIO_RNG_CONFIG_SIZE,
                                dev->read_fn, dev->write_fn, dev);
    if (ret < 0)
    {
        return ret;
    }

    /* 标记为活跃 */
    s_rng_devs_active[i] = true;

    /* 更新统计信息 */
    vmm_stats_update_device(vm_id, VIRTIO_DEVICE_RNG, true);

    return (int32_t)i;
}

kernel_status_t virtio_rng_destroy(uint32_t dev_id)
{
    virtio_rng_dev_t *dev;
    virtio_device_t *vdev;

    dev = rng_dev_find(dev_id);
    if (dev == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 设置设备状态为 RESET */
    rng_dev_set_status(dev, 0x0U);

    /* 注销 VirtIO 设备 */
    vdev = vmm_get_vdevice(dev->vm_id, dev->mmio_base);
    if (vdev != NULL)
    {
        vdev->active = false;
    }

    /* 清空熵缓冲区 */
    (void)memset(dev->entropy.buffer, 0, sizeof(dev->entropy.buffer));
    dev->entropy.count = 0U;

    /* 更新统计信息 */
    vmm_stats_update_device(dev->vm_id, VIRTIO_DEVICE_RNG, false);

    /* 标记为非活跃 */
    s_rng_devs_active[dev_id] = false;

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 随机数生成
 * ======================================================================== */

int32_t virtio_rng_generate(uint32_t dev_id, uint8_t *data, uint32_t len)
{
    virtio_rng_dev_t *dev;
    uint32_t generated;

    dev = rng_dev_find(dev_id);
    if (dev == NULL || data == NULL || len == 0U)
    {
        return -(int32_t)EINVAL;
    }

    /* 生成随机数 */
    generated = rng_generate_random(dev, data, len);
    if (generated == 0U)
    {
        return -(int32_t)EIO;
    }

    /* 更新统计信息 */
    dev->total_bytes += generated;
    dev->requests++;

    return (int32_t)generated;
}

/* ========================================================================
 * 公共 API - 统计信息管理
 * ======================================================================== */

kernel_status_t virtio_rng_get_stats(uint32_t dev_id,
                                     uint64_t *total_bytes,
                                     uint64_t *requests)
{
    virtio_rng_dev_t *dev;

    dev = rng_dev_find(dev_id);
    if (dev == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 返回统计信息 */
    if (total_bytes != NULL)
    {
        *total_bytes = dev->total_bytes;
    }
    if (requests != NULL)
    {
        *requests = dev->requests;
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 随机数生成器重置
 * ======================================================================== */

kernel_status_t virtio_rng_reset(uint32_t dev_id, uint32_t seed)
{
    virtio_rng_dev_t *dev;

    dev = rng_dev_find(dev_id);
    if (dev == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 重置随机数生成器 */
    if (seed == 0U)
    {
        seed = rng_get_time();
    }

    dev->seed = seed;
    dev->state = seed;

    /* 清空熵缓冲区 */
    (void)memset(dev->entropy.buffer, 0, sizeof(dev->entropy.buffer));
    dev->entropy.count = 0U;

    return KERNEL_OK;
}
