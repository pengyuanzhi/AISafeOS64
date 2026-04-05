/**
 * @file    mmu.c
 * @brief   ARM64 MMU 双地址空间初始化（TTBR0/TTBR1）
 * @author  AISafe64 Team
 * @date    2026-04-05
 * @version 2.0
 *
 * @details 实现 ARM64 双地址空间 MMU 映射:
 *          - TTBR0_EL1: 用户态低地址空间（0x0000_0000_0000 - 0x0000_7FFF_FFFF_FFFF）
 *          - TTBR1_EL1: 内核态高地址空间（0xFFFF_0000_0000_0000 - 0xFFFF_FFFF_FFFF_FFFF）
 *
 *          当前策略（阶段1）:
 *          - 内核通过 TTBR0 恒等映射运行（物理地址 = 虚拟地址）
 *          - TTBR1 设置好高地址映射（为将来完全切换做准备）
 *          - TCR_EL1 配置 T0SZ=T1SZ=25（39位地址空间，512GB 每边）
 *          - MAIR_EL1: Attr0=Normal WB, Attr1=Device nGnRE
 *
 *          页表结构（2级: PGD → PUD 1GB Block）:
 *          TTBR0 页表:
 *            PGD[0] → PUD 表（覆盖 0x00000000-0x3FFFFFFF）
 *            PGD[1] → PUD 表（覆盖 0x40000000-0x7FFFFFFF）
 *              PUD[0] = 1GB Normal RW @ 0x00000000 (设备 MMIO)
 *              PUD[1] = 1GB Normal RW @ 0x40000000 (内核代码)
 *
 *          TTBR1 页表:
 *            PGD[0] → PUD 表（index 0 = 虚拟地址 0xFFFF000000000000）
 *            PGD[1] → PUD 表（index 1 = 虚拟地址 0xFFFF000040000000）
 *              PUD[0] = 1GB Normal RW @ 0x00000000 (设备 MMIO 高地址镜像)
 *              PUD[1] = 1GB Normal RW @ 0x40000000 (内核代码高地址镜像)
 *
 * @note    对应需求: KR-005（虚拟内存管理）
 * @note    QEMU virt 平台: 内核加载在 0x40000000, UART 在 0x09000000
 */

#include <stdint.h>
#include <kernel/types.h>

/* ========== 页表项标志位定义 ========== */

#define PTE_VALID        (1ULL << 0U)   /**< 有效位 */
#define PTE_TABLE_BIT    (1ULL << 1U)   /**< 表描述符 */
#define PTE_AF           (1ULL << 10U)  /**< Access Flag */
#define PTE_SH_INNER     (3ULL << 8U)   /**< Inner Shareable */

/** @brief MAIR 属性索引: Attr0 = Normal Write-Back Cacheable */
#define PTE_ATTR_NORMAL  (0ULL << 2U)

/** @brief MAIR 属性索引: Attr1 = Device nGnRE */
#define PTE_ATTR_DEVICE  (1ULL << 2U)

/** @brief 访问权限: EL1 读写, 用户无访问 */
#define PTE_AP_PRIV_RW   (0ULL << 6U)

/** @brief MAIR_EL1 属性值: Normal WB Cacheable */
#define MAIR_NORMAL      0xFFULL

/** @brief MAIR_EL1 属性值: Device nGnRE */
#define MAIR_DEVICE      0x04ULL

/* ========== TCR_EL1 字段定义 ========== */

#define TCR_T0SZ_SHIFT   0U
#define TCR_T1SZ_SHIFT   16U
#define TCR_TG0_4KB      (0ULL << 14U)
#define TCR_TG1_4KB      (2ULL << 30U)
#define TCR_IRGN0_WB     (1ULL << 8U)
#define TCR_ORGN0_WB     (1ULL << 10U)
#define TCR_IRGN1_WB     (1ULL << 24U)
#define TCR_ORGN1_WB     (1ULL << 26U)
#define TCR_SH0_INNER    (3ULL << 12U)
#define TCR_SH1_INNER    (3ULL << 28U)
#define TCR_ASID_8       (0ULL << 36U)
#define TCR_IPS_4TB      (2ULL << 32U)

/** @brief 48 位地址空间 (256TB): T0SZ = 64 - 48 = 16 */
#define TCR_T0SZ_48BIT   16U

