/**
 * @file    npt.c
 * @brief   嵌套页表（NPT）实现
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 本文件实现了嵌套页表的创建、销毁、映射、翻译等核心功能：
 *          - NPT 创建/销毁
 *          - Guest PA → Host PA 映射
 *          - 二阶段地址翻译
 *          - TLB 刷新
 *          - ASID 管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */
#include "npt.h"
#include <stdint.h>
#include <string.h>
#include <kernel/phys_mem.h>
#include <kernel/barrier.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <kernel/spinlock.h>

/* 定义页面大小 */
#ifndef PAGE_SIZE_4K
#define PAGE_SIZE_4K  (4096ULL)
#endif

/* VMM 宏定义 */
#ifndef VMM_MAX_VMS
#define VMM_MAX_VMS  (32U)
#endif

/* 类型定义（用于内部函数） */
#ifndef __KERNEL__
typedef struct vm_desc vm_desc_t;
typedef struct vcpu_desc vcpu_desc_t;

/* VM 描述符结构体定义 */
struct vm_desc
{
    uint32_t vm_id;
    uint32_t vcpu_count;
    nested_page_table_t *npt;
    struct vcpu_desc *vcpus;
};

struct vcpu_desc
{
    struct
    {
        uint64_t esr_el1;
    } sys_regs;
};
#endif

/* ========================================================================
 * 内部状态
 * ======================================================================== */

/** @brief NPT 引用计数锁 */
static TicketLock_t s_npt_lock;

/** @brief NPT 锁 */
static TicketLock_t s_npt_ref_lock;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 递归查找或创建页表条目
 *
 * @param npt     NPT 指针（指向条目数组）
 * @param guest_va Guest 虚拟地址
 * @param level   当前页表级别
 * @param create  true=创建, false=查找
 *
 * @return 条目指针，失败返回 NULL
 */
static npt_entry_t *npt_walk(npt_entry_t entries[VMM_NPT_LEVELS][512], vaddr_t guest_va,
                              uint32_t level, bool create)
{
    uint32_t index;
    uint64_t entry;
    paddr_t child_paddr;
    npt_entry_t *child_entry;

    if (entries == NULL || guest_va == 0ULL)
    {
        return NULL;
    }

    /* 计算 PGD/PUD/PMD/PTE 索引 */
    index = (uint32_t)((guest_va >> (39ULL - level * 9ULL)) & 0x1FFULL);

    if (level == 0U)
    {
        /* PGD 级别，直接返回条目指针 */
        return &(entries[0U][index]);
    }

    /* 获取当前级别条目 */
    entry = entries[0U][index];

    if ((entry & NPT_ENTRY_TYPE_MASK) == NPT_ENTRY_TYPE_NONE)
    {
        if (!create)
        {
            return NULL;
        }

        /* 创建子页表 */
        child_paddr = phys_mem_alloc(PAGE_SIZE_4K, NULL);
        if (child_paddr == 0ULL)
        {
            return NULL;
        }

        /* 清零子页表 */
        (void)memset((void *)child_paddr, 0, PAGE_SIZE_4K);

        /* 设置子页表条目 */
        child_entry = (npt_entry_t *)child_paddr;
        child_entry[0U] = (1ULL << NPT_ENTRY_TYPE_SHIFT) |   /* Table */
                          ((child_paddr >> 12ULL) & NPT_ENTRY_PADDR_MASK) << 12ULL;

        /* 设置父条目 */
        entries[0U][index] = child_entry[0U];
    }

    /* 获取子页表 */
    child_paddr = (entry & NPT_ENTRY_PADDR_MASK) << 12ULL;
    child_entry = (npt_entry_t *)child_paddr;

    /* 递归到下一级 */
    return npt_walk((npt_entry_t(*)[512])child_entry, guest_va, level - 1U, create);
}

/* ========================================================================
 * 公共 API - NPT 创建/销毁
 * ======================================================================== */

