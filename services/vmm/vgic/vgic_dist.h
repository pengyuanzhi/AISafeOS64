/**
 * @file    vgic_dist.h
 * @brief   GIC Distributor 模拟
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details GIC Distributor 寄存器模拟：
 *          - GICD_* 寄存器定义和访问
 *          - 中断使能/禁用
 *          - 中断挂起/清除
 *          - 中断优先级设置
 *          - 中断路由设置
 *
 * @note MISRA-C:2012 合规
 * @note 参考 ARM GICv2 Architecture Specification
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_VMM_VGIC_VGIC_DIST_H
#define SERVICES_VMM_VGIC_VGIC_DIST_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * GIC Distributor 寄存器偏移
 * ======================================================================== */

/** @brief Distributor Control Register (RW) */
#define GICD_CTLR_OFFSET          0x0000U

/** @brief Distributor Type Register (RO) */
#define GICD_TYPER_OFFSET         0x0004U

/** @brief Interrupt Set-Enable Registers (RW) */
#define GICD_ISENABLER_OFFSET     0x0100U

/** @brief Interrupt Clear-Enable Registers (RW) */
#define GICD_ICENABLER_OFFSET     0x0180U

/** @brief Interrupt Set-Pending Registers (RW) */
#define GICD_ISPENDR_OFFSET       0x0200U

/** @brief Interrupt Clear-Pending Registers (RW) */
#define GICD_ICPENDR_OFFSET       0x0280U

/** @brief Interrupt Active Registers (RO) */
#define GICD_IABR_OFFSET          0x0300U

/** @brief Interrupt Priority Registers (RW) */
#define GICD_IPRIORITYR_OFFSET    0x0400U

/** @brief Interrupt Processor Targets Registers (RW) */
#define GICD_ITARGETSR_OFFSET      0x0800U

/** @brief Interrupt Configuration Registers (RW) */
#define GICD_ICFGR_OFFSET         0x0C00U

/** @brief Software Generated Interrupt Register (WO) */
#define GICD_SGIR_OFFSET          0x0F00U

/* ========================================================================
 * GIC Distributor 寄存器位定义
 * ======================================================================== */

/** @brief GICD_CTLR - Enable */
#define GICD_CTLR_ENABLE           (1U << 0)

/** @brief GICD_TYPER - ITLinesNumber (number of implemented interrupt lines) */
#define GICD_TYPER_ITLINESN_MASK  (0x1FU << 0)

/** @brief GICD_TYPER - CPU number (minus 1) */
#define GICD_TYPER_CPUNUM_MASK     (0x7U << 5)

/** @brief GICD_SGIR - Target List Filter */
#define GICD_SGIR_TL_MASK         (0x3U << 24)

/** @brief GICD_SGIR - Target List Filter: Target list is in the SGIList field */
#define GICD_SGIR_TL_LIST          (0x0U << 24)

/** @brief GICD_SGIR - Target List Filter: Send to all CPUs except self */
#define GICD_SGIR_TL_ALL_OTHERS     (0x2U << 24)

/** @brief GICD_SGIR - Target List Filter: Send to self only */
#define GICD_SGIR_TL_SELF          (0x3U << 24)

/** @brief GICD_SGIR - CPU Target List */
#define GICD_SGIR_CPULIST_MASK     (0xFFU << 16)

/** @brief GICD_SGIR - SGIINTID */
#define GICD_SGIR_SGIINTID_MASK    (0xFU << 0)

/* ========================================================================
 * GIC Distributor 寄存器访问函数
 * ======================================================================== */

/**
 * @brief 读取 GIC Distributor 寄存器
 *
 * @param vm_id   VM ID
 * @param offset  寄存器偏移
 * @param size    访问大小（1/2/4 字节）
 * @param value   输出值
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t vgic_dist_read(uint32_t vm_id, uint32_t offset,
                                 uint32_t size, uint32_t *value);

/**
 * @brief 写入 GIC Distributor 寄存器
 *
 * @param vm_id   VM ID
 * @param offset  寄存器偏移
 * @param size    访问大小（1/2/4 字节）
 * @param value   写入值
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t vgic_dist_write(uint32_t vm_id, uint32_t offset,
                                  uint32_t size, uint32_t value);

#endif /* SERVICES_VMM_VGIC_VGIC_DIST_H */
