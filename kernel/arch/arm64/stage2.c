/**
 * @file    stage2.c
 * @brief   ARM Stage-2 页表管理（Guest 物理地址映射）
 * @author  AISafe64 Team
 * @date    2026-07-08
 * @version 1.0
 *
 * @details Stage-2 页表将 Guest 物理地址（IPA）映射到 Host 物理地址（PA）。
 *
 *          ARM64 Stage-2 页表格式：
 *          - L0 → L1 → L2 → L3 四级（与 Stage-1 类似但格式不同）
 *          - L0/L1/L2 支持 block 映射（1GB/512MB/2MB）
 *          - L3 支持 page 映射（4KB）
 *          - 页表项格式与 Stage-1 不同（bits 定义不同）
 *
 *          Stage-2 页表由 VTTBR_EL2 指向。
 *          每个 Guest 有独立的 Stage-2 页表。
 *
 *          本模块运行在 EL1（Host 内核），通过 HVC 调用让 EL2 执行
 *          VTTBR_EL2 写入和 TLB 刷新。
 *
 * @note    QNX 方案：EL2 hypervisor + EL1 Host + EL1/EL0 Guest
 *
 * @revision history
 * v1.0 2026-07-08 初始版本
 */

#include <kernel/stage2.h>
#include <kernel/config.h>
#include <kernel/phys_mem.h>
#include <kernel/virt_phys.h>
#include <string.h>
#include <stdint.h>

/* ========================================================================
 * Stage-2 页表项格式（与 Stage-1 不同！）
 * ========================================================================
 *
 * Stage-2 PTE 位定义：
 * [0]     Valid
 * [1]     Table (0=block/page, 1=pointer to next level)
 * [3:2]   MemAttr (Stage-2 memory attributes)
 * [5:4]   S2AP (Stage-2 Access Permissions: 00=none, 01=R, 10=W, 11=RW)
 * [6]     XN[1] (Execute Never, bits [6] and [7] both must be considered)
 * [7]     XN[0] / AffN (Address Permission Fault)
 * [8]     AF (Access Flag)
 * [9]     Sh (Shareability)
 * [11:10] AttrIndex (Stage-2 memory type index)
 * [47:12] Output Address (PA bits [47:12])
 * [53:48] RES0
 * [54]    DBM (Dirty Bit Modifier)
 * [55]    PBHA[5]
 * [58:56] RES0
 * [59]    DBM/PBHA
 * [61:60] RES0
 * [62]    RES1
 * [63]    RES1
 */

/** @brief Stage-2 Valid 位 */
#define S2_PTE_VALID       (1ULL << 0U)

/** @brief Stage-2 Table 位（指向下一级页表） */
#define S2_PTE_TABLE       (1ULL << 1U)

/** @brief Stage-2 页大小（4KB） */
#define S2_PAGE_SIZE       4096ULL

/** @brief Stage-2 每级条目数（512） */
#define S2_ENTRIES         512U

/** @brief S2AP 值（Stage-2 Access Permissions） */
#define S2AP_NONE          (0ULL << 4U)  /**< @brief 无访问 */
#define S2AP_READ          (1ULL << 4U)  /**< @brief 只读 */
#define S2AP_WRITE         (2ULL << 4U)  /**< @brief 只写 */
#define S2AP_READ_WRITE    (3ULL << 4U)  /**< @brief 读写 */

/** @brief Stage-2 MemAttr（Normal WB Cacheable） */
#define S2_MEMATTR_NORMAL  (0xFULL << 2U)  /**< @brief Inner+Outer WB Cacheable */

/** @brief Stage-2 MemAttr（Device nGnRnE） */
#define S2_MEMATTR_DEVICE  (0x1ULL << 2U)  /**< @brief Device nGnRnE */

/** @brief Stage-2 AF（Access Flag，必须设为 1） */
#define S2_PTE_AF          (1ULL << 8U)

/** @brief Stage-2 XN（Execute Never for all EL） */
#define S2_PTE_XN          (3ULL << 6U)  /**< @brief XN[1:0] = 11 */

/** @brief 从 Stage-2 PTE 提取输出地址 */
#define S2_PTE_ADDR(pte)   ((pte) & 0x0000FFFFFFFFF000ULL)

/** @brief 构造 Stage-2 页映射描述符 */
#define S2_PAGE_DESC(pa, memattr, s2ap) \
    (((pa) & 0x0000FFFFFFFFF000ULL) | S2_PTE_VALID | S2_PTE_AF | (memattr) | (s2ap))

/** @brief 构造 Stage-2 block 映射描述符（同 page 但粒度更大） */
#define S2_BLOCK_DESC(pa, memattr, s2ap) \
    (((pa) & 0x0000FFFFFFFFF000ULL) | S2_PTE_VALID | S2_PTE_AF | (memattr) | (s2ap))

/** @brief 构造 Stage-2 table 描述符（指向下一级） */
#define S2_TABLE_DESC(pa) \
    (((pa) & 0x0000FFFFFFFFF000ULL) | S2_PTE_VALID | S2_PTE_TABLE)

/** @brief Stage-2 PGD 索引提取（IPA bits [47:39]） */
#define S2_PGD_INDEX(ipa)  (((ipa) >> 39U) & 0x1FFU)

/** @brief Stage-2 PUD 索引提取（IPA bits [38:30]） */
#define S2_PUD_INDEX(ipa)  (((ipa) >> 30U) & 0x1FFU)

