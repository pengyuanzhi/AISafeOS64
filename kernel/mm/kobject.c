/**
 * @file    kobject.c
 * @brief   内核对象统一类型系统实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件实现了微内核的统一内核对象类型系统：
 *          - 内核对象子系统的初始化和销毁
 *          - 原子引用计数管理（多核安全）
 *          - 父子对象关系与级联销毁
 *          - 全局对象链表的查找、枚举和泄漏检测
 *
 *          设计要点：
 *          - 每种内核对象类型拥有独立的对象池（object_pool_t）
 *          - 全局链表串联所有活跃对象，支持按类型查找
 *          - 使用 TicketLock_t 保护共享数据结构
 *          - 引用计数使用 GCC __atomic 内建函数保证原子性
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-017~022
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */

#include <kernel/kobject.h>
#include <kernel/object_pool.h>
#include <kernel/errno.h>
#include <kernel/barrier.h>
#include <kernel/config.h>
#include <kernel/compiler.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* ========================================================================
 * 编译时配置与静态断言
 * ======================================================================== */

/**
 * @def KOBJ_POOL_OBJ_SIZE
 * @brief 对象池中单个对象的大小（字节）
 *
 * @details 由于没有 thread_t 等具体结构体定义，
 *          统一使用 sizeof(KObjHeader_t) + 64 字节预留空间。
 *          实际对象头部放在起始位置，剩余空间供具体类型使用。
 */
#define KOBJ_POOL_OBJ_SIZE  (sizeof(KObjHeader_t) + 64U)

/**
 * @def KOBJ_POOL_CAPACITY
 * @brief 每种对象类型的对象池容量
 *
 * @details 每种内核对象类型最多可同时存在的对象数量。
 *          设为 32 以节省内存，可根据需求调整。
 */
#define KOBJ_POOL_CAPACITY  32U

/* 静态断言：对象大小必须能容纳 KObjHeader_t */
static_assert(KOBJ_POOL_OBJ_SIZE >= sizeof(KObjHeader_t),
              "KOBJ_POOL_OBJ_SIZE must be at least sizeof(KObjHeader_t)");

/* 静态断言：容量必须大于 0 */
static_assert(KOBJ_POOL_CAPACITY > 0U,
              "KOBJ_POOL_CAPACITY must be greater than 0");

/* ========================================================================
 * 全局状态定义
 * ======================================================================== */

/**
 * @brief 每种内核对象类型对应的对象池
 *
 * @details 数组索引对应 kobj_type_t 枚举值。
 *          在 kobject_subsys_init() 中统一初始化。
 */
static object_pool_t s_object_pools[KOBJ_TYPE_COUNT];

/**
 * @brief 全局对象链表头
 *
 * @details 所有活跃的内核对象都通过 global_node 字段
 *          链接到此链表上，用于查找、枚举和泄漏检测。
 */
static struct list_head s_global_object_list;

/**
 * @brief 原子对象 ID 计数器
 *
 * @details 每次分配新对象时递增，保证全局唯一。
 *          使用 volatile 确保编译器不缓存其值。
 */
static volatile uint32_t s_next_id;

/**
 * @brief 子系统自旋锁
 *
 * @details 保护 s_global_object_list 和 s_next_id 的并发访问。
 *          所有操作全局链表的函数必须在持锁状态下进行。
 */
static TicketLock_t s_subsys_lock;

/* ========================================================================
 * 静态缓冲区定义
 * ======================================================================== */

/**
 * @brief 每种对象类型的对象池内存缓冲区
 *
 * @details 每种类型独立分配 KOBJ_POOL_CAPACITY 个对象的空间。
 *          缓冲区对齐到 16 字节（ARM64 ABI 要求）。
 */
static ALIGNED(16) uint8_t
    s_pool_buffers[KOBJ_TYPE_COUNT][KOBJ_POOL_OBJ_SIZE * KOBJ_POOL_CAPACITY];

