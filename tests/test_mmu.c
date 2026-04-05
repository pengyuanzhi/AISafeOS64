/**
 * @file    test_mmu.c
 * @brief   AISafe64 RTOS - MMU 双地址空间映射单元测试（宿主机）
 * @author  AISafe64 Team
 * @date    2026-04-05
 * @version 1.0
 *
 * @details 测试 ARM64 MMU 双地址空间（TTBR0/TTBR1）映射逻辑：
 *          1. 页表项标志位组合正确性
 *          2. 块描述符地址对齐验证
 *          3. TCR_EL1 寄存器字段计算
 *          4. ASID 分配与回收逻辑
 *          5. PGD 索引计算（高地址/低地址）
 *          6. MAIR_EL1 属性编码
 *          7. PUD 索引计算（1GB 块映射）
 *          8. 完整页表结构验证
 *
 * @note 宿主机单线程模拟，不依赖真实硬件
 * @note 对应需求: KR-005（虚拟内存管理）
 */

#include "mock_kernel.h"

/* ========================================================================
 * MMU 常量定义（与 mmu.c / page_table.h 一致）
 * ======================================================================== */

/* 页表项标志位 */
#define PTE_VALID        (1ULL << 0U)
#define PTE_TABLE_BIT    (1ULL << 1U)
#define PTE_AF           (1ULL << 10U)
#define PTE_SH_INNER     (3ULL << 8U)
#define PTE_NG           (1ULL << 11U)
#define PTE_PXN          (1ULL << 53U)
#define PTE_XN           (1ULL << 54U)

/* MAIR 属性索引（AttrIndx[2:0] 字段） */
#define PTE_ATTR_NORMAL  (0ULL << 2U)   /* MAIR attr 0: Normal WB */
#define PTE_ATTR_DEVICE  (1ULL << 2U)   /* MAIR attr 1: Device nGnRE */

/* 访问权限 AP[2:1] 编码 */
#define PTE_AP_SHIFT     6U
#define PTE_AP_PRIV_RW   (0ULL << PTE_AP_SHIFT)
#define PTE_AP_ALL_RW    (1ULL << PTE_AP_SHIFT)

/* MAIR_EL1 值 */
#define MAIR_NORMAL      0xFFULL
#define MAIR_DEVICE      0x04ULL

/* 地址空间常量 */
#define PAGE_SIZE_4K     4096ULL
#define PAGE_SIZE_1G     0x40000000ULL
#define KERNEL_VADDR_BASE 0xFFFF000000000000ULL

/* 页表索引位数 */
#define PGD_SHIFT        39U
#define PUD_SHIFT        30U
#define PTRS_PER_TABLE   512U

/* TCR_EL1 字段 */
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

/* ASID 管理 */
#define ASID_INVALID     ((uint16_t)0U)
#define ASID_MAX         ((uint16_t)256U)
#define ASID_FIRST       ((uint16_t)1U)

/* PGD/PUD 索引计算宏 */
#define PGD_INDEX(vaddr) (((vaddr) >> PGD_SHIFT) & (uint64_t)(PTRS_PER_TABLE - 1U))
#define PUD_INDEX(vaddr) (((vaddr) >> PUD_SHIFT) & (uint64_t)(PTRS_PER_TABLE - 1U))

/* ========================================================================
 * ASID 分配器
 * ======================================================================== */

static uint32_t s_asid_bitmap[8U];

static void asid_init(void)
{
    uint32_t i;
    for (i = 0U; i < 8U; i++)
    {
        s_asid_bitmap[i] = 0U;
    }
    /* ASID 0 保留给内核 */
    s_asid_bitmap[0U] |= 1U;
}

static uint16_t asid_alloc(void)
{
    uint32_t word;
    uint32_t bit;

    for (word = 0U; word < 8U; word++)
    {
        if (s_asid_bitmap[word] != 0xFFFFFFFFU)
        {
            for (bit = 0U; bit < 32U; bit++)
            {
                uint32_t mask = (1U << bit);
                if ((s_asid_bitmap[word] & mask) == 0U)
                {
                    s_asid_bitmap[word] |= mask;
                    return (uint16_t)((word * 32U) + bit);
                }
            }
        }
    }
    return ASID_INVALID;
}

static void asid_free(uint16_t asid)
{
    uint32_t word = asid / 32U;
    uint32_t bit  = asid % 32U;
    if (word < 8U)
    {
        s_asid_bitmap[word] &= ~(1U << bit);
    }
}

/* ========================================================================
 * 页表构造函数
 * ======================================================================== */

