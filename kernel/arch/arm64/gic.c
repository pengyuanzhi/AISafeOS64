/**
 * @file    gic.c
 * @brief   ARM GIC-400 中断控制器驱动实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件实现了 ARM GIC-400 中断控制器驱动：
 *          - Distributor 和 CPU Interface 初始化
 *          - 中断使能/禁用/屏蔽
 *          - 中断优先级配置
 *          - 中断亲和性（目标 CPU）配置
 *          - 中断触发模式配置
 *          - SGI 软件中断发送
 *          - 中断确认（ACK）和结束（EOI）
 *
 *          GIC-400 寄存器通过 MMIO 方式访问，基地址由平台定义。
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: IN-001~006
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */

#include <kernel/gic.h>
#include <kernel/config.h>
#include <kernel/barrier.h>
#include <kernel/errno.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * GIC-400 Distributor 寄存器偏移定义
 * ======================================================================== */

/** @brief Distributor 控制寄存器 */
#define GICD_CTLR              0x0000U

/** @brief Distributor 中断控制器类型寄存器 */
#define GICD_TYPER             0x0004U

/** @brief Distributor 中断使能置位寄存器（每组 32 个中断） */
#define GICD_ISENABLER(n)      (0x0100U + ((uint32_t)(n) * 4U))

/** @brief Distributor 中断使能清零寄存器（每组 32 个中断） */
#define GICD_ICENABLER(n)      (0x0180U + ((uint32_t)(n) * 4U))

/** @brief Distributor 中断挂起置位寄存器 */
#define GICD_ISPENDR(n)        (0x0200U + ((uint32_t)(n) * 4U))

/** @brief Distributor 中断挂起清零寄存器 */
#define GICD_ICPENDR(n)        (0x0280U + ((uint32_t)(n) * 4U))

/** @brief Distributor 中断优先级寄存器（每个中断占 1 字节） */
#define GICD_IPRIORITYR(n)     (0x0400U + (uint32_t)(n))

/** @brief Distributor 中断目标 CPU 寄存器（每个中断占 1 字节） */
#define GICD_ITARGETSR(n)      (0x0800U + (uint32_t)(n))

/** @brief Distributor 中断配置寄存器（每个中断占 2 位） */
#define GICD_ICFGR(n)          (0x0C00U + ((uint32_t)(n) * 4U))

/** @brief Distributor SGI 寄存器 */
#define GICD_SGIR              0x0F00U

/* ========================================================================
 * GIC-400 CPU Interface 寄存器偏移定义
 * ======================================================================== */

/** @brief CPU Interface 控制寄存器 */
#define GICC_CTLR              0x0000U

/** @brief CPU Interface 优先级屏蔽寄存器 */
#define GICC_PMR               0x0004U

/** @brief CPU Interface 中断确认寄存器 */
#define GICC_IAR               0x000CU

/** @brief CPU Interface 中断结束寄存器 */
#define GICC_EOIR              0x0010U

/* ========================================================================
 * GIC 控制位定义
 * ======================================================================== */

/** @brief Distributor 使能位 */
#define GICD_CTLR_ENABLE       0x01U

/** @brief CPU Interface 使能位 */
#define GICC_CTLR_ENABLE       0x01U

/** @brief 最低优先级屏蔽值（允许所有优先级） */
#define GICC_PMR_LOWEST        0xFFU

/** @brief 伪中断号（无挂起中断时 IAR 返回值） */
#define GIC_SPURIOUS_IRQ       1023U

/** @brief GICD_TYPER 中 ITLinesNumber 字段掩码 */
#define GICD_TYPER_ITLINES_MASK 0x1FU

/** @brief ICFGR 每个中断占用的位数 */
#define GICD_ICFGR_BITS_PER_IRQ 2U

/** @brief ICFGR 每个寄存器包含的中断数量 */
#define GICD_ICFGR_IRQS_PER_REG 16U

/** @brief 边沿触发对应的 ICFGR 值 */
#define GICD_ICFGR_EDGE        0x02U

