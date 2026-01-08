/**
 * @file gic.c
 * @brief AISafe64 RTOS - GICv3驱动实现
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details ARM Generic Interrupt Controller v3驱动
 *          - GIC Distributor (GICD)
 *          - GIC CPU Interface (GICR)
 *          - 中断分发和处理
 *
 * @note QEMU virt平台使用GICv3
 * @note MISRA-C:2012合规
 */

#include "irq.h"
#include "printk.h"
#include "types.h"

/**
 * @brief GICv3 Distributor寄存器偏移
 */
#define GICD_CTLR 0x0000       /**< Distributor控制寄存器 */
#define GICD_TYPER 0x0004      /**< 中断类型寄存器 */
#define GICD_IIDR 0x0008       /**< 实现ID寄存器 */
#define GICD_IGROUPR 0x0080    /**< 中断分组寄存器 */
#define GICD_ISENABLER 0x0100  /**< 中断使能设置寄存器 */
#define GICD_ICENABLER 0x0180  /**< 中断使能清除寄存器 */
#define GICD_ISPENDR 0x0200    /**< 中断挂起设置寄存器 */
#define GICD_ICPENDR 0x0280    /**< 中断挂起清除寄存器 */
#define GICD_ISACTIVER 0x0300  /**< 中断激活设置寄存器 */
#define GICD_ICACTIVER 0x0380  /**< 中断激活清除寄存器 */
#define GICD_IPRIORITYR 0x0400 /**< 中断优先级寄存器 */
#define GICD_ITARGETSR 0x0800  /**< 中断处理器目标寄存器 */
#define GICD_ICFGR 0x0C00      /**< 中断配置寄存器 */
#define GICD_SGIR 0x0F00       /**< 软件中断生成寄存器 */

/**
 * @brief GICv3 Redistributor寄存器偏移
 */
#define GICR_CTLR 0x0000       /**< Redistributor控制寄存器 */
#define GICR_TYPER 0x0008      /**< Redistributor类型寄存器 */
#define GICR_WAKER 0x0014      /**< 唤醒寄存器 */
#define GICR_ISENABLER0 0x0100 /**< 中断使能设置寄存器0 (SGI+PPI) */
#define GICR_ICENABLER0 0x0180 /**< 中断使能清除寄存器0 (SGI+PPI) */
#define GICR_ISPENDR0 0x0200   /**< 中断挂起设置寄存器0 (SGI+PPI) */
#define GICR_ICPENDR0 0x0280   /**< 中断挂起清除寄存器0 (SGI+PPI) */
#define GICR_IPRIORITYR 0x0400 /**< 中断优先级寄存器 (SGI+PPI) */
#define GICR_ICFGR0 0x0C00     /**< 中断配置寄存器0 (SGI) */
#define GICR_ICFGR1 0x0C04     /**< 中断配置寄存器1 (PPI) */

/**
 * @brief QEMU virt平台GIC地址
 */
#define GICD_BASE 0x08000000UL /**< GIC Distributor基址 */
#define GICR_BASE 0x080A0000UL /**< GIC Redistributor基址 */

/**
 * @brief GIC寄存器访问宏
 */
#define GICD_READ(offset) (*(volatile uint32_t *)(GICD_BASE + (offset)))
#define GICD_WRITE(offset, val) (*(volatile uint32_t *)(GICD_BASE + (offset)) = (val))
#define GICR_READ(offset) (*(volatile uint32_t *)(GICR_BASE + (offset)))
#define GICR_WRITE(offset, val) (*(volatile uint32_t *)(GICR_BASE + (offset)) = (val))

/**
 * @brief GIC初始化
 * @return 成功返回0，失败返回负错误码
 */
int gic_init(void)
{
    uint32_t typer, iidr;
    uint32_t num_irqs, i;

    printk("[INIT] GICv3: Initializing...\n");

    /* 读取GICD类型 */
    typer = GICD_READ(GICD_TYPER);
    num_irqs = ((typer & 0x1F) + 1) * 32; /* ITLinesNumber字段 */
    printk("[INIT] GICv3: Maximum IRQs: %u\n", num_irqs);

    /* 读取GICD实现ID */
    iidr = GICD_READ(GICD_IIDR);
    printk("[INIT] GICv3: Implementer ID: 0x%08X\n", iidr);

    /* 禁用GIC Distributor */
    GICD_WRITE(GICD_CTLR, 0);

    /* 禁用所有中断 */
    for (i = 32; i < num_irqs; i += 32)
    {
        GICD_WRITE(GICD_ICENABLER + (i / 8), 0xFFFFFFFF);
    }

    /* 清除所有挂起 */
    for (i = 32; i < num_irqs; i += 32)
    {
        GICD_WRITE(GICD_ICPENDR + (i / 8), 0xFFFFFFFF);
    }

    /* 设置所有SPI的优先级为默认值 */
    for (i = 32; i < num_irqs; i += 4)
    {
        GICD_WRITE(GICD_IPRIORITYR + i, 0xA0A0A0A0);
    }

    /* 设置所有SPI为Group 0 */
    for (i = 32; i < num_irqs; i += 32)
    {
        GICD_WRITE(GICD_IGROUPR + (i / 8), 0);
    }

    /* 配置所有SPI为电平触发 */
    for (i = 32; i < num_irqs; i += 16)
    {
        GICD_WRITE(GICD_ICFGR + (i / 4), 0);
    }

    /* 初始化Redistributor */
    printk("[INIT] GICv3: Initializing Redistributor...\n");

    /* 唤醒Redistributor */
    uint32_t waker = GICR_READ(GICR_WAKER);
    waker &= ~0x2; /* 清除ProcessorSleep */
    GICR_WRITE(GICR_WAKER, waker);

    /* 等待唤醒完成 */
    while ((GICR_READ(GICR_WAKER) & 0x4) != 0)
    {
        __asm__ volatile("nop");
    }

    /* 禁用所有SGI和PPI */
    GICR_WRITE(GICR_ICENABLER0, 0xFFFFFFFF);

    /* 清除所有SGI和PPI挂起 */
    GICR_WRITE(GICR_ICPENDR0, 0xFFFFFFFF);

    /* 设置SGI和PPI优先级 */
    for (i = 0; i < 32; i += 4)
    {
        GICR_WRITE(GICR_IPRIORITYR + i, 0xA0A0A0A0);
    }

    /* 配置SGI和PPI为电平触发 (SGI固定为电平触发，PPI大部分为边沿触发) */
    GICR_WRITE(GICR_ICFGR0, 0); /* SGI: 电平触发 */
    GICR_WRITE(GICR_ICFGR1, 0); /* PPI: 电平触发 */

    /* 使能GIC Distributor */
    GICD_WRITE(GICD_CTLR, 1);

    printk("[INIT] GICv3: Initialization complete\n");

    return 0;
}

