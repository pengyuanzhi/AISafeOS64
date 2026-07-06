/**
 * @file    page_table.c
 * @brief   ARMv8-A 4 级页表管理实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件实现了 ARMv8-A (AArch64) 4 级页表管理：
 *          - PGD (L0) -> PUD (L1) -> PMD (L2) -> PTE (L3)
 *          - 页表创建/销毁（4KB 对齐，512 条目）
 *          - 4KB 标准页映射/解除映射
 *          - 2MB 大页块映射
 *          - 页面权限修改
 *          - TLB 维护操作（全局刷新和 ASID 刷新）
 *          - TTBR0_EL1 / TTBR1_EL1 系统寄存器访问
 *
 *          虚拟地址 48 位有效地址分解：
 *          [47:39] PGD 索引 (L0, 9 位, 512 项)
 *          [38:30] PUD 索引 (L1, 9 位, 512 项)
 *          [29:21] PMD 索引 (L2, 9 位, 512 项)
 *          [20:12] PTE 索引 (L3, 9 位, 512 项)
 *          [11:0]  页内偏移 (12 位, 4KB)
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: MM-001~007
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 *
 * @revision history
 * v1.0 2026-07-05 初始版本
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */
#include <stdint.h>
#include <string.h>
#include <kernel/page_table.h>
#include <kernel/phys_mem.h>
#include <kernel/barrier.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <kernel/compiler.h>
#include <kernel/virt_phys.h>
#include "hal.h"

/* ========================================================================
 * 编译时断言验证
 * ======================================================================== */

/* 验证：页表大小必须为 4KB（一个物理页） */
static_assert(sizeof(page_table_t) == PAGE_SIZE_4K,
              "page_table_t must be exactly 4096 bytes");

/* 验证：页表项为 8 字节（64 位） */
static_assert(sizeof(((page_table_t *)0)->entries[0U]) == sizeof(uint64_t),
              "PTE must be 8 bytes (64-bit)");

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 获取页表中指定索引位置的条目指针
 *
 * @details 给定页表指针和索引，返回该索引处条目的地址。
 *          用于在页表遍历中访问下一级页表条目。
 *
 * @param table 页表指针（不得为 NULL）
 * @param index 条目索引（有效范围 [0, 511]）
 *
 * @return 指向指定条目的指针
 *
 * @note 调用方负责验证 table 非空和 index 在范围内
 */
static inline uint64_t *walk_table(page_table_t *table, uint32_t index)
{
    return &(table->entries[index]);
}

/**
 * @brief 从页表条目中提取下一级页表的虚拟地址
 *
 * @details PTE 的高位包含下一级页表的物理地址。
 *          本函数将其转换为可访问的指针。
 *          在内核启动后，物理地址通过恒等映射直接可用。
 *
 * @param pte 页表条目值
 *
 * @return 下一级页表指针，失败返回 NULL
 */
static inline page_table_t *pte_to_table(uint64_t pte)
{
    paddr_t paddr = PTE_PADDR(pte);
    /* TTBR1 高地址线性映射：物理地址需经 phys_to_virt 转换为可访问的虚拟地址。
     * （在恒等映射启动阶段同样可用：phys_to_virt 给出线性映射 VA，
     *  而 mmu_early_init 同时建立该线性映射，故始终可访问。） */
    return phys_to_virt(paddr);
}

/**
 * @brief 根据权限和用户态标志计算 PTE 属性
 *
 * @details 根据权限标志和 is_user 标志，选择合适的 ARMv8-A
 *          页表属性组合：
 *          - DEVICE 标志: 使用 Device nGnRnE 属性（非缓存，MMIO/DMA）
 *          - 用户态 + RW: PTE_USER_DATA（读写、执行禁止、非全局）
 *          - 用户态 + RO: PTE_USER_RO（只读、执行禁止、非全局）
 *          - 用户态 + RX: PTE_USER_CODE（只读、可执行、非全局）
 *          - 内核态 + RW: PTE_KERNEL_ATTR（特权读写、全局）
 *          - 内核态 + RX: PTE_KERNEL_CODE（特权读写、可执行、全局）
 *
 * @param perm    页面权限标志
 * @param is_user 是否为用户态页面
 *
 * @return 页表属性位掩码
 */
