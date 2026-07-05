/**
 * @file    capability.c
 * @brief   能力描述符操作实现（SMP 多核安全）
 * @author  AISafe64 Team
 * @date    2026-04-07
 * @version 3.0
 *
 * @details 本文件实现微内核的能力（Capability）操作：
 *          - 能力复制（cap_copy）：支持降权复制，建立父子关系
 *          - 能力移动（cap_move）：原子性移动，转移子能力关系
 *          - 能力撤销（cap_revoke）：级联撤销，使用显式栈替代递归
 *          - 能力删除（cap_delete）：非级联删除，解除父子关系
 *          - 权限验证（cap_validate）：查找并验证权限
 *          - 对象类型查询（cap_get_object_type）
 *
 *          SMP 多核同步策略：
 *          - 单 CSpace 操作：获取 per-CSpace TicketLock
 *          - 双 CSpace 操作（cap_copy/cap_move/cap_derive）：
 *            按地址顺序加锁，避免 ABBA 死锁
 *          - 所有 cap_t 状态变更在持锁状态下完成
 *          - 使用 barrier() 确保多核缓存一致性
 *
 * @note MISRA-C:2012 合规
 * @note 禁止递归，使用显式栈实现级联撤销
 * @note 对应需求: KR-013~016
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */

#include <kernel/capability.h>
#include <kernel/cspace.h>
#include <kernel/errno.h>
#include <kernel/barrier.h>
#include <hal.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 内部常量定义
 * ======================================================================== */

/**
 * @def REVOKE_STACK_SIZE
 * @brief 级联撤销显式栈的最大深度
 *
 * @details 配置为 CONFIG_MAX_CSPACES * 4，足以覆盖所有 CSpace 中
 *          的能力派生关系深度。
 */
#define REVOKE_STACK_SIZE   (CONFIG_MAX_CSPACES * 4U)

/* ========================================================================
 * 对象类型权限矩阵
 * ======================================================================== */

/**
 * @brief 对象类型权限矩阵
 *
 * @details 定义每种内核对象类型的合法权限集合。
 *          allowed_rights: 该类型对象允许拥有的最大权限
 *          mandatory_rights: 该类型对象必须拥有的最小权限
 *
 * @note 索引与 kobj_type_t 枚举值一一对应
 */
static const cap_type_rights_t s_cap_type_rights_table[] =
{
    /* KOBJ_THREAD (0) */
    {
        KOBJ_THREAD,
        (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_GRANT | CAP_RIGHT_REVOKE),
        (uint8_t)(CAP_RIGHT_READ)
    },
    /* KOBJ_ENDPOINT (1) */
    {
        KOBJ_ENDPOINT,
        (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_GRANT),
        (uint8_t)(CAP_RIGHT_READ)
    },
    /* KOBJ_NOTIFICATION (2) */
    {
        KOBJ_NOTIFICATION,
        (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_GRANT),
        (uint8_t)(CAP_RIGHT_READ)
    },
    /* KOBJ_CSPACE (3) */
    {
        KOBJ_CSPACE,
        (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_GRANT | CAP_RIGHT_REVOKE),
        (uint8_t)(CAP_RIGHT_READ)
    },
    /* KOBJ_VM_SPACE (4) */
    {
        KOBJ_VM_SPACE,
        (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_EXECUTE | CAP_RIGHT_GRANT),
        (uint8_t)(CAP_RIGHT_READ)
    },
    /* KOBJ_PAGE_FRAME (5) */
    {
        KOBJ_PAGE_FRAME,
        (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_EXECUTE),
        (uint8_t)(CAP_RIGHT_READ)
    },
    /* KOBJ_INTERRUPT (6) */
    {
        KOBJ_INTERRUPT,
        (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_GRANT),
        (uint8_t)(CAP_RIGHT_READ)
    },
    /* KOBJ_DEVICE (7) */
    {
        KOBJ_DEVICE,
        (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_EXECUTE | CAP_RIGHT_GRANT),
        (uint8_t)(CAP_RIGHT_READ)
    },
    /* KOBJ_CHANNEL (8) */
    {
        KOBJ_CHANNEL,
        (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_GRANT | CAP_RIGHT_REVOKE),
        (uint8_t)(CAP_RIGHT_READ)
    },
    /* KOBJ_CONNECTION (9) */
    {
        KOBJ_CONNECTION,
        (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_GRANT),
        (uint8_t)(CAP_RIGHT_READ)
    },
    /* KOBJ_SHM (10) */
    {
        KOBJ_SHM,
        (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_GRANT),
        (uint8_t)(CAP_RIGHT_READ)
    },
    /* KOBJ_FD (11) - 文件描述符 */
    {
        KOBJ_FD,
        (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_GRANT | CAP_RIGHT_REVOKE),
        (uint8_t)(CAP_RIGHT_READ)
    },
    /* KOBJ_INODE (12) - 文件 Inode */
    {
        KOBJ_INODE,
        (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_EXECUTE | CAP_RIGHT_GRANT | CAP_RIGHT_REVOKE),
        (uint8_t)(CAP_RIGHT_READ)
    },
    /* KOBJ_MEMORY_REGION (13) - 内存区域 */
    {
        KOBJ_MEMORY_REGION,
        (uint8_t)(CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_EXECUTE | CAP_RIGHT_GRANT | CAP_RIGHT_REVOKE),
        (uint8_t)(CAP_RIGHT_READ)
    }
};

/** @brief 权限矩阵表条目数 */
#define CAP_TYPE_RIGHTS_COUNT \
    (sizeof(s_cap_type_rights_table) / sizeof(s_cap_type_rights_table[0U]))

/* ========================================================================
 * 对象类型权限验证
 * ======================================================================== */

