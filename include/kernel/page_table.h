/**
 * @file    page_table.h
 * @brief   ARMv8-A 页表管理接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了 ARMv8-A 4 级页表（PGD → PUD → PMD → PTE）管理接口：
 *          - 页表项（PTE）格式定义
 *          - 页表创建/销毁
 *          - 页面映射/解除映射
 *          - 页面权限设置
 *          - TLB 维护操作
 *
 *          ARMv8-A 虚拟地址布局（48 位有效地址）：
 *          [63:48] 符号扩展
 *          [47:39] PGD 索引（L0，9 位，512 项）
 *          [38:30] PUD 索引（L1，9 位，512 项）
 *          [29:21] PMD 索引（L2，9 位，512 项）
 *          [20:12] PTE 索引（L3，9 位，512 项）
 *          [11:0]  页内偏移（12 位，4KB）
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: MM-001~007
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_PAGE_TABLE_H
#define KERNEL_PAGE_TABLE_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <stdbool.h>
#include <stdint.h>

/* ========================================================================
 * 页大小常量
 * ======================================================================== */

/** @brief 4KB 标准页的偏移位数 */
#define PAGE_SHIFT          12U

/** @brief 4KB 标准页大小 */
#define PAGE_SIZE_4K        ((uint64_t)4096ULL)

/** @brief 2MB 大页大小 */
#define PAGE_SIZE_2M        ((uint64_t)0x200000ULL)

/** @brief 1GB 大页大小 */
#define PAGE_SIZE_1G        ((uint64_t)0x40000000ULL)

/** @brief 页表项数量（每级 512 项，2^9） */
#define PTRS_PER_TABLE      512U

/** @brief 页表索引位数（9 位） */
#define PAGE_TABLE_SHIFT    9U

/* ========================================================================
 * 虚拟地址位域偏移
 * ======================================================================== */

/** @brief L0(PGD) 索引起始位 */
#define PGD_SHIFT           39U

/** @brief L1(PUD) 索引起始位 */
#define PUD_SHIFT           30U

/** @brief L2(PMD) 索引起始位 */
#define PMD_SHIFT           21U

/** @brief L3(PTE) 索引起始位 */
#define PTE_SHIFT           12U

/* ========================================================================
 * 页表项属性位定义（ARMv8-A PTE 格式）
 * ======================================================================== */

/** @brief 有效位（Entry 存在） */
#define PTE_VALID           ((uint64_t)1U << 0U)

/** @brief 页表项（非块映射） */
#define PTE_TABLE           ((uint64_t)1U << 1U)

/** @brief 可写（AF + AP 模型） */
#define PTE_AF              ((uint64_t)1U << 10U)

/** @brief nG 位（非全局，进程私有 TLB） */
#define PTE_NG              ((uint64_t)1U << 11U)

/** @brief PXN 位（特权执行禁止） */
#define PTE_PXN             (1ULL << 53U)

/** @brief XN 位（执行禁止） */
#define PTE_XN              (1ULL << 54U)

/* ========================================================================
 * 页表项属性宏（AP[2:1] 访问权限编码）
 * ======================================================================== */

/** @brief 特权读写 + 用户无访问 */
#define PTE_AP_PRIV_RW      ((uint64_t)0U << 6U)

/** @brief 特权读写 + 用户读写 */
#define PTE_AP_ALL_RW       ((uint64_t)1U << 6U)

/** @brief 特权只读 + 用户无访问 */
#define PTE_AP_PRIV_RO      ((uint64_t)2U << 6U)

/** @brief 特权只读 + 用户只读 */
#define PTE_AP_ALL_RO       ((uint64_t)3U << 6U)

/* ========================================================================
 * 内存属性索引（AttrIndex，bits[4:2]，对应 MAIR_EL1）
 *
 * @details MAIR_EL1 已在 mmu_early_init 中配置：
 *          - MAIR[0] = 0xFF（Normal Write-Back Cacheable）
 *          - MAIR[1] = 0x00（Device nGnRnE）
 *          PTE 通过 AttrIndex 选择使用哪个 MAIR 属性。
 * ======================================================================== */

/** @brief Normal 内存属性索引（MAIR[0]，可缓存） */
#define PTE_ATTR_INDEX_NORMAL   ((uint64_t)0U << 2U)

/** @brief Device 内存属性索引（MAIR[1]，nGnRnE，设备 MMIO） */
#define PTE_ATTR_INDEX_DEVICE   ((uint64_t)1U << 2U)

/** @brief 内核页属性（读写、执行禁止、全局） */
#define PTE_KERNEL_ATTR     (PTE_VALID | PTE_AF | PTE_AP_PRIV_RW)

