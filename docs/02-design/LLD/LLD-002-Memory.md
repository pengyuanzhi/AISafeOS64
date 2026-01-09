# LLD-002: Memory Management Low-Level Design

**Document ID**: LLD-002
**Version**: 1.0
**Date**: 2025-01-09
**Author**: AISafe64 Team
**Status**: Draft
**Parent**: HLD-003 (Kernel Module Design)

---

## 1. Module Overview

### 1.1 Purpose
The Memory Management module provides 4-level page table management for ARMv8-A MMU, virtual-to-physical address translation, page allocation/deallocation, and memory protection for safety-critical systems.

### 1.2 Scope
This document describes the low-level design of:
- 4-level page table hierarchy (PGD → PUD → PMD → PTE)
- Page frame allocation (buddy system)
- Virtual memory mapping and unmapping
- Page fault handling
- TLB management
- Address space isolation

### 1.3 References
- ARMv8-A Architecture Reference Manual (DDI 0487)
- MISRA-C:2012 Guidelines
- HLD-003: Kernel Module Design
- plan.md Section 4.5 (MMU Management)

---

## 2. Data Structure Design

### 2.1 Page Table Hierarchy

```c
/**
 * @brief 4-level Page Table Entry format (ARMv8-A)
 * @note 64-bit descriptor format
 *
 * [63]    UXN (User Execute Never)
 * [62]    PXN (Privileged Execute Never)
 * [61:52] Reserved (SBZ)
 * [51]    DBM (Dirty Bit Modifier, optional)
 * [50]    Contiguous (hint)
 * [49]    PXN (again, for block descriptors)
 * [48]    UXN (again, for block descriptors)
 * [47:12] Output address (physical page number)
 * [11]    NG (Not Global)
 * [10]    AF (Access Flag)
 * [9:8]   SH (Shareability: 00=Non, 11=Inner)
 * [7:6]   AP (Access permissions: 00=RW, 10=RO)
 * [5:2]   AttrIndx (Memory attributes)
 * [1]     Contiguous (again, for block)
 * [0]     Valid/Type (0=Invalid, 1=Block, 3=Table)
 */
typedef uint64_t pte_t;  /**< Page Table Entry */

/**
 * @brief Page Table Hierarchy
 */
typedef struct
{
    pte_t    pgd[512];    /**< L0: Page Global Directory (512 entries) */
    pte_t    pud[512];    /**< L1: Page Upper Directory (512 entries) */
    pte_t    pmd[512];    /**< L2: Page Middle Directory (512 entries) */
    pte_t    pte[512];    /**< L3: Page Table Entry (512 entries) */
} PageTableHierarchy_t;

/* Compile-time validation */
STATIC_ASSERT(sizeof(PageTableHierarchy_t) == 8192U,
              PageTable_size_mismatch);
```

### 2.2 Page Table Entry Flags

```c
/**
 * @brief Page Table Entry Flags
 * @note Compliant with ARMv8-A specification
 */
#define PAGE_VALID           (1UL << 0)   /**< Descriptor valid */
#define PAGE_TABLE           (1UL << 1)   /**< Table descriptor */
#define PAGE_BLOCK           (1UL << 1)   /**< Block descriptor */

/* Access permissions */
#define PAGE_AP_RO           (2UL << 6)   /**< Read-only */
#define PAGE_AP_RW           (0UL << 6)   /**< Read-write */
#define PAGE_AP_USER         (3UL << 6)   /**< User-accessible */

/* Execute never */
#define PAGE_PXN             (1UL << 53)  /**< Privileged XN */
#define PAGE_UXN             (1UL << 54)  /**< User XN */

/* Memory attributes */
#define PAGE_AF              (1UL << 10)  /**< Access flag */
#define PAGE_SH_INNER        (3UL << 8)   /**< Inner shareable */
#define PAGE_SH_OUTER        (2UL << 8)   /**< Outer shareable */
#define PAGE_SH_NONE         (0UL << 8)   /**< Non-shareable */

/* Normal memory attributes (MAIR_IDX) */
#define PAGE_ATTR_NORMAL     (0UL << 2)   /**< Normal memory */
#define PAGE_ATTR_DEVICE     (1UL << 2)   /**< Device memory */
#define PAGE_ATTR_NC         (2UL << 2)   /**< Non-cacheable */

/* Block mapping attributes */
#define PAGE_BLOCK_ATTR      (PAGE_BLOCK | PAGE_AF | PAGE_SH_INNER | PAGE_ATTR_NORMAL)

/* Table mapping attributes */
#define PAGE_TABLE_ATTR      (PAGE_TABLE | PAGE_AF | PAGE_SH_INNER)
```

