/**
 * @file    mmu.c
 * @brief   ARM64 MMU 早期初始化（恒等映射）
 * @author  AISafe64 Team
 * @date    2026-04-05
 * @version 1.0
 *
 * @details 在内核 C 入口中启用 MMU，使用恒等映射（物理地址=虚拟地址）。
 *          仅映射第一个 1GB 区域（覆盖内核代码和设备 MMIO）。
 *          使用 2 级页表: PGD(Level 0) -> Level 1 1GB Block。
 *
 * @note    对应需求: KR-005（虚拟内存管理）
 */

#include <stdint.h>
#include <kernel/types.h>

/* ========== 页表项标志位定义 ========== */

#define PTE_VALID        (1ULL << 0U)   /**< 有效位 */
#define PTE_TABLE_BIT    (1ULL << 1U)   /**< 表描述符 */
#define PTE_BLOCK_BIT    (1ULL << 1U)   /**< 块描述符 */
#define PTE_AF           (1ULL << 10U)  /**< Access Flag */
#define PTE_SH_INNER     (3ULL << 8U)  /**< Inner Shareable */
#define PTE_ATTR_NORMAL  (0ULL << 2U)  /**< MAIR attr 0 */
#define PTE_AP_RW        (0ULL << 53U) /**< EL1 RW */

#define MAIR_NORMAL      0xFFULL
#define MAIR_DEVICE      0x04ULL

/* ========== 静态页表（BSS 段） ========== */

static uint64_t s_pgd[512U] __attribute__((aligned(4096U)));
static uint64_t s_l1_table[512U] __attribute__((aligned(4096U)));

/* ========================================================================
 * MMU 早期初始化
 * ======================================================================== */

/**
 * @brief  MMU 早期初始化（恒等映射）
 *
 * @details 建立 1GB 恒等映射并启用 MMU:
 *          1. 清零页表
 *          2. PGD[0] -> Level 1 页表
 *          3. Level 1[0] = 1GB Normal RW Block (0x0-0x3FFFFFFF)
 *          4. 设置 MAIR_EL1 / TCR_EL1 / TTBR0_EL1
 *          5. 刷新 TLB
 *          6. 启用 SCTLR_EL1.M 位
 */
void mmu_early_init(void)
{
    uint64_t mair_val;
    uint64_t tcr_val;
    uint64_t pgd_paddr;
    uint64_t sctlr_val;
    uint64_t l1_paddr;
    volatile uint64_t *ptr;
    uint32_t i;

    /* ---- 清零页表 ---- */
    ptr = s_pgd;
    for (i = 0U; i < 512U; i++)
    {
        ptr[i] = 0ULL;
    }

    ptr = s_l1_table;
    for (i = 0U; i < 512U; i++)
    {
        ptr[i] = 0ULL;
    }

    /* ---- PGD[0] -> Level 1 页表 ---- */
    l1_paddr = (uint64_t)(uintptr_t)&s_l1_table[0U];
    s_pgd[0U] = PTE_VALID | PTE_TABLE_BIT | l1_paddr;

    /* ---- Level 1[0] = 1GB Normal RW Block ---- */
    s_l1_table[0U] = PTE_VALID
                   | PTE_BLOCK_BIT
                   | PTE_AF
                   | PTE_SH_INNER
                   | PTE_ATTR_NORMAL
                   | PTE_AP_RW;

    /* ---- 设置 MAIR_EL1 ---- */
    mair_val = (MAIR_DEVICE << 8U) | MAIR_NORMAL;
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair_val));
    __asm__ volatile("isb" ::: "memory");

    /* ---- 设置 TCR_EL1 ---- */
    tcr_val = (16ULL << 0U)    /* T0SZ = 16 */
            | (3ULL << 8U)     /* IRGN0 = WB */
            | (3ULL << 10U)    /* ORGN0 = WB */
            | (3ULL << 12U)    /* SH0 = Inner */
            | (0ULL << 14U)    /* TG0 = 4KB */
            | (16ULL << 16U)   /* T1SZ = 16 */
            | (3ULL << 24U)    /* IRGN1 = WB */
            | (3ULL << 26U)    /* ORGN1 = WB */
            | (3ULL << 28U)    /* SH1 = Inner */
            | (2ULL << 30U)    /* TG1 = 4KB */
            | (0ULL << 32U);   /* IPS = 4GB */
    __asm__ volatile("msr tcr_el1, %0" :: "r"(tcr_val));
    __asm__ volatile("isb" ::: "memory");

    /* ---- 写入 TTBR0_EL1 ---- */
    pgd_paddr = (uint64_t)(uintptr_t)&s_pgd[0U];
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(pgd_paddr));
    __asm__ volatile("isb" ::: "memory");

    /* ---- 刷新 TLB ---- */
    __asm__ volatile("tlbi vmalle1is" ::: "memory");
    __asm__ volatile("tlbi vmalle1is" ::: "memory");
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");

    /* ---- 启用 MMU ---- */
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr_val));
    sctlr_val |= 1ULL;
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr_val));
    __asm__ volatile("isb" ::: "memory");
}
