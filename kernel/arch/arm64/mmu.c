/**
 * @file    mmu.c
 * @brief   ARM64 MMU 4KB 精细页表映射 + TTBR1 高地址双地址空间
 * @author  AISafe64 Team
 * @date    2026-07-03
 * @version 7.0
 *
 * @details 标准 ARM64 双地址空间 MMU 映射（4级页表: PGD → PUD → PMD → PTE）:
 *          - TTBR0_EL1: 用户态低地址空间（VA[63]=0），启动后初始为空
 *          - TTBR1_EL1: 内核态高地址空间（VA[63]=1），线性映射
 *
 *          内存布局（QEMU virt，内核 PA 0x40000000）:
 *            线性映射偏移 KERNEL_VA_OFFSET = 0xFFFF000000000000
 *            内核链接基址 VA 0xFFFF000040000000 ↔ PA 0x40000000
 *
 *          TTBR1 页表（高地址内核空间）:
 *            PGD[0] → PUD 表
 *              PUD[0] = 1GB Device Block @ PA 0x00000000 (MMIO: UART/GIC/virtio)
 *                       VA 0xFFFF000000000000 ~ 0xFFFF00003FFFFFFF
 *              PUD[1] → PMD 表（PA 0x40000000 ~ 0x7FFFFFFF，1GB RAM）
 *                PMD[0] → PTE 表（PA 0x40000000 ~ 0x401FFFFF，2MB 内核映像）
 *                  PTE[0..text_end-1]      = 4KB RX  (.text.boot + .text)
 *                  PTE[text_end..ro_end-1] = 4KB R--  (.rodata)
 *                  PTE[ro_end..511]        = 4KB RW-  (.data/.bss/stacks/heap)
 *                PMD[1..511] = 2MB RW- block (空闲 RAM)
 *
 *          TTBR0 启动期临时恒等映射（仅 mmu_early_init 期间使用）:
 *            与 TTBR1 同构但 VA=PA，覆盖内核映像 + MMIO，
 *            使开启 MMU 后能在物理地址继续执行，直至跳转到高地址。
 *            启动完成后 TTBR0 切换为空（用户空间）。
 *
 *          三段权限控制（PTE_AP_PRIV_*，EL0 不可访问）:
 *            - MMIO:            Device nGnRnE, RW-, 不可执行
 *            - .text/.text.boot: Normal WB, 只读可执行 (RX)
 *            - .rodata:          Normal WB, 只读不可执行 (R--)
 *            - .data+.bss+...:   Normal WB, 读写不可执行 (RW-)
 *            - 空闲 RAM:         Normal WB, 读写不可执行 (RW-)
 *
 * @note    对应需求: KR-005（虚拟内存管理）TTBR1 高地址迁移
 * @note    QEMU virt 平台: 内核加载在 0x40000000, UART 在 0x09000000
 */

#include <stdint.h>
#include <kernel/types.h>
#include <kernel/mmu.h>
#include <kernel/virt_phys.h>

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

/** @brief 2MB 块大小（PMD 级） */
#define BLOCK_SIZE_2MB   0x200000ULL

/** @brief 4KB 页大小（PTE 级） */
#define PAGE_SIZE_4KB    0x1000ULL

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

/* ========== 内核地址常量 ========== */

/**
 * @brief 内核映像物理基址（QEMU 加载点，PMD[0] 起点）
 */
#define KERNEL_PHYS_BASE 0x40000000ULL

/* ========== 链接脚本导出符号（段边界，高地址 VMA） ========== */

/** @brief .text 段结束地址（.rodata 开始前，高地址 VA） */
extern char __text_end[];

/** @brief .text + .rodata 只读区域结束地址（高地址 VA） */
extern char __rodata_end[];

/** @brief 内核映像结束地址（含 heap，高地址 VA） */
extern char __kernel_end[];

/* ========== 全局状态 ========== */

/**
 * @brief MMU 就绪标志（mmu_early_init 完成精细页表后置 1）
 *
 * @details boot.S 已用粗粒度页表开启 MMU 并重定位到高地址 VA。
 *          kernel_main 在高地址运行后调用 mmu_early_init 升级页表精度，
 *          完成后置 g_mmu_ready=1。
 */
uint32_t g_mmu_ready = 0U;

/* ========== 静态页表（BSS 段，4KB 对齐） ========== */

/**
 * @brief TTBR1 页表（内核态高地址空间，永久使用）
 */
static uint64_t s_pgd_ttbr1[512U] __attribute__((aligned(4096U)));
static uint64_t s_pud_ttbr1[512U] __attribute__((aligned(4096U)));
static uint64_t s_pmd_kernel[512U] __attribute__((aligned(4096U)));
static uint64_t s_pte_kernel[512U] __attribute__((aligned(4096U)));