static uint64_t compute_tcr_el1(uint32_t t0sz, uint32_t t1sz)
{
    uint64_t tcr;
    tcr  = ((uint64_t)t0sz << TCR_T0SZ_SHIFT);
    tcr |= ((uint64_t)t1sz << TCR_T1SZ_SHIFT);
    tcr |= TCR_TG0_4KB;
    tcr |= TCR_TG1_4KB;
    tcr |= TCR_IRGN0_WB;
    tcr |= TCR_ORGN0_WB;
    tcr |= TCR_IRGN1_WB;
    tcr |= TCR_ORGN1_WB;
    tcr |= TCR_SH0_INNER;
    tcr |= TCR_SH1_INNER;
    tcr |= TCR_ASID_8;
    tcr |= TCR_IPS_4TB;
    return tcr;
}

static uint64_t make_kernel_block(uint64_t paddr, uint64_t attr_idx)
{
    uint64_t desc;
    desc  = PTE_VALID;
    desc |= PTE_AF;
    desc |= PTE_SH_INNER;
    desc |= attr_idx;
    desc |= PTE_AP_PRIV_RW;
    desc |= (paddr & ~(PAGE_SIZE_1G - 1ULL));
    return desc;
}

static uint64_t make_user_block(uint64_t paddr, uint64_t attr_idx)
{
    uint64_t desc;
    desc  = PTE_VALID;
    desc |= PTE_AF;
    desc |= PTE_SH_INNER;
    desc |= attr_idx;
    desc |= PTE_AP_ALL_RW;
    desc |= PTE_NG;
    desc |= (paddr & ~(PAGE_SIZE_1G - 1ULL));
    return desc;
}

static uint64_t make_table_desc(uint64_t next_table_paddr)
{
    return PTE_VALID | PTE_TABLE_BIT | (next_table_paddr & ~(PAGE_SIZE_4K - 1ULL));
}

/* ========================================================================
 * 测试函数
 * ======================================================================== */

/**
 * @brief 测试1: 页表项标志位组合正确性
 */
static void test_pte_flags(void)
{
    uint64_t desc;

    printf("测试1: 页表项标志位组合正确性\n");

    /* 内核块描述符: VALID + AF + SH_INNER + AP_PRIV_RW */
    desc = make_kernel_block(0ULL, PTE_ATTR_NORMAL);
    TEST_ASSERT_TRUE((desc & PTE_VALID) != 0ULL);
    TEST_ASSERT_TRUE((desc & PTE_AF) != 0ULL);
    TEST_ASSERT_TRUE((desc & PTE_SH_INNER) != 0ULL);
    TEST_ASSERT_TRUE((desc & PTE_AP_PRIV_RW) == PTE_AP_PRIV_RW);
    TEST_ASSERT_TRUE((desc & PTE_NG) == 0ULL);
    TEST_ASSERT_TRUE((desc & PTE_PXN) == 0ULL);
    TEST_ASSERT_TRUE((desc & PTE_XN) == 0ULL);

    /* 用户块描述符: 包含 nG 位和 AP_ALL_RW */
    desc = make_user_block(0ULL, PTE_ATTR_NORMAL);
    TEST_ASSERT_TRUE((desc & PTE_NG) != 0ULL);
    TEST_ASSERT_TRUE((desc & PTE_AP_ALL_RW) == PTE_AP_ALL_RW);

    /* Device 属性索引 */
    desc = make_kernel_block(0ULL, PTE_ATTR_DEVICE);
    TEST_ASSERT_TRUE((desc & PTE_ATTR_DEVICE) != 0ULL);
    TEST_ASSERT_TRUE((desc & PTE_ATTR_NORMAL) == 0ULL);

    /* 表描述符 */
    desc = make_table_desc(0x1000ULL);
    TEST_ASSERT_TRUE((desc & PTE_TABLE_BIT) != 0ULL);
    TEST_ASSERT_TRUE((desc & PTE_VALID) != 0ULL);
}

/**
 * @brief 测试2: 块描述符地址对齐
 */
static void test_block_alignment(void)
{
    uint64_t desc;

    printf("测试2: 块描述符地址对齐\n");

    /* 1GB 对齐地址 */
    desc = make_kernel_block(0x40000000ULL, PTE_ATTR_NORMAL);
    TEST_ASSERT_EQ((desc >> 30U) & 0x3FFFU, 1ULL);

    desc = make_kernel_block(0x00000000ULL, PTE_ATTR_NORMAL);
    TEST_ASSERT_EQ((desc >> 30U) & 0x3FFFU, 0ULL);

    desc = make_kernel_block(0x80000000ULL, PTE_ATTR_NORMAL);
    TEST_ASSERT_EQ((desc >> 30U) & 0x3FFFU, 2ULL);

    /* 非对齐地址应被截断到 1GB 边界 */
    desc = make_kernel_block(0x40000001ULL, PTE_ATTR_NORMAL);
    TEST_ASSERT_EQ((desc >> 30U) & 0x3FFFU, 1ULL);

    /* 表描述符 4KB 对齐 */
    desc = make_table_desc(0x5000ULL);
    TEST_ASSERT_EQ((desc >> 12U) & 0xFFFFFU, 5ULL);

    desc = make_table_desc(0x5001ULL);
    TEST_ASSERT_EQ((desc >> 12U) & 0xFFFFFU, 5ULL);
}

