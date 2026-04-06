#ifndef KERNEL_MMU_H
#define KERNEL_MMU_H

#include <stdint.h>

/**
 * @brief MMU 早期初始化（PGD → PUD 1GB Block 双地址空间映射）
 */
void mmu_early_init(void);

/**
 * @brief 从核 MMU 初始化
 * @details 从核加载与主核相同的 TTBR0/TTBR1 页表并启用 MMU
 */
void mmu_init_secondary(void);

/**
 * @brief 创建用户态页表
 * @return PGD 物理地址，失败返回 0
 */
uint64_t mmu_create_user_pgd(void);

/**
 * @brief 销毁用户态页表
 * @param pgd_paddr PGD 物理地址
 */
void mmu_destroy_user_pgd(uint64_t pgd_paddr);

/**
 * @brief 切换到用户态地址空间
 * @param user_pgd_paddr 用户态 PGD 物理地址
 */
void mmu_switch_to_user(uint64_t user_pgd_paddr);

/**
 * @brief 切回内核态地址空间
 */
void mmu_switch_to_kernel(void);

#endif /* KERNEL_MMU_H */
