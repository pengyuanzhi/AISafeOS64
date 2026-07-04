/**
 * @file    virt_phys.h
 * @brief   虚拟地址 / 物理地址转换基础设施（TTBR1 高地址线性映射）
 * @author  AISafe64 Team
 * @date    2026-07-03
 * @version 1.0
 *
 * @details 标准 ARM64 双地址空间下的内核高地址线性映射偏移量与
 *          地址转换辅助函数。
 *
 *          内存布局（QEMU virt 平台）:
 *          - TTBR0 = 用户态低地址空间（VA[63]=0）
 *          - TTBR1 = 内核高地址空间（VA[63]=1）
 *          - 内核线性映射: VA = PA + KERNEL_VA_OFFSET
 *            其中 KERNEL_VA_OFFSET = 0xFFFF000000000000ULL
 *          - 内核链接基址: 0xFFFF000040000000（虚拟）
 *            QEMU 物理加载基址: 0x40000000
 *
 *          这样所有内核符号（代码/数据/BSS）的虚拟地址 - 偏移量
 *          即等于物理地址，实现统一的 virt_to_phys/phys_to_virt。
 *
 * @note    对应需求: KR-005（虚拟内存管理）TTBR1 高地址迁移
 * @note    MISRA-C:2012 合规
 */
#ifndef KERNEL_VIRT_PHYS_H
#define KERNEL_VIRT_PHYS_H

#include <kernel/types.h>
#include <stdint.h>

/**
 * @brief 内核高地址线性映射偏移量（TTBR1 区域）
 *
 * @details 内核空间起始地址 = CONFIG_KERNEL_VADDR_BASE = 0xFFFF000000000000
 *          所有内核符号: VA = PA + KERNEL_VA_OFFSET
 */
#define KERNEL_VA_OFFSET  0xFFFF000000000000ULL

/**
 * @brief 虚拟地址 → 物理地址（内核线性映射）
 *
 * @param va 内核态虚拟地址指针（必须在线性映射范围内）
 * @return 对应的物理地址
 *
 * @note 仅适用于内核线性映射区。用户态地址或未映射地址不可使用。
 */
static inline paddr_t virt_to_phys(const void *va)
{
    return (paddr_t)((uintptr_t)va - KERNEL_VA_OFFSET);
}

/**
 * @brief 物理地址 → 虚拟地址（内核线性映射）
 *
 * @param pa 物理地址（必须在线性映射覆盖范围内）
 * @return 对应的内核态虚拟地址指针
 *
 * @note 仅适用于已建立线性映射的物理区域（RAM/Device）。
 */
static inline void *phys_to_virt(paddr_t pa)
{
    return (void *)(uintptr_t)(pa + KERNEL_VA_OFFSET);
}

#endif /* KERNEL_VIRT_PHYS_H */
