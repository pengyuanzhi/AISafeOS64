/**
 * @file    vmspace.h
 * @brief   虚拟地址空间管理接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了虚拟地址空间管理接口：
 *          - 地址空间创建/销毁
 *          - 虚拟内存区域（VMA）管理
 *          - 地址空间切换
 *          - 共享内存映射
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-009~012, MM-001~007
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_VMSPACE_H
#define KERNEL_VMSPACE_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/list.h>
#include <kernel/page_table.h>
#include <kernel/spinlock.h>
#include <kernel/rbtree.h>
#include <kernel/alignment.h>
#include <stdint.h>

/* ========================================================================
 * 虚拟内存区域（VMA）常量
 * ======================================================================== */

/** @brief 用户空间起始地址 */
#define USER_SPACE_BASE      ((vaddr_t)0x0000000000001000ULL)

/** @brief 用户空间结束地址（内核空间起始） */
#define USER_SPACE_END       ((vaddr_t)CONFIG_KERNEL_VADDR_BASE)

/** @brief 用户空间大小 */
#define USER_SPACE_SIZE      (USER_SPACE_END - USER_SPACE_BASE)

/** @brief 内核空间起始地址 */
#define KERNEL_SPACE_BASE    ((vaddr_t)CONFIG_KERNEL_VADDR_BASE)

/** @brief 内核空间结束地址 */
#define KERNEL_SPACE_END     ((vaddr_t)0xFFFFFFFFFFFFFFFFULL)

/** @brief 最大 VMA 数量（每地址空间） */
#define CONFIG_MAX_VMAS      64U

/* ========================================================================
 * VMA 标志位
 * ======================================================================== */

/** @brief VMA 可读 */
#define VMA_FLAG_READ        (1U << 0U)

/** @brief VMA 可写 */
#define VMA_FLAG_WRITE       (1U << 1U)

/** @brief VMA 可执行 */
#define VMA_FLAG_EXEC        (1U << 2U)

/** @brief VMA 可共享 */
#define VMA_FLAG_SHARED      (1U << 3U)

/** @brief VMA 栈区域（向下增长） */
#define VMA_FLAG_STACK       (1U << 4U)

/** @brief VMA 堆区域（向上增长） */
#define VMA_FLAG_HEAP        (1U << 5U)

/** @brief VMA 设备映射（不可缓存） */
#define VMA_FLAG_DEVICE      (1U << 6U)

/* ========================================================================
 * VMA 类型
 * ======================================================================== */

/**
 * @brief 虚拟内存区域类型
 */
typedef enum
{
    VMA_TYPE_ANONYMOUS = 0U,    /**< @brief 匿名映射（零填充） */
    VMA_TYPE_CODE,              /**< @brief 代码段 */
    VMA_TYPE_DATA,              /**< @brief 数据段 */
    VMA_TYPE_BSS,               /**< @brief BSS 段 */
    VMA_TYPE_HEAP,              /**< @brief 堆 */
    VMA_TYPE_STACK,             /**< @brief 栈 */
    VMA_TYPE_SHARED,            /**< @brief 共享内存 */
    VMA_TYPE_DEVICE             /**< @brief 设备 MMIO 映射 */
} vma_type_t;

/* ========================================================================
 * 虚拟内存区域（VMA）结构
 * ======================================================================== */

/**
 * @brief 虚拟内存区域描述符
 *
 * @details VMA 描述了一段连续的虚拟地址范围及其属性。
 *          每个地址空间由多个 VMA 组成，VMA 之间按地址排序。
 *          使用红黑树管理，查找复杂度 O(log n)。
 */
typedef struct CACHE_ALIGN(64)
{
    vaddr_t         start;          /**< @brief 起始虚拟地址（页对齐） */
    vaddr_t         end;            /**< @brief 结束虚拟地址（页对齐，不含） */
    uint32_t        flags;          /**< @brief 权限标志位 */
    vma_type_t      type;           /**< @brief VMA 类型 */
    paddr_t         phys_base;      /**< @brief 对应的物理地址基址（直接映射时） */
    kobj_id_t       shmem_id;       /**< @brief 共享内存对象 ID（VMA_TYPE_SHARED） */
    struct rb_node  rb_node;        /**< @brief VMA 红黑树节点 */
} vma_t;