### 2.3 Virtual Memory Area (VMA)

```c
/**
 * @brief Virtual Memory Area descriptor
 */
typedef struct VirtualMemoryArea
{
    uint64_t    virt_start;        /**< Virtual start address */
    uint64_t    virt_end;          /**< Virtual end address */
    uint64_t    phys_start;        /**< Physical start address */
    uint64_t    flags;             /**< Protection flags */
    uint32_t    ref_count;         /**< Reference count */
    struct VirtualMemoryArea *next; /**< Next VMA in list */
} VMA_t;

/* VMA flags */
#define VMA_READ             (1U << 0)
#define VMA_WRITE            (1U << 1)
#define VMA_EXECUTE          (1U << 2)
#define VMA_USER             (1U << 3)
#define VMA_SHARED           (1U << 4)
```

### 2.4 Address Space Descriptor

```c
/**
 * @brief Address Space Descriptor
 * @note Represents a task's virtual address space
 */
typedef struct AddressSpace
{
    uint64_t            page_table;        /**< PGD physical address */
    VMA_t              *vma_list;          /**< List of VMAs */
    atomic_uint_fast32_t lock;             /**< VMA list lock */
    uint32_t            ref_count;         /**< Reference count */
    uint32_t            asid;              /**< Address Space ID */
} AddressSpace_t;
```

### 2.5 Page Frame Descriptor

```c
/**
 * @brief Page Frame Descriptor (buddy system)
 */
typedef struct PageFrame
{
    uint64_t            phys_addr;         /**< Physical address */
    uint32_t            order;             /**< Buddy order (log2 size) */
    uint32_t            ref_count;         /**< Reference count */
    struct PageFrame   *buddy;            /**< Buddy page */
    struct PageFrame   *next;             /**< Free list pointer */
} PageFrame_t;
```

### 2.6 Memory Zone Structure

```c
/**
 * @brief Memory Zone (DMA, Normal, HighMem)
 */
typedef enum
{
    ZONE_DMA = 0U,        /**< DMA-able memory (< 4GB) */
    ZONE_NORMAL,          /**< Normal memory (4GB - 128GB) */
    ZONE_HIGHMEM,         /**< High memory (> 128GB) */
    ZONE_MAX
} MemoryZone_t;

/**
 * @brief Memory Zone Descriptor
 */
typedef struct
{
    uint64_t            start;             /**< Zone start physical addr */
    uint64_t            end;               /**< Zone end physical addr */
    uint64_t            present;           /**< Present pages */
    PageFrame_t        *free_list[MAX_ORDER]; /**< Free lists by order */
    atomic_uint_fast32_t lock;             /**< Zone lock */
} MemoryZone_t;
```

---

## 3. API Interface Definition

### 3.1 Page Table Management