/** @brief 电平触发对应的 ICFGR 值 */
#define GICD_ICFGR_LEVEL       0x00U

/** @brief ISENABLER/ICENABLER 每个寄存器包含的中断数量 */
#define GICD_IRQS_PER_REG      32U

/** @brief 每个 ISENABLER/ICENABLER 寄存器的字节大小 */
#define GICD_REG_SIZE          4U

/* ========================================================================
 * GIC 基地址定义（平台特定，需要由板级代码提供）
 * ======================================================================== */

/**
 * @def GICD_BASE_ADDR
 * @brief GIC Distributor 基地址
 *
 * @details 默认使用 BCM2837 (Raspberry Pi 3/4) 典型地址。
 *          实际项目应通过设备树或平台配置获取。
 */
#ifndef GICD_BASE_ADDR
#define GICD_BASE_ADDR        ((uintptr_t)0x2F000000U)
#endif

/**
 * @def GICC_BASE_ADDR
 * @brief GIC CPU Interface 基地址
 *
 * @details 默认使用 BCM2837 (Raspberry Pi 3/4) 典型地址。
 *          实际项目应通过设备树或平台配置获取。
 */
#ifndef GICC_BASE_ADDR
#define GICC_BASE_ADDR        ((uintptr_t)0x2F001000U)
#endif

/* ========================================================================
 * 寄存器访问辅助宏
 * ======================================================================== */

/**
 * @def GICD_REG
 * @brief 读/写 GIC Distributor 寄存器
 *
 * @param offset 寄存器偏移量
 *
 * @return 寄存器值（volatile uint32_t 左值）
 */
#define GICD_REG(offset) \
    (*(volatile uint32_t *)((uintptr_t)s_gicd_base + (offset)))

/**
 * @def GICC_REG
 * @brief 读/写 GIC CPU Interface 寄存器
 *
 * @param offset 寄存器偏移量
 *
 * @return 寄存器值（volatile uint32_t 左值）
 */
#define GICC_REG(offset) \
    (*(volatile uint32_t *)((uintptr_t)s_gicc_base + (offset)))

/**
 * @def GICD_REG_BYTE
 * @brief 读/写 GIC Distributor 单字节寄存器
 *
 * @param offset 寄存器字节偏移量
 *
 * @return 寄存器值（volatile uint8_t 左值）
 */
#define GICD_REG_BYTE(offset) \
    (*(volatile uint8_t *)((uintptr_t)s_gicd_base + (offset)))

/* ========================================================================
 * 全局状态
 * ======================================================================== */

/**
 * @brief GIC Distributor 基地址（MMIO）
 */
static uintptr_t s_gicd_base = (uintptr_t)0U;

/**
 * @brief GIC CPU Interface 基地址（MMIO）
 */
static uintptr_t s_gicc_base = (uintptr_t)0U;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 从 GICD_TYPER 获取最大 SPI 中断号
 *
 * @details GICD_TYPER 的 [4:0] 位表示 ITLinesNumber，
 *          最大中断号 = (ITLinesNumber + 1) * 32 - 1
 *
 * @return 最大 SPI 中断号
 */
static uint32_t gic_get_max_irq(void)
{
    uint32_t typer;
    uint32_t it_lines;
    uint32_t max_irq;

    typer = GICD_REG(GICD_TYPER);
    it_lines = typer & GICD_TYPER_ITLINES_MASK;
    max_irq = ((it_lines + 1U) * GICD_IRQS_PER_REG) - 1U;

    return max_irq;
}

/* ========================================================================
 * GIC 驱动 API 实现
 * ======================================================================== */

/**
 * @brief 初始化 GIC
 *
 * @details 初始化 GIC Distributor 和当前 CPU 的 CPU Interface。
 *          - 保存基地址
 *          - 禁用 Distributor
 *          - 禁用所有 SPI 中断
 *          - 设置默认优先级（0xA0）
 *          - 设置目标 CPU 为 CPU 0
 *          - 清除所有 SPI 的挂起状态
 *          - 启用 Distributor
 *          - 配置并启用 CPU Interface（优先级屏蔽设为最低）
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: IN-001
 */