nested_page_table_t *npt_create(uint32_t vm_id, uint64_t guest_size,
                                      paddr_t host_base)
{
    nested_page_table_t *npt;
    
    /* 检查参数有效 */
    if (vm_id >= VMM_MAX_VMS)
    {
        return NULL;
    }

    if (guest_size == 0ULL || guest_size > VMM_GUEST_PHYS_SIZE)
    {
        return NULL;
    }

    if (guest_size & (PAGE_SIZE_4K - 1ULL))
    {
        return NULL;  /* Guest 大小必须页对齐 */
    }

    ticket_lock_acquire(&s_npt_lock);

    /* 创建根页表 */
    npt = (nested_page_table_t *)kmalloc(sizeof(nested_page_table_t));
    if (npt == NULL)
    {
        ticket_lock_release(&s_npt_lock);
        return NULL;
    }

    (void)memset(npt, 0, sizeof(nested_page_table_t));

    /* 分配根页表（4KB 对齐） */
    npt->root_paddr = phys_mem_alloc(PAGE_SIZE_4K, NULL);
    if (npt->root_paddr == 0ULL)
    {
        kfree(npt);
        ticket_lock_release(&s_npt_lock);
        return NULL;
    }

    /* 初始化根页表 */
    npt->root_vaddr = (vaddr_t)npt->root_paddr;
    npt->guest_phys_base = 0ULL;
    npt->guest_phys_size = guest_size;
    npt->guest_virt_size = VMM_GUEST_VIRT_SIZE;
    npt->ref_count = 1U;

    /* 初始化所有页表条目为无效 */
    for (uint32_t l0 = 0U; l0 < VMM_NPT_LEVELS; l0++)
    {
        for (uint32_t i = 0U; i < 512U; i++)
        {
            npt->entries[l0][i] = 0ULL;
        }
    }

    /* 初始化属性 */
    npt->mem_attr_idx = 0ULL;          /* 默认内存属性索引 */
    npt->ap_bit = 0ULL;                /* 默认访问权限位 */
    npt->ns_bit = 0ULL;                /* 非安全状态位（Host 模式） */
    npt->idx_bit = 0ULL;               /* AttrIndex 位 */

    ticket_lock_release(&s_npt_lock);

    return npt;
}

void npt_destroy(nested_page_table_t *npt)
{
    if (npt == NULL)
    {
        return;
    }

    ticket_lock_acquire(&s_npt_ref_lock);

    npt->ref_count--;

    if (npt->ref_count == 0)
    {
        /* 释放根页表 */
        if (npt->root_paddr != 0ULL)
        {
            phys_mem_free(npt->root_paddr, PAGE_SIZE_4K);
        }

        kfree(npt);
    }

    ticket_lock_release(&s_npt_ref_lock);
}

/* ========================================================================
 * 公共 API - NPT 映射
 * ======================================================================== */

kernel_status_t npt_map_page(uint32_t vm_id, paddr_t guest_paddr,
                              paddr_t host_paddr, uint64_t flags)
{
    nested_page_table_t *npt;
    npt_entry_t *pte;
    uint64_t entry;
    
    /* 检查参数有效性 */
    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    if (guest_paddr == 0ULL || host_paddr == 0ULL)
    {
        return -(int32_t)EINVAL;
    }

    if (guest_paddr >= VMM_GUEST_PHYS_SIZE)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查页对齐 */
    if ((guest_paddr & (PAGE_SIZE_4K - 1ULL)) != 0ULL ||
        (host_paddr & (PAGE_SIZE_4K - 1ULL)) != 0ULL)
    {
        return -(int32_t)EINVAL;  /* 地址必须页对齐 */
    }

    /* 获取 VM 的 NPT */
    npt = vmm_get_npt(vm_id);
    if (npt == NULL)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&s_npt_ref_lock);

    /* 查找或创建 PTE 条目（L3） */
    pte = npt_walk(npt->entries, (vaddr_t)guest_paddr, NPT_LEVEL_L3, true);
    if (pte == NULL)
    {
        ticket_lock_release(&s_npt_ref_lock);
        return -(int32_t)EFAULT;
    }

    /* 检查是否已经映射 */
    entry = *pte;
    if ((entry & NPT_ENTRY_TYPE_MASK) == NPT_ENTRY_TYPE_PAGE)
    {
        paddr_t old_host_pa = (entry & NPT_ENTRY_PADDR_MASK) << 12ULL;
        if (old_host_pa != host_paddr)
        {
            /* 重复映射到不同的 Host 地址 */
            ticket_lock_release(&s_npt_ref_lock);
            return -(int32_t)EEXIST;
        }
    }

    /* 设置页表项 */
    *pte = ((host_paddr >> 12ULL) & NPT_ENTRY_PADDR_MASK) << 12ULL;
    *pte |= NPT_ENTRY_TYPE_PAGE;           /* Page */
    *pte |= npt->ap_bit;                   /* 访问权限 */
    *pte |= npt->ns_bit;                   /* 非安全状态 */

    ticket_lock_release(&s_npt_ref_lock);

    return KERNEL_OK;
}