```c
/**
 * @brief Initialize MMU subsystem
 * @return 0 on success, negative error code on failure
 *
 * @note Must be called before any MMU operation
 * @warning Must be called with MMU disabled
 */
int32_t mmu_init(void);

/**
 * @brief Create new address space
 * @return Pointer to AddressSpace, or NULL on failure
 *
 * @note Allocates new PGD
 */
AddressSpace_t* mmu_create_address_space(void);

/**
 * @brief Destroy address space
 * @param as Address space to destroy
 *
 * @note Frees all page tables and PGD
 */
void mmu_destroy_address_space(AddressSpace_t *as);

/**
 * @brief Map virtual memory to physical memory
 * @param as Address space
 * @param virt_addr Virtual address (must be page-aligned)
 * @param phys_addr Physical address (must be page-aligned)
 * @param size Size in bytes (must be multiple of page size)
 * @param flags Protection flags (see VMA_*)
 * @return 0 on success, negative error code on failure
 *
 * @note Uses 4KB pages by default
 * @note Automatically uses block mappings where possible
 */
int32_t mmu_map(AddressSpace_t *as,
                uint64_t virt_addr,
                uint64_t phys_addr,
                uint64_t size,
                uint64_t flags);

/**
 * @brief Unmap virtual memory
 * @param as Address space
 * @param virt_addr Virtual address (must be page-aligned)
 * @param size Size in bytes (must be multiple of page size)
 * @return 0 on success, negative error code on failure
 *
 * @note Frees page tables if they become empty
 */
int32_t mmu_unmap(AddressSpace_t *as,
                  uint64_t virt_addr,
                  uint64_t size);

/**
 * @brief Change memory protection flags
 * @param as Address space
 * @param virt_addr Virtual address
 * @param size Size
 * @param new_flags New flags
 * @return 0 on success, negative error code on failure
 */
int32_t mmu_protect(AddressSpace_t *as,
                    uint64_t virt_addr,
                    uint64_t size,
                    uint64_t new_flags);
```

### 3.2 Page Allocation

```c
/**
 * @brief Allocate page frames
 * @param zone Memory zone (ZONE_DMA, ZONE_NORMAL, ZONE_HIGHMEM)
 * @param order Allocation order (0 = 1 page, 1 = 2 pages, ...)
 * @return Physical address of allocated page, or 0 on failure
 *
 * @note Uses buddy system allocator
 * @note Physical address is page-aligned
 */
uint64_t page_alloc(MemoryZone_t zone, uint32_t order);

/**
 * @brief Free page frames
 * @param phys_addr Physical address of page (must be page-aligned)
 * @param order Allocation order
 *
 * @note Merges with buddy if free
 */
void page_free(uint64_t phys_addr, uint32_t order);

/**
 * @brief Get virtual address of physical page
 * @param phys_addr Physical address
 * @return Kernel virtual address
 *
 * @note Uses direct mapping region
 */
void* phys_to_virt(uint64_t phys_addr);

/**
 * @brief Get physical address of virtual address
 * @param virt Virtual address
 * @return Physical address
 */
uint64_t virt_to_phys(void *virt);
```

### 3.3 Page Table Query

```c
/**
 * @brief Translate virtual address to physical address
 * @param as Address space
 * @param virt_addr Virtual address
 * @return Physical address, or 0 if not mapped
 */
uint64_t mmu_lookup(AddressSpace_t *as, uint64_t virt_addr);

/**
 * @brief Query memory protection flags
 * @param as Address space
 * @param virt_addr Virtual address
 * @return Protection flags, or 0 if not mapped
 */
uint64_t mmu_query_flags(AddressSpace_t *as, uint64_t virt_addr);
```

---

## 4. Algorithm Implementation Details

### 4.1 4-Level Page Table Walk