/**
 * @brief TTBR0 空页表（启动后用户态初始状态）
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

/**
 * @brief 构造 2MB 块描述符（Level 2 PMD 级）
 * @param paddr 物理地址（2MB 对齐）
 * @param attr_idx MAIR 属性索引（0=Normal, 1=Device）
 * @param flags 权限标志（PTE_AP_RW/PTE_AP_RO + PTE_PXN + PTE_UXN 组合）
 * @return 块描述符值
 */
static uint64_t make_block2m_desc(uint64_t paddr, uint64_t attr_idx,
                                   uint64_t flags)
{
    return PTE_VALID
         | PTE_AF
         | PTE_SH_INNER
         | attr_idx
         | flags
         | (paddr & ~(BLOCK_SIZE_2MB - 1ULL));
}

/**
 * @brief 构造 4KB 页描述符（Level 3 PTE 级）
 * @param paddr 物理地址（4KB 对齐）
 * @param attr_idx MAIR 属性索引（0=Normal, 1=Device）
 * @param flags 权限标志（PTE_AP_RW/PTE_AP_RO + PTE_PXN + PTE_UXN 组合）
 * @return 页描述符值
 */
static uint64_t make_pte_desc(uint64_t paddr, uint64_t attr_idx,
                               uint64_t flags)
{
    return PTE_VALID
         | PTE_TABLE_BIT  /* Level 3 页描述符: bit[1] 必须为 1 */
         | PTE_AF
         | PTE_SH_INNER
         | attr_idx
         | flags
         | (paddr & ~(PAGE_SIZE_4KB - 1ULL));
}

/**
 * @brief 填充内核映像 PMD[0]→PTE 表（4KB 精细权限映射）
 *
 * @details 在给定的 PTE 表上构造内核映像三段权限映射：
 *          PTE[0..text_end-1]      = 4KB Normal RX（.text.boot + .text）
 *          PTE[text_end..ro_end-1] = 4KB Normal R--（.rodata）
 *          PTE[ro_end..511]        = 4KB Normal RW-（.data + .bss + ...）
 *          物理基址固定 KERNEL_PHYS_BASE（0x40000000）。
 *
 * @param pte_table 目标 PTE 表（512 项）
 */
static void build_kernel_pte_table(uint64_t *pte_table)
{
    uint64_t text_end_pa;
    uint64_t rodata_end_pa;
    uint32_t text_end_idx;
    uint32_t ro_end_idx;
    uint32_t i;

    /* 链接符号为高地址 VA，转换为物理地址后计算 PTE 索引 */
    text_end_pa = (uint64_t)(uintptr_t)__text_end - KERNEL_VA_OFFSET;
    rodata_end_pa = (uint64_t)(uintptr_t)__rodata_end - KERNEL_VA_OFFSET;

    text_end_idx = (uint32_t)((text_end_pa - KERNEL_PHYS_BASE
                               + PAGE_SIZE_4KB - 1ULL) / PAGE_SIZE_4KB);
    if (text_end_idx > 512U)
    {
        text_end_idx = 512U;
    }

    ro_end_idx = (uint32_t)((rodata_end_pa - KERNEL_PHYS_BASE
                             + PAGE_SIZE_4KB - 1ULL) / PAGE_SIZE_4KB);
    if (ro_end_idx > 512U)
    {
        ro_end_idx = 512U;
    }

    if (text_end_idx > ro_end_idx)
    {
        text_end_idx = ro_end_idx;
    }

    /* 段1: .text.boot + .text = 4KB Normal RX（仅 EL1 只读可执行） */
    for (i = 0U; i < text_end_idx; i++)
    {
        uint64_t paddr = KERNEL_PHYS_BASE + (uint64_t)i * PAGE_SIZE_4KB;
        pte_table[i] = make_pte_desc(paddr, PTE_ATTR_NORMAL, PTE_AP_RO);
    }

    /* 段2: .rodata = 4KB Normal R--（仅 EL1 只读不可执行） */
    for (i = text_end_idx; i < ro_end_idx; i++)
    {
        uint64_t paddr = KERNEL_PHYS_BASE + (uint64_t)i * PAGE_SIZE_4KB;
        pte_table[i] = make_pte_desc(paddr, PTE_ATTR_NORMAL,
                                      PTE_AP_RO | PTE_PXN | PTE_UXN);
    }

    /* 段3: .data + .bss + percpu + stacks + heap = 4KB Normal RW- */
    for (i = ro_end_idx; i < 512U; i++)
    {
        uint64_t paddr = KERNEL_PHYS_BASE + (uint64_t)i * PAGE_SIZE_4KB;
        pte_table[i] = make_pte_desc(paddr, PTE_ATTR_NORMAL,
                                      PTE_AP_RW | PTE_PXN | PTE_UXN);
    }
}

