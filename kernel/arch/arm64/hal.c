/**
 * @file hal.c
 * @brief ARMv8-A 硬件抽象层实现
 * @author AISafe64 Team
 * @date 2026-03-31
 * @version 2.0
 *
 * @details ARMv8-A 硬件抽象层实现
 *          - PL011 UART 驱动（QEMU virt）
 *          - 缓存维护操作
 *          - CPU 初始化
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-001（内核启动与初始化）
 */

#include "hal.h"
#include <stddef.h>

/* ========== PL011 UART 寄存器定义 ========== */

/** @brief UART 数据寄存器偏移 */
#define UART_DR_OFFSET     0x00U

/** @brief UART 标志寄存器偏移 */
#define UART_FR_OFFSET     0x18U

/** @brief UART 标志寄存器：发送 FIFO 满 */
#define UART_FR_TXFF_BIT   (1U << 5U)

/** @brief UART 波特率分频寄存器偏移 */
#define UART_IBRD_OFFSET   0x24U

/** @brief UART 小数分频寄存器偏移 */
#define UART_FBRD_OFFSET   0x28U

/** @brief UART 线控制寄存器偏移 */
#define UART_LCR_OFFSET    0x2CU

/** @brief UART 控制寄存器偏移 */
#define UART_CR_OFFSET     0x30U

/** @brief UART 使能位 */
#define UART_CR_UARTEN_BIT   (1U << 0U)
#define UART_CR_TXE_BIT      (1U << 8U)
#define UART_CR_RXE_BIT      (1U << 9U)

/** @brief UART FIFO 使能位 */
#define UART_LCR_FEN_BIT     (1U << 4U)

/* ========== PL011 UART 寄存器访问宏 ========== */

/**
 * @brief 读取 UART 寄存器
 * @param base UART 基地址
 * @param offset 寄存器偏移
 * @return 寄存器值
 */
static inline uint32_t uart_read(uint64_t base, uint32_t offset)
{
    return *(volatile uint32_t *)(base + (uint64_t)offset);
}

/**
 * @brief 写入 UART 寄存器
 * @param base UART 基地址
 * @param offset 寄存器偏移
 * @param value 要写入的值
 */
static inline void uart_write(uint64_t base, uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(base + (uint64_t)offset) = value;
}

/* ========== UART 公共接口实现 ========== */

/**
 * @brief 初始化 PL011 UART
 *
 * @details 配置 QEMU virt 平台的 PL011 UART
 *          - 波特率 115200
 *          - 8N1 格式
 *          - 使能发送
 *
 * @param base UART 基地址
 */
void hal_uart_init(uint64_t base)
{
    /* 禁用 UART */
    uart_write(base, UART_CR_OFFSET, 0x00000000U);

    /* 禁用 FIFO */
    uart_write(base, UART_LCR_OFFSET, 0x00000000U);

    /* 设置波特率分频（假设输入时钟 24MHz，波特率 115200）
     * IBRD = 24000000 / (16 * 115200) = 13
     * FBRD = ((24000000 % (16 * 115200)) * 64 + 8) / (16 * 115200) = 1
     */
    uart_write(base, UART_IBRD_OFFSET, 13U);
    uart_write(base, UART_FBRD_OFFSET, 1U);

    /* 使能 FIFO，8位数据 */
    uart_write(base, UART_LCR_OFFSET, UART_LCR_FEN_BIT | 0x70U);

    /* 使能 UART 发送 */
    uart_write(base, UART_CR_OFFSET,
               UART_CR_UARTEN_BIT | UART_CR_TXE_BIT | UART_CR_RXE_BIT);
}

/**
 * @brief UART 发送单个字符（阻塞等待）
 *
 * @param base UART 基地址
 * @param ch 要发送的字符
 */
void hal_uart_putc(uint64_t base, char ch)
{
    /* 等待发送 FIFO 不满 */
    while ((uart_read(base, UART_FR_OFFSET) & UART_FR_TXFF_BIT) != 0U)
    {
        /* 自旋等待 */
    }

    /* 写入数据寄存器 */
    uart_write(base, UART_DR_OFFSET, (uint32_t)(unsigned char)ch);
}

/**
 * @brief UART 发送字符串
 *
 * @param base UART 基地址
 * @param str 以 NULL 结尾的字符串
 */
void hal_uart_puts(uint64_t base, const char *str)
{
    if (str == NULL)
    {
        return;
    }

    while (*str != '\0')
    {
        /* 换行符前先发送回车 */
        if (*str == '\n')
        {
            hal_uart_putc(base, '\r');
        }
        hal_uart_putc(base, *str);
        str++;
    }
}

/* ========== 缓存维护操作实现 ========== */

/** @brief ARM64 缓存行大小（64字节） */
#define CACHE_LINE_SIZE 64U

/**
 * @brief 清理数据缓存到内存
 *
 * @param start 起始虚拟地址
 * @param size 字节数
 */
void hal_dcache_clean(uint64_t start, uint64_t size)
{
    uint64_t end = start + size;
    uint64_t addr = start & ~((uint64_t)CACHE_LINE_SIZE - 1U);

    while (addr < end)
    {
        __asm__ volatile("dc cvac, %0" :: "r"(addr) : "memory");
        addr += (uint64_t)CACHE_LINE_SIZE;
    }
}

/**
 * @brief 使数据缓存无效
 *
 * @param start 起始虚拟地址
 * @param size 字节数
 */
void hal_dcache_invalidate(uint64_t start, uint64_t size)
{
    uint64_t end = start + size;
    uint64_t addr = start & ~((uint64_t)CACHE_LINE_SIZE - 1U);

    while (addr < end)
    {
        __asm__ volatile("dc ivac, %0" :: "r"(addr) : "memory");
        addr += (uint64_t)CACHE_LINE_SIZE;
    }
}

/**
 * @brief 清理并使数据缓存无效
 *
 * @param start 起始虚拟地址
 * @param size 字节数
 */
void hal_dcache_clean_and_invalidate(uint64_t start, uint64_t size)
{
    uint64_t end = start + size;
    uint64_t addr = start & ~((uint64_t)CACHE_LINE_SIZE - 1U);

    while (addr < end)
    {
        __asm__ volatile("dc civac, %0" :: "r"(addr) : "memory");
        addr += (uint64_t)CACHE_LINE_SIZE;
    }
}