```c
/**
 * @brief Walk page tables and return PTE
 * @param as Address space
 * @param virt_addr Virtual address
 * @param alloc If true, allocate missing page tables
 * @return Pointer to PTE, or NULL if not present/allocation failed
 *
 * @note ARMv8-A 4KB page granularity:
 *       - L0 (PGD): [48:39] = 9 bits, 512 entries
 *       - L1 (PUD): [38:30] = 9 bits, 512 entries
 *       - L2 (PMD): [29:21] = 9 bits, 512 entries
 *       - L3 (PTE): [20:12] = 9 bits, 512 entries
 *       - Offset:  [11:0]  = 12 bits, 4096 bytes
 */
static pte_t* page_table_walk(AddressSpace_t *as,
                               uint64_t virt_addr,
                               bool alloc)
{
    pte_t *pgd;
    pte_t *pud;
    pte_t *pmd;
    pte_t *pte;

    /* Extract indices */
    uint64_t pgd_idx = (virt_addr >> 39U) & 0x1FFU;
    uint64_t pud_idx = (virt_addr >> 30U) & 0x1FFU;
    uint64_t pmd_idx = (virt_addr >> 21U) & 0x1FFU;
    uint64_t pte_idx = (virt_addr >> 12U) & 0x1FFU;

    /* Get PGD */
    pgd = (pte_t *)phys_to_virt(as->page_table);
    if (pgd == NULL)
    {
        return NULL;
    }

    /* Walk L1 (PUD) */
    if ((pgd[pgd_idx] & PAGE_VALID) == 0U)
    {
        if (!alloc)
        {
            return NULL;
        }
        /* Allocate new PUD */
        pud = (pte_t *)page_alloc(ZONE_NORMAL, 0);
        if (pud == NULL)
        {
            return NULL;
        }
        /* Clear PUD */
        memset(pud, 0, PAGE_SIZE);
        /* Set PGD entry */
        pgd[pgd_idx] = virt_to_phys(pud) | PAGE_TABLE_ATTR;
    }
    else
    {
        pud = (pte_t *)phys_to_virt(pgd[pgd_idx] & ~0xFFFU);
    }

    /* Walk L2 (PMD) */
    if ((pud[pud_idx] & PAGE_VALID) == 0U)
    {
        if (!alloc)
        {
            return NULL;
        }
        /* Allocate new PMD */
        pmd = (pte_t *)page_alloc(ZONE_NORMAL, 0);
        if (pmd == NULL)
        {
            return NULL;
        }
        memset(pmd, 0, PAGE_SIZE);
        pud[pud_idx] = virt_to_phys(pmd) | PAGE_TABLE_ATTR;
    }
    else
    {
        pmd = (pte_t *)phys_to_virt(pud[pud_idx] & ~0xFFFU);
    }

    /* Check for 1GB block mapping */
    if ((pmd[pmd_idx] & PAGE_TABLE) == 0U)
    {
        /* Block mapping: return PMD entry */
        return &pmd[pmd_idx];
    }

    /* Walk L3 (PTE) */
    if ((pmd[pmd_idx] & PAGE_VALID) == 0U)
    {
        if (!alloc)
        {
            return NULL;
        }
        /* Allocate new PTE */
        pte = (pte_t *)page_alloc(ZONE_NORMAL, 0);
        if (pte == NULL)
        {
            return NULL;
        }
        memset(pte, 0, PAGE_SIZE);
        pmd[pmd_idx] = virt_to_phys(pte) | PAGE_TABLE_ATTR;
    }
    else
    {
        pte = (pte_t *)phys_to_virt(pmd[pmd_idx] & ~0xFFFU);
    }

    /* Check for 2MB block mapping */
    if ((pte[pte_idx] & PAGE_TABLE) == 0U)
    {
        /* Block mapping: return PTE entry */
        return &pte[pte_idx];
    }

    /* Return PTE entry */
    return &pte[pte_idx];
}
```

### 4.2 Virtual Memory Mapping

