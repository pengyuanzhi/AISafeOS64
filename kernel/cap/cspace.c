/**
 * @file    cspace.c
 * @brief   能力空间（CSpace）管理实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件实现了能力空间（CSpace）管理：
 *          - CSpace 静态池分配与回收
 *          - 能力表初始化与空闲链表管理
 *          - 能力槽分配/释放
 *          - 能力插入/查找
 *          - 根能力管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-016
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */

#include <kernel/cspace.h>
#include <kernel/capability.h>
#include <kernel/kobject.h>
#include <kernel/object_pool.h>
#include <kernel/errno.h>
#include <kernel/barrier.h>
#include <kernel/config.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 静态数据结构
 * ======================================================================== */

/**
 * @brief CSpace 静态池
 *
 * @details 预分配 CONFIG_MAX_CSPACES 个 CSpace 结构，
 *          避免运行时动态内存分配。
 */
static cspace_t s_cspace_pool[CONFIG_MAX_CSPACES];

/**
 * @brief CSpace 空闲索引栈
 *
 * @details 使用栈结构管理空闲 CSpace 索引，
 *          分配 O(1)（栈弹出），释放 O(1)（栈压入）。
 */
static uint32_t s_cspace_free_stack[CONFIG_MAX_CSPACES];

/**
 * @brief CSpace 空闲计数（栈顶指针）
 */
static uint32_t s_cspace_free_count;

/**
 * @brief 能力表静态存储
 *
 * @details 每个 CSpace 分配一个 cap_t 数组，
 *          用于存储该 CSpace 的所有能力描述符。
 *          cap_t 包含 list_head 成员（children/sibling），
 *          可直接用于链表操作。
 */
static cap_t s_cap_table_pool[CONFIG_MAX_CSPACES][CSPACE_MAX_CAPACITY];

/**
 * @brief CSpace 子系统全局锁
 *
 * @details 保护 CSpace 池的分配和释放操作，
 *          使用 TicketLock 保证多核公平性。
 */
static TicketLock_t s_subsys_lock;

/**
 * @brief 全局 CSpace 对象 ID 计数器
 *
 * @details 用于为每个新创建的 CSpace 分配唯一 ID。
 *          仅在持锁时递增，无需原子操作。
 */
static kobj_id_t s_cspace_id_counter;

/* ========================================================================
 * 内部辅助函数声明
 * ======================================================================== */

/**
 * @brief 从 CSpace 静态池分配一个空闲 CSpace
 *
 * @param[out] out_cspace 输出 CSpace 指针
 *
 * @return KERNEL_OK 成功
 * @return -ENOMEM 池已耗尽
 */
static kernel_status_t cspace_pool_alloc(cspace_t **out_cspace);

/**
 * @brief 将 CSpace 释放回静态池
 *
 * @param cspace 要释放的 CSpace 指针
 */
static void cspace_pool_free(cspace_t *cspace);

/**
 * @brief 初始化能力表
 *
 * @details 将所有 cap_t 设为 CAP_STATE_FREE，
 *          并构建空闲链表（slot 1 ~ capacity-1 串联，
 *          slot 0 保留给根能力）。
 *
 * @param cspace CSpace 指针
 */
static void cspace_init_cap_table(cspace_t *cspace);

/**
 * @brief 在 CSpace 中创建根能力
 *
 * @details 在 slot 0 创建指向 CSpace 自身的能力，
 *          拥有全部权限。
 *
 * @param cspace CSpace 指针
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t cspace_create_root_cap(cspace_t *cspace);

/* ========================================================================
 * CSpace 子系统 API 实现
 * ======================================================================== */

/**
 * @brief 初始化 CSpace 子系统
 *
 * @details 初始化空闲索引栈，将所有 CSpace 索引压入栈中。
 *          初始化全局锁和 ID 计数器。
 */
kernel_status_t cspace_subsys_init(void)
{
    uint32_t i;

    /* 初始化全局锁 */
    ticket_lock_init(&s_subsys_lock);

    /* 初始化 ID 计数器 */
    s_cspace_id_counter = (kobj_id_t)1U;

    /* 将所有 CSpace 索引压入空闲栈（逆序压入，顺序弹出） */
    s_cspace_free_count = (uint32_t)CONFIG_MAX_CSPACES;

    for (i = 0U; i < (uint32_t)CONFIG_MAX_CSPACES; i++)
    {
        s_cspace_free_stack[i] = (uint32_t)CONFIG_MAX_CSPACES - 1U - i;
    }

    /* 清零 CSpace 池 */
    (void)memset(s_cspace_pool, 0, sizeof(s_cspace_pool));

    return KERNEL_OK;
}

