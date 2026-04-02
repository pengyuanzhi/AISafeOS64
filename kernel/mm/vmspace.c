/**
 * @file    vmspace.c
 * @brief   虚拟地址空间管理实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 实现所有 vmspace.h 中声明的函数：
 *          - vmspace_subsys_init:       地址空间子系统初始化
 *          - vmspace_create:            创建新地址空间
 *          - vmspace_destroy:           销毁地址空间
 *          - vmspace_switch:            切换地址空间
 *          - vmspace_map:               映射内存区域
 *          - vmspace_unmap:             解除映射
 *          - vmspace_protect:           修改页面权限
 *          - vmspace_find_vma:          查找 VMA
 *          - vmspace_get_current:       获取当前地址空间
 *          - vmspace_get_kernel:        获取内核地址空间
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-009~012, MM-001~007
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/vmspace.h>
#include <kernel/page_table.h>
#include <kernel/phys_mem.h>
#include <kernel/barrier.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <kernel/compiler.h>
#include <kernel/spinlock.h>
#include <hal.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * ASID 管理
 * ======================================================================== */

/** @brief ASID 位图（每位代表一个 ASID 是否已分配） */
static uint8_t s_asid_bitmap[ASID_MAX / 8U];

/** @brief ASID 分配锁 */
static TicketLock_t s_asid_lock;

/** @brief ASID 0 保留（无效），从 1 开始分配 */
#define ASID_FIRST_VALID    ((asid_t)1U)

/**
 * @brief 分配一个空闲 ASID
 *
 * @return 空闲 ASID，无空闲返回 ASID_INVALID
 */
static asid_t asid_alloc(void)
{
    uint32_t i;
    asid_t result = ASID_INVALID;

    ticket_lock_acquire(&s_asid_lock);

    /* 从 ASID 1 开始搜索（0 保留为无效） */
    for (i = (uint32_t)ASID_FIRST_VALID; i < (uint32_t)ASID_MAX; i++)
    {
        uint32_t byte_idx = i / 8U;
        uint32_t bit_idx = i % 8U;

        if ((s_asid_bitmap[byte_idx] & (1U << bit_idx)) == 0U)
        {
            s_asid_bitmap[byte_idx] |= (1U << bit_idx);
            result = (asid_t)i;
            break;
        }
    }

    ticket_lock_release(&s_asid_lock);

    return result;
}

/**
 * @brief 释放 ASID
 *
 * @param asid 要释放的 ASID
 */
static void asid_free(asid_t asid)
{
    uint32_t byte_idx;
    uint32_t bit_idx;

    if ((asid == ASID_INVALID) || (asid >= ASID_MAX))
    {
        return;
    }

    byte_idx = (uint32_t)asid / 8U;
    bit_idx = (uint32_t)asid % 8U;

    ticket_lock_acquire(&s_asid_lock);
    s_asid_bitmap[byte_idx] &= ~(1U << bit_idx);
    ticket_lock_release(&s_asid_lock);
}

/* ========================================================================
 * VMA 池管理
 * ======================================================================== */

/** @brief VMA 静态池 */
static vma_t s_vma_pool[CONFIG_MAX_VMAS];

/** @brief VMA 空闲索引栈 */
static uint32_t s_vma_free_stack[CONFIG_MAX_VMAS];

/** @brief VMA 空闲计数 */
static uint32_t s_vma_free_count;

/** @brief VMA 池锁 */
static TicketLock_t s_vma_pool_lock;

/**
 * @brief 分配 VMA 描述符
 *
 * @return VMA 指针，无空闲返回 NULL
 */
static vma_t *vma_alloc(void)
{
    vma_t *vma = NULL;

    ticket_lock_acquire(&s_vma_pool_lock);

    if (s_vma_free_count > 0U)
    {
        s_vma_free_count--;
        vma = &s_vma_pool[s_vma_free_stack[s_vma_free_count]];
    }

    ticket_lock_release(&s_vma_pool_lock);

    return vma;
}

