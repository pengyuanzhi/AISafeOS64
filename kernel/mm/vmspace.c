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
 *
 * @revision history
 * v1.0 2026-07-05 初始版本
 */

#include <kernel/vmspace.h>
#include <kernel/page_table.h>
#include <kernel/phys_mem.h>
#include <kernel/virt_phys.h>
#include <kernel/barrier.h>
#include <arch/arm64/hal.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <kernel/compiler.h>
#include <kernel/spinlock.h>
#include <kernel/rbtree.h>
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

/* ========================================================================
 * VMA 比较函数声明（用于红黑树）
 * ======================================================================== */

/**
 * @brief VMA 比较函数（用于红黑树插入）
 */
static bool vma_less(const struct rb_node *node_a, const struct rb_node *node_b);

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
 * vm_space_t 静态对象池（P0-5）
 *
 * 原实现 vmspace_create 用 page_table_alloc()（4KB 页表内存）当
 * vm_space_t 容器，类型不匹配且浪费/破坏页表分配器。现改用静态池：
 *   - s_space_pool:       预分配的 vm_space_t 数组
 *   - s_space_free_stack: 空闲槽位索引栈
 *   - s_space_free_count: 栈中空闲数量
 *   - s_space_pool_lock:  保护并发分配/归还
 * ======================================================================== */

/** @brief vm_space_t 静态池 */
static vm_space_t s_space_pool[CONFIG_MAX_VM_SPACES];

/** @brief 空闲槽位索引栈（栈顶在数组末尾，与 VMA 池约定一致） */
static uint32_t s_space_free_stack[CONFIG_MAX_VM_SPACES];

/** @brief 空闲槽位数量 */
static uint32_t s_space_free_count;