/**
 * @brief 填充内核 PMD 表（PMD[0]→PTE 精细映射，PMD[1..511] 2MB block）
 *
 * @param pmd_table 目标 PMD 表（512 项）
 * @param pte_table 与 PMD[0] 关联的 PTE 表（内核映像精细权限）
 */
static void build_kernel_pmd_table(uint64_t *pmd_table, uint64_t *pte_table)
{
    uint32_t i;
    uint64_t pte_paddr = virt_to_phys(pte_table);

    /* PMD[0] → PTE 表（KERNEL_PHYS_BASE ~ +2MB，4KB 精细权限） */
    pmd_table[0U] = make_table_desc(pte_paddr);

    /* PMD[1..511] = 2MB Normal RW- Block（空闲 RAM） */
    for (i = 1U; i < 512U; i++)
    {
        uint64_t blk_addr = KERNEL_PHYS_BASE + (uint64_t)i * BLOCK_SIZE_2MB;
        pmd_table[i] = make_block2m_desc(blk_addr, PTE_ATTR_NORMAL,
                                          PTE_AP_RW | PTE_PXN | PTE_UXN);
    }
}

/**
 * @brief 填充 PUD 表（PUD[0]=1GB Device MMIO，PUD[1]→PMD RAM 表）
 *
 * @param pud_table 目标 PUD 表（512 项）
 * @param pmd_table 与 PUD[1] 关联的 PMD 表（覆盖 PA 0x40000000 ~ 1GB RAM）
 */
static void build_kernel_pud_table(uint64_t *pud_table, uint64_t *pmd_table)
{
    uint64_t pmd_paddr = virt_to_phys(pmd_table);

    /* PUD[0] = 1GB Device nGnRnE Block @ PA 0x00000000 (MMIO: UART/GIC/virtio)
     * 对应 VA 0xFFFF000000000000 ~ 0xFFFF00003FFFFFFF */
    pud_table[0U] = make_block1g_desc(0x00000000ULL, PTE_ATTR_DEVICE,
                                       PTE_AP_RW | PTE_PXN | PTE_UXN);

    /* PUD[1] → PMD 表（PA 0x40000000 ~ 0x7FFFFFFF，1GB RAM + 内核映像）
     * 对应 VA 0xFFFF000040000000 ~ 0xFFFF00007FFFFFFF */
    pud_table[1U] = make_table_desc(pmd_paddr);
}

/* ========================================================================
 * MMU 早期初始化（主核）
 * ======================================================================== */

/**
 * @brief  MMU 早期初始化（重建精细页表）
 *
 * @details 本函数在高地址 VA 运行（boot.S 已用粗粒度页表开启 MMU 并重定位）。
 *
 *          boot.S 已建立的粗粒度映射（1GB 块）足以取指/访存，但权限不精细。
 *          本函数重建 TTBR1 精细页表（4KB 权限：RX / R-- / RW-）并切换：
 *
 *          1. 清零所有精细页表
 *          2. 构建 TTBR1 内核高地址映射（PUD[0] Device + PUD[1]→PMD→PTE 精细权限）
 *          3. 设置 MAIR_EL1 / TCR_EL1 / TTBR1，加载精细页表
 *          4. TTBR0 切换为空表（用户空间初始状态）
 *          5. 正常返回（不再做重定位，boot.S 已完成）
 *
 * @note 本函数运行在高地址 VA，所有符号访问（含链接符号）已可正常进行。
 */
