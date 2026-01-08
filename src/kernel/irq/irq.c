/**
 * @file irq.c
 * @brief AISafe64 RTOS - 中断处理实现
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 中断分发和处理
 *          - 中断处理函数注册表
 *          - 中断分发器
 *          - 中断结束处理
 *
 * @note ARMv8-A IRQ异常处理
 * @note MISRA-C:2012合规
 */

#include "irq.h"
#include "printk.h"
#include "types.h"
#include "mm.h"
#include "sched.h"
#include <stddef.h>

/**
 * @brief 中断描述符表
 * @details 索引对应中断号
 */
static irq_desc_t *g_irq_table[1020] = {NULL};

/**
 * @brief Per-CPU 中断嵌套深度
 * @details 每个CPU维护独立的中断深度计数器
 *
 * @note 简单实现：使用数组支持多核（MAX_CPUS=8）
 * @note 未来可优化为真正的 per-CPU 变量
 */
static uint32_t irq_depth_per_cpu[8] = {0};

/**
 * @brief 获取当前CPU的中断深度指针
 * @return 指向当前CPU中断深度的指针
 */
static inline uint32_t *this_cpu_irq_depth(void)
{
    uint32_t cpu = smp_processor_id();
    if (cpu >= 8U)
    {
        /* 不应该发生：CPU ID 超出范围 */
        return &irq_depth_per_cpu[0];
    }
    return &irq_depth_per_cpu[cpu];
}

/**
 * @brief GIC CPU Interface寄存器地址（System Register访问）
 */

/**
 * @brief 读取中断确认寄存器
 * @return 中断号（含CPUID）
 */
static inline uint32_t gic_read_iar(void)
{
    uint32_t iar;
    __asm__ volatile("mrs %0, ICC_IAR1_EL1" : "=r"(iar));
    return iar;
}

/**
 * @brief 写入中断结束寄存器
 * @param eoir 中断号（含CPUID）
 */
static inline void gic_write_eoir(uint32_t eoir)
{
    __asm__ volatile("msr ICC_EOIR1_EL1, %0" ::"r"(eoir));
}

/**
 * @brief 写入中断停用寄存器
 * @param dir 中断号（含CPUID）
 */
static inline void gic_write_dir(uint32_t dir)
{
    __asm__ volatile("msr ICC_DIR_EL1, %0" ::"r"(dir));
}

/**
 * @brief 使能Group 0中断（System Register访问）
 */
static inline void gic_enable_group0(void)
{
    __asm__ volatile("msr ICC_IGRPEN1_EL1, #1" ::: "memory");
}

/**
 * @brief 设置中断优先级掩码（System Register访问）
 * @param mask 优先级掩码
 */
static inline void gic_set_priority_mask(uint32_t mask)
{
    __asm__ volatile("msr ICC_PMR_EL1, %0" ::"r"(mask));
}

/**
 * @brief 注册中断处理函数
 * @param irq 中断号
 * @param handler 处理函数
 * @param arg 参数
 * @return 成功返回0，失败返回负错误码
 */
int irq_register_handler(uint32_t irq, irq_handler_t handler, void *arg)
{
    if (irq >= 1020)
    {
        return -ERROR_INVALID_PARAM;
    }

    if (handler == NULL)
    {
        return -ERROR_INVALID_PARAM;
    }

    /* 检查是否已注册 */
    if (g_irq_table[irq] != NULL)
    {
        printk("[IRQ] Warning: IRQ %u already registered\n", irq);
        return -ERROR_BUSY;
    }

    /* 分配中断描述符 */
    irq_desc_t *desc = (irq_desc_t *)kmalloc(sizeof(irq_desc_t));
    if (desc == NULL)
    {
        return -ERROR_OUT_OF_MEMORY;
    }

    /* 填充描述符 */
    desc->irq = irq;
    desc->handler = handler;
    desc->arg = arg;
    desc->enabled = false;
    desc->next = NULL;

    /* 确定中断类型 */
    if (irq < 16)
    {
        desc->type = IRQ_TYPE_SGI;
        desc->priority = IRQ_PRIORITY_HIGHEST;
    }
    else if (irq < 32)
    {
        desc->type = IRQ_TYPE_PPI;
        desc->priority = IRQ_PRIORITY_HIGH;
    }
    else
    {
        desc->type = IRQ_TYPE_SPI;
        desc->priority = IRQ_PRIORITY_NORMAL;
    }

    /* 注册到表中 */
    g_irq_table[irq] = desc;

    /* 设置默认优先级 */
    irq_set_priority(irq, desc->priority);

    return 0;
}

