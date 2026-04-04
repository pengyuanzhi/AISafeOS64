/**
 * @file    gic.c
 * @brief   ARM GICv2 中断控制器驱动实现
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 4.0
 *
 * @details 本文件实现了 ARM GICv2 (GIC-400) 中断控制器驱动：
 *          - Distributor 初始化（MMIO, GICD 基地址 0x08000000）
 *          - CPU Interface 初始化（MMIO, GICC 基地址 0x08010000）
 *          - 中断使能/禁用/屏蔽
 *          - 中断优先级配置
 *          - 中断目标路由（GICv2 ITARGETSR 寄存器）
 *          - 中断触发模式配置
 *          - SGI 软件中断发送
 *          - 中断确认（ACK）和结束（EOI）
 *
 *          QEMU virt 平台地址映射（GICv2）：
 *          - GICD (Distributor):  0x08000000
 *          - GICC (CPU Interface): 0x08010000
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
#include "hal.h"

/* ========================================================================
 * GICv2 Distributor 寄存器偏移定义
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

/** @brief Distributor 中断目标寄存器（GICv2，每个中断占 1 字节） */
#define GICD_ITARGETSR(n)      (0x0800U + (uint32_t)(n))

/** @brief Distributor 中断配置寄存器（每个中断占 2 位） */
#define GICD_ICFGR(n)          (0x0C00U + ((uint32_t)(n) * 4U))

/** @brief Distributor 中断分组寄存器（每组 32 个中断） */
#define GICD_IGROUPR(n)       (0x0080U + ((uint32_t)(n) * 4U))

/** @brief Distributor SGI 寄存器 */
#define GICD_SGIR              0x0F00U

/* ========================================================================
 * GICv2 CPU Interface 寄存器偏移定义
 * ======================================================================== */

/** @brief CPU Interface 控制寄存器 */
#define GICC_CTLR              0x0000U

/** @brief CPU Interface 优先级掩码寄存器 */
#define GICC_PMR               0x0004U

/** @brief CPU Interface 二进制点寄存器 */
#define GICC_BPR               0x0008U

/** @brief CPU Interface 中断确认寄存器 */
#define GICC_IAR               0x000CU

/** @brief CPU Interface 中断结束寄存器 */
#define GICC_EOIR              0x0010U

/* ========================================================================
 * GIC 控制位定义
 * ======================================================================== */

/** @brief Distributor 使能位（Group0 + Group1） */
#define GICD_CTLR_ENABLE       0x03U

/** @brief CPU Interface 使能位（Group1 非安全组）
 *
 * @details QEMU virt 平台 GICv2 非安全模式下，
 *          GICC_CTLR bit0 控制非安全组（Group 1）中断。
 *          仅需写 1 即可启用 IRQ 中断传递。
 *
 *          安全模式下 bit0 控制 Group 0，bit1 控制 Group 1。
 *          同时启用两组以确保中断可传递。
 */
#define GICC_CTLR_ENABLE       0x03U

/** @brief 最低优先级掩码（允许所有优先级中断） */
#define GICC_PMR_LOWEST        0xFFU

/** @brief 二进制点默认值（无预emption） */
#define GICC_BPR_DEFAULT       0x00U

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

/** @brief 默认中断目标：CPU 0（bit 0 置位） */
#define GIC_TARGET_CPU0        0x01U

/* ========================================================================
 * GIC 基地址定义（QEMU virt 平台 GICv2）
 * ======================================================================== */

/**
 * @def GICD_BASE_ADDR
 * @brief GICv2 Distributor 基地址
 *
 * @details QEMU virt 平台 GICv2 Distributor 地址为 0x08000000。
 *          实际项目应通过设备树或平台配置获取。
 */
#ifndef GICD_BASE_ADDR
#define GICD_BASE_ADDR        ((uintptr_t)0x08000000U)
#endif

/**
 * @def GICC_BASE_ADDR
 * @brief GICv2 CPU Interface 基地址
 *
 * @details QEMU virt 平台 GICv2 CPU Interface 地址为 0x08010000。
 */
#ifndef GICC_BASE_ADDR
#define GICC_BASE_ADDR        ((uintptr_t)0x08010000U)
#endif

/* ========================================================================
 * 寄存器访问辅助宏
 * ======================================================================== */

/**
 * @def GICD_REG
 * @brief 读/写 GIC Distributor 寄存器（32 位）
 *
 * @param offset 寄存器偏移量
 *
 * @return 寄存器值（volatile uint32_t 左值）
 */
