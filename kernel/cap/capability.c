/**
 * @file    capability.c
 * @brief   能力描述符操作实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件实现微内核的能力（Capability）操作：
 *          - 能力复制（cap_copy）：支持降权复制，建立父子关系
 *          - 能力移动（cap_move）：原子性移动，转移子能力关系
 *          - 能力撤销（cap_revoke）：级联撤销，使用显式栈替代递归
 *          - 能力删除（cap_delete）：非级联删除，解除父子关系
 *          - 权限验证（cap_validate）：查找并验证权限
 *          - 对象类型查询（cap_get_object_type）
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
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 从链表节点获取 cap_t 结构体指针
 *
 * @details 通过 sibling 成员的偏移量，从 list_head 指针反算出
 *          包含该节点的 cap_t 结构体首地址。避免使用 typeof 宏。
 *
 * @param node sibling 链表节点指针
 *
 * @return 对应的 cap_t 指针
 */
static inline cap_t *cap_from_sibling(struct list_head *node)
{
    return container_of(node, cap_t, sibling);
}

/* ========================================================================
 * 能力子系统初始化
 * ======================================================================== */

/**
 * @brief 初始化能力子系统
 *
 * @details 当前为空操作。能力子系统依赖 CSpace 子系统，
 *          CSpace 初始化在 cspace_subsys_init() 中完成。
 *          本函数保留用于未来扩展（如全局能力统计）。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: KR-013
 */
kernel_status_t capability_subsys_init(void)
{
    /* 当前版本无需额外初始化，CSpace 子系统已处理 */
    return KERNEL_OK;
}

/* ========================================================================
 * 能力复制
 * ======================================================================== */