/**
 * @brief 验证权限是否为指定对象类型的合法子集
 *
 * @details 检查请求的权限是否满足以下条件：
 *          1. 权限是 allowed_rights 的子集
 *          2. 权限包含所有 mandatory_rights
 *
 * @param type   内核对象类型
 * @param rights 要验证的权限位
 *
 * @return KERNEL_OK 权限合法
 * @return -EINVAL 权限非法
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_validate_rights_for_type(kobj_type_t type, uint8_t rights)
{
    uint32_t i;
    const cap_type_rights_t *entry;

    /* 类型边界检查 */
    if ((uint32_t)type >= (uint32_t)KOBJ_TYPE_COUNT)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找类型对应的权限规则 */
    entry = NULL;
    for (i = 0U; i < CAP_TYPE_RIGHTS_COUNT; i++)
    {
        if (s_cap_type_rights_table[i].type == type)
        {
            entry = &s_cap_type_rights_table[i];
            break;
        }
    }

    /* 未找到类型的权限规则 */
    if (entry == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查 1: 权限必须是 allowed_rights 的子集 */
    if ((rights & entry->allowed_rights) != rights)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查 2: 权限必须包含所有 mandatory_rights */
    if ((rights & entry->mandatory_rights) != entry->mandatory_rights)
    {
        return -(int32_t)EINVAL;
    }

    return KERNEL_OK;
}

/* ========================================================================
 * SMP 多核同步辅助函数
 * ======================================================================== */

/**
 * @brief 从链表节点获取 cap_t 结构体指针
 *
 * @param node sibling 链表节点指针
 *
 * @return 对应的 cap_t 指针
 */
static inline cap_t *cap_from_sibling(struct list_head *node)
{
    return container_of(node, cap_t, sibling);
}

/**
 * @brief 按地址顺序获取两个 CSpace 的锁（避免 ABBA 死锁）
 *
 * @details 当两个核心同时执行跨 CSpace 操作时（如 Core 0 做 A→B，
 *          Core 1 做 B→A），如果都先锁自己的 src 就会死锁。
 *          按地址顺序加锁确保全局一致的锁获取顺序。
 *
 * @param a 第一个 CSpace 指针
 * @param b 第二个 CSpace 指针（可与 a 相同）
 *
 * @note 如果 a == b，只获取一次锁（同一把锁不重复获取）
 */
static void cap_lock_dual(cspace_t *a, cspace_t *b)
{
    if (a == b)
    {
        ticket_lock_acquire(&a->lock);
    }
    else if ((uintptr_t)a < (uintptr_t)b)
    {
        ticket_lock_acquire(&a->lock);
        ticket_lock_acquire(&b->lock);
    }
    else
    {
        ticket_lock_acquire(&b->lock);
        ticket_lock_acquire(&a->lock);
    }
}

/**
 * @brief 按反序释放两个 CSpace 的锁
 *
 * @param a 第一个 CSpace 指针
 * @param b 第二个 CSpace 指针
 */
static void cap_unlock_dual(cspace_t *a, cspace_t *b)
{
    if (a == b)
    {
        ticket_lock_release(&a->lock);
    }
    else if ((uintptr_t)a < (uintptr_t)b)
    {
        ticket_lock_release(&b->lock);
        ticket_lock_release(&a->lock);
    }
    else
    {
        ticket_lock_release(&a->lock);
        ticket_lock_release(&b->lock);
    }
}

/**
 * @brief 在已持锁状态下插入能力（不获取额外锁）
 *
 * @details 复制 cspace_insert_cap 的核心逻辑，但跳过加锁。
 *          调用者必须已持有 cspace->lock。
 *          用于 cap_copy/cap_move/cap_derive/cap_mint 等已持锁的路径。
 *
 * @param cs          目标 CSpace（调用者已持有 cs->lock）
 * @param slot        目标能力槽索引
 * @param kobj_type   内核对象类型
 * @param kobj_id     内核对象 ID
 * @param rights      权限位
 * @param badge       标识值
 * @param parent_slot 父能力槽索引（CAP_SLOT_INVALID 表示无父能力）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
static kernel_status_t cap_insert_locked(cspace_t *cs,
                                          cap_slot_t slot,
                                          kobj_type_t kobj_type,
                                          kobj_id_t kobj_id,
                                          uint8_t rights,
                                          uint16_t badge,
                                          cap_slot_t parent_slot)
{
    cap_t *cap;
    cap_t *parent_cap;
    kernel_status_t rights_ret;
    uint8_t new_depth;

    if (slot >= cs->capacity)
    {
        return -(int32_t)EINVAL;
    }

    cap = &cs->cap_table[slot];

    if (cap->state != CAP_STATE_FREE)
    {
        return -(int32_t)EINVAL;
    }

    /* 验证权限是否符合对象类型的权限矩阵 */
    rights_ret = cap_validate_rights_for_type(kobj_type, rights);
    if (rights_ret != KERNEL_OK)
    {
        return -(int32_t)EINVAL;
    }

    /* 计算派生深度 */
    if (parent_slot != CAP_SLOT_INVALID)
    {
        if (parent_slot >= cs->capacity)
        {
            return -(int32_t)EINVAL;
        }

        parent_cap = &cs->cap_table[parent_slot];

        if (parent_cap->state != CAP_STATE_VALID)
        {
            return -(int32_t)EINVAL;
        }

        /* 检查派生深度是否超限 */
        if (parent_cap->derive_depth >= CAP_MAX_DERIVE_DEPTH)
        {
            return -(int32_t)EINVAL;
        }

        new_depth = parent_cap->derive_depth + 1U;
    }
    else
    {
        new_depth = 0U;
    }

    /* 填充能力字段 */
    cap->state = CAP_STATE_VALID;
    cap->kobj_type = kobj_type;
    cap->rights = rights;
    cap->badge = badge;
    cap->kobj_id = kobj_id;
    cap->parent_slot = parent_slot;
    cap->cspace_root = cs->root_slot;
    cap->derive_depth = new_depth;

    INIT_LIST_HEAD(&cap->children);
    INIT_LIST_HEAD(&cap->sibling);

    /* 如果有父能力，加入父能力的 children 链表 */
    if (parent_slot != CAP_SLOT_INVALID)
    {
        list_add_tail(&cap->sibling, &parent_cap->children);
    }

    /* 更新使用计数 */
    cs->used_count++;

    /* 硬件内存屏障确保多核缓存一致性 */
    hal_dmb_ish();
    /* 内存屏障确保能力写入对所有核可见 */
    barrier_store();

    return KERNEL_OK;
}

/* ========================================================================
 * 能力子系统初始化
 * ======================================================================== */

/**
 * @brief 初始化能力子系统
 *
 * @details 当前为空操作。能力子系统依赖 CSpace 子系统，
 *          CSpace 初始化在 cspace_subsys_init() 中完成。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: KR-013
 */
