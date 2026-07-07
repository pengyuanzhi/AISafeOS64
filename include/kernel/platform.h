/**
 * @file    platform.h
 * @brief   平台硬件配置抽象
 * @author  AISafe64 Team
 * @date    2026-07-07
 * @version 1.0
 *
 * @details 所有硬件地址通过此文件集中管理。
 *          当前平台：QEMU virt (ARM Cortex-A57)
 *          后续支持其他平台时，通过编译时选择不同 platform_*.h。
 *
 *          未来计划：引入 libfdt 从设备树动态解析，
 *          当前阶段使用编译时常量（单一真源）。
 *
 * @revision history
 * v1.0 2026-07-07 初始版本（从 hal.h/gic.c 硬编码提取）
 */

#ifndef KERNEL_PLATFORM_H
#define KERNEL_PLATFORM_H

#include <stdint.h>
#include <kernel/virt_phys.h>

/* ========================================================================
 * QEMU virt 平台硬件地址（物理地址）
 * 经线性映射后使用：VA = PA + KERNEL_VA_OFFSET
 * ======================================================================== */

/** @brief UART PL011 物理基地址 */
#define PLATFORM_UART_PHYS       0x09000000ULL

/** @brief UART PL011 虚拟基地址（线性映射后） */
#define PLATFORM_UART_VA         (PLATFORM_UART_PHYS + KERNEL_VA_OFFSET)

/** @brief GIC Distributor 物理基地址 */
#define PLATFORM_GICD_PHYS       0x08000000ULL

/** @brief GIC Distributor 虚拟基地址 */
#define PLATFORM_GICD_VA         (PLATFORM_GICD_PHYS + KERNEL_VA_OFFSET)

/** @brief GIC CPU Interface 物理基地址 */
#define PLATFORM_GICC_PHYS       0x08010000ULL

/** @brief GIC CPU Interface 虚拟基地址 */
#define PLATFORM_GICC_VA         (PLATFORM_GICC_PHYS + KERNEL_VA_OFFSET)

/** @brief virtio-mmio 物理基地址 */
#define PLATFORM_VIRTIO_PHYS     0x0A000000ULL

/** @brief virtio-mmio 虚拟基地址 */
#define PLATFORM_VIRTIO_VA       (PLATFORM_VIRTIO_PHYS + KERNEL_VA_OFFSET)

/** @brief 物理内存起始地址（RAM 基址） */
#define PLATFORM_RAM_PHYS        0x40000000ULL

/** @brief UART IRQ 号 */
#define PLATFORM_UART_IRQ        33U

/** @brief 通用定时器 IRQ 号（PPI） */
#define PLATFORM_TIMER_IRQ       30U

/** @brief GIC 虚拟中断最大数 */
#define PLATFORM_GIC_MAX_IRQ     128U

/* ========================================================================
 * 设备树
 * ======================================================================== */

/**
 * @brief 获取设备树（DTB）基地址
 *
 * @details QEMU -kernel 启动时 x0 寄存器传递 DTB 物理地址。
 *          boot.S 将其保存到 __dtb_ptr 全局变量。
 *          后续引入 libfdt 后从此地址解析硬件配置。
 *
 * @return DTB 物理地址（0 = 无设备树）
 */
uint64_t platform_get_dtb_addr(void);

#endif /* KERNEL_PLATFORM_H */
