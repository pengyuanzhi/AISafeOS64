/**
 * @file    virtio.h
 * @brief   VirtIO 设备接口
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 本文件定义了 VirtIO 设备相关数据结构和接口：
 *          - 设备类型枚举
 *          - VirtIO 队列
 *          - VirtIO 设备描述符
 *          - 公共 API 接口
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_VMM_DEVICE_VIRTIO_H
#define SERVICES_VMM_DEVICE_VIRTIO_H

#include <kernel/types.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大 VirtIO 队列数 */
#define VIRTIO_MAX_QUEUES          32U

/** @brief 最大虚拟设备数 */
#define VMM_MAX_VDEVICES           8U

/** @brief VirtIO MMIO 区域基址 */
#define VMM_VDEV_MMIO_BASE         0x09000000ULL

/** @brief VirtIO MMIO 区域大小 */
#define VMM_VDEV_MMIO_SIZE         0x00010000ULL

/* ========================================================================
 * 设备类型枚举
 * ======================================================================== */

/**
 * @brief VirtIO 设备类型
 */
typedef enum
{
    VIRTIO_DEVICE_BLOCK = 0U,        /**< @brief VirtIO-Block 块设备 */
    VIRTIO_DEVICE_NET,               /**< @brief VirtIO-Net 网卡 */
    VIRTIO_DEVICE_CONSOLE,           /**< @brief VirtIO-Console 控制台 */
    VIRTIO_DEVICE_RNG,               /**< @brief VirtIO-RNG 随机数 */
    VIRTIO_DEVICE_BALLOON,           /**< @brief VirtIO-Balloon 内存管理 */
    VIRTIO_DEVICE_DEVICE_ID_COUNT    /**< @brief 设备类型计数 */
} virtio_device_type_t;

/* ========================================================================
 * VirtIO 队列描述符
 * ======================================================================== */

/**
 * @brief VirtIO 队列状态
 */
typedef enum
{
    VIRTIO_QUEUE_UNUSED = 0U,        /**< @brief 未使用 */
    VIRTIO_QUEUE_SUSPENDED,          /**< @brief 暂停 */
    VIRTIO_QUEUE_READY,              /**< @brief 就绪 */
    VIRTIO_QUEUE_BLOCKED             /**< @brief 阻塞 */
} virtio_queue_state_t;

/**
 * @brief VirtIO 队列描述符
 *
 * @details VirtIO 队列描述符包含：
 *          - 队列大小
 *          - 队列索引
 *          - 描述符指针
 *          - 状态
 */
typedef struct
{
    /** @brief 队列状态 */
    virtio_queue_state_t state;

    /** @brief 队列大小 */
    uint16_t size;

    /** @brief 队列索引 */
    uint16_t index;

    /** @brief 描述符地址 */
    uint64_t desc_addr;

    /** @brief 可用指针地址 */
    uint64_t avail_addr;

    /** @brief 使用指针地址 */
    uint64_t used_addr;

    /** @brief 当前队列索引 */
    uint16_t cur_idx;

    /** @brief 可用索引 */
    uint16_t avail_idx;

    /** @brief 使用索引 */
    uint16_t used_idx;

} virtio_queue_t;

/* ========================================================================
 * VirtIO 设备描述符
 * ======================================================================== */

/**
 * @brief VirtIO MMIO 访问操作类型
 */
typedef enum
{
    MMIO_READ = 0U,                  /**< @brief 读操作 */
    MMIO_WRITE                       /**< @brief 写操作 */
} mmio_op_t;

/**
 * @brief VirtIO 设备操作回调
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
typedef kernel_status_t (*vdev_op_fn)(uint32_t vm_id, uint32_t vcpu_id,
                                       uint64_t offset, mmio_op_t op,
                                       uint64_t *value, uint32_t size);

/**
 * @brief VirtIO 设备描述符
 *
 * @details 每个 VirtIO 设备对应一个描述符，包含：
 *          - 设备基本信息
 *          - VirtIO 队列数组
 *          - 设备特性
 *          - 设备状态
 *          - MMIO 区域
 *          - 设备操作
 */
typedef struct
{
    /** @brief 基本信息 */
    uint32_t         dev_id;             /**< @brief 设备 ID */
    uint32_t         vm_id;              /**< @brief 所属 VM ID */
    virtio_device_type_t type;           /**< @brief 设备类型 */

    /** @brief VirtIO 队列 */
    virtio_queue_t   vqs[VIRTIO_MAX_QUEUES]; /**< @brief 队列数组 */
    uint32_t         num_vqs;            /**< @brief 队列数量 */
    uint32_t         queue_index;        /**< @brief 队列起始索引 */

    /** @brief 设备特性 */
    uint32_t         features;           /**< @brief 设备特性位图 */
    uint16_t         status;             /**< @brief 设备状态 */
    uint16_t         config_gen;         /**< @brief 配置版本号 */
    uint32_t         device_features;    /**< @brief 设备特性位图 */
    uint32_t         driver_features;    /**< @brief 驱动程序特性位图 */
    uint32_t         device_features_sel;/**< @brief 设备特性选择器 */
    uint32_t         driver_features_sel;/**< @brief 驱动程序特性选择器 */

    /** @brief 配置空间 */
    void*            config;             /**< @brief 配置空间指针 */
    uint32_t         config_size;        /**< @brief 配置空间大小 */

    /** @brief MMIO 区域 */
    uint64_t         mmio_base;          /**< @brief MMIO 基址 */
    uint64_t         mmio_size;          /**< @brief MMIO 大小 */

    /** @brief 设备操作 */
    bool             active;             /**< @brief 活跃标志 */
    vdev_op_fn       read_fn;            /**< @brief 读回调 */
    vdev_op_fn       write_fn;           /**< @brief 写回调 */
    void*            priv;               /**< @brief 设备私有数据 */
} virtio_device_t;

/* ========================================================================
 * VirtIO 模块 API 接口
 *
 * 注意：vmm_register_vdevice() 和 vmm_handle_mmio() 在 vmm.h 中声明
 * ======================================================================== */

/**
 * @brief 获取 VirtIO 设备
 *
 * @param vm_id       VM ID
 * @param mmio_base   MMIO 基址
 *
 * @return 设备指针，不存在返回 NULL
 */
virtio_device_t *vmm_get_vdevice(uint32_t vm_id, uint64_t mmio_base);

/**
 * @brief 刷新 VirtIO 队列
 *
 * @param vm_id       VM ID
 * @param vcpu_id     vCPU ID
 * @param queue_idx   队列索引
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_virtio_kick(uint32_t vm_id, uint32_t vcpu_id,
                                 uint32_t queue_idx);

#endif /* SERVICES_VMM_DEVICE_VIRTIO_H */