/**
 * @brief 每种对象类型的空闲索引栈
 *
 * @details 每个空闲栈容量为 KOBJ_POOL_CAPACITY 个 uint32_t 条目，
 *          供 object_pool_t 的 free_stack 字段使用。
 */
static uint32_t
    s_free_stacks[KOBJ_TYPE_COUNT][KOBJ_POOL_CAPACITY];

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 分配全局唯一的对象 ID
 *
 * @details 原子递增 s_next_id 计数器并返回新值。
 *          ID 从 1 开始（0 为无效 ID KOBJ_ID_INVALID）。
 *
 * @return 新分配的对象 ID
 *
 * @note 多核安全
 */
static kobj_id_t __attribute__((unused)) kobj_alloc_id(void)
{
    uint32_t old_id;

    /* 原子递增，返回旧值 */
    old_id = atomic_inc_u32(&s_next_id);

    /* 如果 old_id 为 0（首次分配），s_next_id 变为 1，直接返回 1 */
    /* 否则返回 old_id + 1 */
    return (kobj_id_t)(old_id + 1U);
}

/* ========================================================================
 * 公共 API 实现
 * ======================================================================== */

/**
 * @brief 初始化内核对象子系统
 *
 * @details 初始化所有对象类型的对象池、全局链表和自旋锁。
 *          每种类型的对象池使用预分配的静态缓冲区和空闲栈。
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效（不应发生）
 *
 * @note 对应需求: KR-017
 * @note 必须在系统启动时调用，且仅调用一次
 */
kernel_status_t kobject_subsys_init(void)
{
    uint32_t type_idx;
    kernel_status_t ret;

    /* 初始化全局链表 */
    INIT_LIST_HEAD(&s_global_object_list);

    /* 初始化原子 ID 计数器 */
    s_next_id = 0U;

    /* 初始化子系统锁 */
    ticket_lock_init(&s_subsys_lock);

    /* 初始化每种对象类型的对象池 */
    for (type_idx = 0U; type_idx < (uint32_t)KOBJ_TYPE_COUNT; type_idx++)
    {
        ret = object_pool_init(
            &s_object_pools[type_idx],
            &s_pool_buffers[type_idx][0],
            (uint32_t)KOBJ_POOL_OBJ_SIZE,
            (uint32_t)KOBJ_POOL_CAPACITY,
            &s_free_stacks[type_idx][0]
        );

        if (ret != KERNEL_OK)
        {
            return ret;
        }
    }

    return KERNEL_OK;
}

/**
 * @brief 初始化内核对象头部
 *
 * @details 设置对象类型、ID、初始引用计数和父对象 ID。
 *          初始化链表节点为空状态。
 *
 * @param obj       对象头部指针（不得为 NULL）
 * @param type      对象类型
 * @param id        对象 ID
 * @param parent_id 父对象 ID（KOBJ_ID_INVALID 表示无父对象）
 *
 * @note 对应需求: KR-017, KR-018
 */
void kobj_header_init(KObjHeader_t *obj,
                       kobj_type_t type,
                       kobj_id_t id,
                       kobj_id_t parent_id)
{
    if (obj == NULL)
    {
        return;
    }

    /* 清零整个对象区域 */
    (void)memset(obj, 0, sizeof(KObjHeader_t));

    /* 设置基本属性 */
    obj->type       = type;
    obj->id         = id;
    obj->ref_count  = 1;
    obj->parent_id  = parent_id;

    /* 初始化链表节点 */
    INIT_LIST_HEAD(&obj->children);
    INIT_LIST_HEAD(&obj->sibling);
    INIT_LIST_HEAD(&obj->global_node);

    /* 内存屏障确保所有写入对其他核可见 */
    barrier();
}

