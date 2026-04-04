/**
 * @file    gic.h
 * @brief   ARM GICv2（通用中断控制器）驱动接口
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 3.0
 *
 * @details 本文件定义了 ARM GICv2 (GIC-400) 驱动接口：
 *          - GIC Distributor 和 CPU Interface 初始化
 *          - 中断优先级管理
 *          - 中断亲和性配置（ITARGETSR）
 *          - 中断使能/禁用
 *          - 中断处理（EOI）
 *
 *          QEMU virt 平台 GICv2 地址映射：
 *          - GICD (Distributor):  0x08000000
 *          - GICC (CPU Interface): 0x08010000
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: IN-001~006
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_GIC_H
#define KERNEL_GIC_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * GIC 配置常量
 * ======================================================================== */

/** @brief 最大 SPI 中断号（共享外设中断） */
#define GIC_MAX_SPI             987U

/** @brief SGI 中断号范围起始（软件生成中断 0-15） */
#define GIC_SGI_BASE            0U

/** @brief SGI 中断号范围结束 */
#define GIC_SGI_END             15U

/** @brief PPI 中断号范围起始（私有外设中断 16-31） */
#define GIC_PPI_BASE            16U

/** @brief PPI 中断号范围结束 */
#define GIC_PPI_END             31U

/** @brief SPI 中断号范围起始（共享外设中断 32-987） */
#define GIC_SPI_BASE            32U

/** @brief 最大中断优先级（数值越小优先级越高） */
#define GIC_PRIORITY_MAX        255U

/** @brief 最低优先级（最低紧迫度） */
#define GIC_PRIORITY_LOWEST     255U

/** @brief 最高优先级（最高紧迫度） */
#define GIC_PRIORITY_HIGHEST    0U

/** @brief 默认中断优先级 */
#define GIC_PRIORITY_DEFAULT    128U

/* ========================================================================
 * 中断触发模式
 * ======================================================================== */

/**
 * @brief 中断触发模式
 */
typedef enum
{
    GIC_TRIGGER_EDGE = 0U,          /**< @brief 边沿触发 */
    GIC_TRIGGER_LEVEL               /**< @brief 电平触发 */
} gic_trigger_mode_t;

/* ========================================================================
 * 中断处理函数类型
 * ======================================================================== */

/**
 * @brief 中断处理函数类型
 *
 * @param irq 中断号
 * @param arg 用户参数
 */
typedef void (*irq_handler_t)(uint32_t irq, void *arg);

/* ========================================================================
 * GIC 驱动 API
 * ======================================================================== */

/**
 * @brief 初始化 GIC
 *
 * @details 初始化 GIC Distributor 和当前 CPU 的 CPU Interface。
 *          使能所有 SGI/PPI，禁用所有 SPI。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: IN-001
 */
kernel_status_t gic_init(void);

/**
 * @brief 初始化从核的 GIC CPU Interface
 *
 * @details 从核启动后调用，仅初始化 CPU Interface。
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t gic_init_secondary(void);

/**
 * @brief 使能指定中断
 *
 * @param irq 中断号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 中断号无效
 *
 * @note 对应需求: IN-002
 */
kernel_status_t gic_enable_irq(uint32_t irq);

/**
 * @brief 禁用指定中断
 *
 * @param irq 中断号
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: IN-002
 */
kernel_status_t gic_disable_irq(uint32_t irq);

/**
 * @brief 设置中断优先级
 *
 * @param irq      中断号
 * @param priority 优先级（0 = 最高，255 = 最低）
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: IN-003
 */
kernel_status_t gic_set_priority(uint32_t irq, uint8_t priority);

/**
 * @brief 获取中断优先级
 *
 * @param irq 中断号
 *
 * @return 优先级值
 */
uint8_t gic_get_priority(uint32_t irq);

/**
 * @brief 设置中断亲和性（目标 CPU）
 *
 * @details 仅对 SPI 有效。指定中断应发送到哪些 CPU。
 *
 * @param irq      中断号
 * @param cpu_mask CPU 位掩码（bit 0 = CPU0，bit 1 = CPU1，...）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: IN-004
 */
kernel_status_t gic_set_affinity(uint32_t irq, uint8_t cpu_mask);

/**
 * @brief 获取当前最高优先级挂起中断号
 *
 * @return 中断号，无挂起中断返回 1023（spurious）
 */
uint32_t gic_acknowledge_irq(void);

/**
 * @brief 通知中断处理完成（EOI）
 *
 * @param irq 中断号
 */
void gic_end_of_interrupt(uint32_t irq);

/**
 * @brief 发送 SGI（软件生成中断）
 *
 * @param sgi_id   SGI 编号（0-15）
 * @param cpu_mask 目标 CPU 位掩码
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: IN-005
 */
kernel_status_t gic_send_sgi(uint32_t sgi_id, uint8_t cpu_mask);

/**
 * @brief 注册中断处理函数
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
                                       void *arg);

/**
 * @brief 设置中断触发模式
 *
 * @param irq    中断号（仅 SPI）
 * @param mode   触发模式
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t gic_set_trigger_mode(uint32_t irq, gic_trigger_mode_t mode);

#endif /* KERNEL_GIC_H */