static uint64_t compute_pte_attr(page_perm_t perm, bool is_user)
{
    uint64_t attr;

    /* Device 属性优先：用于 MMIO/DMA 映射（非缓存） */
    if ((perm & PAGE_PERM_DEVICE) != 0U)
    {
        if (is_user)
        {
            /* 用户态 Device 页（nGnRnE，读写，执行禁止，非全局） */
            attr = PTE_USER_DEVICE;
        }
        else
        {
            /* 内核态 Device 页（nGnRnE，读写，执行禁止，全局） */
            attr = PTE_VALID | PTE_AF | PTE_AP_PRIV_RW | PTE_XN | PTE_ATTR_INDEX_DEVICE;
        }
        return attr;
    }

    if (is_user)
    {
        /* 用户态页面 */
        bool has_write = ((perm & PAGE_PERM_WRITE) != 0U);
        bool has_exec = ((perm & PAGE_PERM_EXEC) != 0U);

        /* 检查是否使用全局映射（不绑 ASID，EL0+EL1 共享） */
        bool use_global = ((perm & PAGE_PERM_GLOBAL) != 0U);

        if (use_global)
        {
            /* 全局映射：EL0+EL1 可访问，不设 nG（不绑 ASID） */
            if (has_write)
            {
                attr = PTE_VALID | PTE_AF | PTE_AP_ALL_RW;  /* 全局 RW */
            }
            else
            {
                attr = PTE_VALID | PTE_AF | PTE_AP_ALL_RO;  /* 全局 RO */
            }
            return attr;
        }

        if (has_write && has_exec)
        {
            /* 用户态 RWX（ELF 单段含代码+数据，需要可读写可执行） */
            attr = PTE_VALID | PTE_AF | PTE_AP_ALL_RW | PTE_NG;
        }
        else if (has_write)
        {
            /* 用户态可读写（执行禁止） */
            attr = PTE_USER_DATA;
        }
        else if (has_exec)
        {
            /* 用户态可读可执行（只读） */
            attr = PTE_USER_CODE;
        }
        else
        {
            /* 用户态只读（执行禁止） */
            attr = PTE_USER_RO;
        }
    }
    else
    {
        /* 内核态页面 */
        bool has_exec = ((perm & PAGE_PERM_EXEC) != 0U);

        if (has_exec)
        {
            /* 内核代码页（可执行） */
            attr = PTE_KERNEL_CODE;
        }
        else
        {
            /* 内核数据页（执行禁止） */
            attr = PTE_KERNEL_ATTR;
        }
    }

    return attr;
}

/* ========================================================================
 * 页表子系统初始化
 * ======================================================================== */

/**
 * @brief 初始化页表子系统
 *
 * @details 分配内核 PGD（顶层页表），并建立内核空间的初始映射。
 *          将内核 PGD 地址写入 TTBR1_EL1 寄存器。
 *
 *          初始化流程：
 *          1. 分配一个 4KB 物理页作为内核 PGD
 *          2. 清零 PGD 所有条目
 *          3. 将 PGD 物理地址写入 TTBR1_EL1
 *
 * @return KERNEL_OK 成功
 * @return -ENOMEM  页表分配失败
 *
 * @note 必须在物理内存管理器初始化之后调用
 * @note 对应需求: MM-005
 */
kernel_status_t page_table_subsys_init(void)
{
    page_table_t *kernel_pgd = page_table_alloc();
    if (kernel_pgd == NULL)
    {
        return -(int32_t)ENOMEM;
    }

    /* 将内核 PGD 物理地址写入 TTBR1_EL1 */
    paddr_t pgd_paddr = virt_to_phys(kernel_pgd);
    hal_write_ttbr1((uint64_t)pgd_paddr);

    return KERNEL_OK;
}

/* ========================================================================
 * 页表分配与释放
 * ======================================================================== */

/**
 * @brief 分配一个新的页表
 *
 * @details 从物理内存管理器分配一个 4KB 物理页帧作为页表。
 *          分配后立即清零所有 512 个条目。
 *
 * @return 页表指针（4KB 对齐），失败返回 NULL
 *
 * @note 对应需求: MM-001
 */
