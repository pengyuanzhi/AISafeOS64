/**
 * @file    uart.c
 * @brief   PL011 UART 用户态驱动
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 用户态 PL011 UART 驱动实现：
 *          - 通过驱动框架 API 映射 MMIO 寄存器
 *          - 支持轮询模式和中断模式收发
 *          - 符合 MISRA-C:2012 规范
 *
 * @note 对应需求: DR-001~005
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/driver_framework.h>
#include <kernel/syscall.h>
#include <kernel/config.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * PL011 寄存器偏移定义
 * ======================================================================== */

/** @brief 数据寄存器 */
#define UART_DR_OFFSET         0x000U

/** @brief 接收状态/错误清除寄存器 */
#define UART_RSR_ECR_OFFSET    0x004U

/** @brief 标志寄存器 */
#define UART_FR_OFFSET         0x018U

/** @brief 波特率分频整数寄存器 */
#define UART_IBRD_OFFSET       0x024U

/** @brief 波特率分频小数寄存器 */
#define UART_FBRD_OFFSET       0x028U

/** @brief 线控制寄存器 */
#define UART_LCR_H_OFFSET      0x02CU

/** @brief 控制寄存器 */
#define UART_CR_OFFSET         0x030U

/** @brief 中断屏蔽设置/清除寄存器 */
#define UART_IMSC_OFFSET       0x038U

/** @brief 原始中断状态寄存器 */
#define UART_RIS_OFFSET        0x03CU

/** @brief 屏蔽中断状态寄存器 */
#define UART_MIS_OFFSET        0x040U

/** @brief 中断清除寄存器 */
#define UART_ICR_OFFSET        0x044U

/* ========================================================================
 * 标志寄存器位定义
 * ======================================================================== */

/** @brief TX 满 */
#define UART_FR_TXFF           (1U << 5U)

/** @brief RX 空 */
#define UART_FR_RXFE           (1U << 4U)

/** @brief UART 忙 */
#define UART_FR_BUSY           (1U << 3U)

/* ========================================================================
 * 控制寄存器位定义
 * ======================================================================== */

/** @brief UART 使能 */
#define UART_CR_UARTEN         (1U << 0U)

/** @brief 发送使能 */
#define UART_CR_TXE            (1U << 8U)

/** @brief 接收使能 */
#define UART_CR_RXE            (1U << 9U)

/* ========================================================================
 * 线控制寄存器位定义
 * ======================================================================== */

/** @brief FIFO 使能 */
#define UART_LCR_FEN           (1U << 4U)

/** @brief 8 位字长 */
#define UART_LCR_WLEN_8        (3U << 5U)

/* ========================================================================
 * 中断位定义
 * ======================================================================== */

/** @brief 接收中断 */
#define UART_INT_RX            (1U << 4U)

/** @brief 发送中断 */
#define UART_INT_TX            (1U << 5U)

/* ========================================================================
 * 默认配置
 * ======================================================================== */

/** @brief 默认波特率（115200）对应的 IBRD 值 */
#define UART_DEFAULT_IBRD      26U

/** @brief 默认波特率（115200）对应的 FBRD 值 */
#define UART_DEFAULT_FBRD      3U

/** @brief 发送超时循环次数 */
#define UART_TX_TIMEOUT        100000U

/** @brief 接收超时循环次数 */
#define UART_RX_TIMEOUT        100000U

/** @brief TX 缓冲区大小 */
#define UART_TX_BUF_SIZE       256U

/** @brief RX 缓冲区大小 */
#define UART_RX_BUF_SIZE       256U

/* ========================================================================
 * SPSC 环形缓冲区（无锁）
 * ======================================================================== */

/**
 * @brief 无锁 SPSC 环形缓冲区
 */
typedef struct
{
    uint8_t     buffer[UART_RX_BUF_SIZE];  /**< @brief 数据缓冲区 */
    volatile uint32_t head;                 /**< @brief 写入位置 */
    volatile uint32_t tail;                 /**< @brief 读取位置 */
} uart_ringbuf_t;

/* ========================================================================
 * UART 驱动状态
 * ======================================================================== */

/**
 * @brief UART 驱动状态
 */
typedef struct
{
    volatile uint8_t *mmio_base;   /**< @brief MMIO 映射基地址 */
    vaddr_t          mapped_addr;  /**< @brief 映射虚拟地址 */
    uint64_t         mapped_size;  /**< @brief 映射大小 */
    uint32_t         irq_number;   /**< @brief 中断号 */
    bool             initialized;  /**< @brief 初始化标志 */
    bool             int_mode;     /**< @brief 中断模式标志 */
    uart_ringbuf_t   rx_buf;       /**< @brief 接收环形缓冲区 */
} uart_state_t;