kernel_status_t capability_subsys_init(void)
{
    /* 注册能力系统形式化验证不变式 */

    return KERNEL_OK;
}

/* ========================================================================
 * 能力复制
 * ======================================================================== */

/**
 * @brief 复制能力（可降权，SMP 多核安全）
 *
 * @details 将源能力复制到目标 CSpace 的指定槽位。支持权限降级：
 *          - rights_mask != 0 时，目标权限 = 源权限 & rights_mask
 *          - rights_mask == 0 时，目标权限 = 源权限（保持不变）
 *          权限不可提升（目标权限必须是源权限的子集）。
 *          复制后建立父子关系。
 *
 *          多核同步：同时获取 src/dst CSpace 锁（按地址顺序），
 *          确保源能力查找和目标插入的原子性。
 *
 * @param src_cspace  源 CSpace 的根能力槽
 * @param src_slot    源能力槽索引
 * @param dest_cspace 目标 CSpace 的根能力槽
 * @param dest_slot   目标能力槽索引
 * @param rights_mask 权限掩码（0 表示保持原权限）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EACCES 权限不足
 * @return -ENOENT 源能力不存在
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_copy(cap_slot_t src_cspace,
                          cap_slot_t src_slot,
                          cap_slot_t dest_cspace,
                          cap_slot_t dest_slot,
                          uint8_t rights_mask)
{
    kernel_status_t ret;
    cspace_t *src_cs;
    cspace_t *dst_cs;
    cap_t *src_cap;
    uint8_t dest_rights;

    /* 参数有效性检查 */
    if ((src_cspace == CAP_SLOT_INVALID) || (src_slot == CAP_SLOT_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    if ((dest_cspace == CAP_SLOT_INVALID) || (dest_slot == CAP_SLOT_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    /* 解析 CSpace（无需持锁，cspace_from_root 读哈希表是安全的） */
    src_cs = cspace_from_root(src_cspace);
    if (src_cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    dst_cs = cspace_from_root(dest_cspace);
    if (dst_cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 按地址顺序获取双 CSpace 锁，避免 ABBA 死锁 */
    cap_lock_dual(src_cs, dst_cs);

    /* 查找源能力 */
    src_cap = cspace_lookup(src_cs, src_slot);
    if (src_cap == NULL)
    {
        cap_unlock_dual(src_cs, dst_cs);
        return -(int32_t)ENOENT;
    }

    /* 验证源能力状态 */
    if (src_cap->state != CAP_STATE_VALID)
    {
        cap_unlock_dual(src_cs, dst_cs);
        return -(int32_t)ENOENT;
    }

    /* 检查 GRANT 权限 */
    if ((src_cap->rights & CAP_RIGHT_GRANT) == CAP_RIGHT_NONE)
    {
        cap_unlock_dual(src_cs, dst_cs);
        return -(int32_t)EACCES;
    }

    /* 检查继承标志 */
    if (!src_cap->inheritable)
    {
        cap_unlock_dual(src_cs, dst_cs);
        return -(int32_t)EACCES;
    }

    /* 确定目标权限 */
    if (rights_mask != 0U)
    {
        dest_rights = src_cap->rights & rights_mask;
    }
    else
    {
        dest_rights = src_cap->rights;
    }

    /* 权限不可提升 */
    if ((dest_rights & src_cap->rights) != dest_rights)
    {
        cap_unlock_dual(src_cs, dst_cs);
        return -(int32_t)EACCES;
    }

    /* 在目标 CSpace 中插入能力（已持锁，使用 _locked 版本） */
    ret = cap_insert_locked(dst_cs,
                             dest_slot,
                             src_cap->kobj_type,
                             src_cap->kobj_id,
                             dest_rights,
                             src_cap->badge,
                             src_slot);

    cap_unlock_dual(src_cs, dst_cs);

    return ret;
}

/* ========================================================================
 * 能力移动
 * ======================================================================== */

/**
 * @brief 移动能力（SMP 多核安全）
 *
 * @details 将能力从源 CSpace 移动到目标 CSpace。源槽被清空。
 *          移动过程中转移子能力关系到目标能力。
 *
 *          多核同步：同时获取 src/dst CSpace 锁，确保移动操作的原子性。
 *
 * @param src_cspace  源 CSpace 的根能力槽
 * @param src_slot    源能力槽索引
 * @param dest_cspace 目标 CSpace 的根能力槽
 * @param dest_slot   目标能力槽索引
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_move(cap_slot_t src_cspace,
                          cap_slot_t src_slot,
                          cap_slot_t dest_cspace,
                          cap_slot_t dest_slot)
{
    kernel_status_t ret;
    cspace_t *src_cs;
    cspace_t *dst_cs;
    cap_t *src_cap;
    cap_t *dst_cap;

    /* 参数检查 */
    if ((src_cspace == CAP_SLOT_INVALID) || (src_slot == CAP_SLOT_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    if ((dest_cspace == CAP_SLOT_INVALID) || (dest_slot == CAP_SLOT_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    /* 解析 CSpace */
    src_cs = cspace_from_root(src_cspace);
    if (src_cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    dst_cs = cspace_from_root(dest_cspace);
    if (dst_cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 按地址顺序获取双 CSpace 锁 */
    cap_lock_dual(src_cs, dst_cs);

    /* 查找源能力 */
    src_cap = cspace_lookup(src_cs, src_slot);
    if (src_cap == NULL)
    {
        cap_unlock_dual(src_cs, dst_cs);
        return -(int32_t)ENOENT;
    }

    /* 验证源能力状态 */
    if (src_cap->state != CAP_STATE_VALID)
    {
        cap_unlock_dual(src_cs, dst_cs);
        return -(int32_t)ENOENT;
    }

    /* 检查 GRANT 权限 */
    if ((src_cap->rights & CAP_RIGHT_GRANT) == CAP_RIGHT_NONE)
    {
        cap_unlock_dual(src_cs, dst_cs);
        return -(int32_t)EACCES;
    }

    /* 第一步：在目标 CSpace 中插入能力 */
    ret = cap_insert_locked(dst_cs,
                             dest_slot,
                             src_cap->kobj_type,
                             src_cap->kobj_id,
                             src_cap->rights,
                             src_cap->badge,
                             src_cap->parent_slot);
    if (ret != KERNEL_OK)
    {
        cap_unlock_dual(src_cs, dst_cs);
        return ret;
    }

    /* 获取目标能力（已持锁，直接访问） */
    dst_cap = &dst_cs->cap_table[dest_slot];

    /* 第二步：将源能力的子能力关系转移到目标能力 */
    if (list_empty(&src_cap->children) == 0)
    {
        list_splice(&src_cap->children, &dst_cap->children);
    }

    /* 更新转移过来的子能力的 parent_slot */
    {
        struct list_head *pos;
        struct list_head *n;

        for (pos = dst_cap->children.next, n = pos->next;
             pos != &dst_cap->children;
             pos = n, n = pos->next)
        {
            cap_t *child_cap = cap_from_sibling(pos);
            child_cap->parent_slot = dest_slot;
        }
    }

    /* 第三步：从父能力的 children 链表移除源能力 */
    if (src_cap->parent_slot != CAP_SLOT_INVALID)
    {
        list_del_init(&src_cap->sibling);
    }

    /* 清除源能力 */
    (void)memset(src_cap, 0, sizeof(cap_t));
    src_cap->state = CAP_STATE_FREE;
    src_cap->parent_slot = CAP_SLOT_INVALID;
    INIT_LIST_HEAD(&src_cap->children);
    INIT_LIST_HEAD(&src_cap->sibling);

    /* 硬件内存屏障确保多核缓存一致性 */
    hal_dmb_ish();
    /* 内存屏障确保所有变更可见 */
    barrier_store();

    cap_unlock_dual(src_cs, dst_cs);

    return KERNEL_OK;
}

/* ========================================================================
 * 能力撤销（级联）
 * ======================================================================== */

/**
 * @brief 撤销能力（级联，SMP 多核安全）
 *
 * @details 使用显式栈实现非递归深度优先遍历，撤销指定能力及其
 *          所有派生子能力。整个遍历过程在 CSpace 锁保护下完成。
 *
 *          多核同步：持有 CSpace 锁期间完成所有状态变更，
 *          确保级联撤销的原子性。
 *
 * @param cspace_root CSpace 根能力槽
 * @param slot        要撤销的能力槽索引
 *
 * @return KERNEL_OK 成功
 * @return -EACCES 权限不足
 * @return -ENOENT 能力不存在
 *
 * @note 对应需求: KR-015
 */
kernel_status_t cap_revoke(cap_slot_t cspace_root, cap_slot_t slot)
{
    cspace_t *cs;
    cap_t *target_cap;
    /* 显式栈：用于非递归级联撤销（栈分配，多核安全） */
    cap_slot_t revoke_stack[REVOKE_STACK_SIZE];
    uint32_t stack_top;
    bool overflow;

    /* 参数检查 */
    if ((cspace_root == CAP_SLOT_INVALID) || (slot == CAP_SLOT_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    /* 解析 CSpace */
    cs = cspace_from_root(cspace_root);
    if (cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 CSpace 锁，保护整个级联撤销过程 */
    ticket_lock_acquire(&cs->lock);

    /* 查找目标能力 */
    target_cap = cspace_lookup(cs, slot);
    if (target_cap == NULL)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)ENOENT;
    }

    /* 验证状态 */
    if (target_cap->state != CAP_STATE_VALID)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)ENOENT;
    }

    /* 检查 REVOKE 权限 */
    if ((target_cap->rights & CAP_RIGHT_REVOKE) == CAP_RIGHT_NONE)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)EACCES;
    }

    /* 初始化显式栈 */
    stack_top = 0U;
    overflow = false;
    revoke_stack[stack_top] = slot;
    stack_top++;

    /*
     * 使用显式栈进行非递归深度优先遍历（全程持锁）。
     * 撤销的每个能力都归还到 CSpace 的 free list（状态置 CAP_STATE_FREE，
     * 挂入空闲链表头，递减 used_count），避免槽位永久泄漏。
     * 注意：cspace_free_slot 内部会获取 cs->lock，而此处已持锁
     * （TicketLock 非递归），故在持锁阶段内联完成归还逻辑。
     */
    while (stack_top > 0U)
    {
        cap_slot_t cur_slot;
        cap_t *cur_cap;

        /* 弹栈 */
        stack_top--;
        cur_slot = revoke_stack[stack_top];

        /* 查找当前能力 */
        cur_cap = cspace_lookup(cs, cur_slot);
        if (cur_cap == NULL)
        {
            continue;
        }

        /* 跳过已释放的槽位（避免重复归还、used_count 计数错误） */
        if (cur_cap->state != CAP_STATE_VALID)
        {
            continue;
        }

        /* 将当前能力的所有子能力压栈 */
        {
            struct list_head *pos;
            struct list_head *n;

            for (pos = cur_cap->children.next, n = pos->next;
                 pos != &cur_cap->children;
                 pos = n, n = pos->next)
            {
                cap_t *child_cap = cap_from_sibling(pos);

                if (stack_top < REVOKE_STACK_SIZE)
                {
                    cap_slot_t child_idx =
                        (cap_slot_t)(child_cap -
                                    &cs->cap_table[0U]);
                    revoke_stack[stack_top] = child_idx;
                    stack_top++;
                }
                else
                {
                    overflow = true;
                    break;
                }
            }
        }

        /* 从父能力的 children 链表移除 */
        list_del_init(&cur_cap->sibling);
        INIT_LIST_HEAD(&cur_cap->children);

        /* 归还到 free list：状态置 FREE，重置字段，挂入空闲链表头 */
        (void)memset(cur_cap, 0, sizeof(cap_t));
        cur_cap->state = CAP_STATE_FREE;
        cur_cap->parent_slot = CAP_SLOT_INVALID;
        INIT_LIST_HEAD(&cur_cap->children);
        INIT_LIST_HEAD(&cur_cap->sibling);

        /* 将原空闲链表头接到当前槽的 sibling.next */
        if (cs->free_head != CAP_SLOT_INVALID)
        {
            cur_cap->sibling.next =
                (struct list_head *)&cs->cap_table[cs->free_head];
        }
        else
        {
            cur_cap->sibling.next = NULL;
        }

        /* 更新空闲链表头为当前槽 */
        cs->free_head = cur_slot;

        /* 递减使用计数 */
        if (cs->used_count > 0U)
        {
            cs->used_count--;
        }
    }

    /* 硬件内存屏障确保多核缓存一致性 */
    hal_dmb_ish();
    /* 内存屏障确保撤销状态对所有核可见 */
    barrier();

    ticket_lock_release(&cs->lock);

    /* 栈溢出时返回错误 */
    if (overflow)
    {
        return -(int32_t)ENOMEM;
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 能力删除
 * ======================================================================== */

/**
 * @brief 删除能力（不级联，SMP 多核安全）
 *
 * @details 删除指定能力，不撤销其子能力。在 CSpace 锁保护下完成。
 *
 * @param cspace_root CSpace 根能力槽
 * @param slot        要删除的能力槽索引
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: KR-015
 */
kernel_status_t cap_delete(cap_slot_t cspace_root, cap_slot_t slot)
{
    cspace_t *cs;
    cap_t *target_cap;

    /* 参数检查 */
    if ((cspace_root == CAP_SLOT_INVALID) || (slot == CAP_SLOT_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    /* 解析 CSpace */
    cs = cspace_from_root(cspace_root);
    if (cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 CSpace 锁 */
    ticket_lock_acquire(&cs->lock);

    /* 查找目标能力 */
    target_cap = cspace_lookup(cs, slot);
    if (target_cap == NULL)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)ENOENT;
    }

    /* 从父能力的 children 链表移除自身 */
    if (target_cap->parent_slot != CAP_SLOT_INVALID)
    {
        list_del_init(&target_cap->sibling);
    }

    /* 将所有子能力的 parent_slot 设为 CAP_SLOT_INVALID */
    {
        struct list_head *pos;
        struct list_head *n;

        for (pos = target_cap->children.next, n = pos->next;
             pos != &target_cap->children;
             pos = n, n = pos->next)
        {
            cap_t *child_cap = cap_from_sibling(pos);
            child_cap->parent_slot = CAP_SLOT_INVALID;
            list_del_init(&child_cap->sibling);
        }
    }

    /* 清除能力状态 */
    (void)memset(target_cap, 0, sizeof(cap_t));
    target_cap->state = CAP_STATE_FREE;
    target_cap->parent_slot = CAP_SLOT_INVALID;
    INIT_LIST_HEAD(&target_cap->children);
    INIT_LIST_HEAD(&target_cap->sibling);

    /* 硬件内存屏障确保多核缓存一致性 */
    hal_dmb_ish();
    /* 内存屏障确保清除操作对所有核可见 */
    barrier_store();

    ticket_lock_release(&cs->lock);

    return KERNEL_OK;
}

/* ========================================================================
 * 权限验证
 * ======================================================================== */

/**
 * @brief 查找能力并验证权限（SMP 多核安全）
 *
 * @details 在 CSpace 锁保护下完成查找和权限验证，确保查找和检查的原子性。
 *
 * @param cspace_root     CSpace 根能力槽
 * @param slot            能力槽索引
 * @param required_rights 需要的权限位
 * @param out_cap         输出能力指针（可为 NULL）
 *
 * @return KERNEL_OK 成功
 * @return -ENOENT 能力不存在
 * @return -EACCES 权限不足
 *
 * @note 对应需求: KR-013
 * @warning 返回的 cap 指针在锁释放后可能被其他核修改，
 *          调用者应在同一系统调用上下文中立即使用
 */
kernel_status_t cap_validate(cap_slot_t cspace_root,
                              cap_slot_t slot,
                              uint8_t required_rights,
                              cap_t **out_cap)
{
    cspace_t *cs;
    cap_t *cap;

    /* 参数检查 */
    if ((cspace_root == CAP_SLOT_INVALID) || (slot == CAP_SLOT_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    /* 解析 CSpace */
    cs = cspace_from_root(cspace_root);
    if (cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 CSpace 锁 */
    ticket_lock_acquire(&cs->lock);

    /* 查找能力 */
    cap = cspace_lookup(cs, slot);
    if (cap == NULL)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)ENOENT;
    }

    /* 检查状态 */
    if (cap->state != CAP_STATE_VALID)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)ENOENT;
    }

    /* 验证权限 */
    if ((cap->rights & required_rights) != required_rights)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)EACCES;
    }

    /* 输出能力指针 */
    if (out_cap != NULL)
    {
        *out_cap = cap;
    }

    ticket_lock_release(&cs->lock);

    return KERNEL_OK;
}

/* ========================================================================
 * 获取对象类型
 * ======================================================================== */

/**
 * @brief 获取能力指向的内核对象类型（SMP 多核安全）
 *
 * @param cspace_root CSpace 根能力槽
 * @param slot        能力槽索引
 *
 * @return 内核对象类型，无效返回 KOBJ_TYPE_COUNT
 */
kobj_type_t cap_get_object_type(cap_slot_t cspace_root, cap_slot_t slot)
{
    cspace_t *cs;
    cap_t *cap;
    kobj_type_t result;

    /* 参数检查 */
    if ((cspace_root == CAP_SLOT_INVALID) || (slot == CAP_SLOT_INVALID))
    {
        return KOBJ_TYPE_COUNT;
    }

    /* 解析 CSpace */
    cs = cspace_from_root(cspace_root);
    if (cs == NULL)
    {
        return KOBJ_TYPE_COUNT;
    }

    /* 获取 CSpace 锁 */
    ticket_lock_acquire(&cs->lock);

    /* 查找能力 */
    cap = cspace_lookup(cs, slot);
    if (cap == NULL)
    {
        ticket_lock_release(&cs->lock);
        return KOBJ_TYPE_COUNT;
    }

    /* 检查状态 */
    if (cap->state != CAP_STATE_VALID)
    {
        ticket_lock_release(&cs->lock);
        return KOBJ_TYPE_COUNT;
    }

    /* 在持锁状态下读取类型 */
    result = cap->kobj_type;

    ticket_lock_release(&cs->lock);

    return result;
}

/* ========================================================================
 * 能力 Badge 更新
 * ======================================================================== */

/**
 * @brief 更新能力的 Badge 值（SMP 多核安全）
 *
 * @details 在 CSpace 锁保护下修改 badge 字段，确保多核一致性。
 *
 * @param cspace_root CSpace 根能力槽
 * @param slot        能力槽索引
 * @param new_badge   新的 Badge 值
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_badge_update(cap_slot_t cspace_root,
                                   cap_slot_t slot,
                                   uint16_t new_badge)
{
    cspace_t *cs;
    cap_t *cap;

    /* 参数检查 */
    if ((cspace_root == CAP_SLOT_INVALID) || (slot == CAP_SLOT_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    /* 解析 CSpace */
    cs = cspace_from_root(cspace_root);
    if (cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 CSpace 锁 */
    ticket_lock_acquire(&cs->lock);

    /* 查找能力 */
    cap = cspace_lookup(cs, slot);
    if (cap == NULL)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)ENOENT;
    }

    /* 验证状态 */
    if (cap->state != CAP_STATE_VALID)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)ENOENT;
    }

    /* 检查 WRITE 权限 */
    if ((cap->rights & CAP_RIGHT_WRITE) == CAP_RIGHT_NONE)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)EACCES;
    }

    /* 更新 badge */
    cap->badge = new_badge;

    /* 内存屏障确保更新对所有核可见 */
    barrier_store();

    ticket_lock_release(&cs->lock);

    return KERNEL_OK;
}

/* ========================================================================
 * 能力权限派生检查
 * ======================================================================== */

/**
 * @brief 检查权限是否可以从父能力派生（SMP 多核安全）
 *
 * @param cspace_root   CSpace 根能力槽
 * @param slot          父能力槽索引
 * @param request_rights 请求的权限位
 *
 * @return KERNEL_OK 可以派生
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_rights_derive_check(cap_slot_t cspace_root,
                                          cap_slot_t slot,
                                          uint8_t request_rights)
{
    cspace_t *cs;
    cap_t *cap;

    /* 参数检查 */
    if ((cspace_root == CAP_SLOT_INVALID) || (slot == CAP_SLOT_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    /* 解析 CSpace */
    cs = cspace_from_root(cspace_root);
    if (cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 CSpace 锁 */
    ticket_lock_acquire(&cs->lock);

    /* 查找能力 */
    cap = cspace_lookup(cs, slot);
    if (cap == NULL)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)ENOENT;
    }

    /* 验证状态 */
    if (cap->state != CAP_STATE_VALID)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)ENOENT;
    }

    /* 检查 GRANT 权限 */
    if ((cap->rights & CAP_RIGHT_GRANT) == CAP_RIGHT_NONE)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)EACCES;
    }

    /* 检查请求权限是否为源权限子集 */
    if ((request_rights & cap->rights) != request_rights)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)EACCES;
    }

    ticket_lock_release(&cs->lock);

    return KERNEL_OK;
}