/** @brief 48 位地址空间 (256TB): T1SZ = 64 - 48 = 16 */
#define TCR_T1SZ_48BIT   16U

/* ========== 静态页表（BSS 段，4KB 对齐） ========== */

/**
 * @brief TTBR0 页表（恒等映射，当前内核运行使用）
 */
static uint64_t s_pgd_ttbr0[512U] __attribute__((aligned(4096U)));
static uint64_t s_pud_ttbr0[512U] __attribute__((aligned(4096U)));

/**
 * @brief TTBR1 页表（内核态高地址映射）
 */
static uint64_t s_pgd_ttbr1[512U] __attribute__((aligned(4096U)));
static uint64_t s_pud_ttbr1[512U] __attribute__((aligned(4096U)));

/**
 * @brief TTBR0 空页表（用户态初始状态）
 */
static uint64_t s_pgd_ttbr0_empty[512U] __attribute__((aligned(4096U)));

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 清零一个页表
 * @param table 页表基地址
 */
static void clear_table(volatile uint64_t *table)
{
    uint32_t i;
    for (i = 0U; i < 512U; i++)
    {
        table[i] = 0ULL;
    }
}

/**
 * @brief 构造 1GB 块描述符
 * @param paddr 物理地址（1GB 对齐）
 * @param attr_idx MAIR 属性索引（0=Normal, 1=Device）
 * @return 块描述符值
 */
static uint64_t make_block_desc(uint64_t paddr, uint64_t attr_idx)
{
    return PTE_VALID       /* bit 0 = 有效 */
         | PTE_AF          /* bit 10 = Access Flag */
         | PTE_SH_INNER    /* bit[9:8] = Inner Shareable */
         | attr_idx        /* bit[4:2] = MAIR 属性索引 */
         | PTE_AP_PRIV_RW  /* bit[7:6] = EL1 读写 */
         | (paddr & ~(0x40000000ULL - 1ULL));  /* 输出地址 [47:30] */
}

/**
 * @brief 构造表描述符（指向下一级页表）
 * @param next_table 下一级页表物理地址
 * @return 表描述符值
 */
static uint64_t make_table_desc(uint64_t next_table)
{
    return PTE_VALID | PTE_TABLE_BIT | (next_table & ~0xFFFULL);
}

/* ========================================================================
 * MMU 早期初始化
 * ======================================================================== */

/**
 * @brief  MMU 早期初始化（双地址空间）
 *
 * @details 建立 TTBR0/TTBR1 双地址空间映射并启用 MMU:
 *
 *          1. 清零所有页表
 *          2. TTBR0 页表（恒等映射）:
 *             PGD[0] → PUD 表（覆盖 0x00000000-0x3FFFFFFF）
 *             PGD[1] → PUD 表（覆盖 0x40000000-0x7FFFFFFF）
 *               PUD[0] = 1GB Normal RW @ 0x00000000
 *               PUD[1] = 1GB Normal RW @ 0x40000000
 *          3. TTBR1 页表（高地址映射）:
 *             PGD[0] → PUD 表（覆盖高地址 0x0-0x3FFFFFFF）
 *             PGD[1] → PUD 表（覆盖高地址 0x40000000-0x7FFFFFFF）
 *               PUD[0] = 1GB Normal RW @ 0x00000000
 *               PUD[1] = 1GB Normal RW @ 0x40000000
 *          4. 设置 MAIR_EL1（Normal WB + Device nGnRE）
 *          5. 设置 TCR_EL1（T0SZ=T1SZ=25, TG0=TG1=4KB）
 *          6. 设置 TTBR0_EL1 和 TTBR1_EL1
 *          7. 刷新 TLB
 *          8. 启用 SCTLR_EL1.M 位
 *
 * @note 内核当前通过 TTBR0 恒等映射运行（PC = 0x4008XXXX）
 */
