/**
 * @file    virtio_rng.h
 * @brief   VirtIO-RNG 随机数设备定义
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details 本文件定义了 VirtIO-RNG 随机数设备的数据结构和接口：
 *          - 随机数设备描述符
 *          - 随机数设备配置空间
 *          - 公共 API 接口
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_VMM_DEVICE_VIRTIO_RNG_H
#define SERVICES_VMM_DEVICE_VIRTIO_RNG_H

#include <kernel/types.h>
#include <stdint.h>
#include <stdbool.h>
#include "virtio.h"

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief VirtIO-RNG 队列数量（仅 1 个队列） */
#define VIRTIO_RNG_NUM_QUEUES          1U

/** @brief VirtIO-RNG 队列大小 */
#define VIRTIO_RNG_QUEUE_SIZE          64U

/** @brief VirtIO-RNG 配置空间大小 */
#define VIRTIO_RNG_CONFIG_SIZE         8U

/** @brief 最大熵缓冲区大小 */
#define VIRTIO_RNG_MAX_ENTROPY         256U

/* ========================================================================
 * 设备特性位
 * ======================================================================== */

/* 目前没有定义 VirtIO-RNG 特性位 */

/* ========================================================================
 * 随机数设备队列索引
 * ======================================================================== */

/** @brief 随机数队列索引 */
#define VIRTIO_RNG_REQQ_IDX            0U

/* ========================================================================
 * 随机数设备配置空间
 * ======================================================================== */

/**
 * @brief VirtIO-RNG 配置
 */
typedef struct
{
    uint64_t entropy;                   /**< @brief 熵值（微秒） */
} virtio_rng_config_t;

/* ========================================================================
 * 随机数设备熵缓冲区
 * ======================================================================== */

/**
 * @brief VirtIO-RNG 熵缓冲区
 */
typedef struct
{
    uint8_t buffer[VIRTIO_RNG_MAX_ENTROPY]; /**< @brief 熵缓冲区 */
    uint32_t count;                    /**< @brief 缓冲区计数 */
} virtio_rng_entropy_t;

/* ========================================================================
 * 随机数设备描述符
 * ======================================================================== */

/**
 * @brief VirtIO-RNG 随机数设备描述符
 *
 * @details 包含随机数设备的完整状态：
 *          - 设备基本信息
 *          - 配置空间
 *          - VirtIO 队列
 *          - 熵缓冲区
 *          - 设备特性
 */
typedef struct
{
    /** @brief 基本信息 */
    uint32_t         dev_id;           /**< @brief 设备 ID */
    uint32_t         vm_id;            /**< @brief 所属 VM ID */
    char             name[16];         /**< @brief 设备名称 */
    bool             active;           /**< @brief 活跃标志 */

    /** @brief 配置空间 */
    virtio_rng_config_t config;        /**< @brief 配置空间 */
    uint32_t         config_size;     /**< @brief 配置空间大小 */

    /** @brief VirtIO 队列 */
    virtio_queue_t   vqs[VIRTIO_RNG_NUM_QUEUES]; /**< @brief 队列数组 */
    uint32_t         num_vqs;         /**< @brief 队列数量 */

    /** @brief 设备特性 */
    uint32_t         features;        /**< @brief 设备特性位图 */
    uint16_t         status;          /**< @brief 设备状态 */

    /** @brief MMIO 区域 */
    uint64_t         mmio_base;       /**< @brief MMIO 基址 */
    uint64_t         mmio_size;       /**< @brief MMIO 大小 */

    /** @brief 熵缓冲区 */
    virtio_rng_entropy_t entropy;     /**< @brief 熵缓冲区 */

    /** @brief 随机数生成器状态 */
    uint32_t         seed;            /**< @brief 随机数种子 */
    uint32_t         state;           /**< @brief 随机数生成器状态 */

    /** @brief 统计信息 */
    uint64_t         total_bytes;     /**< @brief 总共生成的字节数 */
    uint64_t         requests;        /**< @brief 请求数 */

    /** @brief 设备操作 */
    vdev_op_fn       read_fn;          /**< @brief 读回调 */
    vdev_op_fn       write_fn;         /**< @brief 写回调 */
    void*            priv;            /**< @brief 设备私有数据 */
} virtio_rng_dev_t;

/* ========================================================================
 * VirtIO-RNG 模块 API 接口
 * ======================================================================== */

/**
 * @brief 初始化 VirtIO-RNG 随机数设备模块
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_rng_global_init(void);

/**
 * @brief 销毁 VirtIO-RNG 随机数设备模块
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_rng_global_destroy(void);

/**
 * @brief 创建 VirtIO-RNG 随机数设备
 *
 * @param vm_id       VM ID
 * @param name        设备名称
 * @param entropy     熵值（微秒）
 * @param mmio_base   MMIO 基址
 *
 * @return 设备 ID，负数表示错误
 */
int32_t virtio_rng_create(uint32_t vm_id, const char *name,
                          uint64_t entropy, uint64_t mmio_base);

/**
 * @brief 销毁 VirtIO-RNG 随机数设备
 *
 * @param dev_id      设备 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_rng_destroy(uint32_t dev_id);

/**
 * @brief 生成随机数（后端 → Guest）
 *
 * @param dev_id      设备 ID
 * @param data        数据指针
 * @param len         数据长度
 *
 * @return 实际生成的字节数，负数表示错误
 */
int32_t virtio_rng_generate(uint32_t dev_id, uint8_t *data, uint32_t len);

/**
 * @brief 获取设备统计信息
 *
 * @param dev_id      设备 ID
 * @param total_bytes 输出总共生成的字节数
 * @param requests    输出请求数
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_rng_get_stats(uint32_t dev_id,
                                     uint64_t *total_bytes,
                                     uint64_t *requests);

/**
 * @brief 重置随机数生成器
 *
 * @param dev_id      设备 ID
 * @param seed        随机数种子（0 表示使用当前时间）
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_rng_reset(uint32_t dev_id, uint32_t seed);

#endif /* SERVICES_VMM_DEVICE_VIRTIO_RNG_H */
