/**
 * @file    virtio_console.c
 * @brief   VirtIO-Console 控制台设备实现
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details 本文件实现了 VirtIO-Console 控制台设备的所有功能：
 *          - 设备初始化/销毁
 *          - MMIO 读/写操作
 *          - 数据接收/发送
 *          - 统计信息管理
 *          - 控制台大小设置
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */
#include "virtio_console.h"
#include <stdint.h>
#include <string.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include "vmm.h"
#include "vmm_stats.h"

/* ========================================================================
 * 内部状态
 * ======================================================================== */

/** @brief VirtIO-Console 设备表 */
static virtio_console_dev_t s_console_devs[VMM_MAX_VDEVICES];

/** @brief VirtIO-Console 设备活跃标记 */
static bool s_console_devs_active[VMM_MAX_VDEVICES];

/** @brief VirtIO-Console 设备初始化标志 */
static bool s_console_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 查找 VirtIO-Console 设备
 *
 * @param dev_id   设备 ID
 *
 * @return 设备指针，不存在返回 NULL
 */
static virtio_console_dev_t *console_dev_find(uint32_t dev_id)
{
    if (dev_id >= VMM_MAX_VDEVICES)
    {
        return NULL;
    }

    if (!s_console_devs_active[dev_id])
    {
        return NULL;
    }

    return &s_console_devs[dev_id];
}

/**
 * @brief 设置设备状态
 *
 * @param dev      设备指针
 * @param status   设备状态
 */
static void console_dev_set_status(virtio_console_dev_t *dev, uint16_t status)
{
    dev->status = status;

    /* 如果状态为 RESET，关闭设备 */
    if (status == 0x0U)
    {
        dev->active = false;
    }
}