/**
 * @brief 复制能力（可降权）
 *
 * @details 将源能力复制到目标 CSpace 的指定槽位。支持权限降级：
 *          - rights_mask != 0 时，目标权限 = 源权限 & rights_mask
 *          - rights_mask == 0 时，目标权限 = 源权限（保持不变）
 *          权限不可提升（目标权限必须是源权限的子集）。
 *          复制后建立父子关系：子能力的 parent_slot 指向源能力，
 *          并加入源能力的 children 链表。
 *
 * @param src_cspace  源 CSpace 的根能力槽
 * @param src_slot    源能力槽索引
 * @param dest_cspace 目标 CSpace 的根能力槽
 * @param dest_slot   目标能力槽索引
 * @param rights_mask 权限掩码（0 表示保持原权限）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EACCES 权限不足（源能力无 GRANT 权限）
 * @return -ENOENT 源能力不存在或无效
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

    /* 解析源 CSpace */
    src_cs = cspace_from_root(src_cspace);
    if (src_cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 解析目标 CSpace */
    dst_cs = cspace_from_root(dest_cspace);
    if (dst_cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找源能力 */
    src_cap = cspace_lookup(src_cs, src_slot);
    if (src_cap == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 验证源能力状态 */
    if (src_cap->state != CAP_STATE_VALID)
    {
        return -(int32_t)ENOENT;
    }

    /* 检查 GRANT 权限 */
    if ((src_cap->rights & CAP_RIGHT_GRANT) == CAP_RIGHT_NONE)
    {
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

    /* 权限不可提升：目标权限必须是源权限的子集 */
    if ((dest_rights & src_cap->rights) != dest_rights)
    {
        return -(int32_t)EACCES;
    }

    /* 在目标 CSpace 中插入能力 */
    ret = cspace_insert_cap(dst_cs,
                             dest_slot,
                             src_cap->kobj_type,
                             src_cap->kobj_id,
                             dest_rights,
                             src_cap->badge,
                             src_slot);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    /* 建立父子关系：将子能力加入源能力的 children 链表 */
    {
        cap_t *dest_cap;

        dest_cap = cspace_lookup(dst_cs, dest_slot);
        if (dest_cap != NULL)
        {
            list_add_tail(&dest_cap->sibling, &src_cap->children);
        }
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 能力移动
 * ======================================================================== */

/**
 * @brief 移动能力
 *
 * @details 将能力从源 CSpace 的源槽移动到目标 CSpace 的目标槽。
 *          源槽被清空。移动过程中转移子能力关系到目标能力。
 *
 *          操作步骤：
 *          1. 复制能力到目标槽（不降权）
 *          2. 将源能力的子能力关系转移到目标能力
 *          3. 清除源能力
 *
 * @param src_cspace  源 CSpace 的根能力槽
 * @param src_slot    源能力槽索引
 * @param dest_cspace 目标 CSpace 的根能力槽
 * @param dest_slot   目标能力槽索引
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EACCES 权限不足
 * @return -ENOENT 源能力不存在
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

    /* 查找源能力 */
    src_cap = cspace_lookup(src_cs, src_slot);
    if (src_cap == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 验证源能力状态 */
    if (src_cap->state != CAP_STATE_VALID)
    {
        return -(int32_t)ENOENT;
    }

    /* 检查 GRANT 权限 */
    if ((src_cap->rights & CAP_RIGHT_GRANT) == CAP_RIGHT_NONE)
    {
        return -(int32_t)EACCES;
    }

    /* 第一步：在目标 CSpace 中插入能力（保持原权限） */
    ret = cspace_insert_cap(dst_cs,
                             dest_slot,
                             src_cap->kobj_type,
                             src_cap->kobj_id,
                             src_cap->rights,
                             src_cap->badge,
                             src_cap->parent_slot);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    /* 获取目标能力 */
    dst_cap = cspace_lookup(dst_cs, dest_slot);
    if (dst_cap == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 第二步：将源能力的子能力关系转移到目标能力 */
    if (list_empty(&src_cap->children) == 0)
    {
        /* 将源能力的 children 链表合并到目标能力的 children 链表 */
        list_splice(&src_cap->children, &dst_cap->children);
    }

    /* 更新转移过来的子能力的 parent_slot 指向目标 */
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

    /* 第三步：从父能力的 children 链表移除源能力（如果有父能力） */
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

    return KERNEL_OK;
}

/* ========================================================================
 * 能力撤销（级联）
 * ======================================================================== */

/**
 * @brief 撤销能力（级联）
 *
 * @details 递归撤销指定能力及其所有派生子能力。
 *          由于 MISRA-C:2012 禁止递归（规则 16.1），
 *          使用显式栈实现深度优先遍历。
 *
 *          算法：
 *          1. 将目标能力压入显式栈
 *          2. 循环弹栈，将弹出的能力的所有子能力压栈
 *          3. 将弹出的能力状态设为 CAP_STATE_REVOKED
 *          4. 栈为空时结束
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
    /* 显式栈：用于非递归级联撤销 */
    static cap_slot_t revoke_stack[REVOKE_STACK_SIZE];
    uint32_t stack_top;

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

    /* 查找目标能力 */
    target_cap = cspace_lookup(cs, slot);
    if (target_cap == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 验证状态 */
    if (target_cap->state != CAP_STATE_VALID)
    {
        return -(int32_t)ENOENT;
    }

    /* 检查 REVOKE 权限 */
    if ((target_cap->rights & CAP_RIGHT_REVOKE) == CAP_RIGHT_NONE)
    {
        return -(int32_t)EACCES;
    }

    /* 初始化显式栈 */
    stack_top = 0U;
    revoke_stack[stack_top] = slot;
    stack_top++;

    /* 使用显式栈进行非递归深度优先遍历 */
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
                    revoke_stack[stack_top] = child_cap->cspace_root;
                    stack_top++;
                }
            }
        }

        /* 将当前能力状态设为已撤销 */
        cur_cap->state = CAP_STATE_REVOKED;

        /* 从父能力的 children 链表移除 */
        list_del_init(&cur_cap->sibling);
        INIT_LIST_HEAD(&cur_cap->children);
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 能力删除
 * ======================================================================== */

/**
 * @brief 删除能力（不级联）
 *
 * @details 删除指定能力，不撤销其子能力。删除前会解除父子关系：
 *          - 从父能力的 children 链表移除自身
 *          - 将子能力的 parent_slot 设为 CAP_SLOT_INVALID
 *          - 清除能力状态为 CAP_STATE_FREE
 *
 * @param cspace_root CSpace 根能力槽
 * @param slot        要删除的能力槽索引
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOENT 能力不存在
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

    /* 查找目标能力 */
    target_cap = cspace_lookup(cs, slot);
    if (target_cap == NULL)
    {
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
            /* 从父（即将被删除的能力）的 children 链表移除 */
            list_del_init(&child_cap->sibling);
        }
    }

    /* 清除能力状态 */
    (void)memset(target_cap, 0, sizeof(cap_t));
    target_cap->state = CAP_STATE_FREE;
    target_cap->parent_slot = CAP_SLOT_INVALID;
    INIT_LIST_HEAD(&target_cap->children);
    INIT_LIST_HEAD(&target_cap->sibling);

    return KERNEL_OK;
}

/* ========================================================================
 * 权限验证
 * ======================================================================== */

/**
 * @brief 查找能力并验证权限
 *
 * @details 在指定 CSpace 中查找能力，验证其状态有效，
 *          并检查是否具备所需的权限位。
 *
 * @param cspace_root     CSpace 根能力槽
 * @param slot            能力槽索引
 * @param required_rights 需要的权限位
 * @param out_cap         输出能力指针（可为 NULL）
 *
 * @return KERNEL_OK 成功
 * @return -ENOENT 能力不存在或无效
 * @return -EACCES 权限不足
 *
 * @note 对应需求: KR-013
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

    /* 查找能力 */
    cap = cspace_lookup(cs, slot);
    if (cap == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 检查状态 */
    if (cap->state != CAP_STATE_VALID)
    {
        return -(int32_t)ENOENT;
    }

    /* 验证权限：目标权限必须是能力权限的子集 */
    if ((cap->rights & required_rights) != required_rights)
    {
        return -(int32_t)EACCES;
    }

    /* 输出能力指针 */
    if (out_cap != NULL)
    {
        *out_cap = cap;
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 获取对象类型
 * ======================================================================== */

/**
 * @brief 获取能力指向的内核对象类型
 *
 * @details 查找指定能力并返回其指向的内核对象类型。
 *          用于在不暴露完整能力描述符的情况下查询对象类型。
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

    /* 查找能力 */
    cap = cspace_lookup(cs, slot);
    if (cap == NULL)
    {
        return KOBJ_TYPE_COUNT;
    }

    /* 检查状态 */
    if (cap->state != CAP_STATE_VALID)
    {
        return KOBJ_TYPE_COUNT;
    }

    return cap->kobj_type;
}