/** @brief 内核代码页属性（读写、可执行、全局） */
#define PTE_KERNEL_CODE     (PTE_VALID | PTE_AF | PTE_AP_PRIV_RW)

/** @brief 用户数据页属性（读写、执行禁止、非全局） */
#define PTE_USER_DATA       (PTE_VALID | PTE_AF | PTE_AP_ALL_RW | PTE_NG | PTE_PXN | PTE_XN)

/** @brief 用户代码页属性（只读、可执行、非全局） */
#define PTE_USER_CODE       (PTE_VALID | PTE_AF | PTE_AP_ALL_RO | PTE_NG)

/** @brief 用户只读页属性 */
#define PTE_USER_RO         (PTE_VALID | PTE_AF | PTE_AP_ALL_RO | PTE_NG | PTE_PXN | PTE_XN)

/** @brief 全局共享页属性（EL0+EL1 可读可执行，全局，不绑 ASID）
 *
 * @details 用于用户 PGD 中映射内核代码（异常向量表等），
 *          全局映射避免 ASID 不匹配导致 TLB miss。
 */
#define PTE_SHARED_CODE     (PTE_VALID | PTE_AF | PTE_AP_ALL_RO)

/** @brief 用户设备页属性（Device nGnRnE，读写、执行禁止、非全局）
 *
 * @details 用于用户态驱动的 MMIO 映射和 DMA 内存映射。
 *          Device 属性保证内存访问不被重排、不缓存，
 *          免除用户态无法执行的 cache 维护指令（dc cvac 等）。
 *          AttrIndex=1 对应 MAIR[1]=0x00（Device nGnRnE）。
 */
#define PTE_USER_DEVICE     (PTE_VALID | PTE_AF | PTE_AP_ALL_RW | PTE_NG | PTE_PXN | PTE_XN | PTE_ATTR_INDEX_DEVICE)

/* ========================================================================
 * 页表项操作宏
 * ======================================================================== */

/** @brief 从物理地址和属性构造 L3 PTE（页描述符）
 *
 * @details ARM64 L3 PTE 的 bit[1] 必须为 1（表示页描述符），
 *          否则硬件视为无效条目。PTE_MAKE 自动包含此位。
 */
#define PTE_MAKE(paddr, attr)    (((paddr) & ~(PAGE_SIZE_4K - 1ULL)) | (attr) | PTE_TABLE)

/** @brief 从 PTE 提取物理地址
 *
 * @details ARM64 PTE 中 bit[47:12] 为物理地址（48 位 PA），
 *          bit[63:48] 和 bit[11:0] 为属性/标志位。
 *          提取物理地址需清除低 12 位和高 16 位。
 */
#define PTE_PADDR(pte)           ((pte) & 0x0000FFFFFFFFF000ULL)

/** @brief 检查 PTE 是否有效 */
#define PTE_IS_VALID(pte)        (((pte) & PTE_VALID) != 0ULL)

/** @brief 检查是否为页表项（非块映射） */
#define PTE_IS_TABLE(pte)        (((pte) & PTE_TABLE) != 0ULL)

/** @brief 从虚拟地址提取 PGD 索引 */
#define PGD_INDEX(vaddr)   (((vaddr) >> PGD_SHIFT) & (uint64_t)(PTRS_PER_TABLE - 1U))

/** @brief 从虚拟地址提取 PUD 索引 */
#define PUD_INDEX(vaddr)   (((vaddr) >> PUD_SHIFT) & (uint64_t)(PTRS_PER_TABLE - 1U))

/** @brief 从虚拟地址提取 PMD 索引 */
#define PMD_INDEX(vaddr)   (((vaddr) >> PMD_SHIFT) & (uint64_t)(PTRS_PER_TABLE - 1U))

/** @brief 从虚拟地址提取 PTE 索引 */
#define PTE_INDEX(vaddr)   (((vaddr) >> PTE_SHIFT) & (uint64_t)(PTRS_PER_TABLE - 1U))

/* ========================================================================
 * 页面权限枚举
 * ======================================================================== */

/**
 * @brief 页面权限标志
 */