```c
/**
 * @brief Map virtual memory range
 * @param as Address space
 * @param virt_addr Virtual address (page-aligned)
 * @param phys_addr Physical address (page-aligned)
 * @param size Size (multiple of page size)
 * @param flags Protection flags
 * @return 0 on success, negative error code on failure
 */
int32_t mmu_map(AddressSpace_t *as,
                uint64_t virt_addr,
                uint64_t phys_addr,
                uint64_t size,
                uint64_t flags)
{
    uint64_t virt_end = virt_addr + size;
    uint64_t phys = phys_addr;
    uint64_t virt;
    pte_t *pte;

    /* Validate alignment */
    if ((virt_addr & PAGE_MASK) != 0U)
    {
        return -EINVAL;
    }
    if ((phys_addr & PAGE_MASK) != 0U)
    {
        return -EINVAL;
    }
    if ((size & PAGE_MASK) != 0U)
    {
        return -EINVAL;
    }

    /* Lock address space */
    atomic_lock(&as->lock);

    /* Map each page */
    for (virt = virt_addr; virt < virt_end; virt += PAGE_SIZE)
    {
        /* Walk page tables (allocate if needed) */
        pte = page_table_walk(as, virt, true);
        if (pte == NULL)
        {
            atomic_unlock(&as->lock);
            return -ENOMEM;
        }

        /* Check if already mapped */
        if ((*pte & PAGE_VALID) != 0U)
        {
            atomic_unlock(&as->lock);
            return -EEXIST;
        }

        /* Create PTE */
        *pte = phys | flags | PAGE_VALID;

        phys += PAGE_SIZE;
    }

    atomic_unlock(&as->lock);

    /* Flush TLB */
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");

    return 0;
}
```

### 4.3 Block Mapping Optimization

```c
/**
 * @brief Try to create 2MB block mapping
 * @param as Address space
 * @param virt_addr Virtual address (2MB-aligned)
 * @param phys_addr Physical address (2MB-aligned)
 * @param flags Protection flags
 * @return 0 on success, negative error code on failure
 *
 * @note Falls back to 4KB mapping if block mapping fails
 */
static int32_t try_map_block(AddressSpace_t *as,
                              uint64_t virt_addr,
                              uint64_t phys_addr,
                              uint64_t flags)
{
    pte_t *pgd;
    pte_t *pud;
    pte_t *pmd;

    /* Must be 2MB aligned */
    if ((virt_addr & 0x1FFFFFU) != 0U)
    {
        return -EINVAL;
    }
    if ((phys_addr & 0x1FFFFFU) != 0U)
    {
        return -EINVAL;
    }

    /* Extract indices */
    uint64_t pgd_idx = (virt_addr >> 39U) & 0x1FFU;
    uint64_t pud_idx = (virt_addr >> 30U) & 0x1FFU;
    uint64_t pmd_idx = (virt_addr >> 21U) & 0x1FFU;

    /* Get PGD */
    pgd = (pte_t *)phys_to_virt(as->page_table);

    /* Walk to PMD level */
    pud = (pte_t *)phys_to_virt(pgd[pgd_idx] & ~0xFFFU);
    pmd = (pte_t *)phys_to_virt(pud[pud_idx] & ~0xFFFU);

    /* Create 2MB block mapping */
    pmd[pmd_idx] = phys_addr | flags | PAGE_BLOCK;

    return 0;
}
```

### 4.4 Page Fault Handler

```c
/**
 * @brief Page fault handler (called from exception vector)
 * @param fault_addr Faulting virtual address
 * @param fault_status ESR_EL1.FSC (Fault Status Code)
 * @return 1 if handled, 0 if not handled (fatal)
 *
 * @note ARMv8-A Fault Status Codes (FSC):
 *       0x04: Translation fault, level 0
 *       0x05: Translation fault, level 1
 *       0x06: Translation fault, level 2
 *       0x07: Translation fault, level 3
 *       0x08: Access flag fault, level 0
 *       ...
 *       0x0C: Permission fault, level 0
 *       ...
 *       0x0F: Permission fault, level 3
 */
int32_t page_fault_handler(uint64_t fault_addr, uint64_t fault_status)
{
    AddressSpace_t *as;
    uint32_t fsc = (uint32_t)(fault_status & 0x3FU);

    /* Get current address space */
    as = current_task->address_space;

    /* Handle translation fault (page not present) */
    if ((fsc >= 0x04U) && (fsc <= 0x07U))
    {
        /* TODO: Demand paging */
        return 0;  /* Not handled, fatal */
    }

    /* Handle permission fault */
    if ((fsc >= 0x0CU) && (fsc <= 0x0FU))
    {
        /* Access violation */
        log_err("Page fault: permission violation at 0x%llx\n", fault_addr);
        send_sigsegv(current_task);
        return 1;  /* Handled */
    }

    /* Other faults are fatal */
    return 0;
}
```

