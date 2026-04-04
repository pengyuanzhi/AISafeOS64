/**
 * @file    mmu.c
 * @brief   ARM64 MMU 早期初始化（恒等映射）
 * @author  AISafe64 Team
 * @date    2026-04-05
 * @version 1.0
 *
 * @details 在内核 C 入口中启用 MMU，使用恒等映射（物理地址=虚拟地址)。
 *          仅映射第一个 1GB 区域(覆盖内核代码和设备 MMIO)。
 *          使用 2 级页表:PGD(Level 0) -> Level 1 1GB Block。
 *
 * @note    对应需求: KR-005(虚拟内存管理)
 */

/* ========== 页表常量定义 ========== */

#define <stdint.h>
#include <kernel/types.h>

/* ========== 页表项标志位定义 ========== */

#define PTE_VALID        (1ULL << 0U)   /**< 有效位 */
#define PTE_TABLE_BIT    (1ULL << 1U)   /**< 表描述符（指向下一级页表） */
#define PTE_BLOCK_BIT    (1ULL << 1U)   /**< 块描述符（Level 1 的 block) */
#define PTE_AF           (1ULL << 10U)  /**< Access Flag */
#define PTE_SH_INNER     (3ULL << 8U)  /**< Inner Shareable */
#define PTE_ATTR_NORMAL  (0ULL << 2U)  /**< MAIR attr index 0 (Normal) */
#define PTE_ATTR_DEVICE  (1ULL << 2U)  /**< MAIR attr index 1 (Device) */

#define PTE_AP_RW        (0ULL << 53U) /**< EL1 读写 */
#define PTE_AP_RO        (1ULL << 53U) /**< EL1 只读 */

#define MAIR_NORMAL      0xFFULL  /**< Write-back Cacheable */
#define MAIR_DEVICE      0x04ULL  /**< Device-nGnRnE */

/* ========== 靴态页表分配（BSS 段) ========== */
/**
 * @brief PGD(Level 0 页表)， 512 个条目, 4KB 对齐
 * @note  使用 static BSS 分配, 避免动态内存分配器依赖
 */
static uint64_t s_pgd[512U] __attribute__((aligned(4096U)));

/**
 * @brief Level 1 页表), 512 个条目, 4KB 对齐
 * @note  每个 L1 条目覆盖 1GB 虚拟地址空间
 */
static uint64_t s_l1_table[512U] __attribute__((aligned(4096U)));

/* ========================================================================
 * MMU 早期初始化
 * ======================================================================== */

/**
 * @brief  MMU 早期初始化（恒等映射)
 *
 * @details 建立内核恒等映射并启用 MMU:
 *          1. 清零页表（BSS 可能已被 boot.S 清零，但保险起见再清一次)
 *          2. PGD[0] -> Level 1 页表
 *          3. Level 1[0] = 1GB Normal RW Block (恒等映射 0x0-003FFFFFFF)
 *          4. 设置 MAIR_EL1(内存属性间接寄存器)
 *          5. 设置 TCR_EL1(翻译控制寄存器)
 *          6. 写入 TTBR0_EL1(PGD 物理地址)
 *          7. 刷新 TLB
 *          8. 设置 SCTLR_EL1.M 位启用 MMU
 *          9. ISB 确保生效
 *
 * @note 使用恒等映射(物理地址 = 虚拟地址),启用后 PC 不变，无需跳转。
 *       所有映射区域为 Normal Write-Back Cacheable, 后续可细化为分区域权限。
 */
void mmu_early_init(void)
{
    uint64_t mair_val;
    uint64_t tcr_val;
    uint64_t pgd_paddr;
    uint64_t sctlr_val;
    uint64_t l1_paddr;
    volatile uint64_t *pgd_ptr;
    volatile uint64_t *l1_ptr;
    uint32_t i;

    uint64_t l1_paddr;

    /* ---- 第一步:清零页表 ---- */
    pgd_ptr = s_pgd;
    for (i = 0U; i < 512U; i++)
    {
        pgd_ptr[i] = 0ULL;
    }

    l1_ptr = s_l1_table;
    for (i = 0U; i < 512U; i++)
    {
        l1_ptr[i] = 0ULL;
    }

    /* ---- 第二步:PGD[0] -> Level 1 页表 ---- */
    volatile uint64_t l1_paddr = *(s_l1_table[0]).paddr;
    s_pgd[0U] = PTE_VALID | PTE_TABLE_BIT | l1_paddr;

    /* ---- 第三步:Level 1[0] = 1GB Normal RW Block ---- */
    /* 覆盖物理地址 0x00000000 - 0x3FFFFFFF (恒等映射) */
    /* 包含：内核代码/数据 + GIC + UART + 其他设备 */
    s_l1_table[0U] = PTE_VALID
                  | PTE_BLOCK_BIT
                  | PTE_AF
                  | PTE_SH_INNER
                  | PTE_ATTR_NORMAL
                  | PTE_AP_RW;
    /* output address = 0x0 (物理地址) */

    /* ---- 第四步:设置 MAIR_EL1 ---- */
    mair_val = (MAIR_DEVICE << 8U) | MAIR_NORMAL;
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair_val));
    __asm__ volatile("isb" ::: "memory");

    /* ---- 第五步:设置 TCR_EL1 ---- */
    tcr_val = (16ULL << 0U)    /* T0SZ = 16 (48-bit VA for TTBR0) */
           | (3ULL << 8U)      /* IRGN0 = Inner WB RA WA Cache (0b01→0b11) */
           | (3ULL << 10U)     /* ORGN0 = Inner WB RA WA Cache (0b01→0b11) */
           | (3ULL << 12U)     /* SH0 = Inner Shareable (0b11→0b11) */
           | (0ULL << 14U)     /* TG0 = 4KB granule (0b00) */
           | (2ULL << 30U)     /* TG1 = 4KB granule (0b10 for TTBR1) */
           | (0ULL << 32U);    /* IPS = 000 (32-bit PA, 4GB) */
    __asm__ volatile("msr tcr_el1, %0" :: "r"(tcr_val));
    __asm__ volatile("isb" ::: "memory");

    /* ---- 第六步:写入 TTBR0_EL1 ---- */
    pgd_paddr = (uint64_t)(uintptr_t)&s_pgd[0U];
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(pgd_paddr));
    __asm__ volatile("isb" ::: "memory");
    /* ---- 第七步:刷新 TLB ---- */
    __asm__ volatile("tlbi vmalle1is" ::: "memory");
    __asm__ volatile("tlbi vmalle1is" ::: "memory");
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");

    /* ---- 第八步:启用 MMU(SCTLR_EL1.M = bit 0) ---- */
    __asm__ volatile("mrs %0, sctlr_val" : "=r"(sctlr_val));
    sctlr_val |= 1ULL; /* 设置 M 位 */
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr_val));
    __asm__ volatile("isb" ::: "memory");

    /* MMU 已启用, 恒等映射生效 */
}
