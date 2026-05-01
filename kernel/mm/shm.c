/**
 * @file    shm.c
 * @brief   共享内存管理实现
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @details 本文件实现了共享内存管理：
 *          - 共享内存创建和销毁
 *          - 共享内存映射和取消映射
 *          - 映射管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.2.1 - 零拷贝 IPC 实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/mm/shm.h>
#include <kernel/phys_mem.h>
#include <kernel/vmspace.h>
#include <kernel/kobject.h>
#include <kernel/spinlock.h>
#include <kernel/errno.h>
#include <kernel/barrier.h>
#include <hal.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>

/* ========================================================================
 * 内部常量定义
 * ======================================================================== */

/** @brief 最大共享内存对象数量 */
#define CONFIG_MAX_SHM_OBJECTS    64U

/* ========================================================================
 * 全局共享内存对象表
 * ======================================================================== */

/** @brief 全局共享内存对象表（静态分配） */
static shm_t s_shm_objects[CONFIG_MAX_SHM_OBJECTS];

/** @brief 空闲共享内存索引栈 */
static uint32_t s_free_shm_stack[CONFIG_MAX_SHM_OBJECTS];

/** @brief 空闲共享内存计数 */
static uint32_t s_free_shm_count;

/** @brief 共享内存子系统全局锁 */
static TicketLock_t s_shm_subsys_lock;

/** @brief 共享内存映射池（避免动态分配） */
static shm_mapping_t s_mapping_pool[CONFIG_MAX_SHM_OBJECTS * 4];

/** @brief 映射池索引栈 */
static uint32_t s_free_mapping_stack[CONFIG_MAX_SHM_OBJECTS * 4];

/** @brief 空闲映射计数 */
static uint32_t s_free_mapping_count;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 分配空闲共享内存索引
 */
static int32_t alloc_shm_index(void)
{
    int32_t idx;

    ticket_lock_acquire(&s_shm_subsys_lock);

    if (s_free_shm_count == 0U)
    {
        ticket_lock_release(&s_shm_subsys_lock);
        return -1;
    }

    s_free_shm_count--;
    idx = (int32_t)s_free_shm_stack[s_free_shm_count];

    ticket_lock_release(&s_shm_subsys_lock);

    return idx;
}

/**
 * @brief 释放共享内存索引
 */
static void free_shm_index(uint32_t idx)
{
    if (idx >= CONFIG_MAX_SHM_OBJECTS)
    {
        return;
    }

    ticket_lock_acquire(&s_shm_subsys_lock);

    s_free_shm_stack[s_free_shm_count] = idx;
    s_free_shm_count++;

    ticket_lock_release(&s_shm_subsys_lock);
}

/**
 * @brief 分配映射结构
 */
static shm_mapping_t *alloc_mapping(void)
{
    shm_mapping_t *mapping;

    ticket_lock_acquire(&s_shm_subsys_lock);

    if (s_free_mapping_count == 0U)
    {
        ticket_lock_release(&s_shm_subsys_lock);
        return NULL;
    }

    s_free_mapping_count--;
    mapping = &s_mapping_pool[s_free_mapping_count];

    ticket_lock_release(&s_shm_subsys_lock);

    return mapping;
}

/**
 * @brief 释放映射结构
 */
static void free_mapping(shm_mapping_t *mapping)
{
    uint32_t idx;

    if (mapping == NULL)
    {
        return;
    }

    idx = (uint32_t)(mapping - s_mapping_pool);

    if (idx >= (CONFIG_MAX_SHM_OBJECTS * 4U))
    {
        return;
    }

    ticket_lock_acquire(&s_shm_subsys_lock);

    s_free_mapping_stack[s_free_mapping_count] = idx;
    s_free_mapping_count++;

    ticket_lock_release(&s_shm_subsys_lock);
}

/* ========================================================================
 * 共享内存子系统初始化
 * ======================================================================== */

/**
 * @brief 初始化共享内存子系统
 */