/**
 * @brief 创建新的能力空间
 *
 * @details 从静态池分配 CSpace，初始化能力表，
 *          在 slot 0 创建根能力（指向自身，全部权限）。
 */
kernel_status_t cspace_create(uint32_t capacity, cspace_t **out_cspace)
{
    kernel_status_t ret;
    cspace_t *cspace = NULL;
    kobj_id_t new_id;

    /* 参数校验 */
    if (out_cspace == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if ((capacity == 0U) || (capacity > (uint32_t)CSPACE_MAX_CAPACITY))
    {
        return -(int32_t)EINVAL;
    }

    /* 获取子系统锁 */
    ticket_lock_acquire(&s_subsys_lock);

    /* 分配 CSpace 结构 */
    ret = cspace_pool_alloc(&cspace);
    if (ret != KERNEL_OK)
    {
        ticket_lock_release(&s_subsys_lock);
        return ret;
    }

    /* 分配唯一 ID */
    new_id = s_cspace_id_counter;
    s_cspace_id_counter++;

    /* 释放子系统锁（后续操作使用 CSpace 自身锁） */
    ticket_lock_release(&s_subsys_lock);

    /* 初始化内核对象头部 */
    kobj_header_init(&cspace->header, KOBJ_CSPACE, new_id, KOBJ_ID_INVALID);

    /* 设置能力表容量（使用请求值或最大值中较小者） */
    cspace->capacity = capacity;

    /* 关联预分配的能力表存储 */
    {
        uint32_t pool_idx;
        /* 通过指针差计算池索引 */
        pool_idx = (uint32_t)(cspace - &s_cspace_pool[0U]);
        cspace->cap_table = &s_cap_table_pool[pool_idx][0U];
    }

    cspace->used_count = 0U;
    cspace->root_slot = CAP_SLOT_INVALID;
    cspace->free_head = CAP_SLOT_INVALID;

    /* 初始化链表 */
    INIT_LIST_HEAD(&cspace->child_cspaces);
    INIT_LIST_HEAD(&cspace->cspace_node);

    /* 初始化 CSpace 自身锁 */
    ticket_lock_init(&cspace->lock);

    /* 初始化能力表并构建空闲链表 */
    cspace_init_cap_table(cspace);

    /* 创建根能力 */
    ret = cspace_create_root_cap(cspace);
    if (ret != KERNEL_OK)
    {
        /* 创建根能力失败，释放回池中 */
        ticket_lock_acquire(&s_subsys_lock);
        cspace_pool_free(cspace);
        ticket_lock_release(&s_subsys_lock);
        return ret;
    }

    /* 内存屏障确保初始化完成 */
    barrier_store();

    *out_cspace = cspace;

    return KERNEL_OK;
}

/**
 * @brief 销毁能力空间
 *
 * @details 遍历能力表中所有有效能力，将其状态设为 CAP_STATE_FREE。
 *          将 CSpace 释放回静态池。
 */
void cspace_destroy(cspace_t *cspace)
{
    uint32_t i;
    cap_t *cap;

    if (cspace == NULL)
    {
        return;
    }

    /* 获取 CSpace 锁 */
    ticket_lock_acquire(&cspace->lock);

    /* 遍历能力表，清理所有有效能力 */
    for (i = 0U; i < cspace->capacity; i++)
    {
        cap = &cspace->cap_table[i];

        if (cap->state == CAP_STATE_VALID)
        {
            /* 从父能力的 children 链表中移除 */
            if (cap->parent_slot != CAP_SLOT_INVALID)
            {
                list_del(&cap->sibling);
            }

            /* 清空子能力链表 */
            INIT_LIST_HEAD(&cap->children);

            /* 标记为空闲 */
            cap->state = CAP_STATE_FREE;
        }
    }

    /* 清空 CSpace 元数据 */
    cspace->used_count = 0U;
    cspace->free_head = CAP_SLOT_INVALID;
    cspace->root_slot = CAP_SLOT_INVALID;

    /* 从子 CSpace 链表中移除 */
    list_del(&cspace->cspace_node);
    INIT_LIST_HEAD(&cspace->child_cspaces);

    /* 释放 CSpace 锁 */
    ticket_lock_release(&cspace->lock);

    /* 释放回静态池 */
    ticket_lock_acquire(&s_subsys_lock);
    cspace_pool_free(cspace);
    ticket_lock_release(&s_subsys_lock);
}

/**
 * @brief 在 CSpace 中分配一个空闲能力槽
 *
 * @details 从空闲链表头取一个空闲 slot，返回其索引。
 */
kernel_status_t cspace_alloc_slot(cspace_t *cspace, cap_slot_t *out_slot)
{
    cap_t *free_cap;

    if ((cspace == NULL) || (out_slot == NULL))
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&cspace->lock);

    /* 检查是否有空闲槽 */
    if (cspace->free_head == CAP_SLOT_INVALID)
    {
        ticket_lock_release(&cspace->lock);
        return -(int32_t)ENOMEM;
    }

    /* 从空闲链表头取出 */
    free_cap = &cspace->cap_table[cspace->free_head];

    /* 更新空闲链表头：
     * 空闲链表使用 cap_t 的 sibling.next 字段串联，
     * 通过计算偏移得到下一个空闲槽的索引 */
    if (free_cap->sibling.next != NULL)
    {
        /* 计算下一个节点在 cap_table 中的索引 */
        cap_t *next_free = (cap_t *)free_cap->sibling.next;
        cap_slot_t next_idx = (cap_slot_t)(next_free - &cspace->cap_table[0U]);
        cspace->free_head = next_idx;
    }
    else
    {
        /* 空闲链表已空 */
        cspace->free_head = CAP_SLOT_INVALID;
    }

    /* 清空取出的 cap_t */
    (void)memset(free_cap, 0, sizeof(cap_t));
    free_cap->state = CAP_STATE_FREE;

    ticket_lock_release(&cspace->lock);

    *out_slot = (cap_slot_t)(free_cap - &cspace->cap_table[0U]);

    return KERNEL_OK;
}