page_table_t *page_table_alloc(void)
{
    paddr_t paddr = phys_mem_alloc_page();
    if (paddr == 0ULL)
    {
        return NULL;
    }

    /* TTBR1 高地址线性映射：物理地址经 phys_to_virt 转换为虚拟地址 */
    page_table_t *table = phys_to_virt(paddr);

    /* 清零所有 512 个条目（4KB = 512 * 8 字节） */
    (void)memset(table->entries, 0, sizeof(table->entries));

    return table;
}

/**
 * @brief 释放一个页表
 *
 * @details 将页表占用的 4KB 物理页帧归还给物理内存管理器。
 *
 * @param table 要释放的页表指针
 *
 * @note 调用方应确保该页表不再被任何页表项引用
 * @note 如果 table 为 NULL，则不做任何操作
 */
void page_table_free(page_table_t *table)
{
    if (table == NULL)
    {
        return;
    }

    paddr_t paddr = virt_to_phys(table);
    phys_mem_free_page(paddr);
}

/* ========================================================================
 * 页面映射（4KB 标准页）
 * ======================================================================== */

/**
 * @brief 映射 4KB 页面到虚拟地址
 *
 * @details 从 PGD 开始逐级遍历 4 级页表：
 *          PGD (L0) -> PUD (L1) -> PMD (L2) -> PTE (L3)
 *
 *          对于每一级中间页表（L0-L2）：
 *          - 如果条目无效（PTE_VALID 为 0），分配新的页表并填入
 *            PTE_VALID | PTE_TABLE 属性
 *          - 如果条目已有效且为页表类型，继续遍历下一级
 *          - 如果条目已有效但为块映射，返回错误（不允许覆盖）
 *
 *          到达 L3 后，根据 perm 和 is_user 参数设置 PTE 属性，
 *          写入物理地址和属性组合。
 *
 * @param pgd     顶层页表（L0）指针（不得为 NULL）
 * @param vaddr   虚拟地址（建议 4KB 对齐）
 * @param paddr   物理地址（建议 4KB 对齐）
 * @param perm    页面权限标志
 * @param is_user 是否为用户态页面
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL  参数无效（pgd 为 NULL）
 * @return -ENOMEM  页表分配失败
 * @return -EEXIST  映射已存在
 *
 * @note 对应需求: MM-003
 */