### 4.5 TLB Management

```c
/**
 * @brief Invalidate TLB entry for specific address
 * @param as Address space
 * @param virt_addr Virtual address
 */
static inline void tlb_invalidate_page(AddressSpace_t *as, uint64_t virt_addr)
{
    (void)as;  /* ASID not used yet */

    uint64_t addr = virt_addr >> 12U;
    __asm__ volatile("tlbi vae1is, %0" :: "r"(addr));
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}

/**
 * @brief Invalidate entire TLB
 */
static inline void tlb_invalidate_all(void)
{
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}
```

### 4.6 Buddy System Allocator

```c
/**
 * @brief Allocate page frames using buddy system
 * @param zone Memory zone
 * @param order Allocation order (log2 of page count)
 * @return Physical address, or 0 on failure
 *
 * @note Maximum order is MAX_ORDER (typically 10 = 1024 pages)
 */
uint64_t page_alloc(MemoryZone_t zone, uint32_t order)
{
    MemoryZone_t *z = &g_zones[zone];
    PageFrame_t *page;
    PageFrame_t *buddy;
    uint32_t current_order;

    /* Validate order */
    if (order >= MAX_ORDER)
    {
        return 0;
    }

    atomic_lock(&z->lock);

    /* Find free block of required order or higher */
    for (current_order = order; current_order < MAX_ORDER; current_order++)
    {
        if (z->free_list[current_order] != NULL)
        {
            break;
        }
    }

    if (current_order >= MAX_ORDER)
    {
        atomic_unlock(&z->lock);
        return 0;  /* Out of memory */
    }

    /* Remove from free list */
    page = z->free_list[current_order];
    z->free_list[current_order] = page->next;

    /* Split blocks until we reach required order */
    while (current_order > order)
    {
        current_order--;

        /* Calculate buddy address */
        uint64_t buddy_phys = page->phys_addr ^ (1UL << (PAGE_SHIFT + current_order));
        buddy = &g_page_frames[buddy_phys >> PAGE_SHIFT];

        /* Initialize buddy */
        buddy->phys_addr = buddy_phys;
        buddy->order = current_order;
        buddy->ref_count = 0;

        /* Add buddy to free list */
        buddy->next = z->free_list[current_order];
        z->free_list[current_order] = buddy;
    }

    /* Mark page as used */
    page->order = order;
    page->ref_count = 1;

    atomic_unlock(&z->lock);

    return page->phys_addr;
}

/**
 * @brief Free page frames
 * @param phys_addr Physical address
 * @param order Allocation order
 */
void page_free(uint64_t phys_addr, uint32_t order)
{
    MemoryZone_t *zone;
    PageFrame_t *page;
    PageFrame_t *buddy;
    uint64_t buddy_phys;

    /* Determine zone */
    zone = find_zone(phys_addr);
    if (zone == NULL)
    {
        return;
    }

    page = &g_page_frames[phys_addr >> PAGE_SHIFT];

    atomic_lock(&zone->lock);

    /* Merge with free buddies */
    while (order < MAX_ORDER)
    {
        /* Calculate buddy address */
        buddy_phys = phys_addr ^ (1UL << (PAGE_SHIFT + order));
        buddy = &g_page_frames[buddy_phys >> PAGE_SHIFT];

        /* Check if buddy is free */
        if ((buddy->ref_count != 0) || (buddy->order != order))
        {
            break;
        }

        /* Remove buddy from free list */
        remove_from_free_list(zone, buddy, order);

        /* Merge */
        if (buddy_phys < phys_addr)
        {
            phys_addr = buddy_phys;
            page = buddy;
        }
        order++;
    }

    /* Add merged block to free list */
    page->phys_addr = phys_addr;
    page->order = order;
    page->ref_count = 0;
    page->next = zone->free_list[order];
    zone->free_list[order] = page;

    atomic_unlock(&zone->lock);
}
```