/**
 * @brief 释放能力槽到空闲链表
 *
 * @details 将指定 slot 追加到空闲链表头部。
 */
void cspace_free_slot(cspace_t *cspace, cap_slot_t slot)
{
    cap_t *cap;

    if (cspace == NULL)
    {
        return;
    }

    if (slot >= cspace->capacity)
    {
        return;
    }

    ticket_lock_acquire(&cspace->lock);

    cap = &cspace->cap_table[slot];

    /* 已经是空闲状态则忽略 */
    if (cap->state == CAP_STATE_FREE)
    {
        ticket_lock_release(&cspace->lock);
        return;
    }

    /* 从父能力的 children 链表中移除（如果存在） */
    if (cap->parent_slot != CAP_SLOT_INVALID)
    {
        list_del(&cap->sibling);
    }

    /* 清空子能力链表 */
    INIT_LIST_HEAD(&cap->children);

    /* 重置为空闲状态 */
    (void)memset(cap, 0, sizeof(cap_t));
    cap->state = CAP_STATE_FREE;

    /* 将当前空闲槽指向原链表头 */
    if (cspace->free_head != CAP_SLOT_INVALID)
    {
        cap->sibling.next = (struct list_head *)&cspace->cap_table[cspace->free_head];
    }
    else
    {
        cap->sibling.next = NULL;
    }

    /* 更新空闲链表头为当前 slot */
    cspace->free_head = slot;

    /* 更新使用计数 */
    if (cspace->used_count > 0U)
    {
        cspace->used_count--;
    }

    ticket_lock_release(&cspace->lock);
}

/**
 * @brief 在指定槽位插入能力
 *
 * @details 验证 slot 有效且处于空闲状态，填充能力字段，
 *          如果指定了父能力则加入其子能力链表。
 */