/**
 * @brief 使能中断
 * @param irq 中断号
 * @return 成功返回0，失败返回负错误码
 */
int irq_enable(uint32_t irq)
{
    if (irq >= 1020)
    {
        return -ERROR_INVALID_PARAM;
    }

    if (irq < 32)
    {
        /* SGI或PPI: 使用Redistributor */
        GICR_WRITE(GICR_ISENABLER0, (1U << irq));
    }
    else
    {
        /* SPI: 使用Distributor */
        GICD_WRITE(GICD_ISENABLER + ((irq / 32) * 4), (1U << (irq % 32)));
    }

    return 0;
}

/**
 * @brief 禁用中断
 * @param irq 中断号
 * @return 成功返回0，失败返回负错误码
 */
int irq_disable(uint32_t irq)
{
    if (irq >= 1020)
    {
        return -ERROR_INVALID_PARAM;
    }

    if (irq < 32)
    {
        /* SGI或PPI: 使用Redistributor */
        GICR_WRITE(GICR_ICENABLER0, (1U << irq));
    }
    else
    {
        /* SPI: 使用Distributor */
        GICD_WRITE(GICD_ICENABLER + ((irq / 32) * 4), (1U << (irq % 32)));
    }

    return 0;
}

/**
 * @brief 设置中断优先级
 * @param irq 中断号
 * @param priority 优先级 (0-255)
 * @return 成功返回0，失败返回负错误码
 */
int irq_set_priority(uint32_t irq, uint32_t priority)
{
    if (irq >= 1020)
    {
        return -ERROR_INVALID_PARAM;
    }

    if (priority > 255)
    {
        return -ERROR_INVALID_PARAM;
    }

    /* GIC优先级寄存器以字节为单位访问 */
    if (irq < 32)
    {
        /* SGI或PPI */
        uint32_t offset = GICR_IPRIORITYR + irq;
        uint32_t val = GICR_READ(offset);
        val = (val & ~(0xFF << ((irq % 4) * 8))) | (priority << ((irq % 4) * 8));
        GICR_WRITE(offset, val);
    }
    else
    {
        /* SPI */
        uint32_t offset = GICD_IPRIORITYR + irq;
        uint32_t val = GICD_READ(offset);
        val = (val & ~(0xFF << ((irq % 4) * 8))) | (priority << ((irq % 4) * 8));
        GICD_WRITE(offset, val);
    }

    return 0;
}

/**
 * @brief 设置中断触发方式
 * @param irq 中断号
 * @param trigger 触发方式
 * @return 成功返回0，失败返回负错误码
 */
int irq_set_trigger(uint32_t irq, irq_trigger_t trigger)
{
    if (irq >= 1020)
    {
        return -ERROR_INVALID_PARAM;
    }

    /* SGI (0-15) 固定为电平触发，不能修改 */
    if (irq < 16)
    {
        return -ERROR_NOT_SUPPORTED;
    }

    uint32_t mask = (2U << ((irq % 16) * 2));

    if (irq < 32)
    {
        /* PPI: 使用Redistributor */
        uint32_t offset = GICR_ICFGR1;
        uint32_t val = GICR_READ(offset);

        if (trigger == IRQ_TRIGGER_EDGE)
        {
            val |= mask;
        }
        else
        {
            val &= ~mask;
        }

        GICR_WRITE(offset, val);
    }
    else
    {
        /* SPI: 使用Distributor */
        uint32_t offset = GICD_ICFGR + ((irq / 16) * 4);
        uint32_t val = GICD_READ(offset);

        if (trigger == IRQ_TRIGGER_EDGE)
        {
            val |= mask;
        }
        else
        {
            val &= ~mask;
        }

        GICD_WRITE(offset, val);
    }

    return 0;
}

/**
 * @brief 发送SGI (核间中断)
 * @param target_cpu 目标CPU掩码 (bit 0 = CPU0, etc.)
 * @param sgi SGI中断号 (0-15)
 * @return 成功返回0，失败返回负错误码
 */
int irq_send_sgi(uint8_t target_cpu, uint8_t sgi)
{
    if (sgi > 15)
    {
        return -ERROR_INVALID_PARAM;
    }

    if (target_cpu == 0)
    {
        return -ERROR_INVALID_PARAM;
    }

    /* 发送SGI (使用GICD_SGIR寄存器，GICv3兼容模式) */
    uint32_t val = ((target_cpu & 0xFF) << 16) | (sgi & 0xF);
    GICD_WRITE(GICD_SGIR, val);

    return 0;
}