/**
 * @brief 释放 VMA 描述符
 *
 * @param vma 要释放的 VMA
 */
static void vma_free(vma_t *vma)
{
    uint32_t idx;

    if (vma == NULL)
    {
        return;
    }

    idx = (uint32_t)(vma - s_vma_pool);

    ticket_lock_acquire(&s_vma_pool_lock);
    s_vma_free_stack[s_vma_free_count] = idx;
    s_vma_free_count++;
    ticket_lock_release(&s_vma_pool_lock);
}

/* ========================================================================
 * 全局状态
 * ======================================================================== */

/** @brief 内核地址空间（静态分配） */
static vm_space_t s_kernel_space;

/** @brief 每 CPU 当前地址空间指针 */
static vm_space_t *s_current_space[CONFIG_MAX_CPUS];

/* ========================================================================
 * 地址空间子系统初始化
 * ======================================================================== */

kernel_status_t vmspace_subsys_init(void)
{
    uint32_t i;

    /* 初始化 ASID 位图（全部清零，ASID 0 保留） */
    (void)memset(s_asid_bitmap, 0, sizeof(s_asid_bitmap));
    ticket_lock_init(&s_asid_lock);

    /* 初始化 VMA 池 */
    for (i = 0U; i < CONFIG_MAX_VMAS; i++)
    {
        s_vma_free_stack[i] = (CONFIG_MAX_VMAS - 1U) - i;
    }
    s_vma_free_count = CONFIG_MAX_VMAS;
    ticket_lock_init(&s_vma_pool_lock);

    /* 初始化内核地址空间 */
    s_kernel_space.pgd = NULL; /* 由 page_table_subsys_init 设置 */
    s_kernel_space.asid = ASID_INVALID;
    s_kernel_space.vma_count = 0U;
    s_kernel_space.vma_list.next = &s_kernel_space.vma_list;
    s_kernel_space.vma_list.prev = &s_kernel_space.vma_list;
    s_kernel_space.brk_base = 0U;
    s_kernel_space.brk_current = 0U;
    s_kernel_space.stack_top = 0U;
    s_kernel_space.ref_count = 1U;
    ticket_lock_init(&s_kernel_space.lock);

    /* 初始化每 CPU 当前空间指针 */
    for (i = 0U; i < CONFIG_MAX_CPUS; i++)
    {
        s_current_space[i] = &s_kernel_space;
    }

    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 创建地址空间
 * ======================================================================== */

kernel_status_t vmspace_create(vm_space_t **space)
{
    vm_space_t *new_space;
    asid_t new_asid;

    if (space == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 分配 ASID */
    new_asid = asid_alloc();
    if (new_asid == ASID_INVALID)
    {
        return -(int32_t)ENOMEM;
    }

    /* 分配 vm_space_t 结构 */
    /* 简化实现：使用静态分配的空间池 */
    /* 在完整实现中，从对象池分配 */
    new_space = (vm_space_t *)page_table_alloc();
    if (new_space == NULL)
    {
        asid_free(new_asid);
        return -(int32_t)ENOMEM;
    }

    /* 分配 PGD */
    new_space->pgd = page_table_alloc();
    if (new_space->pgd == NULL)
    {
        page_table_free((page_table_t *)new_space);
        asid_free(new_asid);
        return -(int32_t)ENOMEM;
    }

    /* 初始化地址空间 */
    new_space->asid = new_asid;
    new_space->vma_count = 0U;
    new_space->vma_list.next = &new_space->vma_list;
    new_space->vma_list.prev = &new_space->vma_list;
    new_space->brk_base = USER_SPACE_BASE;
    new_space->brk_current = USER_SPACE_BASE;
    new_space->stack_top = 0U;
    new_space->ref_count = 1U;
    ticket_lock_init(&new_space->lock);

    /* 复制内核空间映射（上半部分 PGD 条目） */
    if (s_kernel_space.pgd != NULL)
    {
        uint32_t i;
        for (i = 256U; i < PTRS_PER_TABLE; i++)
        {
            new_space->pgd->entries[i] = s_kernel_space.pgd->entries[i];
        }
    }

    barrier();

    *space = new_space;

    return KERNEL_OK;
}

/* ========================================================================
 * 销毁地址空间
 * ======================================================================== */

void vmspace_destroy(vm_space_t *space)
{
    struct list_head *pos;
    struct list_head *next;

    if (space == NULL)
    {
        return;
    }

    if (space == &s_kernel_space)
    {
        return;
    }

    ticket_lock_acquire(&space->lock);

    /* 遍历并释放所有 VMA */
    pos = space->vma_list.next;
    while (pos != &space->vma_list)
    {
        vma_t *vma = container_of(pos, vma_t, node);
        next = pos->next;

        /* 解除映射并释放物理页 */
        if (vma->start != 0U)
        {
            vaddr_t addr;
            for (addr = vma->start; addr < vma->end; addr += PAGE_SIZE_4K)
            {
                paddr_t paddr;
                if (page_table_lookup(space->pgd, addr, &paddr) == KERNEL_OK)
                {
                    page_table_unmap(space->pgd, addr);
                    phys_mem_free_page(paddr);
                }
            }
        }

        /* 从链表移除并释放 VMA */
        pos->prev->next = pos->next;
        pos->next->prev = pos->prev;
        vma_free(vma);

        pos = next;
    }

    space->vma_count = 0U;

    /* 释放 PGD 和所有子页表 */
    if (space->pgd != NULL)
    {
        /* 释放低半部分用户空间页表 */
        uint32_t i;
        for (i = 0U; i < 256U; i++)
        {
            uint64_t entry = space->pgd->entries[i];
            if (PTE_IS_VALID(entry) && PTE_IS_TABLE(entry))
            {
                paddr_t pte_addr = PTE_PADDR(entry);
                page_table_free((page_table_t *)((uintptr_t)pte_addr));
            }
        }
        page_table_free(space->pgd);
    }

    ticket_lock_release(&space->lock);

    /* 释放 ASID */
    asid_free(space->asid);

    /* 释放空间结构 */
    page_table_free((page_table_t *)space);
}

/* ========================================================================
 * 切换地址空间
 * ======================================================================== */

void vmspace_switch(vm_space_t *space)
{
    uint32_t cpu_id;
    paddr_t pgd_phys;

    if (space == NULL)
    {
        return;
    }

    cpu_id = hal_get_cpu_id();
    s_current_space[cpu_id] = space;

    if (space->pgd != NULL)
    {
        pgd_phys = (paddr_t)((uintptr_t)space->pgd);

        /* 设置 TTBR0_EL1 */
        __asm__ volatile(
            "msr ttbr0_el1, %0\n"
            "isb\n"
            :: "r"((uint64_t)pgd_phys | (uint64_t)space->asid)
            : "memory"
        );
    }

    barrier();
}

/* ========================================================================
 * 映射内存区域
 * ======================================================================== */

vaddr_t vmspace_map(vm_space_t *space,
                     vaddr_t vaddr,
                     uint64_t size,
                     uint32_t flags,
                     vma_type_t type,
                     paddr_t paddr)
{
    vma_t *vma;
    vaddr_t map_start;
    vaddr_t map_end;
    uint64_t map_size;
    uint64_t offset;
    bool is_user;

    if (space == NULL)
    {
        return 0U;
    }

    /* 页对齐 */
    map_size = (size + PAGE_SIZE_4K - 1ULL) & ~(PAGE_SIZE_4K - 1ULL);
    if (map_size == 0ULL)
    {
        return 0U;
    }

    /* 确定映射起始地址 */
    if (vaddr == 0U)
    {
        /* 自动选择空闲区域（简化实现：从 brk_current 开始） */
        ticket_lock_acquire(&space->lock);
        map_start = (space->brk_current + PAGE_SIZE_4K - 1ULL) &
                    ~(PAGE_SIZE_4K - 1ULL);
        ticket_lock_release(&space->lock);
    }
    else
    {
        map_start = vaddr & ~(PAGE_SIZE_4K - 1ULL);
    }

    map_end = map_start + map_size;

    /* 检查是否在用户空间范围内 */
    is_user = (map_start < USER_SPACE_END) ? true : false;

    /* 转换权限标志 */
    page_perm_t perm = PAGE_PERM_NONE;
    if ((flags & VMA_FLAG_READ) != 0U)
    {
        perm = (page_perm_t)((uint32_t)perm | (uint32_t)PAGE_PERM_READ);
    }
    if ((flags & VMA_FLAG_WRITE) != 0U)
    {
        perm = (page_perm_t)((uint32_t)perm | (uint32_t)PAGE_PERM_WRITE);
    }
    if ((flags & VMA_FLAG_EXEC) != 0U)
    {
        perm = (page_perm_t)((uint32_t)perm | (uint32_t)PAGE_PERM_EXEC);
    }

    /* 逐页映射 */
    for (offset = 0ULL; offset < map_size; offset += PAGE_SIZE_4K)
    {
        paddr_t page_paddr;
        kernel_status_t ret;

        if (paddr != 0U)
        {
            page_paddr = paddr + offset;
        }
        else
        {
            page_paddr = phys_mem_alloc_page();
            if (page_paddr == 0U)
            {
                /* 分配失败：回滚已映射的页面 */
                uint64_t rollback;
                for (rollback = 0ULL; rollback < offset; rollback += PAGE_SIZE_4K)
                {
                    paddr_t mapped_paddr;
                    if (page_table_lookup(space->pgd, map_start + rollback,
                                          &mapped_paddr) == KERNEL_OK)
                    {
                        page_table_unmap(space->pgd, map_start + rollback);
                        phys_mem_free_page(mapped_paddr);
                    }
                }
                return 0U;
            }
        }

        ret = page_table_map(space->pgd, map_start + offset,
                              page_paddr, perm, is_user);
        if (ret != KERNEL_OK)
        {
            if (paddr == 0U)
            {
                phys_mem_free_page(page_paddr);
            }
            return 0U;
        }
    }

    /* 创建 VMA 描述符 */
    vma = vma_alloc();
    if (vma == NULL)
    {
        /* VMA 分配失败：回滚映射 */
        for (offset = 0ULL; offset < map_size; offset += PAGE_SIZE_4K)
        {
            paddr_t mapped_paddr;
            if (page_table_lookup(space->pgd, map_start + offset,
                                  &mapped_paddr) == KERNEL_OK)
            {
                page_table_unmap(space->pgd, map_start + offset);
                if (paddr == 0U)
                {
                    phys_mem_free_page(mapped_paddr);
                }
            }
        }
        return 0U;
    }

    /* 填充 VMA */
    vma->start = map_start;
    vma->end = map_end;
    vma->flags = flags;
    vma->type = type;
    vma->phys_base = paddr;
    vma->shmem_id = KOBJ_ID_INVALID;
    vma->node.next = &vma->node;
    vma->node.prev = &vma->node;

    /* 插入 VMA 链表（按地址排序） */
    ticket_lock_acquire(&space->lock);
    {
        struct list_head *insert_pos = space->vma_list.next;
        while (insert_pos != &space->vma_list)
        {
            vma_t *existing = container_of(insert_pos, vma_t, node);
            if (vma->start < existing->start)
            {
                break;
            }
            insert_pos = insert_pos->next;
        }
        vma->node.next = insert_pos;
        vma->node.prev = insert_pos->prev;
        insert_pos->prev->next = &vma->node;
        insert_pos->prev = &vma->node;

        space->vma_count++;
    }
    ticket_lock_release(&space->lock);

    barrier();

    return map_start;
}

/* ========================================================================
 * 解除映射
 * ======================================================================== */

kernel_status_t vmspace_unmap(vm_space_t *space,
                               vaddr_t vaddr,
                               uint64_t size)
{
    vma_t *vma;
    uint64_t unmap_size;
    uint64_t offset;

    if (space == NULL)
    {
        return -(int32_t)EINVAL;
    }

    unmap_size = (size + PAGE_SIZE_4K - 1ULL) & ~(PAGE_SIZE_4K - 1ULL);

    ticket_lock_acquire(&space->lock);

    vma = vmspace_find_vma(space, vaddr);
    if (vma == NULL)
    {
        ticket_lock_release(&space->lock);
        return -(int32_t)ENOENT;
    }

    /* 逐页解除映射 */
    for (offset = 0ULL; offset < unmap_size; offset += PAGE_SIZE_4K)
    {
        paddr_t paddr;
        if (page_table_lookup(space->pgd, vaddr + offset, &paddr) == KERNEL_OK)
        {
            page_table_unmap(space->pgd, vaddr + offset);
            phys_mem_free_page(paddr);
        }
    }

    /* 从 VMA 链表移除 */
    vma->node.prev->next = vma->node.next;
    vma->node.next->prev = vma->node.prev;
    space->vma_count--;

    ticket_lock_release(&space->lock);

    vma_free(vma);

    return KERNEL_OK;
}

/* ========================================================================
 * 修改页面权限
 * ======================================================================== */

kernel_status_t vmspace_protect(vm_space_t *space,
                                 vaddr_t vaddr,
                                 uint64_t size,
                                 uint32_t flags)
{
    page_perm_t perm;
    uint64_t protect_size;
    uint64_t offset;

    if (space == NULL)
    {
        return -(int32_t)EINVAL;
    }

    protect_size = (size + PAGE_SIZE_4K - 1ULL) & ~(PAGE_SIZE_4K - 1ULL);

    /* 转换权限 */
    perm = PAGE_PERM_NONE;
    if ((flags & VMA_FLAG_READ) != 0U)
    {
        perm = (page_perm_t)((uint32_t)perm | (uint32_t)PAGE_PERM_READ);
    }
    if ((flags & VMA_FLAG_WRITE) != 0U)
    {
        perm = (page_perm_t)((uint32_t)perm | (uint32_t)PAGE_PERM_WRITE);
    }
    if ((flags & VMA_FLAG_EXEC) != 0U)
    {
        perm = (page_perm_t)((uint32_t)perm | (uint32_t)PAGE_PERM_EXEC);
    }

    for (offset = 0ULL; offset < protect_size; offset += PAGE_SIZE_4K)
    {
        page_table_protect(space->pgd, vaddr + offset, perm);
    }

    /* 更新 VMA 标志 */
    ticket_lock_acquire(&space->lock);
    {
        vma_t *vma = vmspace_find_vma(space, vaddr);
        if (vma != NULL)
        {
            vma->flags = flags;
        }
    }
    ticket_lock_release(&space->lock);

    return KERNEL_OK;
}

/* ========================================================================
 * 查找 VMA
 * ======================================================================== */

vma_t *vmspace_find_vma(vm_space_t *space, vaddr_t vaddr)
{
    struct list_head *pos;

    if (space == NULL)
    {
        return NULL;
    }

    for (pos = space->vma_list.next; pos != &space->vma_list; pos = pos->next)
    {
        vma_t *vma = container_of(pos, vma_t, node);
        if ((vaddr >= vma->start) && (vaddr < vma->end))
        {
            return vma;
        }
    }

    return NULL;
}

/* ========================================================================
 * 获取当前/内核地址空间
 * ======================================================================== */

vm_space_t *vmspace_get_current(void)
{
    uint32_t cpu_id = hal_get_cpu_id();

    if (cpu_id < CONFIG_MAX_CPUS)
    {
        return s_current_space[cpu_id];
    }

    return &s_kernel_space;
}

vm_space_t *vmspace_get_kernel(void)
{
    return &s_kernel_space;
}