/**
 * @brief 增加对象引用计数
 *
 * @details 原子地将 ref_count 加 1，多核安全。
 *          使用 GCC __atomic_add_fetch 内建函数。
 *
 * @param obj 内核对象头部指针（不得为 NULL）
 *
 * @return 增加后的引用计数
 *
 * @note 原子操作，多核安全
 * @note 对应需求: KR-018
 */
int32_t kobj_ref_inc(KObjHeader_t *obj)
{
    int32_t new_count;

    if (obj == NULL)
    {
        return 0;
    }

    /*
     * 使用 __atomic_add_fetch 原子递增 ref_count。
     * __ATOMIC_ACQ_REL 提供获取-释放语义：
     * - 获取：保证后续读操作在原子操作之后被观测
     * - 释放：保证之前的写操作对其他核可见
     */
    new_count = __atomic_add_fetch(&obj->ref_count, 1, __ATOMIC_ACQ_REL);

    return new_count;
}

/**
 * @brief 减少对象引用计数
 *
 * @details 原子地将 ref_count 减 1。当引用计数归零时，
 *          自动调用 kobj_destroy() 销毁对象并级联处理子对象。
 *
 * @param obj 内核对象头部指针（不得为 NULL）
 *
 * @return 减少后的引用计数
 *
 * @note 原子操作，多核安全
 * @note 对应需求: KR-018, KR-019
 */
int32_t kobj_ref_dec(KObjHeader_t *obj)
{
    int32_t new_count;

    if (obj == NULL)
    {
        return 0;
    }

    /*
     * 使用 __atomic_sub_fetch 原子递减 ref_count。
     * 归零时触发销毁流程。
     */
    new_count = __atomic_sub_fetch(&obj->ref_count, 1, __ATOMIC_ACQ_REL);

    if (new_count <= 0)
    {
        kobj_destroy(obj);
    }

    return new_count;
}

/**
 * @brief 获取对象当前引用计数
 *
 * @param obj 内核对象头部指针
 *
 * @return 当前引用计数，obj 为 NULL 时返回 0
 */
int32_t kobj_ref_count(const KObjHeader_t *obj)
{
    int32_t count;

    if (obj == NULL)
    {
        return 0;
    }

    /* 使用 __atomic_load 原子读取 */
    count = __atomic_load_n(&obj->ref_count, __ATOMIC_ACQUIRE);

    return count;
}

/**
 * @brief 添加子对象
 *
 * @details 将 child 添加到 parent 的 children 链表中，
 *          同时设置 child 的 parent_id。
 *          使用子系统锁保护链表操作。
 *
 * @param parent 父对象（不得为 NULL）
 * @param child  子对象（不得为 NULL）
 *
 * @note 对应需求: KR-019
 */
void kobj_add_child(KObjHeader_t *parent, KObjHeader_t *child)
{
    if ((parent == NULL) || (child == NULL))
    {
        return;
    }

    ticket_lock_acquire(&s_subsys_lock);

    /* 设置父对象 ID */
    child->parent_id = parent->id;

    /* 将子对象添加到父对象的 children 链表尾部 */
    list_add_tail(&child->sibling, &parent->children);

    ticket_lock_release(&s_subsys_lock);
}

/**
 * @brief 移除子对象
 *
 * @details 将 child 从 parent 的 children 链表中移除，
 *          同时将 child 的 parent_id 重置为无效。
 *
 * @param parent 父对象（不得为 NULL）
 * @param child  子对象（不得为 NULL）
 */
void kobj_remove_child(KObjHeader_t *parent, KObjHeader_t *child)
{
    if ((parent == NULL) || (child == NULL))
    {
        return;
    }

    ticket_lock_acquire(&s_subsys_lock);

    /* 从兄弟链表中移除 */
    list_del_init(&child->sibling);

    /* 重置父对象 ID */
    child->parent_id = KOBJ_ID_INVALID;

    ticket_lock_release(&s_subsys_lock);
}