typedef enum
{
    PAGE_PERM_NONE = 0U,            /**< @brief 无权限 */
    PAGE_PERM_READ = (1U << 0),     /**< @brief 可读 */
    PAGE_PERM_WRITE = (1U << 1),    /**< @brief 可写 */
    PAGE_PERM_EXEC = (1U << 2),     /**< @brief 可执行 */
    PAGE_PERM_DEVICE = (1U << 4),   /**< @brief Device/MMIO 映射（非缓存） */
    PAGE_PERM_GLOBAL = (1U << 7),   /**< @brief 全局映射（EL0+EL1 共享，不绑 ASID） */
    PAGE_PERM_RW = PAGE_PERM_READ | PAGE_PERM_WRITE,
    PAGE_PERM_RX = PAGE_PERM_READ | PAGE_PERM_EXEC,
    PAGE_PERM_RWX = PAGE_PERM_READ | PAGE_PERM_WRITE | PAGE_PERM_EXEC,
    /**< @brief Device RW（MMIO 映射，用户态驱动用） */
    PAGE_PERM_DEVICE_RW = PAGE_PERM_READ | PAGE_PERM_WRITE | PAGE_PERM_DEVICE
} page_perm_t;

/* ========================================================================
 * 页表结构体
 * ======================================================================== */

/**
 * @brief 页表结构（一个页表 = 512 个 64 位条目 = 4KB）
 */
typedef struct
{
    uint64_t entries[PTRS_PER_TABLE]; /**< @brief 页表条目数组 */
} page_table_t;

/**
 * @brief 地址空间标识（ASID）
 *
 * @details ARMv8-A 使用 8/16 位 ASID 标识不同的地址空间，
 *          减少在进程切换时的 TLB 冲刷开销。
 */
typedef uint16_t asid_t;

/** @brief 无效 ASID */
#define ASID_INVALID   ((asid_t)0U)

/** @brief 最大 ASID 数量（8 位 ASID） */
#define ASID_MAX       ((asid_t)256U)

/* ========================================================================
 * 页表管理 API
 * ======================================================================== */

/**
 * @brief 初始化页表子系统
 *
 * @details 初始化内核页表，设置恒等映射和内核空间映射。
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t page_table_subsys_init(void);

/**
 * @brief 创建新的页表（分配 4KB 页面）
 *
 * @return 页表指针，失败返回 NULL
 *
 * @note 页表按 4KB 对齐
 */
page_table_t *page_table_alloc(void);

/**
 * @brief 释放页表
 *
 * @param table 要释放的页表指针
 */
void page_table_free(page_table_t *table);

/**
 * @brief 映射 4KB 页面到虚拟地址
 *
 * @param pgd    顶层页表（L0）指针
 * @param vaddr  虚拟地址（4KB 对齐）
 * @param paddr  物理地址（4KB 对齐）
 * @param perm   页面权限
 * @param is_user 是否为用户态页面
 *
 * @return KERNEL_OK 成功
 * @return -ENOMEM  页表分配失败
 */
kernel_status_t page_table_map(page_table_t *pgd,
                                vaddr_t vaddr,
                                paddr_t paddr,
                                page_perm_t perm,
                                bool is_user);

/**
 * @brief 解除虚拟地址的映射
 *
 * @param pgd   顶层页表指针
 * @param vaddr 虚拟地址
 *
 * @return KERNEL_OK 成功
 * @return -ENOENT 未找到映射
 */
kernel_status_t page_table_unmap(page_table_t *pgd, vaddr_t vaddr);

/**
 * @brief 查询虚拟地址的物理映射
 *
 * @param pgd    顶层页表指针
 * @param vaddr  虚拟地址
 * @param paddr  输出参数，返回物理地址
 *
 * @return KERNEL_OK 成功
 * @return -ENOENT 未找到映射
 */
kernel_status_t page_table_lookup(page_table_t *pgd,
                                   vaddr_t vaddr,
                                   paddr_t *paddr);

/**
 * @brief 修改页面权限
 *
 * @param pgd   顶层页表指针
 * @param vaddr 虚拟地址
 * @param perm  新的页面权限
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t page_table_protect(page_table_t *pgd,
                                    vaddr_t vaddr,
                                    page_perm_t perm);

/**
 * @brief 映射大页（2MB 块映射）
 *
 * @param pgd    顶层页表指针
 * @param vaddr  虚拟地址（2MB 对齐）
 * @param paddr  物理地址（2MB 对齐）
 * @param perm   页面权限
 * @param is_user 是否为用户态页面
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t page_table_map_block(page_table_t *pgd,
                                      vaddr_t vaddr,
                                      paddr_t paddr,
                                      page_perm_t perm,
                                      bool is_user);

/**
 * @brief 刷新指定地址空间的 TLB
 *
 * @param asid 地址空间标识
 */
void tlb_flush_asid(asid_t asid);

/**
 * @brief 刷新全部 TLB */
void tlb_flush_all(void);

#endif /* KERNEL_PAGE_TABLE_H */