kernel_status_t page_table_map(page_table_t *pgd,
                                vaddr_t vaddr,
                                paddr_t paddr,
                                page_perm_t perm,
                                bool is_user)
{
    if (pgd == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 提取各级索引 */
    uint64_t idx0 = PGD_INDEX(vaddr);
    uint64_t idx1 = PUD_INDEX(vaddr);
    uint64_t idx2 = PMD_INDEX(vaddr);
    uint64_t idx3 = PTE_INDEX(vaddr);

    /* ===== L0 -> L1 遍历 ===== */
    uint64_t *entry0 = walk_table(pgd, (uint32_t)idx0);
    if (!PTE_IS_VALID(*entry0))
    {
        /* L0 条目无效，分配新的 PUD (L1) 页表 */
        page_table_t *pud = page_table_alloc();
        if (pud == NULL)
        {
            return -(int32_t)ENOMEM;
        }
        paddr_t pud_paddr = virt_to_phys(pud);
        *entry0 = (pud_paddr & ~(PAGE_SIZE_4K - 1ULL)) | PTE_VALID | PTE_TABLE;
        barrier();
    }
    else if (!PTE_IS_TABLE(*entry0))
    {
        /* L0 条目有效但不是页表类型（块映射冲突） */
        return -(int32_t)EEXIST;
    }
    else
    {
        /* MISRA-C:2012 要求的空 else 分支 */
    }

    page_table_t *pud = pte_to_table(*entry0);
    if (pud == NULL)
    {
        return -(int32_t)EFAULT;
    }

    /* ===== L1 -> L2 遍历 ===== */
    uint64_t *entry1 = walk_table(pud, (uint32_t)idx1);
    if (!PTE_IS_VALID(*entry1))
    {
        /* L1 条目无效，分配新的 PMD (L2) 页表 */
        page_table_t *pmd = page_table_alloc();
        if (pmd == NULL)
        {
            return -(int32_t)ENOMEM;
        }
        paddr_t pmd_paddr = virt_to_phys(pmd);
        *entry1 = (pmd_paddr & ~(PAGE_SIZE_4K - 1ULL)) | PTE_VALID | PTE_TABLE;
        barrier();
    }
    else if (!PTE_IS_TABLE(*entry1))
    {
        /* L1 条目有效但不是页表类型（块映射冲突） */
        return -(int32_t)EEXIST;
    }
    else
    {
        /* MISRA-C:2012 要求的空 else 分支 */
    }

    page_table_t *pmd = pte_to_table(*entry1);
    if (pmd == NULL)
    {
        return -(int32_t)EFAULT;
    }

    /* ===== L2 -> L3 遍历 ===== */
    uint64_t *entry2 = walk_table(pmd, (uint32_t)idx2);
    if (!PTE_IS_VALID(*entry2))
    {
        /* L2 条目无效，分配新的 PTE (L3) 页表 */
        page_table_t *pte_table = page_table_alloc();
        if (pte_table == NULL)
        {
            return -(int32_t)ENOMEM;
        }
        paddr_t pte_paddr = virt_to_phys(pte_table);
        *entry2 = (pte_paddr & ~(PAGE_SIZE_4K - 1ULL)) | PTE_VALID | PTE_TABLE;
        barrier();
    }
    else if (!PTE_IS_TABLE(*entry2))
    {
        /* L2 条目有效但不是页表类型（2MB 块映射冲突） */
        return -(int32_t)EEXIST;
    }
    else
    {
        /* MISRA-C:2012 要求的空 else 分支 */
    }

    page_table_t *pte_table = pte_to_table(*entry2);
    if (pte_table == NULL)
    {
        return -(int32_t)EFAULT;
    }

    /* ===== L3: 设置 PTE 映射 ===== */
    uint64_t *entry3 = walk_table(pte_table, (uint32_t)idx3);
    if (PTE_IS_VALID(*entry3))
    {
        /* L3 条目已有效，映射已存在 */
        return -(int32_t)EEXIST;
    }

    /* 计算页面属性 */
    uint64_t attr = compute_pte_attr(perm, is_user);

    /*
     * 调试代码已移除（P0-1）：
     *   原先在 is_user 路径上向 UART 打印 perm/attr 十六进制，
     *   并在写入后读回 PTE 校验。UART 是毫秒级阻塞设备，
     *   出现在用户态页映射热路径上会严重拖慢系统并破坏实时性。
     *   热路径上不得保留任何 UART 输出。
     */

    /* 写入 L3 PTE：物理地址 | 属性 */
    *entry3 = PTE_MAKE(paddr, attr);
    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 页面解除映射
 * ======================================================================== */

/**
 * @brief 解除虚拟地址的映射
 *
 * @details 从 PGD 开始逐级遍历 4 级页表，找到 L3 条目后将其清零，
 *          随后刷新 TLB 确保映射变更立即生效。
 *
 * @param pgd   顶层页表指针（不得为 NULL）
 * @param vaddr 要解除映射的虚拟地址
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL  参数无效（pgd 为 NULL）
 * @return -ENOENT  未找到映射
 *
 * @note 解除映射后会刷新全部 TLB
 */
kernel_status_t page_table_unmap(page_table_t *pgd, vaddr_t vaddr)
{
    if (pgd == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 提取各级索引 */
    uint64_t idx0 = PGD_INDEX(vaddr);
    uint64_t idx1 = PUD_INDEX(vaddr);
    uint64_t idx2 = PMD_INDEX(vaddr);
    uint64_t idx3 = PTE_INDEX(vaddr);

    /* ===== L0 遍历 ===== */
    uint64_t *entry0 = walk_table(pgd, (uint32_t)idx0);
    if (!PTE_IS_VALID(*entry0) || !PTE_IS_TABLE(*entry0))
    {
        return -(int32_t)ENOENT;
    }

    page_table_t *pud = pte_to_table(*entry0);
    if (pud == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* ===== L1 遍历 ===== */
    uint64_t *entry1 = walk_table(pud, (uint32_t)idx1);
    if (!PTE_IS_VALID(*entry1) || !PTE_IS_TABLE(*entry1))
    {
        return -(int32_t)ENOENT;
    }

    page_table_t *pmd = pte_to_table(*entry1);
    if (pmd == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* ===== L2 遍历 ===== */
    uint64_t *entry2 = walk_table(pmd, (uint32_t)idx2);
    if (!PTE_IS_VALID(*entry2) || !PTE_IS_TABLE(*entry2))
    {
        return -(int32_t)ENOENT;
    }

    page_table_t *pte_table = pte_to_table(*entry2);
    if (pte_table == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* ===== L3: 清除 PTE ===== */
    uint64_t *entry3 = walk_table(pte_table, (uint32_t)idx3);
    if (!PTE_IS_VALID(*entry3))
    {
        return -(int32_t)ENOENT;
    }

    /* 清除 L3 PTE */
    *entry3 = 0ULL;
    barrier();

    /* 刷新 TLB 确保变更生效。
     * 注：当前使用全核全 ASID 刷新（tlbi vmalle1is），开销较大。
     * 待 HAL 层增加 tlbi vae1is（按 VA 失效）接口后，
     * 可改为按地址单页失效以降低开销（P2-1，性能优化）。 */
    tlb_flush_all();

    return KERNEL_OK;
}

/* ========================================================================
 * 页面查询
 * ======================================================================== */

/**
 * @brief 查询虚拟地址的物理映射
 *
 * @details 从 PGD 开始逐级遍历 4 级页表，找到 L3 条目后
 *          提取物理地址并写入输出参数。
 *
 * @param pgd    顶层页表指针（不得为 NULL）
 * @param vaddr  要查询的虚拟地址
 * @param paddr  输出参数，返回物理地址（不得为 NULL）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL  参数无效（pgd 或 paddr 为 NULL）
 * @return -ENOENT  未找到映射
 */
kernel_status_t page_table_lookup(page_table_t *pgd,
                                   vaddr_t vaddr,
                                   paddr_t *paddr)
{
    if ((pgd == NULL) || (paddr == NULL))
    {
        return -(int32_t)EINVAL;
    }

    /* 提取各级索引 */
    uint64_t idx0 = PGD_INDEX(vaddr);
    uint64_t idx1 = PUD_INDEX(vaddr);
    uint64_t idx2 = PMD_INDEX(vaddr);
    uint64_t idx3 = PTE_INDEX(vaddr);

    /* ===== L0 遍历 ===== */
    uint64_t *entry0 = walk_table(pgd, (uint32_t)idx0);
    if (!PTE_IS_VALID(*entry0) || !PTE_IS_TABLE(*entry0))
    {
        return -(int32_t)ENOENT;
    }

    page_table_t *pud = pte_to_table(*entry0);
    if (pud == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* ===== L1 遍历 ===== */
    uint64_t *entry1 = walk_table(pud, (uint32_t)idx1);
    if (!PTE_IS_VALID(*entry1) || !PTE_IS_TABLE(*entry1))
    {
        return -(int32_t)ENOENT;
    }

    page_table_t *pmd = pte_to_table(*entry1);
    if (pmd == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* ===== L2 遍历 ===== */
    uint64_t *entry2 = walk_table(pmd, (uint32_t)idx2);
    if (!PTE_IS_VALID(*entry2) || !PTE_IS_TABLE(*entry2))
    {
        return -(int32_t)ENOENT;
    }

    page_table_t *pte_table = pte_to_table(*entry2);
    if (pte_table == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* ===== L3: 读取物理地址 ===== */
    uint64_t *entry3 = walk_table(pte_table, (uint32_t)idx3);
    if (!PTE_IS_VALID(*entry3))
    {
        return -(int32_t)ENOENT;
    }

    *paddr = PTE_PADDR(*entry3);

    return KERNEL_OK;
}

/* ========================================================================
 * 页面权限修改
 * ======================================================================== */

/**
 * @brief 修改页面的访问权限
 *
 * @details 遍历页表找到 L3 条目，保留物理地址不变，
 *          仅修改属性位。修改后刷新 TLB 确保新权限生效。
 *
 * @param pgd   顶层页表指针（不得为 NULL）
 * @param vaddr 虚拟地址
 * @param perm  新的页面权限
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL  参数无效
 * @return -ENOENT  未找到映射
 */
kernel_status_t page_table_protect(page_table_t *pgd,
                                    vaddr_t vaddr,
                                    page_perm_t perm)
{
    if (pgd == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 提取各级索引 */
    uint64_t idx0 = PGD_INDEX(vaddr);
    uint64_t idx1 = PUD_INDEX(vaddr);
    uint64_t idx2 = PMD_INDEX(vaddr);
    uint64_t idx3 = PTE_INDEX(vaddr);

    /* ===== L0 遍历 ===== */
    uint64_t *entry0 = walk_table(pgd, (uint32_t)idx0);
    if (!PTE_IS_VALID(*entry0) || !PTE_IS_TABLE(*entry0))
    {
        return -(int32_t)ENOENT;
    }

    page_table_t *pud = pte_to_table(*entry0);
    if (pud == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* ===== L1 遍历 ===== */
    uint64_t *entry1 = walk_table(pud, (uint32_t)idx1);
    if (!PTE_IS_VALID(*entry1) || !PTE_IS_TABLE(*entry1))
    {
        return -(int32_t)ENOENT;
    }

    page_table_t *pmd = pte_to_table(*entry1);
    if (pmd == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* ===== L2 遍历 ===== */
    uint64_t *entry2 = walk_table(pmd, (uint32_t)idx2);
    if (!PTE_IS_VALID(*entry2) || !PTE_IS_TABLE(*entry2))
    {
        return -(int32_t)ENOENT;
    }

    page_table_t *pte_table = pte_to_table(*entry2);
    if (pte_table == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* ===== L3: 修改权限 ===== */
    uint64_t *entry3 = walk_table(pte_table, (uint32_t)idx3);
    if (!PTE_IS_VALID(*entry3))
    {
        return -(int32_t)ENOENT;
    }

    /* 提取原始物理地址 */
    paddr_t orig_paddr = PTE_PADDR(*entry3);

    /* 判断原始页面是否为用户态（通过 nG 位判断） */
    bool is_user = ((*entry3 & PTE_NG) != 0ULL);

    /* 计算新属性 */
    uint64_t new_attr = compute_pte_attr(perm, is_user);

    /* 写入新的 PTE：保留物理地址，更新属性 */
    *entry3 = PTE_MAKE(orig_paddr, new_attr);
    barrier();

    /* 刷新 TLB 使新权限生效。
     * 注：当前使用全核全 ASID 刷新，开销较大。
     * 待 HAL 层增加按 VA 失效（tlbi vae1is）接口后优化（P2-1）。 */
    tlb_flush_all();

    return KERNEL_OK;
}

/* ========================================================================
 * 大页块映射（2MB）
 * ======================================================================== */

/**
 * @brief 映射 2MB 大页（L2 块映射）
 *
 * @details 在 L2 (PMD) 级别设置块映射，跳过 L3 分页。
 *          2MB 块映射适用于大块连续内存区域，可减少 TLB 压力。
 *
 *          遍历路径：PGD (L0) -> PUD (L1) -> PMD (L2)
 *          在 PMD 条目中直接写入块映射（不设置 PTE_TABLE 位）。
 *
 * @param pgd     顶层页表指针（不得为 NULL）
 * @param vaddr   虚拟地址（建议 2MB 对齐）
 * @param paddr   物理地址（建议 2MB 对齐）
 * @param perm    页面权限
 * @param is_user 是否为用户态页面
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL  参数无效
 * @return -ENOMEM  页表分配失败
 * @return -EEXIST  映射已存在
 */
kernel_status_t page_table_map_block(page_table_t *pgd,
                                      vaddr_t vaddr,
                                      paddr_t paddr,
                                      page_perm_t perm,
                                      bool is_user)
{
    if (pgd == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 提取各级索引 */
    uint64_t idx0 = PGD_INDEX(vaddr);
    uint64_t idx1 = PUD_INDEX(vaddr);
    uint64_t idx2 = PMD_INDEX(vaddr);

    /* ===== L0 -> L1 遍历 ===== */
    uint64_t *entry0 = walk_table(pgd, (uint32_t)idx0);
    if (!PTE_IS_VALID(*entry0))
    {
        /* L0 条目无效，分配新的 PUD (L1) 页表 */
        page_table_t *pud = page_table_alloc();
        if (pud == NULL)
        {
            return -(int32_t)ENOMEM;
        }
        paddr_t pud_paddr = virt_to_phys(pud);
        *entry0 = (pud_paddr & ~(PAGE_SIZE_4K - 1ULL)) | PTE_VALID | PTE_TABLE;
        barrier();
    }
    else if (!PTE_IS_TABLE(*entry0))
    {
        /* L0 条目有效但不是页表类型 */
        return -(int32_t)EEXIST;
    }
    else
    {
        /* MISRA-C:2012 要求的空 else 分支 */
    }

    page_table_t *pud = pte_to_table(*entry0);
    if (pud == NULL)
    {
        return -(int32_t)EFAULT;
    }

    /* ===== L1 -> L2 遍历 ===== */
    uint64_t *entry1 = walk_table(pud, (uint32_t)idx1);
    if (!PTE_IS_VALID(*entry1))
    {
        /* L1 条目无效，分配新的 PMD (L2) 页表 */
        page_table_t *pmd = page_table_alloc();
        if (pmd == NULL)
        {
            return -(int32_t)ENOMEM;
        }
        paddr_t pmd_paddr = virt_to_phys(pmd);
        *entry1 = (pmd_paddr & ~(PAGE_SIZE_4K - 1ULL)) | PTE_VALID | PTE_TABLE;
        barrier();
    }
    else if (!PTE_IS_TABLE(*entry1))
    {
        /* L1 条目有效但不是页表类型 */
        return -(int32_t)EEXIST;
    }
    else
    {
        /* MISRA-C:2012 要求的空 else 分支 */
    }

    page_table_t *pmd = pte_to_table(*entry1);
    if (pmd == NULL)
    {
        return -(int32_t)EFAULT;
    }

    /* ===== L2: 设置 2MB 块映射 ===== */
    uint64_t *entry2 = walk_table(pmd, (uint32_t)idx2);
    if (PTE_IS_VALID(*entry2))
    {
        /* L2 条目已有效（块映射或页表映射冲突） */
        return -(int32_t)EEXIST;
    }

    /* 计算块映射属性（复用 compute_pte_attr 逻辑） */
    uint64_t attr = compute_pte_attr(perm, is_user);

    /*
     * 2MB 块映射的物理地址必须 2MB 对齐。
     * PTE_MAKE 使用 4KB 掩码，此处需要手动处理 2MB 对齐。
     * 块映射不设置 PTE_TABLE 位，仅设置 PTE_VALID。
     * attr 中已包含 PTE_VALID 和 PTE_AF 等必要位。
     */
    uint64_t block_paddr = paddr & ~(PAGE_SIZE_2M - 1ULL);
    *entry2 = block_paddr | attr;
    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * TLB 维护操作
 * ======================================================================== */

/**
 * @brief 刷新指定地址空间的 TLB
 *
 * @details 使用 TLBI ASIDE1IS 指令，仅刷新指定 ASID
 *          关联的 TLB 条目。在进程切换时使用，避免
 *          刷新全局 TLB 带来的性能损失。
 *
 *          操作序列：
 *          1. 构造 ASID 操作数（ASID << 48）
 *          2. 执行 TLBI ASIDE1IS 指令
 *          3. 执行 DSB ISH 数据同步屏障
 *          4. 执行 ISB 指令同步屏障
 *
 * @param asid 地址空间标识（有效范围 [0, 255]）
 *
 * @note 对应需求: MM-006
 */
void tlb_flush_asid(asid_t asid)
{
    hal_tlb_invalidate_asid((uint64_t)asid);
}

/**
 * @brief 刷新全部 TLB
 *
 * @details 使用 TLBI VMALLE1IS 指令，刷新当前 EL1 下
 *          所有 TLB 条目（包括所有 ASID）。
 *
 *          操作序列：
 *          1. 执行 TLBI VMALLE1IS 指令
 *          2. 执行 DSB ISH 数据同步屏障
 *          3. 执行 ISB 指令同步屏障
 *
 * @note 此操作会影响所有核心的 TLB，开销较大
 * @note 优先使用 tlb_flush_asid() 进行局部刷新
 */
void tlb_flush_all(void)
{
    hal_tlb_invalidate_all();
}