---

## 5. Performance Requirements

### 5.1 Timing Constraints

| Operation | Maximum Latency |
|-----------|-----------------|
| **Page Allocation** | 1 μs |
| **Page Free** | 500 ns |
| **Page Table Walk** | 200 ns |
| **TLB Invalidation** | 100 ns |
| **Page Fault Handler** | 5 μs |
| **Memory Mapping** | 10 μs per page |

### 5.2 Memory Constraints

| Resource | Limit |
|----------|-------|
| **Page Table Memory** | ≤ 2% of RAM |
| **Page Frame Descriptors** | 64 bytes per page |
| **VMA Structures** | ≤ 64 bytes per mapping |

### 5.3 TLB Efficiency

- **Block Mapping Rate**: > 80% for kernel mappings
- **TLB Miss Rate**: < 1% (normal workload)

---

## 6. MISRA-C:2012 Compliance

### 6.1 Critical Rules

| Rule | Requirement |
|------|-------------|
| Rule 11.1 | No pointer-integer conversion except uintptr_t |
| Rule 11.4 | No implicit pointer conversions |
| Rule 11.6 | Cast must preserve const/volatile |
| Rule 18.1 | Pointer arithmetic limited to array bounds |

### 6.2 Type Safety

```c
/* ✅ Correct: Use uintptr_t for pointer-integer conversion */
uint64_t paddr = (uint64_t)virt_to_phys(virt);

/* ❌ Wrong: Direct cast */
uint64_t paddr = (uint64_t)virt;
```

### 6.3 Runtime Checks

```c
/* Compile-time assertions */
STATIC_ASSERT(PAGE_SIZE == 4096U, page_size_mismatch);
STATIC_ASSERT(sizeof(pte_t) == 8U, pte_size_mismatch);
```

---

## 7. Testing Strategy

### 7.1 Unit Tests

| Test Case | Description |
|-----------|-------------|
| **TC-MM-001** | Page allocation and deallocation |
| **TC-MM-002** | Buddy system coalescing |
| **TC-MM-003** | Page table walk (all levels) |
| **TC-MM-004** | 4KB page mapping |
| **TC-MM-005** | 2MB block mapping |
| **TC-MM-006** | 1GB block mapping |
| **TC-MM-007** | TLB invalidation |
| **TC-MM-008** | Page fault handling |
| **TC-MM-009** | Memory protection (RO, NX) |
| **TC-MM-010** | Address space isolation |

### 7.2 Integration Tests

| Test Case | Description |
|-----------|-------------|
| **TC-MM-INT-001** | MMU with scheduler |
| **TC-MM-INT-002** | MMU with sync primitives |
| **TC-MM-INT-003** | User-kernel transitions |
| **TC-MM-INT-004** | DMA memory mapping |

### 7.3 Performance Tests

| Test Case | Metric | Target |
|-----------|--------|--------|
| **TC-MM-PERF-001** | Page allocation throughput | > 1M pages/sec |
| **TC-MM-PERF-002** | TLB miss rate | < 1% |
| **TC-MM-PERF-003** | Page table walk latency | < 200 ns |

### 7.4 Coverage Requirements

- **Statement Coverage**: > 95%
- **Branch Coverage**: > 90%
- **MC/DC Coverage**: > 85% (critical functions)