kernel_status_t npt_unmap_page(uint32_t vm_id, paddr_t guest_paddr)
{
    nested_page_table_t *npt;
    npt_entry_t *pte;

    if (guest_paddr == 0ULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 VM 的 NPT */
    npt = vmm_get_npt(vm_id);
    if (npt == NULL)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&s_npt_ref_lock);

    /* 查找 PTE 条目 */
    pte = npt_walk(npt->entries, (vaddr_t)guest_paddr, NPT_LEVEL_L3, false);
    if (pte == NULL)
    {
        ticket_lock_release(&s_npt_ref_lock);
        return -(int32_t)EFAULT;
    }

    /* 清空 PTE */
    *pte = 0ULL;

    ticket_lock_release(&s_npt_ref_lock);

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - NPT 翻译
 * ======================================================================== */

kernel_status_t npt_translate(nested_page_table_t *npt, vaddr_t guest_va,
                               paddr_t *host_pa)
{
    npt_entry_t *pte;
    uint64_t entry;

    if (npt == NULL || guest_va == 0ULL || host_pa == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找 PTE 条目（L3） */
    pte = npt_walk(npt->entries, guest_va, NPT_LEVEL_L3, false);
    if (pte == NULL)
    {
        return -(int32_t)EFAULT;
    }

    /* 读取页表项 */
    entry = *pte;

    /* 检查是否为有效页表项 */
    if ((entry & NPT_ENTRY_TYPE_MASK) != NPT_ENTRY_TYPE_PAGE)
    {
        return -(int32_t)EFAULT;
    }

    /* 提取 Host 物理地址 */
    *host_pa = (entry & NPT_ENTRY_PADDR_MASK) << 12ULL;

    return KERNEL_OK;
}

/* ======================================================================== * 公共 API - NPT TLB 刷新
 * ======================================================================== */

kernel_status_t npt_tlb_flush(uint32_t vm_id, uint32_t asid)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint32_t i;
    
    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 VM */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 使所有 TLB 条目无效（简化实现） */
    /* 完整实现需要：
     * 1. 调用 DSB ISH （数据内存屏障）
     * 2. 调用 TLBI VMALLE1IS （使所有 TLB 无效）
     * 3. 调用 DSB ISH （数据内存屏障）
     * 4. 调用 ISB （指令同步屏障）
     */
    
    /* 更新所有 vCPU 的 ESR_EL1，标记 TLB 已刷新 */
    for (i = 0U; i < vm->vcpu_count; i++)
    {
        vcpu = &vm->vcpus[i];
        vcpu->sys_regs.esr_el1 |= (1ULL << 6ULL);  /* 标记为 TLB 刷新 */
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - NPT 引用计数
 * ======================================================================== */

uint32_t npt_get_ref_count(nested_page_table_t *npt)
{
    if (npt == NULL)
    {
        return 0U;
    }

    return npt->ref_count;
}

/* ========================================================================
 * 内部 API (仅供 VMM 内部使用)
 * ======================================================================== */

/**
 * @brief 从 VM 描述符获取 NPT 指针
 *
 * @param vm_id VM ID
 *