void mmu_early_init(void)
{
    uint64_t mair_val;
    uint64_t tcr_val;
    uint64_t pgd1_paddr;
    uint64_t pud1_paddr;
    uint64_t empty_paddr;

    /* ---- 第1步: 清零所有精细页表 ---- */
    clear_table(s_pgd_ttbr1);
    clear_table(s_pud_ttbr1);
    clear_table(s_pmd_kernel);
    clear_table(s_pte_kernel);
    clear_table(s_pgd_ttbr0_empty);

    /* ---- 第2步: 构建 TTBR1 内核高地址精细映射 ---- */
    /* PTE 表（内核映像 4KB 精细权限） */
    build_kernel_pte_table(s_pte_kernel);

    /* PMD 表（PMD[0]→PTE 精细，PMD[1..511] 2MB block） */
    build_kernel_pmd_table(s_pmd_kernel, s_pte_kernel);

    /* PUD 表（PUD[0]=1GB Device MMIO，PUD[1]→PMD RAM） */
    build_kernel_pud_table(s_pud_ttbr1, s_pmd_kernel);

    /* PGD[0] → PUD 表（高地址 VA 的 PGD 索引为 0） */
    pud1_paddr = virt_to_phys(s_pud_ttbr1);
    s_pgd_ttbr1[0U] = make_table_desc(pud1_paddr);
    pgd1_paddr = virt_to_phys(s_pgd_ttbr1);
    empty_paddr = virt_to_phys(s_pgd_ttbr0_empty);

    /* ---- 第3步: 重新设置 MAIR_EL1（与 boot.S 一致）---- */
    mair_val = (MAIR_DEVICE << 8U) | MAIR_NORMAL;
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair_val));
    __asm__ volatile("isb");

    /* ---- 第4步: 设置 TCR_EL1 ---- */
    tcr_val = ((uint64_t)TCR_T0SZ_48BIT << TCR_T0SZ_SHIFT)
            | ((uint64_t)TCR_T0SZ_48BIT << TCR_T1SZ_SHIFT)
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

    /* ---- 第5步: 加载精细 TTBR1 页表（替换 boot.S 的粗粒度表）---- */
    __asm__ volatile("msr ttbr1_el1, %0" :: "r"(pgd1_paddr));
    __asm__ volatile("isb");

    /* ---- 第6步: 切换 TTBR0 为空表（用户空间初始状态）----
     * boot.S 的 TTBR0 恒等映射使命命期结束。失效低地址 TLB。
     */
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(empty_paddr));
    __asm__ volatile("isb");
    __asm__ volatile("tlbi vmalle1");
    __asm__ volatile("dsb nsh");
    __asm__ volatile("isb");

    /* ---- 正常返回 ----
     * boot.S 已完成 MMU 启用与高地址重定位，本函数仅升级页表精度。
     */
    g_mmu_ready = 1U;
    return;
}

/* ========================================================================
 * 从核 MMU 初始化
 * ======================================================================== */

/**
 * @brief  从核 MMU 初始化
 *
 * @details 从核已由 boot.S（secondary_entry）用粗粒度页表开启 MMU 并
 *          重定位到高地址 VA，跳转到 smp_secondary_entry 后调用本函数。
 *          本函数加载主核已构建的精细 TTBR1 页表，TTBR0 设为空表。
 *
 * @note 本函数运行在高地址 VA（boot.S 已完成 MMU 启用与重定位）。
 */
void mmu_init_secondary(void)
{
    uint64_t mair_val;
    uint64_t tcr_val;
    uint64_t pgd1_paddr;

    /* 设置 MAIR_EL1（与主核一致） */
    mair_val = (MAIR_DEVICE << 8U) | MAIR_NORMAL;
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair_val));
    __asm__ volatile("isb");

    /* 设置 TCR_EL1（与主核一致） */
    tcr_val = ((uint64_t)TCR_T0SZ_48BIT << TCR_T0SZ_SHIFT)
            | ((uint64_t)TCR_T0SZ_48BIT << TCR_T1SZ_SHIFT)
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

    /* 加载主核已构建的精细 TTBR1 页表 */
    pgd1_paddr = virt_to_phys(s_pgd_ttbr1);
    __asm__ volatile("msr ttbr1_el1, %0" :: "r"(pgd1_paddr));
    __asm__ volatile("isb");

    /* 切换 TTBR0 为空表（用户空间），失效 boot.S 恒等映射 TLB */
    __asm__ volatile("msr ttbr0_el1, %0"
                     :: "r"(virt_to_phys(s_pgd_ttbr0_empty)));
    __asm__ volatile("isb");
    __asm__ volatile("tlbi vmalle1");
    __asm__ volatile("dsb nsh");
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

    for (i = 0U; i < 16U; i++)
    {
        if ((s_user_pgd_bitmap & (1UL << i)) == 0U)
        {
            s_user_pgd_bitmap |= (1UL << i);
            /* 用户 PGD 作为 TTBR0 使用，必须从空开始。
             * 内核映射通过 TTBR1_EL1 独立提供，用户 PGD 不需复制内核映射。
             */
            clear_table(s_user_pgds[i]);

            pgd_paddr = virt_to_phys(s_user_pgds[i]);
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
        if (virt_to_phys(s_user_pgds[i]) == pgd_paddr)
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
    /* 仅失效 TTBR0（非 Inner Shareable），避免影响 TTBR1 内核映射的 TLB */
    __asm__ volatile("tlbi vmalle1");
    __asm__ volatile("dsb nsh");
    __asm__ volatile("isb");
}

/**
 * @brief 切回内核态地址空间
 */
void mmu_switch_to_kernel(void)
{
    /* 内核运行在高地址（TTBR1），TTBR0 重置为空表即可。
     * 不再恢复启动期恒等映射（仅 mmu_early_init 期间使用）。
     */
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(virt_to_phys(s_pgd_ttbr0_empty)));
    __asm__ volatile("isb");
    __asm__ volatile("tlbi vmalle1");
    __asm__ volatile("dsb nsh");
    __asm__ volatile("isb");
}