/** @brief vm_space_t 池锁 */
static TicketLock_t s_space_pool_lock;

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

    /* P0-5：初始化 vm_space_t 静态池（索引栈，栈顶在数组末尾） */
    (void)memset(s_space_pool, 0, sizeof(s_space_pool));
    for (i = 0U; i < CONFIG_MAX_VM_SPACES; i++)
    {
        s_space_free_stack[i] = (CONFIG_MAX_VM_SPACES - 1U) - i;
    }
    s_space_free_count = CONFIG_MAX_VM_SPACES;
    ticket_lock_init(&s_space_pool_lock);

    /* 初始化内核地址空间 */
    s_kernel_space.pgd = NULL; /* 由 page_table_subsys_init 设置 */
    s_kernel_space.asid = ASID_INVALID;
    s_kernel_space.vma_count = 0U;
    s_kernel_space.vma_rb_root.node = NULL;
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

    /*
     * P0-5 修复：
     *   原实现用 page_table_alloc()（4KB 页表内存）当 vm_space_t 容器，
     *   类型不匹配且破坏页表分配器。现从静态对象池分配 vm_space_t。
     */
    new_space = NULL;
    ticket_lock_acquire(&s_space_pool_lock);
    if (s_space_free_count > 0U)
    {
        uint32_t slot = s_space_free_stack[s_space_free_count - 1U];
        s_space_free_count--;
        new_space = &s_space_pool[slot];
    }
    ticket_lock_release(&s_space_pool_lock);

    if (new_space == NULL)
    {
        asid_free(new_asid);
        return -(int32_t)ENOMEM;
    }

    /* 分配 PGD */
    new_space->pgd = page_table_alloc();
    if (new_space->pgd == NULL)
    {
        /* P0-5：归还 vm_space_t 到静态池 */
        ticket_lock_acquire(&s_space_pool_lock);
        if (s_space_free_count < CONFIG_MAX_VM_SPACES)
        {
            s_space_free_stack[s_space_free_count] =
                (uint32_t)(new_space - &s_space_pool[0]);
            s_space_free_count++;
        }
        ticket_lock_release(&s_space_pool_lock);
        asid_free(new_asid);
        return -(int32_t)ENOMEM;
    }

    /* 初始化地址空间 */
    new_space->asid = new_asid;
    new_space->vma_count = 0U;
    new_space->vma_rb_root.node = NULL;
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

    /* 遍历并释放所有 VMA（使用红黑树遍历） */
    {
        struct rb_node *rb_node = rb_first(&space->vma_rb_root);
        struct rb_node *next_rb_node;

        while (rb_node != NULL)
        {
            vma_t *vma = rb_entry(rb_node, vma_t, rb_node);
            next_rb_node = rb_next(rb_node);

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

            /* 从红黑树移除并释放 VMA */
            rb_delete(&space->vma_rb_root, rb_node);
            vma_free(vma);

            rb_node = next_rb_node;
        }
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
                /*
                 * P0-6 修复：
                 *   pte_addr 是从 PTE 中解析出的物理地址（PA），
                 *   原代码 page_table_free((page_table_t *)(uintptr_t)pte_addr)
                 *   把 PA 当虚拟指针用，访问非法地址。
                 *   page_table_free 期望的是内核虚拟地址，需先 phys_to_virt。
                 */
                paddr_t pte_addr = PTE_PADDR(entry);
                page_table_free((page_table_t *)phys_to_virt(pte_addr));
            }
        }
        page_table_free(space->pgd);
    }

    ticket_lock_release(&space->lock);

    /* 释放 ASID */
    asid_free(space->asid);

    /*
     * P0-5 修复：
     *   原实现用 page_table_free((page_table_t *)space) 释放 vm_space_t，
     *   把 vm_space_t 当页表归还，破坏页表分配器。
     *   现归还到静态 vm_space_t 对象池。仅对池内对象归还，
     *   池外对象（理论上不存在）忽略以防越界。
     */
    if ((space >= &s_space_pool[0]) &&
        (space < &s_space_pool[CONFIG_MAX_VM_SPACES]))
    {
        ticket_lock_acquire(&s_space_pool_lock);
        if (s_space_free_count < CONFIG_MAX_VM_SPACES)
        {
            s_space_free_stack[s_space_free_count] =
                (uint32_t)(space - &s_space_pool[0]);
            s_space_free_count++;
        }
        ticket_lock_release(&s_space_pool_lock);
    }
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
        /*
         * P0-4 修复：
         *   原代码 pgd_phys = (paddr_t)((uintptr_t)space->pgd) 直接把
         *   高地址虚拟指针（TTBR1 区，0xFFFF...）当物理地址写 TTBR0，
         *   导致取页表时访问非法物理地址，必然 fault。
         *   现通过 virt_to_phys 取得 PGD 的真实物理地址。
         *
         *   关于 ASID：ARMv8 中 ASID 应放在 TTBR0 的高位（bit48+），
         *   原代码 | asid 把 ASID 放进低位会污染物理地址位，错误。
         *   当前阶段暂不 OR ASID（置 0），由 ASID 机制另作处理，
         *   优先保证写入的 PA 正确。
         */
        pgd_phys = virt_to_phys(space->pgd);

        /* 设置 TTBR0_EL1，写入真实物理地址（ASID 暂设为 0） */
        hal_write_ttbr0((uint64_t)pgd_phys);
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

    /* 插入 VMA 到红黑树（按地址排序） */
    ticket_lock_acquire(&space->lock);
    {
        rb_insert(&space->vma_rb_root, &vma->rb_node, vma_less);
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

    /* 从红黑树移除 VMA */
    rb_delete(&space->vma_rb_root, &vma->rb_node);
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
 * VMA 比较函数（用于红黑树）
 * ======================================================================== */

/**
 * @brief VMA 比较函数（用于红黑树查找）
 *
 * @details 按 start 地址比较 VMA。
 *
 * @param key     查找的虚拟地址
 * @param node    红黑树节点（VMA 节点）
 *
 * @return <0 如果 key < node->start，0 如果 key 在 [start, end) 内，>0 如果 key >= node->end
 */
static int32_t vma_compare(const void *key, const struct rb_node *node)
{
    const vaddr_t vaddr = *(const vaddr_t *)key;
    const vma_t *vma = rb_entry(node, vma_t, rb_node);

    if (vaddr < vma->start)
    {
        return -(int32_t)EINVAL;
    }
    else if (vaddr >= vma->end)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

/**
 * @brief VMA 比较函数（用于红黑树插入）
 *
 * @details 按 start 地址比较 VMA，用于插入时的位置查找。
 *
 * @param node_a  第一个 VMA 节点
 * @param node_b  第二个 VMA 节点
 *
 * @return true 如果 node_a < node_b（start 地址比较）
 */
static bool vma_less(const struct rb_node *node_a, const struct rb_node *node_b)
{
    const vma_t *vma_a = rb_entry(node_a, vma_t, rb_node);
    const vma_t *vma_b = rb_entry(node_b, vma_t, rb_node);

    return (vma_a->start < vma_b->start);
}

/* ========================================================================
 * 查找 VMA
 * ======================================================================== */

vma_t *vmspace_find_vma(vm_space_t *space, vaddr_t vaddr)
{
    struct rb_node *node;

    if (space == NULL)
    {
        return NULL;
    }

    /* 使用红黑树查找 VMA，复杂度 O(log n) */
    node = rb_search(&space->vma_rb_root, &vaddr, vma_compare);

    if (node != NULL)
    {
        return rb_entry(node, vma_t, rb_node);
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