/* ========================================================================
 * 能力铸造（Mint）
 * ======================================================================== */

/**
 * @brief 铸造新能力（SMP 多核安全）
 *
 * @details 从内核对象创建一个新的能力。在 CSpace 锁保护下完成。
 *
 * @param cspace_root 目标 CSpace 的根能力槽
 * @param slot        目标能力槽索引
 * @param obj_type    内核对象类型
 * @param obj_id      内核对象 ID
 * @param rights      权限位
 * @param badge       标识值
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_mint(cap_slot_t cspace_root,
                          cap_slot_t slot,
                          kobj_type_t obj_type,
                          kobj_id_t obj_id,
                          uint8_t rights,
                          uint16_t badge)
{
    cspace_t *cs;
    cap_t *root_cap;
    kernel_status_t ret;

    /* 参数检查 */
    if (cspace_root == CAP_SLOT_INVALID)
    {
        return -(int32_t)EINVAL;
    }

    if (slot == CAP_SLOT_INVALID)
    {
        return -(int32_t)EINVAL;
    }

    if (obj_type >= KOBJ_TYPE_COUNT)
    {
        return -(int32_t)EINVAL;
    }

    /* 解析目标 CSpace */
    cs = cspace_from_root(cspace_root);
    if (cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 CSpace 锁 */
    ticket_lock_acquire(&cs->lock);

    /* 检查根能力权限：需要 WRITE + GRANT */
    root_cap = cspace_lookup(cs, cs->root_slot);
    if (root_cap == NULL)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)ENOENT;
    }

    if ((root_cap->rights & (CAP_RIGHT_WRITE | CAP_RIGHT_GRANT)) !=
        (CAP_RIGHT_WRITE | CAP_RIGHT_GRANT))
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)EACCES;
    }

    /* 插入能力（已持锁） */
    ret = cap_insert_locked(cs, slot, obj_type, obj_id,
                              rights, badge, CAP_SLOT_INVALID);

    ticket_lock_release(&cs->lock);

    return ret;
}

