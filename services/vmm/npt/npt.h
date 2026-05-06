/**
 * @file    npt.h
 * @brief   嵌套页表（NPT）接口
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 本文件定义了嵌套页表（NPT）相关数据结构和接口：
 *          - NPT 级别枚举
 *          - NPT 条目类型
 *          - 嵌套页表描述符
 *          - 公共 API 接口
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_VMM_NPT_NPT_H
#define SERVICES_VMM_NPT_NPT_H

#include <kernel/types.h>
#include <stdint.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 嵌套页表级别数（ARMv8 4级） */
#define VMM_NPT_LEVELS             4U

/** @brief Guest 物理地址空间大小（1GB） */
#define VMM_GUEST_PHYS_SIZE        0x40000000ULL

/** @brief Guest 虚拟地址空间大小（48位） */
#define VMM_GUEST_VIRT_SIZE        (1ULL << 48ULL)

/* ========================================================================
 * NPT 级别枚举
 * ======================================================================== */

/**
 * @brief 嵌套页表级别
 */
typedef enum
{
    NPT_LEVEL_L0 = 0U,                  /**< @brief PGD 级别（页全局目录） */
    NPT_LEVEL_L1 = 1U,                  /**< @brief PUD 级别（页上级目录） */
    NPT_LEVEL_L2 = 2U,                  /**< @brief PMD 级别（页中间目录） */
    NPT_LEVEL_L3 = 3U,                  /**< @brief PTE 级别（页表项） */
    NPT_LEVEL_COUNT
} npt_level_t;

/* ========================================================================
 * NPT 条目类型
 * ======================================================================== */

/**
 * @brief NPT 条目类型掩码
 *
 * @details ARMv8-A 页表条目格式：
 *          [63:59] 类型/权限 (Table/Block/Page)
 *          [58:48] 粗粒度块大小/AttrIndex
 *          [47:12] 物理地址 (物理内存对齐)
 *          [11:0]  偏移
 */
#define NPT_ENTRY_TYPE_SHIFT       (59ULL)
#define NPT_ENTRY_TYPE_MASK        (0x1FULL)
#define NPT_ENTRY_TYPE_NONE        (0x0ULL)          /**< @brief 无 */
#define NPT_ENTRY_TYPE_TABLE       (0x1ULL)          /**< @brief 下级页表 */
#define NPT_ENTRY_TYPE_BLOCK       (0x3ULL)          /**< @brief 2MB 页块 */
#define NPT_ENTRY_TYPE_PAGE        (0x5ULL)          /**< @brief 4KB 页 */

#define NPT_ENTRY_PADDR_SHIFT      (12ULL)
#define NPT_ENTRY_PADDR_MASK       ((1ULL << 52ULL) - 1ULL)

/* ========================================================================
 * 嵌套页表描述符
 * ======================================================================== */

/**
 * @brief 嵌套页表条目
 *
 * @details 64 位页表条目
 */
typedef uint64_t npt_entry_t;

/**
 * @brief 嵌套页表描述符
 *
 * @details 二阶段地址翻译：Guest VA → Guest PA → Host PA
 *
 * @details ARMv8-A 4 级页表：
 *          [47:39] PGD 索引 (L0, 9 位, 512 项)
 *          [38:30] PUD 索引 (L1, 9 位, 512 项)
 *          [29:21] PMD 索引 (L2, 9 位, 512 项)
 *          [20:12] PTE 索引 (L3, 9 位, 512 项)
 *          [11:0]  页内偏移 (12 位, 4KB)
 */
typedef struct
{
    /** @brief NPT 根页表物理地址 */
    paddr_t    root_paddr;

    /** @brief NPT 根页表虚拟地址 */
    vaddr_t    root_vaddr;

    /** @brief 4 级页表数组 */
    npt_entry_t entries[VMM_NPT_LEVELS][512];

    /** @brief Guest 地址空间 */
    uint64_t   guest_phys_base;         /**< @brief Guest 物理地址基址 */
    uint64_t   guest_phys_size;         /**< @brief Guest 物理地址空间大小 */
    uint64_t   guest_virt_size;         /**< @brief Guest 虚拟地址空间大小 */

    /** @brief 映射信息 */
    uint32_t   ref_count;               /**< @brief 引用计数 */

    /** @brief 属性 */
    uint64_t   mem_attr_idx;            /**< @brief 内存属性索引 (MAIR) */
    uint64_t   ap_bit;                  /**< @brief 访问权限位 */
    uint64_t   ns_bit;                  /**< @brief 非安全状态位 */
    uint64_t   idx_bit;                 /**< @brief AttrIndex 位 */
} nested_page_table_t;

/* ========================================================================
 * 公共 API 接口
 * ======================================================================== */

/**
 * @brief 创建嵌套页表
 *
 * @param vm_id       VM ID
 * @param guest_size  Guest 物理内存大小
 * @param host_base   Host 物理内存基地址
 *
 * @return 成功返回 NPT 指针，失败返回 NULL
 */
nested_page_table_t *npt_create(uint32_t vm_id, uint64_t guest_size,
                                 paddr_t host_base);

/**
 * @brief 销毁嵌套页表
 *
 * @param npt NPT 指针
 */
void npt_destroy(nested_page_table_t *npt);

/**
 * @brief 映射 Guest 物理页到 Host
 *
 * @param vm_id       VM ID
 * @param guest_paddr Guest 物理地址
 * @param host_paddr  Host 物理地址
 * @param flags       页表属性标志
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t npt_map_page(uint32_t vm_id, paddr_t guest_paddr,
                             paddr_t host_paddr, uint64_t flags);

/**
 * @brief 解除映射 Guest 物理页
 *
 * @param vm_id       VM ID
 * @param guest_paddr Guest 物理地址
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t npt_unmap_page(uint32_t vm_id, paddr_t guest_paddr);

/**
 * @brief 二阶段地址翻译
 *
 * @param npt         NPT 指针
 * @param guest_va    Guest 虚拟地址
 * @param host_pa     输出: Host 物理地址
 *
 * @return KERNEL_OK 成功
 * @return -EFAULT 访问不合法
 */
kernel_status_t npt_translate(nested_page_table_t *npt,
                               vaddr_t guest_va,
                               paddr_t *host_pa);

/**
 * @brief 刷新 NPT TLB
 *
 * @param vm_id   VM ID
 * @param asid    ASID (可选，0 表示刷新所有)
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t npt_tlb_flush(uint32_t vm_id, uint32_t asid);

/**
 * @brief 获取 NPT 引用计数
 *
 * @param npt NPT 指针
 *
 * @return 引用计数
 */
uint32_t npt_get_ref_count(nested_page_table_t *npt);

#endif /* SERVICES_VMM_NPT_NPT_H */