/**
 * @brief 销毁对象（内部调用）
 *
 * @details 引用计数归零时由 kobj_ref_dec 调用。执行以下操作：
 *          1. 遍历 children 链表，级联减少每个子对象的引用计数
 *          2. 从全局对象链表中移除
 *          3. 清零对象内存
 *          4. 释放回对象池
 *
 * @param obj 要销毁的对象
 *
 * @note 对应需求: KR-019
 * @warning 必须在子系统锁保护下调用全局链表操作
 *
 * P0-3 修复说明（违反 MISRA C:2012 规则 17.2 "函数不得递归调用"）：
 *   原实现中 kobj_ref_dec -> kobj_destroy -> (遍历 children)
 *   -> kobj_ref_dec -> kobj_destroy 形成间接递归，深层对象树会
 *   耗尽栈空间。现已改为完全迭代式：使用显式数组当工作栈，
 *   不再调用 kobj_ref_dec。对每个子对象直接做引用计数原子递减，
 *   归零则压栈继续销毁其孙对象。
 */
void kobj_destroy(KObjHeader_t *obj)
{
    /*
     * 显式工作栈（迭代式级联销毁，避免递归）。
     * 栈深度上限：理论上等于对象树最大深度。此处取与每种类型对象池
     * 容量同量级的上限，足以覆盖最深的父子链。
     */
    #define KOBJ_DESTROY_STACK_SIZE  (KOBJ_POOL_CAPACITY * (uint32_t)KOBJ_TYPE_COUNT + 1U)

    KObjHeader_t *destroy_stack[KOBJ_DESTROY_STACK_SIZE];
    uint32_t stack_top;
    KObjHeader_t *current_obj;
    kobj_type_t obj_type;

    if (obj == NULL)
    {
        return;
    }

    /* 类型边界检查 */
    if ((uint32_t)obj->type >= (uint32_t)KOBJ_TYPE_COUNT)
    {
        return;
    }

    /* 压入初始待销毁对象 */
    destroy_stack[0U] = obj;
    stack_top = 1U;

    /*
     * 迭代处理栈上的对象，直到栈空。
     * 每个对象出栈后：把它所有 children 的引用计数原子递减，
     * 归零的 child 压栈等待销毁（不再调用 kobj_ref_dec，避免递归）。
     */
    while (stack_top > 0U)
    {
        stack_top--;
        current_obj = destroy_stack[stack_top];

        /* 二次校验类型边界（防御） */
        if ((uint32_t)current_obj->type >= (uint32_t)KOBJ_TYPE_COUNT)
        {
            continue;
        }
        obj_type = current_obj->type;

        ticket_lock_acquire(&s_subsys_lock);

        /*
         * 处理当前对象的所有子对象。
         * 直接对每个 child 做引用计数原子递减 + 归零判断 + 压栈，
         * 全部迭代完成，绝不调用 kobj_ref_dec。
         */
        while (!list_empty(&current_obj->children))
        {
            struct list_head *pos;
            KObjHeader_t *child;
            int32_t child_new_count;

            pos = current_obj->children.next;
            child = list_entry(pos, KObjHeader_t, sibling);

            /* 从兄弟链表摘除并重置父 ID */
            list_del_init(&child->sibling);
            child->parent_id = KOBJ_ID_INVALID;

            /*
             * 原子递减 child 引用计数（与 kobj_ref_dec 等价的单步操作）。
             * 锁仅保护链表结构，引用计数本身靠原子操作保证多核安全。
             */
            child_new_count = __atomic_sub_fetch(&child->ref_count, 1,
                                                 __ATOMIC_ACQ_REL);

            if (child_new_count <= 0)
            {
                /* 归零：压栈等待销毁其孙对象 */
                if (stack_top < KOBJ_DESTROY_STACK_SIZE)
                {
                    destroy_stack[stack_top] = child;
                    stack_top++;
                }
                /* 栈溢出时不压入，避免越界（理论上不应发生） */
            }
        }

        /* 从全局对象链表中移除 */
        list_del_init(&current_obj->global_node);

        ticket_lock_release(&s_subsys_lock);

        /* 清零对象内存（释放前清除敏感数据） */
        (void)memset(current_obj, 0, KOBJ_POOL_OBJ_SIZE);

        /* 释放回对应类型的对象池 */
        object_pool_free(&s_object_pools[obj_type], (void *)current_obj);
    }
}