void mmu_early_init(void)
{
    uint64_t mair_val;
    uint64_t tcr_val;
    uint64_t sctlr_val;
    uint64_t pgd0_paddr;
    uint64_t pud0_paddr;
    uint64_t pgd1_paddr;
    uint64_t pud1_paddr;
    uint64_t empty_paddr;

    /* ---- 第1步: 清零所有页表 ---- */
    clear_table(s_pgd_ttbr0);
    clear_table(s_pud_ttbr0);
    clear_table(s_pgd_ttbr1);
    clear_table(s_pud_ttbr1);
    clear_table(s_pgd_ttbr0_empty);

    /* ---- 第2步: 构建 TTBR0 页表（恒等映射） ---- */
    pgd0_paddr = (uint64_t)(uintptr_t)&s_pgd_ttbr0[0U];
    pud0_paddr = (uint64_t)(uintptr_t)&s_pud_ttbr0[0U];

    /* PGD[0] → PUD 表（覆盖 0x00000000-0x3FFFFFFF） */
    s_pgd_ttbr0[0U] = make_table_desc(pud0_paddr);

    /* PGD[1] → PUD 表（覆盖 0x40000000-0x7FFFFFFF，内核代码在此） */
    s_pgd_ttbr0[1U] = make_table_desc(pud0_paddr);

    /* PUD[0] = 1GB Normal RW Block @ 0x00000000 (设备 MMIO: UART, GIC) */
    s_pud_ttbr0[0U] = make_block_desc(0x00000000ULL, PTE_ATTR_NORMAL);

    /* PUD[1] = 1GB Normal RW Block @ 0x40000000 (内核代码+数据) */
    s_pud_ttbr0[1U] = make_block_desc(0x40000000ULL, PTE_ATTR_NORMAL);

    /* ---- 第3步: 构建 TTBR1 页表（高地址映射） ---- */
    pgd1_paddr = (uint64_t)(uintptr_t)&s_pgd_ttbr1[0U];
    pud1_paddr = (uint64_t)(uintptr_t)&s_pud_ttbr1[0U];

    /* PGD[0] → PUD 表（index 0 = 虚拟地址 0xFFFF000000000000） */
    s_pgd_ttbr1[0U] = make_table_desc(pud1_paddr);

    /* PGD[1] → PUD 表（index 1 = 虚拟地址 0xFFFF000040000000） */
    s_pgd_ttbr1[1U] = make_table_desc(pud1_paddr);

    /* PUD[0] = 1GB Normal RW Block @ 0x00000000 (设备 MMIO 高地址镜像) */
    s_pud_ttbr1[0U] = make_block_desc(0x00000000ULL, PTE_ATTR_NORMAL);

    /* PUD[1] = 1GB Normal RW Block @ 0x40000000 (内核代码高地址镜像) */
    s_pud_ttbr1[1U] = make_block_desc(0x40000000ULL, PTE_ATTR_NORMAL);

    /* ---- 第4步: 设置 MAIR_EL1 ---- */
    mair_val = (MAIR_DEVICE << 8U) | MAIR_NORMAL;
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair_val));
    __asm__ volatile("isb");

    /* ---- 第5步: 设置 TCR_EL1 ---- */
    tcr_val = ((uint64_t)TCR_T0SZ_48BIT << TCR_T0SZ_SHIFT)
            | ((uint64_t)TCR_T1SZ_48BIT << TCR_T1SZ_SHIFT)
            | TCR_TG0_4KB
            | TCR_TG1_4KB
            | TCR_IRGN0_WB
            | TCR_ORGN0_WB
            | TCR_IRGN1_WB
            | TCR_ORGN1_WB
            | TCR_SH0_INNER
            | TCR_SH1_INNER
            | TCR_ASID_8
            | TCR_IPS_4TB;

    __asm__ volatile("msr tcr_el1, %0" :: "r"(tcr_val));
    __asm__ volatile("isb");

    /* ---- 第6步: 设置 TTBR0_EL1 和 TTBR1_EL1 ---- */
    /* T0SZ=16 意味着 48 位地址空间，页表遍历从 Level 0 (PGD) 开始 */
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(pgd0_paddr));
    __asm__ volatile("isb");

    __asm__ volatile("msr ttbr1_el1, %0" :: "r"(pgd1_paddr));
    __asm__ volatile("isb");

    /* ---- 第7步: 刷新 TLB ---- */
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("tlbi vmalle1");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");

    /* ---- 第8步: 启用 MMU (SCTLR_EL1.M = 1) ---- */
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr_val));
    sctlr_val |= (1ULL << 0U);
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr_val));
    __asm__ volatile("isb");

    /* ---- 保存空页表地址（供进程切换使用） ---- */
    empty_paddr = (uint64_t)(uintptr_t)&s_pgd_ttbr0_empty[0U];
    (void)empty_paddr;
}