---

## 8. Configuration Options

### 8.1 MenuConfig Options

```kconfig
config MMU
    bool "MMU Support"
    default y

config PAGE_SIZE
    int "Page size (bytes)"
    default 4096
    depends on MMU

config MAX_ORDER
    int "Maximum allocation order"
    range 8 12
    default 10
    depends on MMU
    help
      Maximum order for buddy system.
      10 = 1024 pages (4MB with 4KB pages)

config ZONE_DMA
    bool "DMA memory zone"
    default y
    depends on MMU

config BLOCK_MAPPING_2MB
    bool "2MB block mapping"
    default y
    depends on MMU

config BLOCK_MAPPING_1GB
    bool "1GB block mapping"
    default y
    depends on MMU
```

---

## 9. Error Handling

### 9.1 Error Codes

| Error Code | Description |
|------------|-------------|
| `ERROR_INVALID_ADDRESS` | Address not aligned |
| `ERROR_ALREADY_MAPPED` | Virtual address already mapped |
| `ERROR_NOT_MAPPED` | Virtual address not mapped |
| `ERROR_OUT_OF_MEMORY` | Page allocation failed |
| `ERROR_INVALID_FLAGS` | Invalid protection flags |

### 9.2 Error Recovery

- **Page Allocation Failure**: Return NULL (caller handles)
- **Page Table Allocation Failure**: Return error, rollback
- **Page Fault (Access Violation)**: Send SIGSEGV to task
- **TLB Parity Error**: Trigger system panic (fatal)

---

## 10. Traceability

### 10.1 Requirements Traceability

| LLD Section | HLD Section | Plan.md Section |
|-------------|-------------|-----------------|
| Page Table Hierarchy | 4.5 MMU Management | 4.5.1 |
| Buddy System | 4.2 Memory Management | - |
| Block Mapping | 4.5.0 Early MMU | 4.5.0 |
| Page Fault Handler | 4.5.2 Page Fault | 4.5.2 |

### 10.2 Test Coverage Traceability

| Test Case | Requirement |
|-----------|-------------|
| TC-MM-004 | MM-001: 4KB mapping |
| TC-MM-005 | MM-002: 2MB block mapping |
| TC-MM-PERF-001 | NFR-001: Allocation < 1μs |

---

## Appendix A: ARMv8-A Page Table Format

### A.1 Level 3 Page Table Entry (4KB page)

```
  63   62 61      52 51 50 49 48 47                 12 11 10 9 8 7 6 5 4 3 2 1 0
 +-----+---+---------+--+--+--+--+--------------------+--+--+--+-----+-+-+-+-+
 | UXN | PXN| SBZ     |DB|Cont|   Output Address     |NG|AF|SH|  AP  |AttrIndx|0|
 +-----+---+---------+--+--+--+--+--------------------+--+--+--+-----+-+-+-+-+
```

### A.2 Level 2 Block Descriptor (2MB block)

```
  63   62 61      52 51 50 49 48 47                 12 11 10 9 8 7 6 5 4 3 2 1 0
 +-----+---+---------+--+--+--+--+--------------------+--+--+--+-----+-+-+-+-+
 | UXN | PXN| SBZ     |DB|Cont|   Output Address     |NG|AF|SH|  AP  |AttrIndx|1|
 +-----+---+---------+--+--+--+--+--------------------+--+--+--+-----+-+-+-+-+
```

### A.3 Table Descriptor

```
  63   62            52 51                     12 11 10 9 8 7 6 5 4 3 2 1 0
 +-----+---------------+------------------------+--+--+--+-----+-+-+-+-+
 | UXN | PXN| SBZ      |   Next Table Address    |NG|AF|SH|  AP  |AttrIndx|3|
 +-----+---------------+------------------------+--+--+--+-----+-+-+-+-+
```

---

**Document End**