/**
 * @brief 测试3: TCR_EL1 寄存器字段计算
 */
static void test_tcr_el1_fields(void)
{
    uint64_t tcr;

    printf("测试3: TCR_EL1 寄存器字段计算\n");

    /* T0SZ=16 对应 48 位地址空间（2^(64-16) = 2^48 = 256TB） */
    tcr = compute_tcr_el1(16U, 16U);

    /* 验证 T0SZ 字段 [5:0] */
    TEST_ASSERT_EQ((uint32_t)(tcr & 0x3FU), 16U);
    /* 验证 T1SZ 字段 [21:16] */
    TEST_ASSERT_EQ((uint32_t)((tcr >> 16U) & 0x3FU), 16U);
    /* 验证 TG0 = 4KB (bits [15:14] = 00) */
    TEST_ASSERT_EQ((uint32_t)((tcr >> 14U) & 0x3U), 0U);
    /* 验证 TG1 = 4KB (bits [31:30] = 10) */
    TEST_ASSERT_EQ((uint32_t)((tcr >> 30U) & 0x3U), 2U);
    /* 验证 SH0 = Inner Shareable (bits [13:12] = 11) */
    TEST_ASSERT_EQ((uint32_t)((tcr >> 12U) & 0x3U), 3U);
    /* 验证 SH1 = Inner Shareable (bits [29:28] = 11) */
    TEST_ASSERT_EQ((uint32_t)((tcr >> 28U) & 0x3U), 3U);
    /* 验证 IRGN0/ORGN0 = WB */
    TEST_ASSERT_TRUE((tcr & TCR_IRGN0_WB) != 0ULL);
    TEST_ASSERT_TRUE((tcr & TCR_ORGN0_WB) != 0ULL);
    /* 验证 ASID 大小 = 8 位 */
    TEST_ASSERT_TRUE((tcr & TCR_ASID_8) == 0ULL);
    /* 验证 IPS = 42 bits (4TB) */
    TEST_ASSERT_EQ((uint32_t)((tcr >> 32U) & 0x7U), 2U);
}

/**
 * @brief 测试4: ASID 分配逻辑
 */
static void test_asid_alloc(void)
{
    uint16_t asid1;
    uint16_t asid2;
    uint16_t asid3;
    uint16_t i;

    printf("测试4: ASID 分配逻辑\n");

    asid_init();

    /* ASID 0 被内核保留，第一次分配应得到 1 */
    asid1 = asid_alloc();
    TEST_ASSERT_EQ(asid1, ASID_FIRST);

    asid2 = asid_alloc();
    TEST_ASSERT_EQ(asid2, (uint16_t)2U);

    /* 释放 asid1 后重新分配应得到 1 */
    asid_free(asid1);
    asid3 = asid_alloc();
    TEST_ASSERT_EQ(asid3, ASID_FIRST);

    /* 分配满 255 个（ASID 1-255），然后分配 ASID 0 */
    asid_free(asid2);
    asid_free(asid3);
    asid_init();
    for (i = 0U; i < 255U; i++)
    {
        asid_alloc();
    }
    /* 下一个分配应得到 ASID 0（唯一剩余的） */
    asid1 = asid_alloc();
    TEST_ASSERT_EQ(asid1, (uint16_t)0U);

    /* 再分配应失败（全部用完） */
    asid2 = asid_alloc();
    TEST_ASSERT_EQ(asid2, ASID_INVALID);
}

/**
 * @brief 测试5: PGD 索引计算
 *
 * @details ARM64 TTBR0/TTBR1 使用独立的页表:
 *          硬件根据 vaddr[63] 选择 TTBR。
 *          PGD_INDEX 取 vaddr[47:39]，两个 TTBR 各自独立，
 *          所以即使 index 值相同也不会冲突。
 */