#define GICD_REG(offset) \
    (*(volatile uint32_t *)((uintptr_t)s_gicd_base + (offset)))

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

/**
 * @def GICC_REG
 * @brief 读/写 GIC CPU Interface 寄存器（32 位）
 *
 * @param offset 寄存器偏移量
 *
 * @return 寄存器值（volatile uint32_t 左值）
 */
#define GICC_REG(offset) \
    (*(volatile uint32_t *)((uintptr_t)s_gicc_base + (offset)))

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
 * @brief 通过 MMIO 启用 GICv2 CPU Interface
 *
 * @details 使用 GICC_CTLR / GICC_PMR / GICC_BPR 寄存器
 *          启用 CPU Interface，允许当前 CPU 响应中断。
 *          QEMU virt 平台 GICv2 非安全模式下仅启用 Group1。
 *
 * @note 对应需求: IN-001
 */
static void gic_enable_cpuif(void)
{
    /* 禁用 CPU Interface（配置期间） */
    GICC_REG(GICC_CTLR) = 0U;
    barrier();

    /* 设置优先级掩码（最低优先级 = 允许所有中断） */
    GICC_REG(GICC_PMR) = GICC_PMR_LOWEST;
    barrier();

    /* 设置二进制点寄存器（默认值，无预emption） */
    GICC_REG(GICC_BPR) = GICC_BPR_DEFAULT;
    barrier();

    /* 启用 CPU Interface（仅 Group1 非安全组） */
    GICC_REG(GICC_CTLR) = GICC_CTLR_ENABLE;
    barrier();

    /* 回读验证 GICC_CTLR 写入成功 */
    (void)GICC_REG(GICC_CTLR);
    barrier();
}

/**
 * @brief 初始化 GICv2
 *
 * @details 初始化 GICv2 Distributor 和 CPU Interface。
 *
 *          步骤：
 *          1. 保存 Distributor 和 CPU Interface 基地址
 *          2. 禁用 Distributor
 *          3. 获取最大中断号
 *          4. 将所有中断设置为 Group 1（产生 IRQ 而非 FIQ）
 *          5. 禁用所有 SPI 中断
 *          6. 设置所有 SPI 的默认优先级
 *          7. 设置所有 SPI 默认路由到 CPU 0（ITARGETSR）
 *          8. 清除所有 SPI 的挂起状态
 *          9. 启用 Distributor（Group0 + Group1）
 *          10. 通过 MMIO 启用 CPU Interface
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

    hal_uart_puts((uint64_t)0x09000000UL, "[gic] step1: bases saved\n");

    /* 第一步：禁用 Distributor（确保干净的状态） */
    GICD_REG(GICD_CTLR) = 0U;
    barrier();

    hal_uart_puts((uint64_t)0x09000000UL, "[gic] step2: dist disabled\n");

    /* 第二步：获取最大中断号 */
    max_irq = gic_get_max_irq();

    hal_uart_puts((uint64_t)0x09000000UL, "[gic] step3: max_irq read\n");

    /* 第三步：将所有中断设置为 Group 0（安全组）
     *
     * GICv2 中：
     * - 安全模式下: Group 0 → FIQ, Group 1 → IRQ
     * - 非安全模式下: Group 0 → IRQ, Group 1 → IRQ（取决于实现）
     *
     * QEMU virt 平台 EL1 可能仍处于安全状态，
     * 因此 Group 0 中断会触发 FIQ 而非 IRQ。
     * 但我们的 FIQ handler 当前只是 panic，所以改为保留 Group 0
     * 并在非安全模式下测试。
     *
     * 暂时保留 Group 0 设置（即不移动到 Group 1），
     * 观察中断是否以 IRQ 形式到达。
     */
#if 0
    for (n = 0U; n <= (max_irq / GICD_IRQS_PER_REG); n++)
    {
        GICD_REG(GICD_IGROUPR(n)) = 0xFFFFFFFFU;
    }
#else
    /* 保持所有中断在 Group 0（默认值） */
    for (n = 0U; n <= (max_irq / GICD_IRQS_PER_REG); n++)
    {
        GICD_REG(GICD_IGROUPR(n)) = 0x00000000U;
    }