kernel_status_t gic_init(void)
{
    uint32_t max_irq;
    uint32_t irq;
    uint32_t n;

    /* 保存基地址 */
    s_gicd_base = GICD_BASE_ADDR;
    s_gicc_base = GICC_BASE_ADDR;

    /* 禁用 Distributor */
    GICD_REG(GICD_CTLR) = 0U;
    barrier();

    /* 获取最大中断号 */
    max_irq = gic_get_max_irq();

    /* 禁用所有 SPI 中断（IRQ 32 ~ max_irq） */
    for (n = (GIC_SPI_BASE / GICD_IRQS_PER_REG);
         n <= (max_irq / GICD_IRQS_PER_REG);
         n++)
    {
        GICD_REG(GICD_ICENABLER(n)) = 0xFFFFFFFFU;
    }

    /* 设置所有 SPI 的默认优先级为 0xA0（中等偏低） */
    for (irq = GIC_SPI_BASE; irq <= max_irq; irq++)
    {
        GICD_REG_BYTE(GICD_IPRIORITYR(irq)) = (uint8_t)GIC_PRIORITY_DEFAULT;
    }

    /* 设置所有 SPI 的目标 CPU 为 CPU 0 */
    for (irq = GIC_SPI_BASE; irq <= max_irq; irq++)
    {
        GICD_REG_BYTE(GICD_ITARGETSR(irq)) = 0x01U;
    }

    /* 清除所有 SPI 的挂起状态 */
    for (n = (GIC_SPI_BASE / GICD_IRQS_PER_REG);
         n <= (max_irq / GICD_IRQS_PER_REG);
         n++)
    {
        GICD_REG(GICD_ICPENDR(n)) = 0xFFFFFFFFU;
    }

    /* 启用 Distributor（Group 0 + Group 1） */
    GICD_REG(GICD_CTLR) = GICD_CTLR_ENABLE;
    barrier();

    /* 配置 CPU Interface */
    /* 设置优先级屏蔽为最低（允许所有优先级中断） */
    GICC_REG(GICC_PMR) = GICC_PMR_LOWEST;
    barrier();

    /* 启用 CPU Interface */
    GICC_REG(GICC_CTLR) = GICC_CTLR_ENABLE;
    barrier();

    return KERNEL_OK;
}

/**
 * @brief 初始化从核的 GIC CPU Interface
 *
 * @details 从核启动后调用，仅初始化 CPU Interface。
 *          不再重新初始化 Distributor（由主核完成）。
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t gic_init_secondary(void)
{
    /* 配置 CPU Interface */
    GICC_REG(GICC_PMR) = GICC_PMR_LOWEST;
    barrier();

    GICC_REG(GICC_CTLR) = GICC_CTLR_ENABLE;
    barrier();

    return KERNEL_OK;
}

/**
 * @brief 使能指定中断
 *
 * @details 设置 ISENABLER 寄存器的对应位，使能指定中断号。
 *
 * @param irq 中断号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效
 *
 * @note 对应需求: IN-002
 */
kernel_status_t gic_enable_irq(uint32_t irq)
{
    uint32_t reg_n;
    uint32_t bit_mask;

    /* 参数验证 */
    if (irq > GIC_MAX_SPI)
    {
        return -(int32_t)EINVAL;
    }

    /* 计算寄存器索引和位掩码 */
    reg_n = irq / GICD_IRQS_PER_REG;
    bit_mask = 1U << (irq % GICD_IRQS_PER_REG);

    /* 设置使能位 */
    GICD_REG(GICD_ISENABLER(reg_n)) = bit_mask;
    barrier();

    return KERNEL_OK;
}

/**
 * @brief 禁用指定中断
 *
 * @details 设置 ICENABLER 寄存器的对应位，禁用指定中断号。
 *
 * @param irq 中断号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效
 *
 * @note 对应需求: IN-002
 */