/* ========================================================================
 * 地址空间结构
 * ======================================================================== */

/**
 * @brief 虚拟地址空间
 *
 * @details 每个进程/线程拥有独立的虚拟地址空间。
 *          包含一个顶层页表（PGD）、VMA 红黑树和 ASID。
 *          使用红黑树管理 VMA，查找复杂度 O(log n)。
 */
typedef struct CACHE_ALIGN(64)
{
    page_table_t    *pgd;           /**< @brief 顶层页表（L0） */
    asid_t          asid;           /**< @brief 地址空间标识 */
    uint32_t        vma_count;      /**< @brief VMA 数量 */
    struct rb_root  vma_rb_root;    /**< @brief VMA 红黑树根 */
    vaddr_t         brk_base;       /**< @brief 堆基址 */
    vaddr_t         brk_current;    /**< @brief 当前堆顶 */
    vaddr_t         stack_top;      /**< @brief 栈顶地址 */
    uint32_t        ref_count;      /**< @brief 引用计数（共享地址空间） */
    TicketLock_t    lock;           /**< @brief 地址空间锁 */
} vm_space_t;

/* ========================================================================
 * 地址空间管理 API
 * ======================================================================== */

/**
 * @brief 初始化地址空间子系统
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmspace_subsys_init(void);

/**
 * @brief 创建新的虚拟地址空间
 *
 * @details 分配新的 PGD 和 ASID，复制内核空间映射。
 *
 * @param space 输出参数，新地址空间指针
 *
 * @return KERNEL_OK 成功
 * @return -ENOMEM 内存不足
 *
 * @note 对应需求: KR-009
 */
kernel_status_t vmspace_create(vm_space_t **space);

/**
 * @brief 销毁虚拟地址空间
 *
 * @param space 要销毁的地址空间
 *
 * @note 对应需求: KR-009
 */
void vmspace_destroy(vm_space_t *space);

/**
 * @brief 切换到指定的地址空间
 *
 * @param space 目标地址空间
 *
 * @note 对应需求: KR-010
 */
void vmspace_switch(vm_space_t *space);

/**
 * @brief 在地址空间中映射内存区域
 *
 * @param space   目标地址空间
 * @param vaddr   起始虚拟地址（页对齐），0 表示自动选择
 * @param size    映射大小（字节，自动页对齐）
 * @param flags   VMA 权限标志
 * @param type    VMA 类型
 * @param paddr   物理地址（直接映射），0 表示自动分配
 *
 * @return 实际映射的起始虚拟地址，失败返回 0
 *
 * @note 对应需求: KR-009, MM-001
 */
vaddr_t vmspace_map(vm_space_t *space,
                     vaddr_t vaddr,
                     uint64_t size,
                     uint32_t flags,
                     vma_type_t type,
                     paddr_t paddr);

/**
 * @brief 解除虚拟地址映射
 *
 * @param space 目标地址空间
 * @param vaddr 起始虚拟地址
 * @param size  大小（字节）
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: KR-010
 */
kernel_status_t vmspace_unmap(vm_space_t *space,
                               vaddr_t vaddr,
                               uint64_t size);

/**
 * @brief 设置页面权限
 *
 * @param space 目标地址空间
 * @param vaddr 虚拟地址
 * @param size  大小
 * @param flags 新权限标志
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmspace_protect(vm_space_t *space,
                                 vaddr_t vaddr,
                                 uint64_t size,
                                 uint32_t flags);

/**
 * @brief 查找虚拟地址所在的 VMA
 *
 * @param space 地址空间
 * @param vaddr 虚拟地址
 *
 * @return VMA 指针，未找到返回 NULL
 */
vma_t *vmspace_find_vma(vm_space_t *space, vaddr_t vaddr);

/**
 * @brief 获取当前活跃的地址空间
 *
 * @return 当前地址空间指针
 */
vm_space_t *vmspace_get_current(void);

/**
 * @brief 获取内核地址空间
 *
 * @return 内核地址空间指针
 */
vm_space_t *vmspace_get_kernel(void);

#endif /* KERNEL_VMSPACE_H */
