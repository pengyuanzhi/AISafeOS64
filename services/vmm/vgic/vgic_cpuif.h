/**
 * @file    vgic_cpuif.h
 * @brief   GIC CPU Interface 模拟
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details GIC CPU Interface 寄存器模拟：
 *          - GICC_* 寄存器定义和访问
 *          - 中断优先级屏蔽（PMR）
 *          - 中断二进制点（BPR）
 *          - 中断确认（IAR）
 *          - 中断结束（EOIR）
 *          - 运行优先级（RPR）
 *
 * @note MISRA-C:2012 合规
 * @note 参考 ARM GICv2 Architecture Specification
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_VMM_VGIC_VGIC_CPUIF_H
#define SERVICES_VMM_VGIC_VGIC_CPUIF_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * GIC CPU Interface 寄存器偏移
 * ======================================================================== */

/** @brief CPU Interface Control Register (RW) */
#define GICC_CTLR_OFFSET          0x0000U

/** @brief Interrupt Priority Mask Register (RW) */
#define GICC_PMR_OFFSET           0x0004U

/** @brief Binary Point Register (RW) */
#define GICC_BPR_OFFSET           0x0008U

/** @brief Interrupt Acknowledge Register (RO) */
#define GICC_IAR_OFFSET           0x000CU

/** @brief End of Interrupt Register (WO) */
#define GICC_EOIR_OFFSET          0x0010U

/** @brief Running Priority Register (RO) */
#define GICC_RPR_OFFSET           0x0014U

/** @brief Highest Priority Pending Interrupt Register (RO) */
#define GICC_HPPIR_OFFSET         0x0018U

/** @brief Aliased Binary Point Register (RW) */
#define GICC_ABPR_OFFSET          0x001CU

/* ========================================================================
 * GIC CPU Interface 寄存器位定义
 * ======================================================================== */

/** @brief GICC_CTLR - Enable Group 0 */
#define GICC_CTLR_ENABLE_GRP0      (1U << 0)

/** @brief GICC_CTLR - Enable Group 1 */
#define GICC_CTLR_ENABLE_GRP1      (1U << 1)

/** @brief GICC_CTLR - FIQ En bypass */
#define GICC_CTLR_FIQBYP           (1U << 5)

/** @brief GICC_CTLR - IRQ En bypass */
#define GICC_CTLR_IRQBYP           (1U << 6)

/** @brief GICC_CTLR - Common Bypass */
#define GICC_CTLR_CBPR             (1U << 7)

/** @brief GICC_CTLR - EOI Mode */
#define GICC_CTLR_EOImode          (1U << 9)

/** @brief GICC_IAR - CPUID */
#define GICC_IAR_CPUID_MASK        (0x3FFU << 10)

/** @brief GICC_IAR - Interrupt ID */
#define GICC_IAR_INTID_MASK        (0x3FFU << 0)

/** @brief GICC_IAR - Spurious interrupt ID (1023) */
#define GICC_IAR_SPURIOUS          (1023U)

/** @brief GICC_EOIR - CPUID */
#define GICC_EOIR_CPUID_MASK       (0x3FFU << 10)

/** @brief GICC_EOIR - Interrupt ID */
#define GICC_EOIR_INTID_MASK       (0x3FFU << 0)

/* ========================================================================
 * GIC CPU Interface 寄存器访问函数
 * ======================================================================== */

/**
 * @brief 读取 GIC CPU Interface 寄存器
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param offset  寄存器偏移
 * @param size    访问大小（1/2/4 字节）
 * @param value   输出值
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOENT VM 或 vCPU 不存在
 */
kernel_status_t vgic_cpuif_read(uint32_t vm_id, uint32_t vcpu_id,
                                  uint32_t offset, uint32_t size,
                                  uint32_t *value);

/**
 * @brief 写入 GIC CPU Interface 寄存器
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param offset  寄存器偏移
 * @param size    访问大小（1/2/4 字节）
 * @param value   写入值
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOENT VM 或 vCPU 不存在
 */
kernel_status_t vgic_cpuif_write(uint32_t vm_id, uint32_t vcpu_id,
                                   uint32_t offset, uint32_t size,
                                   uint32_t value);

/**
 * @brief 获取最高优先级待处理中断
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param irq     输出中断号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOENT VM 或 vCPU 不存在
 */
kernel_status_t vgic_get_highest_priority_irq(uint32_t vm_id,
                                              uint32_t vcpu_id,
                                              uint32_t *irq);

/**
 * @brief 中断确认（ACK）
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param irq     中断号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOENT VM 或 vCPU 不存在
 */
kernel_status_t vgic_ack_irq(uint32_t vm_id, uint32_t vcpu_id,
                             uint32_t irq);

/**
 * @brief 中断结束（EOI）
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param irq     中断号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOENT VM 或 vCPU 不存在
 */
kernel_status_t vgic_end_irq(uint32_t vm_id, uint32_t vcpu_id,
                             uint32_t irq);

#endif /* SERVICES_VMM_VGIC_VGIC_CPUIF_H */