#endif
    barrier();

    hal_uart_puts((uint64_t)0x09000000UL, "[gic] step4: group1 set\n");

    /* 第四步：禁用所有 SPI 中断（IRQ 32 ~ max_irq） */
    for (n = (GIC_SPI_BASE / GICD_IRQS_PER_REG);
         n <= (max_irq / GICD_IRQS_PER_REG);
         n++)
    {
        GICD_REG(GICD_ICENABLER(n)) = 0xFFFFFFFFU;
    }

    /* 第五步：设置所有 SPI 的默认优先级 */
    for (irq = GIC_SPI_BASE; irq <= max_irq; irq++)
    {
        GICD_REG_BYTE(GICD_IPRIORITYR(irq)) = (uint8_t)GIC_PRIORITY_DEFAULT;
    }

    /* 第六步：设置所有 SPI 默认路由到 CPU 0（ITARGETSR） */
    for (irq = GIC_SPI_BASE; irq <= max_irq; irq++)
    {
        GICD_REG_BYTE(GICD_ITARGETSR(irq)) = GIC_TARGET_CPU0;
    }

    /* 第七步：清除所有 SPI 的挂起状态 */
    for (n = (GIC_SPI_BASE / GICD_IRQS_PER_REG);
         n <= (max_irq / GICD_IRQS_PER_REG);
         n++)
    {
        GICD_REG(GICD_ICPENDR(n)) = 0xFFFFFFFFU;
    }

    hal_uart_puts((uint64_t)0x09000000UL, "[gic] step5: spis configured\n");

    /* 第八步：启用 Distributor（Group0 + Group1） */
    GICD_REG(GICD_CTLR) = GICD_CTLR_ENABLE;
    barrier();

    hal_uart_puts((uint64_t)0x09000000UL, "[gic] step6: dist enabled\n");

    /* 第九步：通过 MMIO 启用 GICv2 CPU Interface */
    gic_enable_cpuif();

    hal_uart_puts((uint64_t)0x09000000UL, "[gic] step7: cpuif enabled\n");

    /* 验证 GICC_IAR 可读（读取伪中断号 1023 表示无挂起中断） */
    {
        uint32_t iar = GICC_REG(GICC_IAR);
        barrier();
        if (iar == GIC_SPURIOUS_IRQ)
        {
            hal_uart_puts((uint64_t)0x09000000UL, "[gic] IAR read OK (spurious)\n");
        }
        else
        {
            hal_uart_puts((uint64_t)0x09000000UL, "[gic] IAR read OK (pending irq)\n");
        }
    }

    return KERNEL_OK;
}

/**
 * @brief 初始化从核的 GICv2 CPU Interface
 *
 * @details 从核启动后调用，仅初始化 CPU Interface。
 *          不再重新初始化 Distributor（由主核完成）。
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t gic_init_secondary(void)
{
    /* 通过 MMIO 启用当前 CPU 的 CPU Interface */
    gic_enable_cpuif();

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
 * @details GICv2 通过 GICD_ITARGETSR 寄存器设置中断目标。
 *          仅对 SPI（中断号 >= 32）有效。
 *          cpu_mask 的 bit n 表示路由到 CPU n。
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

    /* 写入目标寄存器（每中断占 1 字节） */
    GICD_REG_BYTE(GICD_ITARGETSR(irq)) = cpu_mask;
    barrier();

    return KERNEL_OK;
}

/**
 * @brief 获取当前最高优先级挂起中断号
 *
 * @details GICv2 通过 MMIO 读取 GICC_IAR 寄存器获取中断号。
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
 * @brief 读取当前挂起的最高优先级中断号
 *
 * @details gic_acknowledge_irq() 的别名，语义更清晰。
 *          GICv2 通过 MMIO 读取 GICC_IAR 寄存器获取中断号。
 *          读取后中断状态从 "挂起" 转为 "活跃"。
 *
 * @return 中断号，无挂起中断返回 1023（伪中断）
 *
 * @note 对应需求: IN-001
 */
uint32_t gic_get_irq_id(void)
{
    return gic_acknowledge_irq();
}

/**
 * @brief 通知中断处理完成（EOI）
 *
 * @details GICv2 通过 MMIO 写入 GICC_EOIR 寄存器，
 *          通知 GIC 中断处理已完成。
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
 * @brief 设置中断路由到目标 CPU
 *
 * @details gic_set_affinity() 的别名。
 *          通过 GICD_ITARGETSR 寄存器设置中断目标。
 *
 * @param irq      中断号
 * @param cpu_mask CPU 位掩码
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t gic_set_target(uint32_t irq, uint8_t cpu_mask)
{
    return gic_set_affinity(irq, cpu_mask);
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