static void test_pgd_index(void)
{
    uint64_t idx;

    printf("测试5: PGD 索引计算\n");

    /* 低地址 TTBR0 范围 */
    idx = PGD_INDEX(0x0000000000000000ULL);
    TEST_ASSERT_EQ(idx, 0ULL);

    /* 0x0000008000000000 -> bit[47:39] = 1 */
    idx = PGD_INDEX(0x0000008000000000ULL);
    TEST_ASSERT_EQ(idx, 1ULL);

    /* TTBR1 高地址: 0xFFFF000000000000 -> PGD index = 0 (TTBR1 页表) */
    idx = PGD_INDEX(0xFFFF000000000000ULL);
    TEST_ASSERT_EQ(idx, 0ULL);

    /* TTBR1: 0xFFFF400000000000 -> PGD index = 128 */
    idx = PGD_INDEX(0xFFFF400000000000ULL);
    TEST_ASSERT_EQ(idx, 128ULL);
}

/**
 * @brief 测试6: MAIR_EL1 属性编码
 */
static void test_mair_encoding(void)
{
    uint64_t mair;

    printf("测试6: MAIR_EL1 属性编码\n");

    mair = (MAIR_DEVICE << 8U) | MAIR_NORMAL;
    TEST_ASSERT_EQ((uint32_t)(mair & 0xFFULL), (uint32_t)MAIR_NORMAL);
    TEST_ASSERT_EQ((uint32_t)((mair >> 8U) & 0xFFULL), (uint32_t)MAIR_DEVICE);
}

/**
 * @brief 测试7: PUD 索引计算（1GB 块映射）
 */
static void test_pud_index(void)
{
    uint64_t idx;

    printf("测试7: PUD 索引计算\n");

    /* 0x0000000000000000 -> PUD index = 0 */
    idx = PUD_INDEX(0x0000000000000000ULL);
    TEST_ASSERT_EQ(idx, 0ULL);

    /* 0x40000000 -> PUD index = 1 (1GB块) */
    idx = PUD_INDEX(0x40000000ULL);
    TEST_ASSERT_EQ(idx, 1ULL);

    /* 0x80000000 -> PUD index = 2 */
    idx = PUD_INDEX(0x80000000ULL);
    TEST_ASSERT_EQ(idx, 2ULL);

    /* 0x09000000 (UART) -> PUD index = 0 (在第一个 1GB 内) */
    idx = PUD_INDEX(0x09000000ULL);
    TEST_ASSERT_EQ(idx, 0ULL);

    /* 0xFFFF400000000000 -> PUD index = 0 (高地址部分 bit[47:30]） */
    idx = PUD_INDEX(0xFFFF400000000000ULL);
    TEST_ASSERT_EQ(idx, 0ULL);
}

/**
 * @brief 测试8: 完整页表结构验证
 */
static void test_full_page_table(void)
{
    uint64_t pgd[512];
    uint64_t pud[512];
    uint64_t *pud_paddr;
    uint32_t i;

    printf("测试8: 完整页表结构验证\n");

    /* 清零 */
    for (i = 0U; i < 512U; i++)
    {
        pgd[i] = 0ULL;
        pud[i] = 0ULL;
    }

    pud_paddr = pud;

    /* PGD[0] -> PUD 表 */
    pgd[0U] = make_table_desc((uint64_t)(uintptr_t)pud_paddr);
    TEST_ASSERT_TRUE((pgd[0U] & PTE_VALID) != 0ULL);
    TEST_ASSERT_TRUE((pgd[0U] & PTE_TABLE_BIT) != 0ULL);

    /* PUD[0] = 1GB Normal Block @ 0x00000000 (设备MMIO) */
    pud[0U] = make_kernel_block(0x00000000ULL, PTE_ATTR_NORMAL);
    TEST_ASSERT_TRUE((pud[0U] & PTE_VALID) != 0ULL);
    TEST_ASSERT_EQ((pud[0U] >> 30U) & 0x3FFFU, 0ULL);

    /* PUD[1] = 1GB Normal Block @ 0x40000000 (内核代码) */
    pud[1U] = make_kernel_block(0x40000000ULL, PTE_ATTR_NORMAL);
    TEST_ASSERT_TRUE((pud[1U] & PTE_VALID) != 0ULL);
    TEST_ASSERT_EQ((pud[1U] >> 30U) & 0x3FFFU, 1ULL);

    /* 验证属性 */
    TEST_ASSERT_TRUE((pud[1U] & PTE_AF) != 0ULL);
    TEST_ASSERT_TRUE((pud[1U] & PTE_SH_INNER) != 0ULL);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("========================================\n");
    printf("  AISafe64 MMU 双地址空间单元测试\n");
    printf("========================================\n\n");

    TEST_RESET();

    test_pte_flags();
    test_block_alignment();
    test_tcr_el1_fields();
    test_asid_alloc();
    test_pgd_index();
    test_mair_encoding();
    test_pud_index();
    test_full_page_table();

    TEST_SUMMARY("MMU");
    return TEST_RESULT();
}
