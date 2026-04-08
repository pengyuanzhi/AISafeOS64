/**
 * @file    drv_uart.c
 * @brief   PL011 UART 内核驱动（适配新驱动框架）
 * @author  AISafe64 Team
 * @date    2026-04-08
 * @version 1.0
 *
 * @details QEMU virt PL011 UART 驱动：
 *          - 通过 HAL 接口操作 UART
 *          - 支持读写和 ioctl
 *          - 使用新 driver.h 框架注册
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: DV-001
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/driver.h>
#include <hal.h>
#include <stdint.h>

/* ========================================================================
 * UART 私有数据
 * ======================================================================== */

/**
 * @brief PL011 UART 驱动私有数据
 */
typedef struct
{
    uint64_t    base;       /**< @brief MMIO 基地址 */
    uint32_t    irq;        /**< @brief 中断号 */
    uint32_t    opened;     /**< @brief 打开计数 */
} uart_priv_t;

/** @brief UART 驱动私有数据实例 */
static uart_priv_t s_uart_priv;

/* ========================================================================
 * 驱动操作函数实现
 * ======================================================================== */

/**
 * @brief 探测并初始化 UART 设备
 *
 * @param dev_data 设备描述符指针
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t uart_probe(void *dev_data)
{
    device_desc_t *dev = (device_desc_t *)dev_data;

    if (dev == NULL)
    {
        return -22; /* EINVAL */
    }

    s_uart_priv.base = (uint64_t)dev->mmio_base;
    s_uart_priv.irq = dev->irq;
    s_uart_priv.opened = 0U;

    /* 通过 HAL 初始化 UART（已在 kernel_main 中初始化，此处不重复） */
    return KERNEL_OK;
}

/**
 * @brief 移除 UART 设备
 */
static kernel_status_t uart_remove(void *dev_data)
{
    (void)dev_data;
    s_uart_priv.opened = 0U;
    return KERNEL_OK;
}

/**
 * @brief 从 UART 读取数据
 *
 * @param dev_data 设备私有数据（未使用）
 * @param buf      接收缓冲区
 * @param size     请求读取字节数
 * @param offset   偏移（未使用）
 *
 * @return 实际读取字节数
 */
static int64_t uart_read(void *dev_data, void *buf,
                          uint64_t size, uint64_t offset)
{
    uint64_t i;
    (void)dev_data;
    (void)offset;

    if (buf == NULL)
    {
        return -22;
    }

    for (i = 0U; i < size; i++)
    {
        char ch;
        int rc;

        rc = hal_uart_getc(s_uart_priv.base, &ch);
        if (rc != 0)
        {
            break; /* 无数据可读 */
        }
        ((uint8_t *)buf)[i] = (uint8_t)ch;
    }

    return (int64_t)i;
}

/**
 * @brief 向 UART 写入数据
 *
 * @param dev_data 设备私有数据（未使用）
 * @param buf      发送缓冲区
 * @param size     写入字节数
 * @param offset   偏移（未使用）
 *
 * @return 实际写入字节数
 */
static int64_t uart_write(void *dev_data, const void *buf,
                           uint64_t size, uint64_t offset)
{
    uint64_t i;
    (void)dev_data;
    (void)offset;

    if (buf == NULL)
    {
        return -22;
    }

    for (i = 0U; i < size; i++)
    {
        hal_uart_putc(s_uart_priv.base,
                      (char)((const uint8_t *)buf)[i]);
    }

    return (int64_t)size;
}

/**
 * @brief UART 设备控制命令
 *
 * @param dev_data 设备私有数据（未使用）
 * @param cmd      命令号（0=GET_BASE）
 * @param arg      参数
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t uart_ioctl(void *dev_data, uint32_t cmd, void *arg)
{
    (void)dev_data;

    if (cmd == 0U) /* GET_BASE */
    {
        if (arg != NULL)
        {
            *((uint64_t *)arg) = s_uart_priv.base;
        }
        return KERNEL_OK;
    }

    return -22; /* EINVAL */
}

/**
 * @brief UART 中断处理函数
 */
static void uart_irq(uint32_t irq, void *dev_data)
{
    (void)irq;
    (void)dev_data;
    /* 简化实现：实际应读取 UART 数据并投递到 IPC 通道 */
}

/* ========================================================================
 * 驱动操作函数表
 * ======================================================================== */

/** @brief PL011 UART 驱动操作函数表 */
static const driver_ops_t s_drv_uart_ops =
{
    uart_probe,     /**< @brief probe */
    uart_remove,    /**< @brief remove */
    NULL,           /**< @brief suspend */
    NULL,           /**< @brief resume */
    uart_read,      /**< @brief read */
    uart_write,     /**< @brief write */
    uart_ioctl,     /**< @brief ioctl */
    uart_irq        /**< @brief irq_handler */
};

/* ========================================================================
 * 驱动注册入口
 * ======================================================================== */

/**
 * @brief 注册 PL011 UART 驱动到驱动框架
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t drv_uart_register(void)
{
    driver_match_t match;
    uint32_t i;

    /* 清零 match 结构 */
    for (i = 0U; i < sizeof(match.compatible); i++)
    {
        match.compatible[i] = '\0';
    }

    /* compatible = "pl011" */
    match.compatible[0U] = 'p';
    match.compatible[1U] = 'l';
    match.compatible[2U] = '0';
    match.compatible[3U] = '1';
    match.compatible[4U] = '1';
    match.vendor_id = 0U;
    match.device_id = 0U;
    match.class_code = 0U;

    return driver_register_kern("uart-pl011", DRIVER_TYPE_UART,
                                 &match, &s_drv_uart_ops);
}
