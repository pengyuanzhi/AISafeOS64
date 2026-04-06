/**
 * @file    mmu.c
 * @brief   ARM64 MMU 页表映射（PGD → PUD 1GB Block）
 * @author  AISafe64 Team
 * @date    2026-04-06
 * @version 4.0
 *
 * @details 实现 ARM64 双地址空间 MMU 映射（2级页表: PGD → PUD 1GB Block）:
 *          - TTBR0_EL1: 恒等映射（物理地址 = 虚拟地址）
 *          - TTBR1_EL1: 内核态高地址空间
 *
 *          页表结构:
 *          TTBR0 页表:
 *            PGD[0] → PUD 表（覆盖 0x00000000-0x7FFFFFFF）
 *              PUD[0] = 1GB Device Block @ 0x00000000 (MMIO: UART, GIC)
 *              PUD[1] = 1GB Normal RW Block @ 0x40000000 (内核 + RAM)
 *
 *          TTBR1 页表（高地址镜像）:
 *            PUD[0] = 1GB Normal RW @ 0x00000000
 *            PUD[1] = 1GB Normal RW @ 0x40000000
 *
 *          权限控制:
 *            - MMIO (0x00000000-0x3FFFFFFF): Device nGnRnE, 读写不可执行
 *            - 内核/RAM (0x40000000-0x7FFFFFFF): Normal WB, 读写可执行 (RWX)
 *
 * @note    对应需求: KR-005（虚拟内存管理）
 * @note    QEMU virt 平台: 内核加载在 0x40000000, UART 在 0x09000000
 * @note    后续将引入 4KB 页映射实现 text(RX) / rodata(R--) / data(RW-) 精细权限
 */

#include <stdint.h>
#include <kernel/types.h>
#include <kernel/mmu.h>

/* ========== 页表项标志位定义 ========== */

#define PTE_VALID        (1ULL << 0U)   /**< 有效位 */
#define PTE_TABLE_BIT    (1ULL << 1U)   /**< 表描述符 */
#define PTE_AF           (1ULL << 10U)  /**< Access Flag */
#define PTE_SH_INNER     (3ULL << 8U)   /**< Inner Shareable */

/** @brief MAIR 属性索引: Attr0 = Normal Write-Back Cacheable */
#define PTE_ATTR_NORMAL  (0ULL << 2U)

/** @brief MAIR 属性索引: Attr1 = Device nGnRnE */
#define PTE_ATTR_DEVICE  (1ULL << 2U)

/** @brief 访问权限: EL1 读写, EL0 无访问 (AP[2:1] = 00) */
#define PTE_AP_RW        (0ULL << 6U)

/** @brief 访问权限: EL1 只读, EL0 无访问 (AP[2:1] = 10) */
#define PTE_AP_RO        (2ULL << 6U)

/** @brief Privileged Execute Never (bit 53) */
#define PTE_PXN          (1ULL << 53U)

/** @brief Unprivileged Execute Never (bit 54) */
#define PTE_UXN          (1ULL << 54U)

/** @brief MAIR_EL1 属性值: Normal WB Cacheable */
#define MAIR_NORMAL      0xFFULL

/** @brief MAIR_EL1 属性值: Device nGnRnE */
#define MAIR_DEVICE      0x00ULL

/** @brief 1GB 块大小（PUD 级） */
#define BLOCK_SIZE_1GB   0x40000000ULL

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
 * @param flags 权限标志（PTE_AP_RW/PTE_AP_RO + PTE_PXN + PTE_UXN 组合）
 * @return 块描述符值
 */