kernel_status_t gic_disable_irq(uint32_t irq)
{
    uint32_t reg_n;
    uint32_t bit_mask;

    /* 参数验证 */
    if (irq > GIC_MAX_SPI)
    {
        return -(int32_t)EINVAL;
    }

    /* 计算寄存器索引和位掩码 */
    reg_n = irq / GICD_IRQS_PER_REG;
    bit_mask = 1U << (irq % GICD_IRQS_PER_REG);

    /* 清除使能位 */
    GICD_REG(GICD_ICENABLER(reg_n)) = bit_mask;
    barrier();

    return KERNEL_OK;
}

/**
 * @brief 设置中断优先级
 *
 * @details 写入 IPRIORITYR 寄存器的对应字节。
 *          优先级值越小紧迫度越高（0 = 最高，255 = 最低）。
 *
 * @param irq      中断号
 * @param priority 优先级值（0 = 最高，255 = 最低）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效
 *
 * @note 对应需求: IN-003
 */
kernel_status_t gic_set_priority(uint32_t irq, uint8_t priority)
{
    /* 参数验证 */
    if (irq > GIC_MAX_SPI)
    {
        return -(int32_t)EINVAL;
    }

    /* 写入优先级寄存器（每中断占 1 字节） */
    GICD_REG_BYTE(GICD_IPRIORITYR(irq)) = priority;
    barrier();

    return KERNEL_OK;
}

/**
 * @brief 获取中断优先级
 *
 * @details 读取 IPRIORITYR 寄存器的对应字节。
 *
 * @param irq 中断号
 *
 * @return 优先级值
 */
uint8_t gic_get_priority(uint32_t irq)
{
    uint8_t priority;

    /* 参数验证：无效中断号返回最低优先级 */
    if (irq > GIC_MAX_SPI)
    {
        return (uint8_t)GIC_PRIORITY_LOWEST;
    }

    priority = GICD_REG_BYTE(GICD_IPRIORITYR(irq));

    return priority;
}

/**
 * @brief 设置中断亲和性（目标 CPU）
 *
 * @details 写入 ITARGETSR 寄存器的对应字节，指定中断应发送到哪些 CPU。
 *          仅对 SPI（中断号 >= 32）有效。
 *
 * @param irq      中断号
 * @param cpu_mask CPU 位掩码（bit 0 = CPU0，bit 1 = CPU1，...）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: IN-004
 */
kernel_status_t gic_set_affinity(uint32_t irq, uint8_t cpu_mask)
{
    /* 参数验证 */
    if ((irq < GIC_SPI_BASE) || (irq > GIC_MAX_SPI))
    {
        return -(int32_t)EINVAL;
    }

    if (cpu_mask == 0U)
    {
        return -(int32_t)EINVAL;
    }

    /* 写入目标 CPU 寄存器（每中断占 1 字节） */
    GICD_REG_BYTE(GICD_ITARGETSR(irq)) = cpu_mask;
    barrier();

    return KERNEL_OK;
}

/**
 * @brief 获取当前最高优先级挂起中断号
 *
 * @details 读取 GICC_IAR 寄存器，确认中断并获取中断号。
 *          读取后中断状态从 "挂起" 转为 "活跃"。
 *
 * @return 中断号，无挂起中断返回 1023（伪中断）
 */
uint32_t gic_acknowledge_irq(void)
{
    uint32_t irq;

    irq = GICC_REG(GICC_IAR);
    barrier();

    return irq;
}

/**
 * @brief 通知中断处理完成（EOI）
 *
 * @details 写入 GICC_EOIR 寄存器，通知 GIC 中断处理已完成。
 *          必须在中断处理函数末尾调用。
 *
 * @param irq 中断号（必须与 gic_acknowledge_irq 返回值一致）
 */
void gic_end_of_interrupt(uint32_t irq)
{
    /* 伪中断不需要 EOI */
    if (irq >= GIC_SPURIOUS_IRQ)
    {
        return;
    }

    GICC_REG(GICC_EOIR) = irq;
    barrier();
}