/**
 * @brief 根据类型和 ID 查找内核对象
 *
 * @details 遍历全局对象链表，按指定的类型和 ID 查找匹配的对象。
 *          时间复杂度 O(n)，适用于低频管理操作。
 *
 * @param type 对象类型
 * @param id   对象 ID
 *
 * @return 匹配的对象头部指针，未找到返回 NULL
 */
KObjHeader_t *kobj_find(kobj_type_t type, kobj_id_t id)
{
    struct list_head *pos;
    KObjHeader_t *obj;
    KObjHeader_t *result;

    result = NULL;

    ticket_lock_acquire(&s_subsys_lock);

    list_for_each(pos, &s_global_object_list)
    {
        obj = list_entry(pos, KObjHeader_t, global_node);

        if ((obj->type == type) && (obj->id == id))
        {
            result = obj;
            break;
        }
    }

    ticket_lock_release(&s_subsys_lock);

    return result;
}

/**
 * @brief 检查对象类型是否匹配
 *
 * @param obj  对象指针
 * @param type 期望的类型
 *
 * @return true 类型匹配，false 不匹配或 obj 为 NULL
 */
bool kobj_check_type(const KObjHeader_t *obj, kobj_type_t type)
{
    if (obj == NULL)
    {
        return false;
    }

    return (obj->type == type);
}

/**
 * @brief 枚举所有活跃内核对象
 *
 * @details 遍历全局对象链表，统计指定类型的活跃对象数量。
 *          当 type 为 KOBJ_TYPE_COUNT 时统计所有类型。
 *
 * @param type  要枚举的类型（KOBJ_TYPE_COUNT 表示所有类型）
 * @param count 输出活跃对象数量（不得为 NULL）
 *
 * @note 对应需求: KR-022
 */
void kobj_enum_active(kobj_type_t type, uint32_t *count)
{
    struct list_head *pos;
    KObjHeader_t *obj;

    if (count == NULL)
    {
        return;
    }

    *count = 0U;

    ticket_lock_acquire(&s_subsys_lock);

    list_for_each(pos, &s_global_object_list)
    {
        obj = list_entry(pos, KObjHeader_t, global_node);

        if ((type == KOBJ_TYPE_COUNT) || (obj->type == type))
        {
            (*count)++;
        }
    }

    ticket_lock_release(&s_subsys_lock);
}

/**
 * @brief 检测孤立对象（无父对象且无引用）
 *
 * @details 遍历全局对象链表，查找满足以下条件的对象：
 *          - parent_id == KOBJ_ID_INVALID（无父对象）
 *          - ref_count <= 0（引用计数非正）
 *
 *          这些对象可能是泄漏或管理异常的对象。
 *
 * @param count 输出孤立对象数量（不得为 NULL）
 *
 * @note 对应需求: KR-022
 */
void kobj_detect_orphans(uint32_t *count)
{
    struct list_head *pos;
    KObjHeader_t *obj;
    int32_t ref;

    if (count == NULL)
    {
        return;
    }

    *count = 0U;

    ticket_lock_acquire(&s_subsys_lock);

    list_for_each(pos, &s_global_object_list)
    {
        obj = list_entry(pos, KObjHeader_t, global_node);

        /* 读取原子引用计数 */
        ref = __atomic_load_n(&obj->ref_count, __ATOMIC_ACQUIRE);

        if ((obj->parent_id == KOBJ_ID_INVALID) && (ref <= 0))
        {
            (*count)++;
        }
    }

    ticket_lock_release(&s_subsys_lock);
}