/**
 * @brief MMIO 读操作（VirtIO-Console）
 *
 * @param dev      设备指针
 * @param offset   MMIO 偏移
 * @param value    输出值
 * @param size     访问大小
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t console_dev_mmio_read(virtio_console_dev_t *dev,
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
    /* 注意：这里只处理 VirtIO-Console 特有的寄存器，通用 VirtIO 寄存器由 virtio.c 处理 */

    /* 配置空间读取 */
    if (offset >= 0x100U && offset < 0x100U + VIRTIO_CONSOLE_CONFIG_SIZE)
    {
        config_ptr = (uint8_t *)&dev->config;
        offset -= 0x100U;

        if (offset + size > VIRTIO_CONSOLE_CONFIG_SIZE)
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
 * @brief MMIO 写操作（VirtIO-Console）
 *
 * @param dev      设备指针
 * @param offset   MMIO 偏移
 * @param value    写入值
 * @param size     访问大小
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t console_dev_mmio_write(virtio_console_dev_t *dev,
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
    /* 注意：这里只处理 VirtIO-Console 特有的寄存器，通用 VirtIO 寄存器由 virtio.c 处理 */

    /* 配置空间写入 */
    if (offset >= 0x100U && offset < 0x100U + VIRTIO_CONSOLE_CONFIG_SIZE)
    {
        config_ptr = (uint8_t *)&dev->config;
        offset -= 0x100U;

        if (offset + size > VIRTIO_CONSOLE_CONFIG_SIZE)
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
 * @brief VirtIO-Console 读回调
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
static kernel_status_t virtio_console_read_cb(uint32_t vm_id, uint32_t vcpu_id,
                                                 uint64_t offset, mmio_op_t op,
                                                 uint64_t *value, uint32_t size)
{
    virtio_console_dev_t *dev;
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

    /* 查找 VirtIO-Console 设备 */
    dev = (virtio_console_dev_t *)vdev->priv;
    if (dev == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* MMIO 读操作 */
    return console_dev_mmio_read(dev, offset, value, size);
}

/**
 * @brief VirtIO-Console 写回调
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
static kernel_status_t virtio_console_write_cb(uint32_t vm_id, uint32_t vcpu_id,
                                                  uint64_t offset, mmio_op_t op,
                                                  uint64_t *value, uint32_t size)
{
    virtio_console_dev_t *dev;
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

    /* 查找 VirtIO-Console 设备 */
    dev = (virtio_console_dev_t *)vdev->priv;
    if (dev == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* MMIO 写操作 */
    return console_dev_mmio_write(dev, offset, *value, size);
}

/**
 * @brief 初始化缓冲区
 *
 * @param buf       缓冲区指针
 */
static void buffer_init(virtio_console_buf_t *buf)
{
    if (buf == NULL)
    {
        return;
    }

    (void)memset(buf->buffer, 0, sizeof(buf->buffer));
    buf->head = 0U;
    buf->tail = 0U;
    buf->count = 0U;
}

/**
 * @brief 向缓冲区写入数据
 *
 * @param buf       缓冲区指针
 * @param data      数据指针
 * @param len       数据长度
 *
 * @return 实际写入的字节数
 */
static uint32_t buffer_write(virtio_console_buf_t *buf,
                               const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint32_t avail;
    uint32_t written;

    if (buf == NULL || data == NULL || len == 0U)
    {
        return 0U;
    }

    /* 计算可用空间 */
    avail = VIRTIO_CONSOLE_BUFFER_SIZE - buf->count;
    if (len > avail)
    {
        len = avail;
    }

    written = 0U;
    for (i = 0U; i < len; i++)
    {
        buf->buffer[buf->tail] = data[i];
        buf->tail = (buf->tail + 1U) % VIRTIO_CONSOLE_BUFFER_SIZE;
        buf->count++;
        written++;
    }

    return written;
}

/**
 * @brief 从缓冲区读取数据
 *
 * @param buf       缓冲区指针
 * @param data      数据指针
 * @param len       数据长度
 *
 * @return 实际读取的字节数
 */
static uint32_t buffer_read(virtio_console_buf_t *buf,
                              uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint32_t read_count;

    if (buf == NULL || data == NULL || len == 0U)
    {
        return 0U;
    }

    /* 计算可读取的数据量 */
    if (len > buf->count)
    {
        len = buf->count;
    }

    read_count = 0U;
    for (i = 0U; i < len; i++)
    {
        data[i] = buf->buffer[buf->head];
        buf->head = (buf->head + 1U) % VIRTIO_CONSOLE_BUFFER_SIZE;
        buf->count--;
        read_count++;
    }

    return read_count;
}

/* ========================================================================
 * 公共 API - 全局初始化/销毁
 * ======================================================================== */

kernel_status_t virtio_console_global_init(void)
{
    uint32_t i;

    if (s_console_initialized)
    {
        return KERNEL_OK;
    }

    /* 初始化设备表 */
    (void)memset(s_console_devs, 0, sizeof(s_console_devs));
    (void)memset(s_console_devs_active, false, sizeof(s_console_devs_active));

    /* 初始化每个设备 */
    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        s_console_devs[i].dev_id = i;
        s_console_devs[i].vm_id = VMM_MAX_VMS;
        s_console_devs[i].active = false;
    }

    s_console_initialized = true;
    return KERNEL_OK;
}

kernel_status_t virtio_console_global_destroy(void)
{
    uint32_t i;

    if (!s_console_initialized)
    {
        return KERNEL_OK;
    }

    /* 销毁所有活跃设备 */
    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        if (s_console_devs_active[i])
        {
            (void)virtio_console_destroy(i);
        }
    }

    s_console_initialized = false;
    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 设备初始化/销毁
 * ======================================================================== */

int32_t virtio_console_create(uint32_t vm_id, const char *name,
                               uint16_t cols, uint16_t rows,
                               uint64_t mmio_base)
{
    uint32_t i;
    virtio_console_dev_t *dev;
    virtio_device_t *vdev;
    int32_t ret;

    if (!s_console_initialized)
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
        if (!s_console_devs_active[i])
        {
            break;
        }
    }

    if (i >= VMM_MAX_VDEVICES)
    {
        return -(int32_t)ENOMEM;
    }

    dev = &s_console_devs[i];

    /* 初始化设备 */
    (void)memset(dev, 0, sizeof(virtio_console_dev_t));
    dev->dev_id = i;
    dev->vm_id = vm_id;
    dev->name[0] = '\0';
    (void)strncpy(dev->name, name, 15);
    dev->name[15] = '\0';
    dev->mmio_base = mmio_base;
    dev->mmio_size = VIRTIO_CONSOLE_CONFIG_SIZE;
    dev->active = true;
    dev->num_vqs = VIRTIO_CONSOLE_NUM_QUEUES;
    dev->config_size = VIRTIO_CONSOLE_CONFIG_SIZE;

    /* 设置设备状态为 RESET */
    console_dev_set_status(dev, 0x0U);

    /* 设置配置空间 */
    dev->config.cols = (cols == 0U) ? 80U : cols;
    dev->config.rows = (rows == 0U) ? 25U : rows;
    dev->config.max_nr_ports = VIRTIO_CONSOLE_MAX_PORTS;
    dev->config.emerg_wr = 0U;

    /* 设置设备特性 */
    dev->features = VIRTIO_CONSOLE_F_SIZE;   /* 支持大小 */

    /* 初始化队列 */
    for (i = 0U; i < VIRTIO_CONSOLE_NUM_QUEUES; i++)
    {
        dev->vqs[i].state = VIRTIO_QUEUE_UNUSED;
        dev->vqs[i].size = VIRTIO_CONSOLE_QUEUE_SIZE;
        dev->vqs[i].index = i;
    }

    /* 初始化缓冲区 */
    buffer_init(&dev->rx_buf);
    buffer_init(&dev->tx_buf);

    /* 初始化统计信息 */
    dev->rx_bytes = 0ULL;
    dev->tx_bytes = 0ULL;
    dev->rx_dropped = 0ULL;
    dev->tx_dropped = 0ULL;

    /* 设置回调函数 */
    dev->read_fn = virtio_console_read_cb;
    dev->write_fn = virtio_console_write_cb;

    /* 注册 VirtIO 设备 */
    ret = vmm_register_vdevice(vm_id, dev->dev_id, VIRTIO_DEVICE_CONSOLE,
                                mmio_base, VIRTIO_CONSOLE_CONFIG_SIZE,
                                dev->read_fn, dev->write_fn, dev);
    if (ret < 0)
    {
        return ret;
    }

    /* 标记为活跃 */
    s_console_devs_active[i] = true;

    /* 更新统计信息 */
    vmm_stats_update_device(vm_id, VIRTIO_DEVICE_CONSOLE, true);

    return (int32_t)i;
}

kernel_status_t virtio_console_destroy(uint32_t dev_id)
{
    virtio_console_dev_t *dev;
    virtio_device_t *vdev;

    dev = console_dev_find(dev_id);
    if (dev == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 设置设备状态为 RESET */
    console_dev_set_status(dev, 0x0U);

    /* 注销 VirtIO 设备 */
    vdev = vmm_get_vdevice(dev->vm_id, dev->mmio_base);
    if (vdev != NULL)
    {
        vdev->active = false;
    }

    /* 清空缓冲区 */
    buffer_init(&dev->rx_buf);
    buffer_init(&dev->tx_buf);

    /* 更新统计信息 */
    vmm_stats_update_device(dev->vm_id, VIRTIO_DEVICE_CONSOLE, false);

    /* 标记为非活跃 */
    s_console_devs_active[dev_id] = false;

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 数据接收/发送
 * ======================================================================== */

int32_t virtio_console_receive(uint32_t dev_id,
                                  const uint8_t *data, uint32_t len)
{
    virtio_console_dev_t *dev;
    uint32_t written;

    dev = console_dev_find(dev_id);
    if (dev == NULL || data == NULL || len == 0U)
    {
        return -(int32_t)EINVAL;
    }

    /* 向接收缓冲区写入数据 */
    written = buffer_write(&dev->rx_buf, data, len);
    if (written == 0U)
    {
        dev->rx_dropped++;
        return -(int32_t)ENOBUFS;
    }

    /* 更新统计信息 */
    dev->rx_bytes += written;

    return (int32_t)written;
}

int32_t virtio_console_transmit(uint32_t dev_id,
                                  uint8_t *data, uint32_t len)
{
    virtio_console_dev_t *dev;
    uint32_t read_count;

    dev = console_dev_find(dev_id);
    if (dev == NULL || data == NULL || len == 0U)
    {
        return -(int32_t)EINVAL;
    }

    /* 从发送缓冲区读取数据 */
    read_count = buffer_read(&dev->tx_buf, data, len);
    if (read_count == 0U)
    {
        dev->tx_dropped++;
        return -(int32_t)ENODATA;
    }

    /* 更新统计信息 */
    dev->tx_bytes += read_count;

    return (int32_t)read_count;
}

/* ========================================================================
 * 公共 API - 统计信息管理
 * ======================================================================== */

kernel_status_t virtio_console_get_stats(uint32_t dev_id,
                                          uint64_t *rx_bytes,
                                          uint64_t *tx_bytes)
{
    virtio_console_dev_t *dev;

    dev = console_dev_find(dev_id);
    if (dev == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 返回统计信息 */
    if (rx_bytes != NULL)
    {
        *rx_bytes = dev->rx_bytes;
    }
    if (tx_bytes != NULL)
    {
        *tx_bytes = dev->tx_bytes;
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 控制台大小设置
 * ======================================================================== */

kernel_status_t virtio_console_set_size(uint32_t dev_id,
                                         uint16_t cols, uint16_t rows)
{
    virtio_console_dev_t *dev;

    dev = console_dev_find(dev_id);
    if (dev == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 设置控制台大小 */
    dev->config.cols = (cols == 0U) ? 80U : cols;
    dev->config.rows = (rows == 0U) ? 25U : rows;

    /* TODO: 注入中断到 Guest 通知大小变化 */

    return KERNEL_OK;
}