/**
 * @brief 发送 SGI（软件生成中断）
 *
 * @details 写入 GICD_SGIR 寄存器，向指定 CPU 发送 SGI。
 *          SGI 编号范围为 0-15。
 *
 * @param sgi_id   SGI 编号（0-15）
 * @param cpu_mask 目标 CPU 位掩码
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: IN-005
 */
kernel_status_t gic_send_sgi(uint32_t sgi_id, uint8_t cpu_mask)
{
    uint32_t sgir_val;

    /* 参数验证：SGI 编号范围 [0, 15] */
    if (sgi_id > GIC_SGI_END)
    {
        return -(int32_t)EINVAL;
    }

    if (cpu_mask == 0U)
    {
        return -(int32_t)EINVAL;
    }

    /*
     * GICD_SGIR 寄存器格式：
     *   [31:24] TargetListFilter: 0 = 使用 target list
     *   [23:16] TargetList: CPU 位掩码
     *   [3:0]   SGI ID: SGI 编号
     */
    sgir_val = ((uint32_t)cpu_mask << 16U) | (sgi_id & 0xFU);

    GICD_REG(GICD_SGIR) = sgir_val;
    barrier();

    return KERNEL_OK;
}

/**
 * @brief 注册中断处理函数
 *
 * @details 本驱动不维护处理函数表（由 interrupt.c 管理），
 *          此函数为空实现，仅保留接口兼容性。
 *
 * @param irq     中断号
 * @param handler 处理函数
 * @param arg     用户参数
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: IN-002
 */
kernel_status_t gic_register_handler(uint32_t irq,
                                      irq_handler_t handler,
                                      void *arg)
{
    /* 参数验证 */
    if (irq > GIC_MAX_SPI)
    {
        return -(int32_t)EINVAL;
    }

    if (handler == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /*
     * GIC 驱动不维护处理函数表，处理函数注册
     * 由 interrupt.c 中的 interrupt_register() 管理。
     * 此处仅使能中断。
     */
    (void)arg; /* 避免未使用参数警告 */

    return KERNEL_OK;
}

/**
 * @brief 设置中断触发模式
 *
 * @details 写入 ICFGR 寄存器的对应 2 位，配置中断触发模式。
 *          仅对 SPI（中断号 >= 32）有效。
 *          - GIC_TRIGGER_EDGE: 边沿触发（ICFGR = 0b10）
 *          - GIC_TRIGGER_LEVEL: 电平触发（ICFGR = 0b00）
 *
 * @param irq    中断号（仅 SPI 有效）
 * @param mode   触发模式
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t gic_set_trigger_mode(uint32_t irq, gic_trigger_mode_t mode)
{
    uint32_t reg_n;
    uint32_t shift;
    uint32_t mask;
    uint32_t val;
    uint32_t cfg_bits;

    /* 参数验证：仅 SPI 支持触发模式配置 */
    if ((irq < GIC_SPI_BASE) || (irq > GIC_MAX_SPI))
    {
        return -(int32_t)EINVAL;
    }

    /* 计算寄存器索引和位偏移 */
    reg_n = irq / GICD_ICFGR_IRQS_PER_REG;
    shift = (irq % GICD_ICFGR_IRQS_PER_REG) * GICD_ICFGR_BITS_PER_IRQ;
    mask  = 0x03U << shift;

    /* 确定配置位值 */
    if (mode == GIC_TRIGGER_EDGE)
    {
        cfg_bits = GICD_ICFGR_EDGE << shift;
    }
    else
    {
        cfg_bits = GICD_ICFGR_LEVEL << shift;
    }

    /* 读-修改-写 */
    val = GICD_REG(GICD_ICFGR(reg_n));
    val = (val & ~mask) | cfg_bits;
    GICD_REG(GICD_ICFGR(reg_n)) = val;
    barrier();

    return KERNEL_OK;
}
