/**
 * @file uart.c
 * @brief AISafe64 RTOS - UART驱动（PL011）
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details PL011 UART驱动（ARM标准UART）
 *          - 支持轮询模式输出
 *          - 115200波特率，8N1
 *          - 用于早期调试输出
 *
 * @note MISRA-C:2012合规
 * @note 后续可扩展为中断模式
 */

#include "uart.h"
#include "types.h"

/**
 * @brief PL011 UART寄存器定义
 *
 * @note 基地址：QEMU virt平台 = 0x09000000
 */
typedef struct
{
    volatile uint32_t DR;  /**< Data Register (0x00) */
    volatile uint32_t RSR; /**< Receive Status Register (0x04) */
    volatile uint8_t RESERVED1[0x10];
    volatile uint32_t FR; /**< Flag Register (0x18) */
    volatile uint8_t RESERVED2[0x04];
    volatile uint32_t ILPR;  /**< IrDA Low-Power Counter Register (0x20) */
    volatile uint32_t IBRD;  /**< Integer Baud Rate Register (0x24) */
    volatile uint32_t FBRD;  /**< Fractional Baud Rate Register (0x28) */
    volatile uint32_t LCR_H; /**< Line Control Register (0x2C) */
    volatile uint32_t CR;    /**< Control Register (0x30) */
    volatile uint32_t IFLS;  /**< Interrupt FIFO Level Select Register (0x34) */
    volatile uint32_t IMSC;  /**< Interrupt Mask Set/Clear Register (0x38) */
    volatile uint32_t RIS;   /**< Raw Interrupt Status Register (0x3C) */
    volatile uint32_t MIS;   /**< Masked Interrupt Status Register (0x40) */
    volatile uint32_t ICR;   /**< Interrupt Clear Register (0x44) */
    volatile uint32_t DMACR; /**< DMA Control Register (0x48) */
} UART_t;

/**
 * @brief UART标志寄存器位定义
 */
#define UART_FR_TXFE (1U << 7) /**< Transmit FIFO empty */
#define UART_FR_RXFF (1U << 6) /**< Receive FIFO full */
#define UART_FR_TXFF (1U << 5) /**< Transmit FIFO full */
#define UART_FR_RXFE (1U << 4) /**< Receive FIFO empty */
#define UART_FR_BUSY (1U << 3) /**< UART busy */

/**
 * @brief UART控制寄存器位定义
 */
#define UART_CR_UARTEN (1U << 0) /**< UART使能 */
#define UART_CR_TXE (1U << 8)    /**< 发送使能 */
#define UART_CR_RXE (1U << 9)    /**< 接收使能 */

/**
 * @brief UART行控制寄存器位定义
 */
#define UART_LCR_H_WLEN_8 (0x3U << 5) /**< 8位数据 */
#define UART_LCR_H_FEN (1U << 4)      /**< FIFO使能 */

/**
 * @brief UART基地址（QEMU virt平台）
 */
#define UART0_BASE 0x09000000UL

/**
 * @brief UART寄存器指针
 */
static UART_t *const uart0 = (UART_t *)UART0_BASE;

/**
 * @brief 初始化UART
 * @return 成功返回0，失败返回负错误码
 *
 * @details 配置UART为115200波特率，8N1
 *          - UART时钟：24MHz（QEMU virt平台）
 *          - 波特率：115200
 *          - 数据位：8
 *          - 停止位：1
 *          - 校验：无
 *
 * @note 波特率计算：
 *       BAUDDIV = UARTCLK / (16 × BaudRate)
 *       BAUDDIV = 24000000 / (16 × 115200) = 13.02
 *       IBRD = 13
 *       FBRD = (0.02 × 64) + 0.5 = 1
 */
int uart_init(void)
{
    uint32_t uartclk = 24000000U; /* UART时钟24MHz */
    uint32_t baudrate = 115200U;  /* 波特率115200 */
    uint32_t bdiv;
    uint32_t ibrd;
    uint32_t fbrd;

    /* 计算波特率除数 */
    bdiv = uartclk / (16U * baudrate);
    if (bdiv == 0U)
    {
        return -1; /* 无效波特率 */
    }

    ibrd = bdiv;
    fbrd = ((uartclk % (16U * baudrate)) * 64U + (8U * baudrate)) / (16U * baudrate);

    /* 禁用UART */
    uart0->CR = 0U;

    /* 等待UART空闲 */
    while ((uart0->FR & UART_FR_BUSY) != 0U)
    {
        /* 等待 */
    }

    /* 禁用FIFO（配置时需要） */
    uart0->LCR_H &= ~UART_LCR_H_FEN;

    /* 设置波特率 */
    uart0->IBRD = ibrd;
    uart0->FBRD = fbrd;

    /* 配置8位数据，无校验，1停止位 */
    uart0->LCR_H = UART_LCR_H_WLEN_8;

    /* 使能FIFO */
    uart0->LCR_H |= UART_LCR_H_FEN;

    /* 禁用中断（轮询模式） */
    uart0->IMSC = 0U;

    /* 使能UART、发送和接收 */
    uart0->CR = (UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE);

    return 0;
}

/**
 * @brief 发送一个字符到UART
 * @param ch 字符
 *
 * @details 轮询等待发送FIFO为空
 */
void uart_putc(char ch)
{
    /* 等待发送FIFO为空 */
    while ((uart0->FR & UART_FR_TXFE) == 0U)
    {
        /* 等待 */
    }

    /* 发送字符 */
    uart0->DR = (uint32_t)ch;
}

/**
 * @brief 从UART接收一个字符
 * @return 接收的字符
 *
 * @details 轮询等待接收FIFO非空
 */
char uart_getc(void)
{
    /* 等待接收FIFO非空 */
    while ((uart0->FR & UART_FR_RXFE) != 0U)
    {
        /* 等待 */
    }

    /* 读取字符 */
    return (char)(uart0->DR & 0xFFU);
}

/**
 * @brief 发送字符串到UART
 * @param str 字符串指针
 *
 * @details 发送直到遇到'\0'
 */
void uart_puts(const char *str)
{
    if (str == NULL)
    {
        return;
    }

    while (*str != '\0')
    {
        if (*str == '\n')
        {
            uart_putc('\r'); /* LF前发送CR */
        }
        uart_putc(*str);
        str++;
    }
}