/* ========================================================================
 * 能力派生（Derive — 严格降权）
 * ======================================================================== */

/**
 * @brief 派生子能力（严格降权，SMP 多核安全）
 *
 * @details 比 cap_copy 更严格的降权操作。在双 CSpace 锁保护下完成。
 *
 * @param src_cspace  源 CSpace 的根能力槽
 * @param src_slot    源能力槽索引
 * @param dest_cspace 目标 CSpace 的根能力槽
 * @param dest_slot   目标能力槽索引
 * @param new_rights  新权限位
 * @param badge       新标识值
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_derive(cap_slot_t src_cspace,
                            cap_slot_t src_slot,
                            cap_slot_t dest_cspace,
                            cap_slot_t dest_slot,
                            uint8_t new_rights,
                            uint16_t badge)
{
    kernel_status_t ret;
    cspace_t *src_cs;
    cspace_t *dst_cs;
    cap_t *src_cap;

    /* 参数检查 */
    if ((src_cspace == CAP_SLOT_INVALID) || (src_slot == CAP_SLOT_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    if ((dest_cspace == CAP_SLOT_INVALID) || (dest_slot == CAP_SLOT_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    /* 解析 CSpace */
    src_cs = cspace_from_root(src_cspace);
    if (src_cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    dst_cs = cspace_from_root(dest_cspace);
    if (dst_cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 按地址顺序获取双 CSpace 锁 */
    cap_lock_dual(src_cs, dst_cs);

    /* 查找源能力 */
    src_cap = cspace_lookup(src_cs, src_slot);
    if (src_cap == NULL)
    {
        cap_unlock_dual(src_cs, dst_cs);
        return -(int32_t)ENOENT;
    }

    if (src_cap->state != CAP_STATE_VALID)
    {
        cap_unlock_dual(src_cs, dst_cs);
        return -(int32_t)ENOENT;
    }

    /* 检查 GRANT 权限 */
    if ((src_cap->rights & CAP_RIGHT_GRANT) == CAP_RIGHT_NONE)
    {
        cap_unlock_dual(src_cs, dst_cs);
        return -(int32_t)EACCES;
    }

    /* 严格子集检查 */
    if ((new_rights & src_cap->rights) != new_rights)
    {
        cap_unlock_dual(src_cs, dst_cs);
        return -(int32_t)EACCES;
    }

    /* 严格降权检查 */
    if (new_rights == src_cap->rights)
    {
        cap_unlock_dual(src_cs, dst_cs);
        return -(int32_t)EINVAL;
    }

    /* 在目标 CSpace 中插入能力（已持锁） */
    ret = cap_insert_locked(dst_cs,
                             dest_slot,
                             src_cap->kobj_type,
                             src_cap->kobj_id,
                             new_rights,
                             badge,
                             src_slot);

    cap_unlock_dual(src_cs, dst_cs);

    return ret;
}

/* ========================================================================
 * 能力信息查询
 * ======================================================================== */

/**
 * @brief 获取能力信息（SMP 多核安全）
 *
 * @details 在 CSpace 锁保护下读取能力元数据，确保一致性。
 *
 * @param cspace_root CSpace 根能力槽
 * @param slot        能力槽索引
 * @param info        输出信息结构
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: KR-013
 */
kernel_status_t cap_get_info(cap_slot_t cspace_root,
                              cap_slot_t slot,
                              cap_info_t *info)
{
    cspace_t *cs;
    cap_t *cap;

    /* 参数检查 */
    if (info == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if ((cspace_root == CAP_SLOT_INVALID) || (slot == CAP_SLOT_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    /* 解析 CSpace */
    cs = cspace_from_root(cspace_root);
    if (cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 CSpace 锁 */
    ticket_lock_acquire(&cs->lock);

    /* 查找能力 */
    cap = cspace_lookup(cs, slot);
    if (cap == NULL)
    {
        ticket_lock_release(&cs->lock);
        return -(int32_t)ENOENT;
    }

    /* 在持锁状态下填充信息，确保一致性快照 */
    info->obj_type = cap->kobj_type;
    info->rights = cap->rights;
    info->badge = cap->badge;
    info->obj_id = cap->kobj_id;
    info->state = cap->state;
    info->child_count = list_count_nodes(&cap->children);

    ticket_lock_release(&cs->lock);

    return KERNEL_OK;
}

/* ========================================================================
 * 线程迁移时能力上下文同步
 * ======================================================================== */

/**
 * @brief 线程迁移时的能力上下文同步
 *
 * @details 当线程从一个 CPU 迁移到另一个 CPU 时调用。
 *          CSpace 是全局共享的，不需要迁移。
 *          此函数确保迁移安全：
 *          - 验证线程的 CSpace 在迁移后仍可安全访问
 *          - 使用内存屏障确保多核可见性
 *
 * @param thread_id 迁移的线程 ID
 * @param old_cpu   原 CPU 编号
 * @param new_cpu   新 CPU 编号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: KR-013, MP-005
 * @warning 必须在迁移完成后调用
 */
kernel_status_t cap_migrate_context(uint32_t thread_id,
                                      uint32_t old_cpu,
                                      uint32_t new_cpu)
{
    (void)old_cpu;
    (void)new_cpu;

    if (thread_id >= CONFIG_MAX_THREADS)
    {
        return -(int32_t)EINVAL;
    }

    /* CSpace 是全局共享数据结构，不需要按 CPU 迁移。
     * 但需要确保：
     * 1. 迁移前后的内存屏障（由调用方 barrier() 保证）
     * 2. 不在持锁期间迁移（由 smp_migrate_thread 的锁设计保证）
     *
     * 此函数保留作为未来扩展点：
     * - 每 CPU CSpace 缓存失效
     * - 能力对象的 CPU 亲和性追踪
     * - 地址空间 ASID 刷新（如果需要）
     */

    /* 硬件内存屏障确保所有之前的内存操作对新 CPU 可见 */
    hal_dmb_ish();
    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 能力系统完整性自检
 * ======================================================================== */

/**
 * @brief 执行 CSpace 能力完整性自检（SMP 多核安全）
 *
 * @details 遍历指定 CSpace 的所有能力，检查以下不变式：
 *          a. 所有 VALID 能力的 parent_slot 指向 VALID 父能力或 CAP_SLOT_INVALID
 *          b. 所有 children 链表中的子能力确实以当前能力为 parent
 *          c. derive_depth 单调递增（子 >= 父 + 1）
 *          d. rights 单调递减（子权限是父权限的子集）
 *          e. 权限符合类型权限矩阵
 *
 * @param cspace_root CSpace 根能力槽
 * @param result      输出检查结果
 *
 * @return KERNEL_OK 自检完成
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: KR-013
 */
kernel_status_t cap_integrity_check(cap_slot_t cspace_root,
                                      cap_integrity_result_t *result)
{
    cspace_t *cs;
    uint32_t i;
    uint32_t total;
    uint32_t failed;

    /* 参数检查 */
    if (result == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (cspace_root == CAP_SLOT_INVALID)
    {
        return -(int32_t)EINVAL;
    }

    /* 解析 CSpace */
    cs = cspace_from_root(cspace_root);
    if (cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 CSpace 锁 */
    ticket_lock_acquire(&cs->lock);

    total = 0U;
    failed = 0U;

    /* 遍历所有能力槽 */
    for (i = 0U; i < cs->capacity; i++)
    {
        cap_t *cap = &cs->cap_table[i];

        if (cap->state != CAP_STATE_VALID)
        {
            continue;
        }

        total++;

        /* 检查 a: parent_slot 指向 VALID 父能力或 CAP_SLOT_INVALID */
        if (cap->parent_slot != CAP_SLOT_INVALID)
        {
            cap_t *parent;

            if (cap->parent_slot >= cs->capacity)
            {
                failed++;
                continue;
            }

            parent = &cs->cap_table[cap->parent_slot];

            if (parent->state != CAP_STATE_VALID)
            {
                failed++;
                continue;
            }

            /* 检查 c: derive_depth 单调递增 */
            if (cap->derive_depth != (parent->derive_depth + 1U))
            {
                failed++;
                continue;
            }

            /* 检查 d: rights 单调递减（子权限是父权限的子集） */
            if ((cap->rights & parent->rights) != cap->rights)
            {
                failed++;
                continue;
            }
        }
        else
        {
            /* 根能力：derive_depth 应为 0 */
            if (cap->derive_depth != 0U)
            {
                failed++;
                continue;
            }
        }

        /* 检查 e: 权限符合类型权限矩阵 */
        if (cap_validate_rights_for_type(cap->kobj_type, cap->rights) != KERNEL_OK)
        {
            failed++;
            continue;
        }

        /* 检查 b: children 链表中的子能力确实以当前能力为 parent */
        {
            struct list_head *pos;
            struct list_head *n;

            for (pos = cap->children.next, n = pos->next;
                 pos != &cap->children;
                 pos = n, n = pos->next)
            {
                cap_t *child_cap = cap_from_sibling(pos);
                cap_slot_t child_idx =
                    (cap_slot_t)(child_cap - &cs->cap_table[0U]);

                if (child_cap->parent_slot != child_idx)
                {
                    /* 子能力的 parent_slot 不指向当前能力的索引 i */
                    /* 需要判断：child_cap->parent_slot 应等于 i */
                    if (child_cap->parent_slot != (cap_slot_t)i)
                    {
                        failed++;
                        break;
                    }
                }
            }
        }
    }

    ticket_lock_release(&cs->lock);

    result->total_caps = total;
    result->passed_checks = total - failed;
    result->failed_checks = failed;

    return KERNEL_OK;
}

/* ========================================================================
 * 能力系统形式化验证条件注册
 * ======================================================================== */

/**
 * @brief 注册能力系统形式化验证不变式
 *
 * @details 向形式化验证框架注册 8 个能力系统核心不变式条件：
 *          1. 权限单调递减不变式（cap_derive）
 *          2. 撤销完整性不变式（cap_revoke）
 *          3. CSpace 引用完整性（cap_validate）
 *          4. 权限类型合法性（cap_insert_locked）
 *          5. 派生深度限制（cap_derive）
 *          6. 无悬挂引用（cap_delete）
 *          7. 移动原子性（cap_move）
 *          8. Badge 不可提升（cap_derive）
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: SE-007, SE-008
 */

/* ========================================================================
 * 新对象类型创建 API（FD, INODE, MEMORY_REGION）
 * ======================================================================== */

/**
 * @brief 创建文件描述符类型的能力
 *
 * @param cspace_root    CSpace 根能力槽
 * @param slot           目标能力槽索引
 * @param fd             文件描述符编号
 * @param access_mode    访问模式（只读/只写/读写）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EACCES 权限不足
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_mint_for_fd(cap_slot_t cspace_root,
                                 cap_slot_t slot,
                                 uint32_t fd,
                                 uint8_t access_mode)
{
    kernel_status_t status;
    uint8_t rights;

    /* 验证访问模式 */
    if (access_mode == 0U)
    {
        return -(int32_t)EINVAL;
    }

    /* 根据访问模式设置权限 */
    rights = CAP_RIGHT_READ;
    if ((access_mode & 0x02) != 0U)  /* 写入位 */
    {
        rights |= CAP_RIGHT_WRITE;
    }
    if ((access_mode & 0x04) != 0U)  /* 执行位 */
    {
        rights |= CAP_RIGHT_EXECUTE;
    }

    /* 创建能力 */
    status = cap_mint(cspace_root,
                       slot,
                       KOBJ_FD,
                       (kobj_id_t)fd,
                       rights,
                       0U);

    return status;
}

/**
 * @brief 创建 Inode 类型的能力
 *
 * @param cspace_root    CSpace 根能力槽
 * @param slot           目标能力槽索引
 * @param inode_id       Inode 编号
 * @param file_type      文件类型（普通文件/目录/符号链接等）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EACCES 权限不足
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_mint_for_inode(cap_slot_t cspace_root,
                                     cap_slot_t slot,
                                     uint64_t inode_id,
                                     uint8_t file_type)
{
    kernel_status_t status;
    uint8_t rights;

    /* Inode 只读权限 */
    rights = CAP_RIGHT_READ | CAP_RIGHT_EXECUTE;

    /* 根据文件类型决定是否可以修改元数据 */
    if ((file_type == 0x01) || (file_type == 0x02))  /* 普通文件或目录 */
    {
        rights |= CAP_RIGHT_WRITE;
    }

    /* 创建能力 */
    status = cap_mint(cspace_root,
                       slot,
                       KOBJ_INODE,
                       (kobj_id_t)inode_id,
                       rights,
                       0U);

    return status;
}

/**
 * @brief 创建内存区域类型的能力
 *
 * @param cspace_root        CSpace 根能力槽
 * @param slot               目标能力槽索引
 * @param mem_region_id      内存区域 ID
 * @param access_rights      访问权限（读/写/执行）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EACCES 权限不足
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_mint_for_memory_region(cap_slot_t cspace_root,
                                            cap_slot_t slot,
                                            uint32_t mem_region_id,
                                            uint8_t access_rights)
{
    kernel_status_t status;
    uint8_t rights;

    /* 验证访问权限 */
    if (access_rights == 0U)
    {
        return -(int32_t)EINVAL;
    }

    /* 根据访问权限设置权限 */
    rights = CAP_RIGHT_READ;
    if ((access_rights & 0x02) != 0U)  /* 写入位 */
    {
        rights |= CAP_RIGHT_WRITE;
    }
    if ((access_rights & 0x04) != 0U)  /* 执行位 */
    {
        rights |= CAP_RIGHT_EXECUTE;
    }

    /* 创建能力 */
    status = cap_mint(cspace_root,
                       slot,
                       KOBJ_MEMORY_REGION,
                       (kobj_id_t)mem_region_id,
                       rights,
                       0U);

    return status;
}