kernel_status_t cspace_insert_cap(cspace_t *cspace,
                                   cap_slot_t slot,
                                   kobj_type_t kobj_type,
                                   kobj_id_t kobj_id,
                                   uint8_t rights,
                                   uint16_t badge,
                                   cap_slot_t parent_slot)
{
    cap_t *cap;
    cap_t *parent_cap;

    if (cspace == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 验证 slot 范围 */
    if (slot >= cspace->capacity)
    {
        return -(int32_t)EINVAL;
    }

    /* 验证内核对象类型 */
    if (kobj_type >= KOBJ_TYPE_COUNT)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&cspace->lock);

    cap = &cspace->cap_table[slot];

    /* 验证 slot 处于空闲状态 */
    if (cap->state != CAP_STATE_FREE)
    {
        ticket_lock_release(&cspace->lock);
        return -(int32_t)EINVAL;
    }

    /* 填充能力字段 */
    cap->state = CAP_STATE_VALID;
    cap->kobj_type = kobj_type;
    cap->rights = rights;
    cap->badge = badge;
    cap->kobj_id = kobj_id;
    cap->parent_slot = parent_slot;
    cap->cspace_root = cspace->root_slot;

    /* 初始化子能力链表 */
    INIT_LIST_HEAD(&cap->children);
    INIT_LIST_HEAD(&cap->sibling);

    /* 如果有父能力，加入父能力的 children 链表 */
    if (parent_slot != CAP_SLOT_INVALID)
    {
        /* 验证父 slot 有效性 */
        if (parent_slot >= cspace->capacity)
        {
            cap->state = CAP_STATE_FREE;
            ticket_lock_release(&cspace->lock);
            return -(int32_t)EINVAL;
        }

        parent_cap = &cspace->cap_table[parent_slot];

        if (parent_cap->state != CAP_STATE_VALID)
        {
            cap->state = CAP_STATE_FREE;
            ticket_lock_release(&cspace->lock);
            return -(int32_t)EINVAL;
        }

        list_add_tail(&cap->sibling, &parent_cap->children);
    }

    /* 更新使用计数 */
    cspace->used_count++;

    /* 内存屏障确保能力写入可见 */
    barrier_store();

    ticket_lock_release(&cspace->lock);

    return KERNEL_OK;
}

/**
 * @brief 查找能力
 *
 * @details 检查 slot 有效且状态为 CAP_STATE_VALID，
 *          返回对应 cap_t 指针。
 */
cap_t *cspace_lookup(cspace_t *cspace, cap_slot_t slot)
{
    cap_t *cap;

    if (cspace == NULL)
    {
        return NULL;
    }

    /* 检查 slot 范围 */
    if (slot >= cspace->capacity)
    {
        return NULL;
    }

    cap = &cspace->cap_table[slot];

    /* 检查能力状态 */
    if (cap->state != CAP_STATE_VALID)
    {
        return NULL;
    }

    return cap;
}

/**
 * @brief 从槽索引解析 CSpace
 *
 * @details 遍历 s_cspace_pool 查找 header.id 匹配的 CSpace。
 *          此为简化实现，生产环境应使用更高效的映射。
 */
cspace_t *cspace_from_root(cap_slot_t cspace_root)
{
    uint32_t i;
    cspace_t *cspace;

    /* 遍历 CSpace 池查找匹配的根能力 */
    for (i = 0U; i < (uint32_t)CONFIG_MAX_CSPACES; i++)
    {
        cspace = &s_cspace_pool[i];

        /* 检查 CSpace 是否已初始化（类型为 KOBJ_CSPACE 且有有效 ID） */
        if ((cspace->header.type == KOBJ_CSPACE) &&
            (cspace->header.id != KOBJ_ID_INVALID) &&
            (cspace->root_slot == cspace_root))
        {
            return cspace;
        }
    }

    return NULL;
}

/**
 * @brief 获取当前线程的 CSpace
 *
 * @details 简化实现：返回 NULL。
 *          完整实现应从当前线程的 TCB 中获取关联的 CSpace。
 */
cspace_t *cspace_get_current(void)
{
    /* TODO: 从当前线程 TCB 获取关联的 CSpace */
    return NULL;
}

/* ========================================================================
 * 内部辅助函数实现
 * ======================================================================== */

/**
 * @brief 从 CSpace 静态池分配一个空闲 CSpace
 */
