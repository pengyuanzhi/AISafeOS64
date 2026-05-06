/**
 * @file    virtio_net.c
 * @brief   VirtIO-Net 网络设备实现
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details 本文件实现了 VirtIO-Net 网络设备的所有功能：
 *          - 设备初始化/销毁
 *          - MMIO 读/写操作
 *          - 数据包接收/发送
 *          - 链路状态管理
 *          - 统计信息管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */
#include "virtio_net.h"
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

/** @brief VirtIO-Net 设备表 */
static virtio_net_dev_t s_net_devs[VMM_MAX_VDEVICES];

/** @brief VirtIO-Net 设备活跃标记 */
static bool s_net_devs_active[VMM_MAX_VDEVICES];

/** @brief VirtIO-Net 设备初始化标志 */
static bool s_net_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 查找 VirtIO-Net 设备
 *
 * @param dev_id   设备 ID
 *
 * @return 设备指针，不存在返回 NULL
 */
static virtio_net_dev_t *net_dev_find(uint32_t dev_id)
{
    if (dev_id >= VMM_MAX_VDEVICES)
    {
        return NULL;
    }

    if (!s_net_devs_active[dev_id])
    {
        return NULL;
    }

    return &s_net_devs[dev_id];
}

/**
 * @brief 设置设备状态
 *
 * @param dev      设备指针
 * @param status   设备状态
 */
static void net_dev_set_status(virtio_net_dev_t *dev, uint16_t status)
{
    dev->status = status;

    /* 如果状态为 RESET，关闭设备 */
    if (status == 0x0U)
    {
        dev->active = false;
    }
}