kernel_status_t shm_subsys_init(void)
{
    uint32_t i;

    /* 初始化全局锁 */
    ticket_lock_init(&s_shm_subsys_lock);

    /* 初始化空闲共享内存栈 */
    for (i = 0U; i < CONFIG_MAX_SHM_OBJECTS; i++)
    {
        s_free_shm_stack[i] = i;
        list_head_init(&s_shm_objects[i].mappings);
    }
    s_free_shm_count = CONFIG_MAX_SHM_OBJECTS;

    /* 初始化映射池 */
    for (i = 0U; i < (CONFIG_MAX_SHM_OBJECTS * 4U); i++)
    {
        s_free_mapping_stack[i] = i;
    }
    s_free_mapping_count = CONFIG_MAX_SHM_OBJECTS * 4U;

    return KERNEL_OK;
}

/* ========================================================================
 * 共享内存创建
 * ======================================================================== */

/**
 * @brief 创建共享内存对象
 */
kernel_status_t shm_create(uint64_t size, kobj_id_t *id)
{
    int32_t idx;
    shm_t *shm;
    kernel_status_t ret;
    uint64_t num_pages;
    uint64_t i;

    /* 参数检查 */
    if ((size == 0ULL) || (id == NULL))
    {
        return -(int32_t)EINVAL;
    }

    /* 分配共享内存对象 */
    idx = alloc_shm_index();
    if (idx < 0)
    {
        return -(int32_t)ENOMEM;
    }

    shm = &s_shm_objects[(uint32_t)idx];

    /* 初始化共享内存对象 */
    (void)memset(shm, 0, sizeof(shm_t));
    shm->header.type = KOBJ_SHM;
    shm->size = size;
    shm->ref_count = 0U;
    list_head_init(&shm->mappings);
    ticket_lock_init(&shm->lock);

    /* 分配物理内存 */
    num_pages = (size + PAGE_SIZE_4K - 1ULL) / PAGE_SIZE_4K;
    ret = phys_mem_alloc_contiguous(num_pages, &shm->phys_addr);
    if (ret != KERNEL_OK)
    {
        free_shm_index((uint32_t)idx);
        return ret;
    }

    /* 清零物理内存 */
    for (i = 0ULL; i < num_pages; i++)
    {
        uint64_t phys_page = shm->phys_addr + (i * PAGE_SIZE_4K);
        uint64_t *virt_addr;

        virt_addr = (uint64_t *)(uintptr_t)phys_to_virt(phys_page);
        if (virt_addr != NULL)
        {
            (void)memset(virt_addr, 0, PAGE_SIZE_4K);
        }
    }

    /* 注册到内核对象系统 */
    ret = kobject_register((KObjHeader_t *)shm, id);
    if (ret != KERNEL_OK)
    {
        /* 释放物理内存 */
        phys_mem_free_contiguous(shm->phys_addr, num_pages);
        free_shm_index((uint32_t)idx);
        return ret;
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 共享内存销毁
 * ======================================================================== */

/**
 * @brief 销毁共享内存对象
 */
kernel_status_t shm_destroy(kobj_id_t id)
{
    shm_t *shm;
    uint64_t num_pages;

    /* 获取共享内存对象 */
    shm = shm_get(id);
    if (shm == NULL)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&shm->lock);

    /* 检查是否仍有映射 */
    if (!list_empty(&shm->mappings))
    {
        ticket_lock_release(&shm->lock);
        return -(int32_t)EBUSY;
    }

    /* 释放物理内存 */
    num_pages = (shm->size + PAGE_SIZE_4K - 1ULL) / PAGE_SIZE_4K;
    phys_mem_free_contiguous(shm->phys_addr, num_pages);

    /* 注销内核对象 */
    kobject_unregister(id);

    ticket_lock_release(&shm->lock);

    /* 释放共享内存对象 */
    free_shm_index((uint32_t)(shm - s_shm_objects));

    return KERNEL_OK;
}

/* ========================================================================
 * 共享内存获取
 * ======================================================================== */

/**
 * @brief 获取共享内存对象
 */
shm_t *shm_get(kobj_id_t id)
{
    KObjHeader_t *header;

    header = kobject_get(id);
    if (header == NULL)
    {
        return NULL;
    }

    if (header->type != KOBJ_SHM)
    {
        return NULL;
    }

    return (shm_t *)header;
}

/* ========================================================================
 * 共享内存映射
 * ======================================================================== */

/**
 * @brief 映射共享内存到用户空间
 */
kernel_status_t shm_map(kobj_id_t id, kobj_id_t vm_space_id,
                        uint64_t vaddr, uint64_t size, uint32_t flags,
                        uint64_t *out_vaddr)
{
    shm_t *shm;
    shm_mapping_t *mapping;
    kernel_status_t ret;
    uint64_t map_size;
    uint64_t map_vaddr;
    uint64_t num_pages;
    uint64_t i;

    /* 参数检查 */
    if (out_vaddr == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取共享内存对象 */
    shm = shm_get(id);
    if (shm == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 确定映射大小 */
    if (size == 0ULL)
    {
        map_size = shm->size;
    }
    else
    {
        if (size > shm->size)
        {
            return -(int32_t)EINVAL;
        }
        map_size = size;
    }

    /* 分配映射结构 */
    mapping = alloc_mapping();
    if (mapping == NULL)
    {
        return -(int32_t)ENOMEM;
    }

    /* 初始化映射结构 */
    mapping->vm_space_id = vm_space_id;
    mapping->size = map_size;
    mapping->flags = flags;

    ticket_lock_acquire(&shm->lock);

    /* 确定虚拟地址 */
    if (vaddr == 0ULL)
    {
        /* 自动分配虚拟地址（简化实现） */
        map_vaddr = 0x10000000ULL;
    }
    else
    {
        map_vaddr = vaddr;
    }

    mapping->vaddr = map_vaddr;

    /* 计算页数 */
    num_pages = (map_size + PAGE_SIZE_4K - 1ULL) / PAGE_SIZE_4K;

    /* 映射物理页到虚拟地址空间 */
    for (i = 0ULL; i < num_pages; i++)
    {
        uint64_t phys_page = shm->phys_addr + (i * PAGE_SIZE_4K);
        uint64_t virt_page = map_vaddr + (i * PAGE_SIZE_4K);

        /* TODO: 调用虚拟内存管理接口映射页面 */
        (void)phys_page;
        (void)virt_page;
    }

    /* 添加到映射链表 */
    list_add_tail(&mapping->list, &shm->mappings);
    shm->ref_count++;

    ticket_lock_release(&shm->lock);

    *out_vaddr = map_vaddr;

    return KERNEL_OK;
}

/* ========================================================================
 * 共享内存取消映射
 * ======================================================================== */

/**
 * @brief 取消映射共享内存
 */
kernel_status_t shm_unmap(kobj_id_t id, kobj_id_t vm_space_id,
                          uint64_t vaddr, uint64_t size)
{
    shm_t *shm;
    struct list_head *pos;
    struct list_head *n;

    /* 获取共享内存对象 */
    shm = shm_get(id);
    if (shm == NULL)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&shm->lock);

    /* 查找并删除映射 */
    list_for_each_safe(pos, n, &shm->mappings)
    {
        shm_mapping_t *mapping;

        mapping = list_entry(pos, shm_mapping_t, list);

        /* 匹配虚拟地址空间和虚拟地址 */
        if ((mapping->vm_space_id == vm_space_id) &&
            (mapping->vaddr == vaddr))
        {
            /* 从链表中删除 */
            list_del(&mapping->list);
            shm->ref_count--;

            /* TODO: 取消页面映射 */

            /* 释放映射结构 */
            free_mapping(mapping);

            ticket_lock_release(&shm->lock);
            return KERNEL_OK;
        }
    }

    ticket_lock_release(&shm->lock);

    return -(int32_t)EINVAL;
}
