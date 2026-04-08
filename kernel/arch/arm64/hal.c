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

/** @brief UART 中断屏蔽寄存器偏移 */
#define UART_IMSC_OFFSET   0x38U

/** @brief UART 接收中断使能位 */
#define UART_IMSC_RXIM_BIT (1U << 4U)

/** @brief UART 标志寄存器：接收 FIFO 空 */
#define UART_FR_RXFE_BIT   (1U << 4U)

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

/* ========== UART 接收接口实现 ========== */

/**
 * @brief UART 接收单个字符（非阻塞）
 *
 * @param base UART 基地址
 * @param ch   输出字符指针
 *
 * @return 非0表示成功读取，0表示无数据
 */
int hal_uart_getc(uint64_t base, char *ch)
{
    uint32_t fr;

    if (ch == NULL)
    {
        return 0;
    }

    /* 检查接收 FIFO 是否为空 */
    fr = uart_read(base, UART_FR_OFFSET);
    if ((fr & UART_FR_RXFE_BIT) != 0U)
    {
        return 0;
    }

    /* 读取数据寄存器 */
    *ch = (char)(uart_read(base, UART_DR_OFFSET) & 0xFFU);

    return 1;
}

/**
 * @brief 使能 UART 接收中断
 *
 * @param base UART 基地址
 */
void hal_uart_enable_rx_irq(uint64_t base)
{
    uint32_t imsc;

    /* 读取当前中断屏蔽寄存器 */
    imsc = uart_read(base, UART_IMSC_OFFSET);

    /* 使能接收中断（RXIM） */
    imsc |= UART_IMSC_RXIM_BIT;
    uart_write(base, UART_IMSC_OFFSET, imsc);
}

/* ========== 定时器接口实现 ========== */

/**
 * @brief 读取物理定时器计数值
 * @return CNTPCT_EL0 当前值
 */
uint64_t hal_timer_get_count(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(val));
    return val;
}

/**
 * @brief 读取物理定时器频率
 * @return CNTFRQ_EL0 频率值（Hz）
 */
uint64_t hal_timer_get_freq(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

/**
 * @brief 读取物理定时器控制寄存器
 * @return CNTP_CTL_EL0 当前值
 */
uint64_t hal_timer_get_control(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(val));
    return val;
}

/**
 * @brief 设置物理定时器比较值
 * @param val 要写入 CNTP_CVAL_EL0 的值
 */
void hal_timer_set_compare(uint64_t val)
{
    __asm__ volatile("msr cntp_cval_el0, %0" :: "r"(val));
    __asm__ volatile("isb");
}

/**
 * @brief 设置物理定时器控制寄存器
 * @param val 要写入 CNTP_CTL_EL0 的值
 */
void hal_timer_set_control(uint64_t val)
{
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(val));
    __asm__ volatile("isb");
}

/* ========== 内存屏障接口实现 ========== */

/**
 * @brief 数据内存屏障 (Inner Shareable)
 */
void hal_dmb_ish(void)
{
    __asm__ volatile("dmb ish" ::: "memory");
}

/**
 * @brief 数据内存屏障 (Inner Shareable, Store)
 */
void hal_dmb_ishst(void)
{
    __asm__ volatile("dmb ishst" ::: "memory");
}

/**
 * @brief 数据内存屏障 (Inner Shareable, Load)
 */
void hal_dmb_ishld(void)
{
    __asm__ volatile("dmb ishld" ::: "memory");
}

/* ========== 页表寄存器接口实现 ========== */

/**
 * @brief 读取 TTBR0_EL1 寄存器
 * @return TTBR0_EL1 当前值
 */
uint64_t hal_read_ttbr0(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(val));
    return val;
}

/**
 * @brief 读取 TTBR1_EL1 寄存器
 * @return TTBR1_EL1 当前值
 */
uint64_t hal_read_ttbr1(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, ttbr1_el1" : "=r"(val));
    return val;
}

/**
 * @brief 写入 TTBR0_EL1 寄存器
 * @param val 要写入的值
 */
void hal_write_ttbr0(uint64_t val)
{
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(val));
    __asm__ volatile("isb");
}

/**
 * @brief 写入 TTBR1_EL1 寄存器
 * @param val 要写入的值
 */
void hal_write_ttbr1(uint64_t val)
{
    __asm__ volatile("msr ttbr1_el1, %0" :: "r"(val));
    __asm__ volatile("isb");
}

/**
 * @brief 刷新指定 ASID 的 TLB
 * @param asid 地址空间标识
 */
void hal_tlb_invalidate_asid(uint64_t asid)
{
    uint64_t operand = (asid & 0xFFULL) << 48ULL;
    __asm__ volatile("tlbi aside1is, %0" :: "r"(operand));
    __asm__ volatile("dmb ish" ::: "memory");
    __asm__ volatile("isb");
}

/* ========== 低功耗等待接口实现 ========== */

/**
 * @brief 等待事件（低功耗 WFE）
 */
void hal_wfe(void)
{
    __asm__ volatile("wfe" ::: "memory");
}

/**
 * @brief 发送事件
 */
void hal_sev(void)
{
    __asm__ volatile("sev" ::: "memory");
}