/** @brief UART 驱动实例 */
static uart_state_t s_uart;

/* ========================================================================
 * MMIO 寄存器访问
 * ======================================================================== */

/**
 * @brief 写入 UART 寄存器
 *
 * @param offset 寄存器偏移
 * @param value  写入值
 */
static void uart_write_reg(uint32_t offset, uint32_t value)
{
    volatile uint8_t *addr = s_uart.mmio_base + offset;
    *((volatile uint32_t *)addr) = value;
}

/**
 * @brief 读取 UART 寄存器
 *
 * @param offset 寄存器偏移
 *
 * @return 寄存器值
 */
static uint32_t uart_read_reg(uint32_t offset)
{
    volatile uint8_t *addr = s_uart.mmio_base + offset;
    return *((volatile uint32_t *)addr);
}

/* ========================================================================
 * 环形缓冲区操作
 * ======================================================================== */

/**
 * @brief 向环形缓冲区写入一个字节
 *
 * @param buf   环形缓冲区指针
 * @param data  写入数据
 *
 * @return true 成功，false 缓冲区满
 */
static bool ringbuf_put(uart_ringbuf_t *buf, uint8_t data)
{
    uint32_t next_head;

    next_head = (buf->head + 1U) % UART_RX_BUF_SIZE;

    if (next_head == buf->tail)
    {
        return false; /* 缓冲区满 */
    }

    buf->buffer[buf->head] = data;
    __asm__ volatile("dmb ishst" ::: "memory");
    buf->head = next_head;

    return true;
}

/**
 * @brief 从环形缓冲区读取一个字节
 *
 * @param buf   环形缓冲区指针
 * @param data  输出数据指针
 *
 * @return true 成功，false 缓冲区空
 */
static bool ringbuf_get(uart_ringbuf_t *buf, uint8_t *data)
{
    if (buf->tail == buf->head)
    {
        return false; /* 缓冲区空 */
    }

    *data = buf->buffer[buf->tail];
    __asm__ volatile("dmb ishld" ::: "memory");
    buf->tail = (buf->tail + 1U) % UART_RX_BUF_SIZE;

    return true;
}

/**
 * @brief 获取环形缓冲区中的数据量
 *
 * @param buf 环形缓冲区指针
 *
 * @return 数据量
 */
static uint32_t ringbuf_count(const uart_ringbuf_t *buf)
{
    uint32_t head = buf->head;
    uint32_t tail = buf->tail;

    if (head >= tail)
    {
        return head - tail;
    }

    return (UART_RX_BUF_SIZE - tail) + head;
}

/* ========================================================================
 * 轮询模式收发
 * ======================================================================== */

/**
 * @brief 轮询发送单个字节
 *
 * @param ch 待发送字节
 */
static void uart_poll_putc(uint8_t ch)
{
    uint32_t timeout = UART_TX_TIMEOUT;

    /* 等待 TX FIFO 非满 */
    while ((uart_read_reg(UART_FR_OFFSET) & UART_FR_TXFF) != 0U)
    {
        timeout--;
        if (timeout == 0U)
        {
            return;
        }
    }

    uart_write_reg(UART_DR_OFFSET, (uint32_t)ch);
}

/**
 * @brief 轮询接收单个字节
 *
 * @param ch  输出字节指针
 *
 * @return true 成功，false 超时或无数据
 */
static bool uart_poll_getc(uint8_t *ch)
{
    uint32_t timeout = UART_RX_TIMEOUT;

    /* 等待 RX FIFO 非空 */
    while ((uart_read_reg(UART_FR_OFFSET) & UART_FR_RXFE) != 0U)
    {
        timeout--;
        if (timeout == 0U)
        {
            return false;
        }
    }

    *ch = (uint8_t)(uart_read_reg(UART_DR_OFFSET) & 0xFFU);
    return true;
}

/* ========================================================================
 * 中断处理
 * ======================================================================== */

/**
 * @brief UART 中断处理函数（上半部）
 *
 * @param irq 中断号
 */