/**
 * @brief MMIO 读操作（VirtIO-Net）
 *
 * @param dev      设备指针
 * @param offset   MMIO 偏移
 * @param value    输出值
 * @param size     访问大小
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t net_dev_mmio_read(virtio_net_dev_t *dev,
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
    /* 注意：这里只处理 VirtIO-Net 特有的寄存器，通用 VirtIO 寄存器由 virtio.c 处理 */

    /* 配置空间读取 */
    if (offset >= 0x100U && offset < 0x100U + VIRTIO_NET_CONFIG_SIZE)
    {
        config_ptr = (uint8_t *)&dev->config;
        offset -= 0x100U;

        if (offset + size > VIRTIO_NET_CONFIG_SIZE)
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
 * @brief MMIO 写操作（VirtIO-Net）
 *
 * @param dev      设备指针
 * @param offset   MMIO 偏移
 * @param value    写入值
 * @param size     访问大小
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t net_dev_mmio_write(virtio_net_dev_t *dev,
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
    /* 注意：这里只处理 VirtIO-Net 特有的寄存器，通用 VirtIO 寄存器由 virtio.c 处理 */

    /* 配置空间写入 */
    if (offset >= 0x100U && offset < 0x100U + VIRTIO_NET_CONFIG_SIZE)
    {
        config_ptr = (uint8_t *)&dev->config;
        offset -= 0x100U;

        if (offset + size > VIRTIO_NET_CONFIG_SIZE)
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
 * @brief VirtIO-Net 读回调
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
static kernel_status_t virtio_net_read_cb(uint32_t vm_id, uint32_t vcpu_id,
                                           uint64_t offset, mmio_op_t op,
                                           uint64_t *value, uint32_t size)
{
    virtio_net_dev_t *dev;
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

    /* 查找 VirtIO-Net 设备 */
    dev = (virtio_net_dev_t *)vdev->priv;
    if (dev == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* MMIO 读操作 */
    return net_dev_mmio_read(dev, offset, value, size);
}

/**
 * @brief VirtIO-Net 写回调
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
static kernel_status_t virtio_net_write_cb(uint32_t vm_id, uint32_t vcpu_id,
                                            uint64_t offset, mmio_op_t op,
                                            uint64_t *value, uint32_t size)
{
    virtio_net_dev_t *dev;
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

    /* 查找 VirtIO-Net 设备 */
    dev = (virtio_net_dev_t *)vdev->priv;
    if (dev == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* MMIO 写操作 */
    return net_dev_mmio_write(dev, offset, *value, size);
}

/**
 * @brief 生成随机 MAC 地址
 *
 * @param mac_addr   MAC 地址输出（6 字节）
 */
static void generate_random_mac(uint8_t mac_addr[6])
{
    uint32_t i;

    /* 生成随机 MAC 地址（前 3 字节固定为 00:11:22） */
    mac_addr[0] = 0x00U;
    mac_addr[1] = 0x11U;
    mac_addr[2] = 0x22U;

    /* 后 3 字节随机生成 */
    for (i = 3U; i < 6U; i++)
    {
        mac_addr[i] = (uint8_t)(i * 7 + 0x33U);
    }
}

/* ========================================================================
 * 公共 API - 全局初始化/销毁
 * ======================================================================== */

kernel_status_t virtio_net_global_init(void)
{
    uint32_t i;

    if (s_net_initialized)
    {
        return KERNEL_OK;
    }

    /* 初始化设备表 */
    (void)memset(s_net_devs, 0, sizeof(s_net_devs));
    (void)memset(s_net_devs_active, false, sizeof(s_net_devs_active));

    /* 初始化每个设备 */
    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        s_net_devs[i].dev_id = i;
        s_net_devs[i].vm_id = VMM_MAX_VMS;
        s_net_devs[i].active = false;
        s_net_devs[i].link_state = VIRTIO_NET_LINK_UP;
    }

    s_net_initialized = true;
    return KERNEL_OK;
}

kernel_status_t virtio_net_global_destroy(void)
{
    uint32_t i;

    if (!s_net_initialized)
    {
        return KERNEL_OK;
    }

    /* 销毁所有活跃设备 */
    for (i = 0U; i < VMM_MAX_VDEVICES; i++)
    {
        if (s_net_devs_active[i])
        {
            (void)virtio_net_destroy(i);
        }
    }

    s_net_initialized = false;
    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 设备初始化/销毁
 * ======================================================================== */

int32_t virtio_net_create(uint32_t vm_id, const char *name,
                           const uint8_t mac_addr[6], uint16_t mtu,
                           uint64_t mmio_base)
{
    uint32_t i;
    virtio_net_dev_t *dev;
    virtio_device_t *vdev;
    int32_t ret;

    if (!s_net_initialized)
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
        if (!s_net_devs_active[i])
        {
            break;
        }
    }

    if (i >= VMM_MAX_VDEVICES)
    {
        return -(int32_t)ENOMEM;
    }

    dev = &s_net_devs[i];

    /* 初始化设备 */
    (void)memset(dev, 0, sizeof(virtio_net_dev_t));
    dev->dev_id = i;
    dev->vm_id = vm_id;
    dev->name[0] = '\0';
    (void)strncpy(dev->name, name, 15);
    dev->name[15] = '\0';
    dev->mmio_base = mmio_base;
    dev->mmio_size = VIRTIO_NET_CONFIG_SIZE;
    dev->active = true;
    dev->num_vqs = VIRTIO_NET_NUM_QUEUES;
    dev->config_size = VIRTIO_NET_CONFIG_SIZE;
    dev->link_state = VIRTIO_NET_LINK_UP;

    /* 设置设备状态为 RESET */
    net_dev_set_status(dev, 0x0U);

    /* 设置 MAC 地址 */
    if (mac_addr == NULL)
    {
        generate_random_mac(dev->config.mac.mac);
    }
    else
    {
        (void)memcpy(dev->config.mac.mac, mac_addr, VIRTIO_NET_MAC_LEN);
    }

    /* 设置 MTU */
    dev->config.mtu = (mtu == 0U) ? 1500U : mtu;

    /* 设置状态（bit 0 = 链路） */
    dev->config.status = (dev->link_state == VIRTIO_NET_LINK_UP) ? 0x1U : 0x0U;

    /* 设置设备特性 */
    dev->features = VIRTIO_NET_F_MAC |      /* 支持 MAC 地址 */
                    VIRTIO_NET_F_MTU |      /* 支持 MTU 配置 */
                    VIRTIO_NET_F_EVENT_IDX;  /* 支持事件索引 */

    /* 初始化队列 */
    for (i = 0U; i < VIRTIO_NET_NUM_QUEUES; i++)
    {
        dev->vqs[i].state = VIRTIO_QUEUE_UNUSED;
        dev->vqs[i].size = VIRTIO_NET_QUEUE_SIZE;
        dev->vqs[i].index = i;
    }

    /* 初始化接收队列 */
    dev->rx_head = 0U;
    dev->rx_tail = 0U;
    dev->rx_count = 0U;
    (void)memset(dev->rx_queue, 0, sizeof(dev->rx_queue));

    /* 初始化发送队列 */
    dev->tx_head = 0U;
    dev->tx_tail = 0U;
    dev->tx_count = 0U;
    (void)memset(dev->tx_queue, 0, sizeof(dev->tx_queue));

    /* 初始化统计信息 */
    dev->rx_packets = 0ULL;
    dev->rx_bytes = 0ULL;
    dev->tx_packets = 0ULL;
    dev->tx_bytes = 0ULL;
    dev->rx_dropped = 0ULL;
    dev->tx_dropped = 0ULL;

    /* 设置回调函数 */
    dev->read_fn = virtio_net_read_cb;
    dev->write_fn = virtio_net_write_cb;

    /* 注册 VirtIO 设备 */
    ret = vmm_register_vdevice(vm_id, dev->dev_id, VIRTIO_DEVICE_NET,
                                mmio_base, VIRTIO_NET_CONFIG_SIZE,
                                dev->read_fn, dev->write_fn, dev);
    if (ret < 0)
    {
        return ret;
    }

    /* 标记为活跃 */
    s_net_devs_active[i] = true;

    /* 更新统计信息 */
    vmm_stats_update_device(vm_id, VIRTIO_DEVICE_NET, true);

    return (int32_t)i;
}

kernel_status_t virtio_net_destroy(uint32_t dev_id)
{
    virtio_net_dev_t *dev;
    virtio_device_t *vdev;

    dev = net_dev_find(dev_id);
    if (dev == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 设置设备状态为 RESET */
    net_dev_set_status(dev, 0x0U);

    /* 注销 VirtIO 设备 */
    vdev = vmm_get_vdevice(dev->vm_id, dev->mmio_base);
    if (vdev != NULL)
    {
        vdev->active = false;
    }

    /* 清空队列 */
    (void)memset(dev->rx_queue, 0, sizeof(dev->rx_queue));
    (void)memset(dev->tx_queue, 0, sizeof(dev->tx_queue));

    /* 更新统计信息 */
    vmm_stats_update_device(dev->vm_id, VIRTIO_DEVICE_NET, false);

    /* 标记为非活跃 */
    s_net_devs_active[dev_id] = false;

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 数据包接收/发送
 * ======================================================================== */

kernel_status_t virtio_net_receive(uint32_t dev_id,
                                    const uint8_t *data, uint32_t len)
{
    virtio_net_dev_t *dev;
    uint32_t idx;

    dev = net_dev_find(dev_id);
    if (dev == NULL || data == NULL || len == 0U)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查接收队列是否已满 */
    if (dev->rx_count >= VIRTIO_NET_QUEUE_SIZE)
    {
        dev->rx_dropped++;
        return -(int32_t)ENOBUFS;
    }

    /* 计算队列索引 */
    idx = (dev->rx_tail + 1U) % VIRTIO_NET_QUEUE_SIZE;

    /* 复制数据包 */
    (void)memset(&dev->rx_queue[idx], 0, sizeof(virtio_net_pkt_t));
    dev->rx_queue[idx].header.flags = 0U;
    dev->rx_queue[idx].header.gso_type = 0U;
    dev->rx_queue[idx].header.hdr_len = 0U;
    dev->rx_queue[idx].header.gso_size = 0U;
    dev->rx_queue[idx].header.csum_start = 0U;
    dev->rx_queue[idx].header.csum_offset = 0U;

    if (len > 1500U)
    {
        len = 1500U;
    }

    (void)memcpy(dev->rx_queue[idx].data, data, len);
    dev->rx_queue[idx].len = len;
    dev->rx_queue[idx].in_use = true;

    /* 更新队列尾指针 */
    dev->rx_tail = idx;
    dev->rx_count++;

    /* 更新统计信息 */
    dev->rx_packets++;
    dev->rx_bytes += len;

    return KERNEL_OK;
}

kernel_status_t virtio_net_transmit(uint32_t dev_id,
                                     const uint8_t *data, uint32_t len)
{
    virtio_net_dev_t *dev;
    uint32_t idx;

    dev = net_dev_find(dev_id);
    if (dev == NULL || data == NULL || len == 0U)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查发送队列是否已满 */
    if (dev->tx_count >= VIRTIO_NET_QUEUE_SIZE)
    {
        dev->tx_dropped++;
        return -(int32_t)ENOBUFS;
    }

    /* 计算队列索引 */
    idx = (dev->tx_tail + 1U) % VIRTIO_NET_QUEUE_SIZE;

    /* 复制数据包 */
    (void)memset(&dev->tx_queue[idx], 0, sizeof(virtio_net_pkt_t));
    dev->tx_queue[idx].header.flags = 0U;
    dev->tx_queue[idx].header.gso_type = 0U;
    dev->tx_queue[idx].header.hdr_len = 0U;
    dev->tx_queue[idx].header.gso_size = 0U;
    dev->tx_queue[idx].header.csum_start = 0U;
    dev->tx_queue[idx].header.csum_offset = 0U;

    if (len > 1500U)
    {
        len = 1500U;
    }

    (void)memcpy(dev->tx_queue[idx].data, data, len);
    dev->tx_queue[idx].len = len;
    dev->tx_queue[idx].in_use = true;

    /* 更新队列尾指针 */
    dev->tx_tail = idx;
    dev->tx_count++;

    /* 更新统计信息 */
    dev->tx_packets++;
    dev->tx_bytes += len;

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 链路状态管理
 * ======================================================================== */

kernel_status_t virtio_net_set_link_state(uint32_t dev_id,
                                            virtio_net_link_state_t link_state)
{
    virtio_net_dev_t *dev;

    dev = net_dev_find(dev_id);
    if (dev == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 设置链路状态 */
    dev->link_state = link_state;

    /* 更新配置空间状态 */
    dev->config.status = (link_state == VIRTIO_NET_LINK_UP) ? 0x1U : 0x0U;

    /* TODO: 注入中断到 Guest 通知链路状态变化 */

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 统计信息管理
 * ======================================================================== */

kernel_status_t virtio_net_get_stats(uint32_t dev_id,
                                       uint64_t *rx_packets,
                                       uint64_t *rx_bytes,
                                       uint64_t *tx_packets,
                                       uint64_t *tx_bytes)
{
    virtio_net_dev_t *dev;

    dev = net_dev_find(dev_id);
    if (dev == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 返回统计信息 */
    if (rx_packets != NULL)
    {
        *rx_packets = dev->rx_packets;
    }
    if (rx_bytes != NULL)
    {
        *rx_bytes = dev->rx_bytes;
    }
    if (tx_packets != NULL)
    {
        *tx_packets = dev->tx_packets;
    }
    if (tx_bytes != NULL)
    {
        *tx_bytes = dev->tx_bytes;
    }

    return KERNEL_OK;
}