static kernel_status_t cspace_pool_alloc(cspace_t **out_cspace)
{
    uint32_t idx;

    if (out_cspace == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (s_cspace_free_count == 0U)
    {
        return -(int32_t)ENOMEM;
    }

    /* 从栈顶弹出一个空闲索引 */
    s_cspace_free_count--;
    idx = s_cspace_free_stack[s_cspace_free_count];

    *out_cspace = &s_cspace_pool[idx];

    return KERNEL_OK;
}

/**
 * @brief 将 CSpace 释放回静态池
 */
static void cspace_pool_free(cspace_t *cspace)
{
    uint32_t idx;

    if (cspace == NULL)
    {
        return;
    }

    /* 检查池已满 */
    if (s_cspace_free_count >= (uint32_t)CONFIG_MAX_CSPACES)
    {
        return;
    }

    /* 计算池索引 */
    idx = (uint32_t)(cspace - &s_cspace_pool[0U]);

    /* 验证索引有效性 */
    if (idx >= (uint32_t)CONFIG_MAX_CSPACES)
    {
        return;
    }

    /* 清零 CSpace 结构 */
    (void)memset(cspace, 0, sizeof(cspace_t));

    /* 压入空闲栈 */
    s_cspace_free_stack[s_cspace_free_count] = idx;
    s_cspace_free_count++;
}

/**
 * @brief 初始化能力表
 *
 * @details 将所有 cap_t 设为 CAP_STATE_FREE，
 *          构建 slot 1 ~ capacity-1 的空闲链表。
 *          使用 cap_t 的 sibling.next 字段串联空闲节点。
 *
 * @note slot 0 保留给根能力，不加入空闲链表。
 */
static void cspace_init_cap_table(cspace_t *cspace)
{
    uint32_t i;
    cap_t *cap;

    /* 清零整个能力表 */
    (void)memset(cspace->cap_table, 0, sizeof(cap_t) * cspace->capacity);

    /* 将所有 cap_t 初始化为空闲状态 */
    for (i = 0U; i < cspace->capacity; i++)
    {
        cap = &cspace->cap_table[i];
        cap->state = CAP_STATE_FREE;
        INIT_LIST_HEAD(&cap->children);
        INIT_LIST_HEAD(&cap->sibling);
    }

    /* 构建空闲链表（slot 1 ~ capacity-1）：
     * 使用 sibling.next 指向下一个空闲 cap_t 的地址。
     * free_head 指向第一个空闲 slot（slot 1）。 */
    for (i = 1U; i < cspace->capacity; i++)
    {
        cap = &cspace->cap_table[i];

        if (i + 1U < cspace->capacity)
        {
            cap->sibling.next = (struct list_head *)&cspace->cap_table[i + 1U];
        }
        else
        {
            /* 最后一个空闲 slot */
            cap->sibling.next = NULL;
        }
    }

    /* 设置空闲链表头（slot 1，因为 slot 0 保留给根能力） */
    if (cspace->capacity > 1U)
    {
        cspace->free_head = 1U;
    }
    else
    {
        cspace->free_head = CAP_SLOT_INVALID;
    }
}

/**
 * @brief 在 CSpace 中创建根能力
 *
 * @details 在 slot 0 创建指向 CSpace 自身的能力。
 *          根能力拥有全部权限（CAP_RIGHT_ALL），
 *          无父能力（parent_slot = CAP_SLOT_INVALID）。
 */
static kernel_status_t cspace_create_root_cap(cspace_t *cspace)
{
    cap_t *root_cap;

    if (cspace == NULL)
    {
        return -(int32_t)EINVAL;
    }

    root_cap = &cspace->cap_table[0U];

    /* 填充根能力字段 */
    root_cap->state = CAP_STATE_VALID;
    root_cap->kobj_type = KOBJ_CSPACE;
    root_cap->rights = (uint8_t)CAP_RIGHT_ALL;
    root_cap->badge = 0U;
    root_cap->kobj_id = cspace->header.id;
    root_cap->parent_slot = CAP_SLOT_INVALID;
    root_cap->cspace_root = 0U; /* 根能力的 cspace_root 指向自身 slot 0 */

    /* 初始化子能力链表 */
    INIT_LIST_HEAD(&root_cap->children);
    INIT_LIST_HEAD(&root_cap->sibling);

    /* 记录根能力所在 slot */
    cspace->root_slot = 0U;

    /* 更新使用计数 */
    cspace->used_count = 1U;

    return KERNEL_OK;
}