static void uart_interrupt_handler(uint32_t irq)
{
    uint32_t status;
    uint8_t ch;

    (void)irq;

    status = uart_read_reg(UART_MIS_OFFSET);

    /* 处理接收中断 */
    if ((status & UART_INT_RX) != 0U)
    {
        while ((uart_read_reg(UART_FR_OFFSET) & UART_FR_RXFE) == 0U)
        {
            ch = (uint8_t)(uart_read_reg(UART_DR_OFFSET) & 0xFFU);
            (void)ringbuf_put(&s_uart.rx_buf, ch);
        }
    }

    /* 清除中断 */
    uart_write_reg(UART_ICR_OFFSET, status);
}

/* ========================================================================
 * 驱动操作函数表实现
 * ======================================================================== */

/**
 * @brief 初始化 UART 驱动
 *
 * @param device_info 设备信息
 *
 * @return DRIVER_OK 成功
 */
static driver_result_t uart_init(const device_info_t *device_info)
{
    driver_result_t ret;

    if (device_info == NULL)
    {
        return DRIVER_INVALID_PARAM;
    }

    (void)memset(&s_uart, 0, sizeof(uart_state_t));

    /* 映射 MMIO 区域 */
    ret = driver_map_mmio(device_info->mmio_base, device_info->mmio_size,
                          &s_uart.mapped_addr);
    if (ret != DRIVER_OK)
    {
        return ret;
    }

    s_uart.mmio_base = (volatile uint8_t *)s_uart.mapped_addr;
    s_uart.mapped_size = device_info->mmio_size;
    s_uart.irq_number = device_info->irq_number;

    /* 禁用 UART */
    uart_write_reg(UART_CR_OFFSET, 0U);

    /* 等待 UART 空闲 */
    while ((uart_read_reg(UART_FR_OFFSET) & UART_FR_BUSY) != 0U)
    {
        /* 等待 */
    }

    /* 禁用中断 */
    uart_write_reg(UART_IMSC_OFFSET, 0U);

    /* 清除待处理中断 */
    uart_write_reg(UART_ICR_OFFSET, 0x7FFU);

    /* 设置波特率 115200（假设时钟 48MHz） */
    uart_write_reg(UART_IBRD_OFFSET, UART_DEFAULT_IBRD);
    uart_write_reg(UART_FBRD_OFFSET, UART_DEFAULT_FBRD);

    /* 8N1，使能 FIFO */
    uart_write_reg(UART_LCR_H_OFFSET, UART_LCR_WLEN_8 | UART_LCR_FEN);

    /* 使能 TX + RX */
    uart_write_reg(UART_CR_OFFSET, UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE);

    s_uart.int_mode = false;
    s_uart.initialized = true;

    return DRIVER_OK;
}

/**
 * @brief 关闭 UART 驱动
 */
static void uart_deinit(void)
{
    if (!s_uart.initialized)
    {
        return;
    }

    /* 等待 TX FIFO 排空 */
    while ((uart_read_reg(UART_FR_OFFSET) & UART_FR_BUSY) != 0U)
    {
        /* 等待 */
    }

    /* 禁用 UART */
    uart_write_reg(UART_CR_OFFSET, 0U);

    /* 解除 MMIO 映射 */
    if (s_uart.mapped_addr != (vaddr_t)0U)
    {
        (void)driver_unmap_mmio(s_uart.mapped_addr, s_uart.mapped_size);
        s_uart.mapped_addr = (vaddr_t)0U;
        s_uart.mmio_base = NULL;
    }

    s_uart.initialized = false;
}

/**
 * @brief 打开 UART 设备
 *
 * @param flags 打开标志
 *
 * @return DRIVER_OK 成功
 */
static driver_result_t uart_open(uint32_t flags)
{
    (void)flags;

    if (!s_uart.initialized)
    {
        return DRIVER_ERROR;
    }

    /* 如果要求中断模式 */
    if ((flags & 0x01U) != 0U)
    {
        driver_result_t ret;

        /* 绑定中断 */
        ret = driver_interrupt_attach(s_uart.irq_number);
        if (ret != DRIVER_OK)
        {
            return ret;
        }

        /* 注册中断处理函数 */
        uart_write_reg(UART_IMSC_OFFSET, UART_INT_RX);

        s_uart.int_mode = true;
    }

    return DRIVER_OK;
}

/**
 * @brief 关闭 UART 设备
 */
static void uart_close(void)
{
    if (s_uart.int_mode)
    {
        /* 禁用中断 */
        uart_write_reg(UART_IMSC_OFFSET, 0U);
        (void)driver_interrupt_detach(s_uart.irq_number);
        s_uart.int_mode = false;
    }
}

/**
 * @brief 从 UART 读取数据
 *
 * @param buf    缓冲区
 * @param size   请求读取大小
 * @param offset 未使用
 *
 * @return 读取的字节数
 */
