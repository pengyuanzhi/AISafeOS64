/**
 * @file    virtio_console.h
 * @brief   VirtIO-Console 控制台设备定义
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details 本文件定义了 VirtIO-Console 控制台设备的数据结构和接口：
 *          - 控制台设备描述符
 *          - 控制台设备配置空间
 *          - 公共 API 接口
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_VMM_DEVICE_VIRTIO_CONSOLE_H
#define SERVICES_VMM_DEVICE_VIRTIO_CONSOLE_H

#include <kernel/types.h>
#include <stdint.h>
#include <stdbool.h>
#include "virtio.h"

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief VirtIO-Console 队列数量（rxq + txq） */
#define VIRTIO_CONSOLE_NUM_QUEUES        2U

/** @brief VirtIO-Console 队列大小 */
#define VIRTIO_CONSOLE_QUEUE_SIZE        128U

/** @brief VirtIO-Console 配置空间大小 */
#define VIRTIO_CONSOLE_CONFIG_SIZE       16U

/** @brief 最大控制台端口数 */
#define VIRTIO_CONSOLE_MAX_PORTS         1U

/** @brief 控制台缓冲区大小 */
#define VIRTIO_CONSOLE_BUFFER_SIZE      4096U

/* ========================================================================
 * 设备特性位
 * ======================================================================== */

/** @brief 设备支持多个端口 */
#define VIRTIO_CONSOLE_F_MULTIPORT      (1U << 0)

/** @brief 设备支持紧急写入 */
#define VIRTIO_CONSOLE_F_EMERG_WRITE    (1U << 1)

/** @brief 设备支持大小 */
#define VIRTIO_CONSOLE_F_SIZE           (1U << 2)

/* ========================================================================
 * 控制台设备队列索引
 * ======================================================================== */

/** @brief 接收队列索引 */
#define VIRTIO_CONSOLE_RXQ_IDX          0U

/** @brief 发送队列索引 */
#define VIRTIO_CONSOLE_TXQ_IDX          1U

/* ========================================================================
 * 控制台设备配置空间
 * ======================================================================== */

/**
 * @brief VirtIO-Console 控制台配置
 */
typedef struct
{
    uint16_t cols;                      /**< @brief 列数 */
    uint16_t rows;                      /**< @brief 行数 */
    uint32_t max_nr_ports;              /**< @brief 最大端口数 */
    uint32_t emerg_wr;                   /**< @brief 紧急写入 */
} virtio_console_config_t;

/* ========================================================================
 * 控制台设备缓冲区
 * ======================================================================== */

/**
 * @brief VirtIO-Console 控制台缓冲区
 */
typedef struct
{
    uint8_t buffer[VIRTIO_CONSOLE_BUFFER_SIZE]; /**< @brief 缓冲区 */
    uint32_t head;                   /**< @brief 缓冲区头 */
    uint32_t tail;                   /**< @brief 缓冲区尾 */
    uint32_t count;                  /**< @brief 缓冲区计数 */
} virtio_console_buf_t;

/* ========================================================================
 * 控制台设备描述符
 * ======================================================================== */

/**
 * @brief VirtIO-Console 控制台设备描述符
 *
 * @details 包含控制台设备的完整状态：
 *          - 设备基本信息
 *          - 配置空间
 *          - VirtIO 队列
 *          - 缓冲区
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
    virtio_console_config_t config;  /**< @brief 配置空间 */
    uint32_t         config_size;     /**< @brief 配置空间大小 */

    /** @brief VirtIO 队列 */
    virtio_queue_t   vqs[VIRTIO_CONSOLE_NUM_QUEUES]; /**< @brief 队列数组 */
    uint32_t         num_vqs;         /**< @brief 队列数量 */

    /** @brief 设备特性 */
    uint32_t         features;        /**< @brief 设备特性位图 */
    uint16_t         status;          /**< @brief 设备状态 */

    /** @brief MMIO 区域 */
    uint64_t         mmio_base;       /**< @brief MMIO 基址 */
    uint64_t         mmio_size;       /**< @brief MMIO 大小 */

    /** @brief 缓冲区 */
    virtio_console_buf_t rx_buf;     /**< @brief 接收缓冲区 */
    virtio_console_buf_t tx_buf;     /**< @brief 发送缓冲区 */

    /** @brief 统计信息 */
    uint64_t         rx_bytes;        /**< @brief 接收字节数 */
    uint64_t         tx_bytes;        /**< @brief 发送字节数 */
    uint64_t         rx_dropped;      /**< @brief 丢弃接收字节数 */
    uint64_t         tx_dropped;      /**< @brief 丢弃发送字节数 */

    /** @brief 设备操作 */
    vdev_op_fn       read_fn;          /**< @brief 读回调 */
    vdev_op_fn       write_fn;         /**< @brief 写回调 */
    void*            priv;            /**< @brief 设备私有数据 */
} virtio_console_dev_t;

/* ========================================================================
 * VirtIO-Console 模块 API 接口
 * ======================================================================== */

/**
 * @brief 初始化 VirtIO-Console 控制台设备模块
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_console_global_init(void);

/**
 * @brief 销毁 VirtIO-Console 控制台设备模块
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_console_global_destroy(void);

/**
 * @brief 创建 VirtIO-Console 控制台设备
 *
 * @param vm_id       VM ID
 * @param name        设备名称
 * @param cols        列数
 * @param rows        行数
 * @param mmio_base   MMIO 基址
 *
 * @return 设备 ID，负数表示错误
 */
int32_t virtio_console_create(uint32_t vm_id, const char *name,
                               uint16_t cols, uint16_t rows,
                               uint64_t mmio_base);

/**
 * @brief 销毁 VirtIO-Console 控制台设备
 *
 * @param dev_id      设备 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_console_destroy(uint32_t dev_id);

/**
 * @brief 接收数据（后端 → Guest）
 *
 * @param dev_id      设备 ID
 * @param data        数据指针
 * @param len         数据长度
 *
 * @return 实际接收的字节数，负数表示错误
 */
int32_t virtio_console_receive(uint32_t dev_id,
                                  const uint8_t *data, uint32_t len);

/**
 * @brief 发送数据（Guest → 后端）
 *
 * @param dev_id      设备 ID
 * @param data        数据指针
 * @param len         数据长度
 *
 * @return 实际发送的字节数，负数表示错误
 */
int32_t virtio_console_transmit(uint32_t dev_id,
                                  uint8_t *data, uint32_t len);

/**
 * @brief 获取设备统计信息
 *
 * @param dev_id      设备 ID
 * @param rx_bytes    输出接收字节数
 * @param tx_bytes    输出发送字节数
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_console_get_stats(uint32_t dev_id,
                                          uint64_t *rx_bytes,
                                          uint64_t *tx_bytes);

/**
 * @brief 设置控制台大小
 *
 * @param dev_id      设备 ID
 * @param cols        列数
 * @param rows        行数
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t virtio_console_set_size(uint32_t dev_id,
                                         uint16_t cols, uint16_t rows);

#endif /* SERVICES_VMM_DEVICE_VIRTIO_CONSOLE_H */