/**
 * @brief 注销中断处理函数
 * @param irq 中断号
 * @return 成功返回0，失败返回负错误码
 */
int irq_unregister_handler(uint32_t irq)
{
    if (irq >= 1020)
    {
        return -ERROR_INVALID_PARAM;
    }

    irq_desc_t *desc = g_irq_table[irq];
    if (desc == NULL)
    {
        return -ERROR_NOT_FOUND;
    }

    /* 禁用中断 */
    if (desc->enabled)
    {
        irq_disable(irq);
    }

    /* 释放描述符 */
    kfree(desc);
    g_irq_table[irq] = NULL;

    return 0;
}

/**
 * @brief 中断处理入口（汇编调用）
 * @details 由start.S的IRQ异常处理调用
 */
void irq_handler(void)
{
    /* 增加中断深度（进入中断上下文） */
    uint32_t *depth_ptr = this_cpu_irq_depth();
    (*depth_ptr)++;

    /* 读取中断确认寄存器 */
    uint32_t iar = gic_read_iar();
    uint32_t irq = iar & 0x3FF;

    /* 检查是否为伪中断 */
    if (irq >= 1020)
    {
        /* 伪中断，直接结束 */
        gic_write_eoir(iar);
        (*depth_ptr)--;
        return;
    }

    /* 查找中断处理函数 */
    irq_desc_t *desc = g_irq_table[irq];

    if (desc != NULL && desc->handler != NULL)
    {
        /* 调用中断处理函数 */
        desc->handler(irq, desc->arg);
    }
    else
    {
        /* 未注册的处理函数，打印警告 */
        printk("[IRQ] Spurious IRQ: %u\n", irq);
    }

    /* 结断中断处理 */
    gic_write_eoir(iar);

    /* 对于Level触发的SPI，还需要清除挂起位 */
    if (irq >= 32)
    {
        /* 清除SPI挂起位 */
        uint32_t offset = 0x0280 + ((irq / 32) * 4);
        uint32_t mask = 1U << (irq % 32);
        *(volatile uint32_t *)(0x08000000UL + offset) = mask;
    }

    /* 减少中断深度（退出中断上下文） */
    (*depth_ptr)--;
}

/**
 * @brief 检查是否在中断上下文中
 * @return 在中断中返回true，否则返回false
 *
 * @details 通过检查 per-CPU 中断深度计数器判断
 *          - 深度 > 0：在中断上下文中
 *          - 深度 = 0：在进程上下文中
 */
bool in_interrupt(void)
{
    uint32_t *depth_ptr = this_cpu_irq_depth();
    return (*depth_ptr) > 0U;
}

/**
 * @brief 获取当前中断嵌套深度
 * @return 中断嵌套深度（0表示不在中断中）
 *
 * @details 返回 per-CPU 中断深度计数器的当前值
 *          - 0：不在中断中
 *          - 1：在一层中断中
 *          - >1：在中断嵌套中
 */
uint32_t irq_get_depth(void)
{
    uint32_t *depth_ptr = this_cpu_irq_depth();
    return *depth_ptr;
}

/**
 * @brief 初始化中断子系统
 * @return 成功返回0，失败返回负错误码
 */
int irq_init_subsystem(void)
{
    /* 初始化GIC */
    int ret = gic_init();
    if (ret != 0)
    {
        printk("[IRQ] GIC initialization failed: %d\n", ret);
        return ret;
    }

    /* 使能Group 0中断 */
    gic_enable_group0();

    /* 设置优先级掩码（允许所有中断） */
    gic_set_priority_mask(0xFF);

    printk("[IRQ] Interrupt subsystem initialized\n");

    return 0;
}