static int64_t uart_read(void *buf, uint64_t size, uint64_t offset)
{
    uint8_t *dst;
    uint64_t count;
    uint8_t  ch;

    (void)offset;

    if (buf == NULL)
    {
        return -(int64_t)22; /* -EINVAL */
    }

    if (!s_uart.initialized)
    {
        return -(int64_t)19; /* -ENODEV */
    }

    dst = (uint8_t *)buf;

    if (s_uart.int_mode)
    {
        /* 中断模式：从环形缓冲区读取 */
        count = 0U;
        while ((count < size) && ringbuf_get(&s_uart.rx_buf, &ch))
        {
            dst[count] = ch;
            count++;
        }
    }
    else
    {
        /* 轮询模式 */
        count = 0U;
        while (count < size)
        {
            if (!uart_poll_getc(&ch))
            {
                break;
            }
            dst[count] = ch;
            count++;
        }
    }

    return (int64_t)count;
}

/**
 * @brief 向 UART 写入数据
 *
 * @param buf    数据缓冲区
 * @param size   写入大小
 * @param offset 未使用
 *
 * @return 写入的字节数
 */
static int64_t uart_write(const void *buf, uint64_t size, uint64_t offset)
{
    const uint8_t *src;
    uint64_t count;

    (void)offset;

    if (buf == NULL)
    {
        return -(int64_t)22; /* -EINVAL */
    }

    if (!s_uart.initialized)
    {
        return -(int64_t)19; /* -ENODEV */
    }

    src = (const uint8_t *)buf;

    for (count = 0U; count < size; count++)
    {
        uart_poll_putc(src[count]);
    }

    return (int64_t)count;
}

/**
 * @brief UART I/O 控制
 *
 * @param cmd 命令
 * @param arg 参数
 *
 * @return DRIVER_OK 成功
 */
static driver_result_t uart_ioctl(uint32_t cmd, void *arg)
{
    if (!s_uart.initialized)
    {
        return DRIVER_NOT_FOUND;
    }

    switch (cmd)
    {
        case 0x0001U: /* 设置波特率 */
        {
            uint32_t baud;
            if (arg == NULL)
            {
                return DRIVER_INVALID_PARAM;
            }
            baud = *(uint32_t *)arg;
            /* 简化实现：仅支持 115200 */
            (void)baud;
            break;
        }

        case 0x0002U: /* 获取接收缓冲区数据量 */
        {
            uint32_t *count_ptr;
            if (arg == NULL)
            {
                return DRIVER_INVALID_PARAM;
            }
            count_ptr = (uint32_t *)arg;
            *count_ptr = ringbuf_count(&s_uart.rx_buf);
            break;
        }

        default:
            return DRIVER_INVALID_PARAM;
    }

    return DRIVER_OK;
}

/* ========================================================================
 * 驱动操作函数表
 * ======================================================================== */

/**
 * @brief PL011 UART 驱动操作函数表
 */
static const driver_ops_t s_uart_ops =
{
    .init              = uart_init,
    .deinit            = uart_deinit,
    .open              = uart_open,
    .close             = uart_close,
    .read              = uart_read,
    .write             = uart_write,
    .ioctl             = uart_ioctl,
    .interrupt_handler = uart_interrupt_handler
};

/* ========================================================================
 * 驱动入口
 * ======================================================================== */

/**
 * @brief UART 驱动主函数
 *
 * @details 初始化驱动，注册到驱动框架，进入消息循环
 *
 * @return 0 成功（不会返回）
 */
int main(void)
{
    driver_result_t ret;
    device_info_t dev_info;

    /* 填充设备信息（实际由设备管理器提供） */
    dev_info.device_id   = 0U;
    dev_info.vendor_id   = 0U;
    dev_info.device_type = 0U;
    dev_info.mmio_base   = (paddr_t)0x09000000ULL; /* PL011 基地址 */
    dev_info.mmio_size   = 0x1000ULL;              /* 4KB */
    dev_info.irq_number  = 33U;                    /* SPI #1 */

    /* 初始化硬件 */
    ret = uart_init(&dev_info);
    if (ret != DRIVER_OK)
    {
        return (int)ret;
    }

    /* 注册驱动 */
    ret = driver_register("uart-pl011", &s_uart_ops);
    if (ret != DRIVER_OK)
    {
        uart_deinit();
        return (int)ret;
    }

    /* 进入消息循环 */
    for (;;)
    {
        /* 通过 IPC 接收并处理 I/O 请求 */
    }

    return 0;
}