static uint64_t make_block1g_desc(uint64_t paddr, uint64_t attr_idx,
                                   uint64_t flags)
{
    return PTE_VALID
         | PTE_AF
         | PTE_SH_INNER
         | attr_idx
         | flags
         | (paddr & ~(BLOCK_SIZE_1GB - 1ULL));
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
 * @brief  MMU 早期初始化（PGD → PUD 1GB Block 映射）
 *
 * @details 建立 TTBR0/TTBR1 双地址空间映射并启用 MMU:
 *
 *          1. 清零所有页表
 *          2. TTBR0 页表（恒等映射，PGD → PUD 1GB block）:
 *             PUD[0] = 1GB Device Block @ 0x00000000 (MMIO)
 *             PUD[1] = 1GB Normal RW Block @ 0x40000000 (内核 + RAM)
 *          3. TTBR1 页表（高地址镜像，1GB block）
 *          4. 设置 MAIR_EL1 / TCR_EL1 / TTBR0 / TTBR1
 *          5. 启用 MMU
 *
 * @note 内核通过 TTBR0 恒等映射运行（PC = 0x4008XXXX）
 * @note 2MB 粒度无法区分 text/data 权限（全在 0x40000000-0x401FFFFF 内），
 *       故内核区域统一使用 Normal RW 1GB block，后续引入 4KB 页映射实现精细权限
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

    /* ---- 第1步: 清零所有页表 ---- */
    clear_table(s_pgd_ttbr0);
    clear_table(s_pud_ttbr0);
    clear_table(s_pgd_ttbr1);
    clear_table(s_pud_ttbr1);
    clear_table(s_pgd_ttbr0_empty);

    /* ---- 第2步: 构建 TTBR0 PUD 表（1GB block） ---- */
    pud0_paddr = (uint64_t)(uintptr_t)&s_pud_ttbr0[0U];

    /* PUD[0] = 1GB Device nGnRnE Block @ 0x00000000 (MMIO: UART 0x09000000, GIC) */
    s_pud_ttbr0[0U] = make_block1g_desc(0x00000000ULL, PTE_ATTR_DEVICE,
                                          PTE_AP_RW | PTE_PXN | PTE_UXN);

    /* PUD[1] = 1GB Normal RWX Block @ 0x40000000 (内核代码 + 数据 + RAM)
     * 注意: 内核代码在此区域执行，不能设置 PXN (Privileged Execute Never)
     * 后续 4KB 页映射可精确划分 text(RX) / rodata(R--) / data(RW-) */
    s_pud_ttbr0[1U] = make_block1g_desc(0x40000000ULL, PTE_ATTR_NORMAL,
                                          PTE_AP_RW | PTE_UXN);

    /* ---- 第3步: 构建 TTBR0 PGD ---- */
    pgd0_paddr = (uint64_t)(uintptr_t)&s_pgd_ttbr0[0U];
    s_pgd_ttbr0[0U] = make_table_desc(pud0_paddr);
    s_pgd_ttbr0[1U] = make_table_desc(pud0_paddr);

    /* ---- 第4步: 构建 TTBR1 页表（高地址镜像，1GB block） ---- */
    pgd1_paddr = (uint64_t)(uintptr_t)&s_pgd_ttbr1[0U];
    pud1_paddr = (uint64_t)(uintptr_t)&s_pud_ttbr1[0U];

    s_pgd_ttbr1[0U] = make_table_desc(pud1_paddr);
    s_pgd_ttbr1[1U] = make_table_desc(pud1_paddr);

    s_pud_ttbr1[0U] = make_block1g_desc(0x00000000ULL, PTE_ATTR_NORMAL,
                                          PTE_AP_RW | PTE_PXN | PTE_UXN);
    /* 高地址镜像: 内核代码也需要执行权限 */
    s_pud_ttbr1[1U] = make_block1g_desc(0x40000000ULL, PTE_ATTR_NORMAL,
                                          PTE_AP_RW | PTE_UXN);

    /* ---- 第5步: 设置 MAIR_EL1 ---- */
    mair_val = (MAIR_DEVICE << 8U) | MAIR_NORMAL;
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair_val));
    __asm__ volatile("isb");

    /* ---- 第6步: 设置 TCR_EL1 ---- */
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

    /* ---- 第7步: 设置 TTBR0_EL1 和 TTBR1_EL1 ---- */
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(pgd0_paddr));
    __asm__ volatile("isb");

    __asm__ volatile("msr ttbr1_el1, %0" :: "r"(pgd1_paddr));
    __asm__ volatile("isb");

    /* ---- 第8步: 刷新 TLB ---- */
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("tlbi vmalle1");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");

    /* ---- 第9步: 启用 MMU (SCTLR_EL1.M = 1) ---- */
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr_val));
    sctlr_val |= (1ULL << 0U);
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr_val));
    __asm__ volatile("isb");
}

