/**
 * @file    hal_intc.h
 * @brief   中断控制器硬件抽象层（HAL）接口
 * @author  AISafe64 Team
 * @date    2026-07-04
 * @version 1.0
 *
 * @details 本文件定义了架构无关的中断控制器抽象接口（hal_intc）。
 *          内核核心（kernel/irq/、kernel/driver/ 等）通过本接口访问
 *          中断控制器，不直接依赖具体硬件（如 ARM GIC）。
 *
 *          具体硬件实现作为 hal_intc 的后端：
 *          - ARM64：kernel/arch/arm64/gic.c（GICv2 后端）
 *
 *          该抽象使内核核心与中断控制器硬件解耦，便于移植到
 *          其他架构（如 RISC-V AIA、x86 APIC）。
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: IN-001~006
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_HAL_INTC_H
#define KERNEL_HAL_INTC_H

#include <kernel/types.h>
#include <stdbool.h>
#include <stdint.h>

/* ========================================================================
 * 中断触发类型（架构无关）
 * ======================================================================== */

/**
 * @brief 中断触发类型
 *
 * @details 描述中断信号的有效电平/边沿特性。
 *          不同硬件后端负责将本类型映射到具体的寄存器配置
 *          （如 GIC 的 GICD_ICFGR 边沿/电平位）。
 */
typedef enum
{
    IRQ_TRIGGER_EDGE_RISING  = 0U,  /**< @brief 上升沿触发 */
    IRQ_TRIGGER_EDGE_FALLING = 1U,  /**< @brief 下降沿触发 */
    IRQ_TRIGGER_LEVEL_HIGH   = 2U,  /**< @brief 高电平触发 */
    IRQ_TRIGGER_LEVEL_LOW    = 3U   /**< @brief 低电平触发 */
} irq_trigger_t;

/* ========================================================================
 * 初始化
 * ======================================================================== */

/**
 * @brief 初始化中断控制器（主核）
 *
 * @details 初始化中断控制器的 Distributor 和当前 CPU 的 CPU Interface。
 *          使能所有 SGI/PPI，禁用所有 SPI。
 */
void hal_intc_init(void);

/**
 * @brief 初始化中断控制器（从核）
 *
 * @details 从核启动后调用，仅初始化当前 CPU 的 CPU Interface。
 */
void hal_intc_init_secondary(void);

/* ========================================================================
 * 单线控制
 * ======================================================================== */

/**
 * @brief 使能指定中断
 *
 * @param irq 中断号
 */
void hal_intc_enable(uint32_t irq);

/**
 * @brief 禁用指定中断
 *
 * @param irq 中断号
 */
void hal_intc_disable(uint32_t irq);

/* ========================================================================
 * 配置
 * ======================================================================== */

/**
 * @brief 设置中断优先级
 *
 * @param irq  中断号
 * @param prio 优先级（0 = 最高，255 = 最低）
 */
void hal_intc_set_priority(uint32_t irq, uint8_t prio);

/**
 * @brief 设置中断亲和性（目标 CPU）
 *
 * @param irq      中断号
 * @param cpu_mask CPU 位掩码（bit 0 = CPU0，bit 1 = CPU1，...）
 */
void hal_intc_set_affinity(uint32_t irq, uint32_t cpu_mask);

/**
 * @brief 设置中断触发类型
 *
 * @param irq     中断号
 * @param trigger 触发类型
 */
void hal_intc_set_trigger(uint32_t irq, irq_trigger_t trigger);

/* ========================================================================
 * 中断处理
 * ======================================================================== */

/**
 * @brief 确认并获取当前最高优先级挂起中断号
 *
 * @details 读取中断确认寄存器（如 GICC_IAR），返回当前挂起的
 *          最高优先级中断号。读取后中断状态从 "挂起" 转为 "活跃"。
 *
 * @return 中断号，无挂起中断返回伪中断号
 */
uint32_t hal_intc_acknowledge(void);

/**
 * @brief 通知中断处理完成（End Of Interrupt）
 *
 * @param irq 中断号
 */
void hal_intc_eoi(uint32_t irq);

/**
 * @brief 判断是否为伪中断
 *
 * @param irq 中断号
 * @return true 表示伪中断（无有效挂起中断）
 */
bool hal_intc_is_spurious(uint32_t irq);

/* ========================================================================
 * 中断类型判定
 * ======================================================================== */

/**
 * @brief 判断是否为 SGI（软件生成中断）
 *
 * @param irq 中断号
 * @return true 表示是 SGI（ARM GIC: 0-15）
 */
bool hal_intc_is_sgi(uint32_t irq);

/**
 * @brief 判断是否为 PPI（私有外设中断）
 *
 * @param irq 中断号
 * @return true 表示是 PPI（ARM GIC: 16-31）
 */
bool hal_intc_is_ppi(uint32_t irq);

/**
 * @brief 判断是否为 SPI（共享外设中断）
 *
 * @param irq 中断号
 * @return true 表示是 SPI（ARM GIC: >= 32）
 */
bool hal_intc_is_spi(uint32_t irq);

/* ========================================================================
 * IPI（核间中断）
 * ======================================================================== */

/**
 * @brief 发送核间中断（IPI / SGI）
 *
 * @param cpu_mask 目标 CPU 位掩码
 * @param ipi_type IPI 类型编号
 */
void hal_intc_send_ipi(uint32_t cpu_mask, uint32_t ipi_type);

#endif /* KERNEL_HAL_INTC_H */