/** @brief Stage-2 PMD 索引提取（IPA bits [29:21]） */
#define S2_PMD_INDEX(ipa)  (((ipa) >> 21U) & 0x1FFU)

/** @brief Stage-2 PTE 索引提取（IPA bits [20:12]） */
#define S2_PTE_INDEX(ipa)  (((ipa) >> 12U) & 0x1FFU)

/* ========================================================================
 * 数据结构
 * ======================================================================== */

/**
 * @brief Stage-2 页表（512 个 64 位条目，4KB 对齐）
 */
typedef struct
{
    uint64_t entries[S2_ENTRIES] __attribute__((aligned(4096)));
} s2_page_table_t;

/**
 * @brief Guest 地址空间
 */
struct s2_vm
{
    s2_page_table_t *pgd;        /**< @brief L0 页表（VTTBR_EL2 指向这里） */
    paddr_t          pgd_paddr;  /**< @brief PGD 物理地址（写入 VTTBR_EL2） */
    bool             in_use;     /**< @brief 是否活跃 */
};

/** @brief Guest 地址空间池 */
#ifndef CONFIG_MAX_GUESTS
#define CONFIG_MAX_GUESTS 4U
#endif

static struct s2_vm s_guests[CONFIG_MAX_GUESTS];

/* ========================================================================
 * 内部辅助
 * ======================================================================== */

/**
 * @brief 分配 Stage-2 页表（4KB 对齐物理页）
 *
 * @return 页表虚拟指针，失败返回 NULL
 */
static s2_page_table_t *s2_alloc_table(void)
{
    paddr_t pa = phys_mem_alloc_page();
    if (pa == 0ULL)
    {
        return NULL;
    }
    s2_page_table_t *tbl = (s2_page_table_t *)phys_to_virt(pa);
    (void)memset(tbl, 0, sizeof(s2_page_table_t));
    return tbl;
}

/* ========================================================================
 * 公共 API
 * ======================================================================== */

int32_t s2_create_vm(uint32_t *out_vm_id)
{
    uint32_t i;

    if (out_vm_id == NULL)
    {
        return -1;
    }

    for (i = 0U; i < CONFIG_MAX_GUESTS; i++)
    {
        if (!s_guests[i].in_use)
        {
            s2_page_table_t *pgd = s2_alloc_table();
            if (pgd == NULL)
            {
                return -1;
            }

            s_guests[i].pgd = pgd;
            s_guests[i].pgd_paddr = virt_to_phys(pgd);
            s_guests[i].in_use = true;
            *out_vm_id = i;
            return 0;
        }
    }

    return -1;
}

int32_t s2_map(uint32_t vm_id, uint64_t ipa, uint64_t pa,
               uint64_t size, uint32_t perm)
{
    struct s2_vm *vm;
    uint64_t memattr;
    uint64_t s2ap;

    if (vm_id >= CONFIG_MAX_GUESTS)
    {
        return -1;
    }

    vm = &s_guests[vm_id];
    if (!vm->in_use || (vm->pgd == NULL))
    {
        return -1;
    }

    /* 确定内存属性 */
    memattr = S2_MEMATTR_NORMAL;

    /* 确定权限 */
    s2ap = S2AP_READ_WRITE;
    if ((perm & S2_PERM_RO) != 0U)
    {
        s2ap = S2AP_READ;
    }

    /* 简化实现：用 1GB block 映射覆盖整个 Guest 物理空间
     * 后续完善为精细页映射 */
    {
        uint64_t offset;
        for (offset = 0ULL; offset < size; offset += (1ULL << 30U))
        {
            uint64_t cur_ipa = ipa + offset;
            uint64_t cur_pa = pa + offset;
            uint32_t idx0 = S2_PGD_INDEX(cur_ipa);

            /* 1GB block descriptor */
            vm->pgd->entries[idx0] = S2_BLOCK_DESC(cur_pa, memattr, s2ap);
        }
    }

    return 0;
}

int32_t s2_switch_vm(uint32_t vm_id)
{
    if (vm_id >= CONFIG_MAX_GUESTS)
    {
        return -1;
    }

    if (!s_guests[vm_id].in_use)
    {
        return -1;
    }

    /* 通过 HVC 调用 EL2 设置 VTTBR_EL2
     * HVC #0, x0 = VTTBR_EL2 值 = VMID(高8位) | PGD物理地址
     *
     * 当前简化：直接写 VTTBR_EL2（仅在 EL2 可写）
     * 实际需要 HVC trap 到 EL2 */
    {
        uint64_t vttbr = s_guests[vm_id].pgd_paddr
                        | ((uint64_t)vm_id << 48U);  /* VMID in bits[55:48] */

        /* HVC 调用：x0 = VTTBR 值 */
        __asm__ volatile(
            "mov x0, %0\n"
            "mov x1, #1\n"      /* S2_HVC_SWITCH_VM */
            "hvc #0\n"
            :: "r"(vttbr) : "x0", "x1", "memory"
        );
    }

    return 0;
}

int32_t s2_destroy_vm(uint32_t vm_id)
{
    if (vm_id >= CONFIG_MAX_GUESTS)
    {
        return -1;
    }

    s_guests[vm_id].in_use = false;
    s_guests[vm_id].pgd = NULL;
    s_guests[vm_id].pgd_paddr = 0ULL;

    return 0;
}