/**
 * @brief  从核 MMU 初始化
 *
 * @details 从核需要设置与主核相同的 TTBR0/TTBR1 页表。
 *          由于页表在 BSS 段（已由主核初始化完成），
 *          从核只需重新加载 TTBR 寄存器并刷新 TLB。
 *
 * @note 必须在从核启用 MMU 之前调用
 */
void mmu_init_secondary(void)
{
    uint64_t pgd0_paddr;
    uint64_t pgd1_paddr;
    uint64_t mair_val;
    uint64_t tcr_val;
    uint64_t sctlr_val;

    /* 设置 MAIR_EL1（与主核一致） */
    mair_val = (MAIR_DEVICE << 8U) | MAIR_NORMAL;
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair_val));
    __asm__ volatile("isb");

    /* 设置 TCR_EL1（与主核一致） */
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

    /* 加载 TTBR0/TTBR1（页表已由主核初始化） */
    pgd0_paddr = (uint64_t)(uintptr_t)&s_pgd_ttbr0[0U];
    pgd1_paddr = (uint64_t)(uintptr_t)&s_pgd_ttbr1[0U];

    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(pgd0_paddr));
    __asm__ volatile("isb");

    __asm__ volatile("msr ttbr1_el1, %0" :: "r"(pgd1_paddr));
    __asm__ volatile("isb");

    /* 刷新 TLB */
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("tlbi vmalle1");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");

    /* 启用 MMU */
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr_val));
    sctlr_val |= (1ULL << 0U);
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr_val));
    __asm__ volatile("isb");
}

/* ========================================================================
 * 用户态地址空间管理
 * ======================================================================== */

/** @brief 用户态 PGD 静态池（最多 16 个进程） */
static uint64_t s_user_pgds[16U][512U] __attribute__((aligned(4096U)));

/** @brief PGD 池使用位图 */
static uint32_t s_user_pgd_bitmap;

/** @brief ASID 计数器 */
static uint32_t s_asid_counter;

/**
 * @brief 分配一个用户态 PGD
 * @return PGD 物理地址，失败返回 0
 */
uint64_t mmu_create_user_pgd(void)
{
    uint32_t i;
    uint64_t pgd_paddr;
    uint64_t pud1_paddr;

    for (i = 0U; i < 16U; i++)
    {
        if ((s_user_pgd_bitmap & (1UL << i)) == 0U)
        {
            s_user_pgd_bitmap |= (1UL << i);
            clear_table(s_user_pgds[i]);

            /* 复制内核高地址映射到 TTBR1 部分（PGD 高 256 entries） */
            pud1_paddr = (uint64_t)(uintptr_t)&s_pud_ttbr1[0U];
            {
                uint32_t j;
                for (j = 256U; j < 512U; j++)
                {
                    s_user_pgds[i][j] = s_pgd_ttbr1[j];
                }
            }

            pgd_paddr = (uint64_t)(uintptr_t)&s_user_pgds[i][0U];
            return pgd_paddr;
        }
    }
    return 0ULL;
}

/**
 * @brief 释放用户态 PGD
 * @param pgd_paddr PGD 物理地址
 */
void mmu_destroy_user_pgd(uint64_t pgd_paddr)
{
    uint32_t i;
    for (i = 0U; i < 16U; i++)
    {
        if ((uint64_t)(uintptr_t)&s_user_pgds[i][0U] == pgd_paddr)
        {
            s_user_pgd_bitmap &= ~(1UL << i);
            clear_table(s_user_pgds[i]);
            return;
        }
    }
}

/**
 * @brief 切换到用户态地址空间
 * @param user_pgd_paddr 用户态 PGD 物理地址
 */
void mmu_switch_to_user(uint64_t user_pgd_paddr)
{
    s_asid_counter++;
    if (s_asid_counter >= 65535U)
    {
        s_asid_counter = 1U;
    }

    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(user_pgd_paddr));
    __asm__ volatile("isb");
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}

/**
 * @brief 切回内核态地址空间
 */
void mmu_switch_to_kernel(void)
{
    uint64_t pgd0_paddr = (uint64_t)(uintptr_t)&s_pgd_ttbr0[0U];
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(pgd0_paddr));
    __asm__ volatile("isb");
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}
