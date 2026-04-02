/**
 * @file    driver_framework.h
 * @brief   用户态驱动框架接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了用户态驱动框架接口：
 *          - MMIO 映射（通过能力授权）
 *          - DMA 缓冲区管理
 *          - 中断绑定与处理
 *          - 驱动注册/注销
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: DR-001~005
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_DRIVER_FRAMEWORK_H
#define KERNEL_DRIVER_FRAMEWORK_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 驱动类型定义
 * ======================================================================== */

/** @brief 驱动操作结果 */
typedef enum
{
    DRIVER_OK = 0,                   /**< @brief 操作成功 */
    DRIVER_ERROR = -1,               /**< @brief 通用错误 */
    DRIVER_NO_RESOURCE = -2,         /**< @brief 资源不足 */
    DRIVER_NOT_FOUND = -3,           /**< @brief 未找到设备 */
    DRIVER_BUSY = -4,                /**< @brief 设备忙 */
    DRIVER_TIMEOUT = -5,             /**< @brief 超时 */
    DRIVER_INVALID_PARAM = -6        /**< @brief 参数无效 */
} driver_result_t;

/**
 * @brief 驱动设备信息
 */
typedef struct
{
    uint32_t    device_id;           /**< @brief 设备 ID */
    uint32_t    vendor_id;           /**< @brief 厂商 ID */
    uint32_t    device_type;         /**< @brief 设备类型 */
    paddr_t     mmio_base;           /**< @brief MMIO 基地址 */
    uint64_t    mmio_size;           /**< @brief MMIO 大小 */
    uint32_t    irq_number;          /**< @brief 中断号 */
    char        name[32];            /**< @brief 设备名 */
} device_info_t;

/**
 * @brief DMA 缓冲区描述符
 */
typedef struct
{
    vaddr_t     virt_addr;           /**< @brief 虚拟地址 */
    paddr_t     phys_addr;           /**< @brief 物理地址 */
    uint64_t    size;                /**< @brief 大小 */
    uint32_t    handle;              /**< @brief 缓冲区句柄 */
} dma_buffer_t;

/* ========================================================================
 * 驱动操作函数表
 * ======================================================================== */

/**
 * @brief 驱动操作函数表
 *
 * @details 驱动通过实现此函数表来注册其操作。
 */
typedef struct
{
    /**
     * @brief 初始化驱动
     *
     * @param device_info 设备信息
     *
     * @return DRIVER_OK 成功
     */
    driver_result_t (*init)(const device_info_t *device_info);

    /**
     * @brief 关闭驱动
     */
    void (*deinit)(void);

    /**
     * @brief 打开设备
     *
     * @param flags 打开标志
     *
     * @return DRIVER_OK 成功
     */
    driver_result_t (*open)(uint32_t flags);

    /**
     * @brief 关闭设备
     */
    void (*close)(void);

    /**
     * @brief 读取数据
     *
     * @param buf   缓冲区
     * @param size  大小
     * @param offset 偏移
     *
     * @return 读取的字节数，负数表示错误
     */
    int64_t (*read)(void *buf, uint64_t size, uint64_t offset);

    /**
     * @brief 写入数据
     *
     * @param buf   缓冲区
     * @param size  大小
     * @param offset 偏移
     *
     * @return 写入的字节数，负数表示错误
     */
    int64_t (*write)(const void *buf, uint64_t size, uint64_t offset);

    /**
     * @brief I/O 控制
     *
     * @param cmd   命令
     * @param arg   参数
     *
     * @return DRIVER_OK 成功
     */
    driver_result_t (*ioctl)(uint32_t cmd, void *arg);

    /**
     * @brief 中断处理（上半部，在中断上下文调用）
     *
     * @param irq 中断号
     */
    void (*interrupt_handler)(uint32_t irq);
} driver_ops_t;

/* ========================================================================
 * 驱动框架 API
 * ======================================================================== */

/**
 * @brief 注册驱动
 *
 * @param name 驱动名称
 * @param ops  驱动操作函数表
 *
 * @return DRIVER_OK 成功
 *
 * @note 对应需求: DR-001
 */
driver_result_t driver_register(const char *name, const driver_ops_t *ops);

/**
 * @brief 注销驱动
 *
 * @param name 驱动名称
 *
 * @return DRIVER_OK 成功
 */
driver_result_t driver_unregister(const char *name);

/**
 * @brief 映射 MMIO 区域
 *
 * @param mmio_base MMIO 物理基地址
 * @param size      大小
 * @param[out] virt_addr 输出虚拟地址
 *
 * @return DRIVER_OK 成功
 *
 * @note 对应需求: DR-003
 */
driver_result_t driver_map_mmio(paddr_t mmio_base, uint64_t size,
                                  vaddr_t *virt_addr);

/**
 * @brief 解除 MMIO 映射
 *
 * @param virt_addr 虚拟地址
 * @param size      大小
 *
 * @return DRIVER_OK 成功
 */
driver_result_t driver_unmap_mmio(vaddr_t virt_addr, uint64_t size);

/**
 * @brief 分配 DMA 缓冲区
 *
 * @param size    大小
 * @param[out] buffer DMA 缓冲区描述符
 *
 * @return DRIVER_OK 成功
 *
 * @note 对应需求: DR-004
 */
driver_result_t driver_dma_alloc(uint64_t size, dma_buffer_t *buffer);

/**
 * @brief 释放 DMA 缓冲区
 *
 * @param buffer DMA 缓冲区描述符
 *
 * @return DRIVER_OK 成功
 */
driver_result_t driver_dma_free(dma_buffer_t *buffer);

/**
 * @brief 绑定中断到当前驱动
 *
 * @param irq 中断号
 *
 * @return DRIVER_OK 成功
 *
 * @note 对应需求: DR-002
 */
driver_result_t driver_interrupt_attach(uint32_t irq);

/**
 * @brief 解除中断绑定
 *
 * @param irq 中断号
 *
 * @return DRIVER_OK 成功
 */
driver_result_t driver_interrupt_detach(uint32_t irq);

#endif /* KERNEL_DRIVER_FRAMEWORK_H */
