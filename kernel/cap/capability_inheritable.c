/**
 * @file    capability_inheritable.c
 * @brief   能力继承标志实现
 * @author  AISafe64 Team
 * @date    2026-05-25
 * @version 1.0
 *
 * @details 本文件实现能力继承标志相关的 API：
 *          - cap_set_inheritable(): 设置能力继承标志
 *          - cap_get_inheritable(): 查询能力继承标志
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-014
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

/* ========================================================================
 * 能力继承控制 API 实现
 * ======================================================================== */

/**
 * @brief 设置能力继承标志
 *
 * @details 设置指定能力的继承标志。
 *          inheritable = true: 子能力可以继续派生和传播
 *          inheritable = false: 子能力不能派生和传播
 *
 * @param cspace_root  CSpace 根能力槽
 * @param slot         能力槽索引
 * @param inheritable  继承标志
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOENT 能力不存在
 * @return -EACCES 权限不足
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_set_inheritable(cap_slot_t cspace_root,
                                     cap_slot_t slot,
                                     bool inheritable)
{
    cspace_t *cspace;
    cap_t *cap;
    kernel_status_t status;

    /* 参数验证 */
    if (slot == CAP_SLOT_INVALID)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找 CSpace */
    cspace = cspace_find(cspace_root);
    if (cspace == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 获取 CSpace 锁 */
    ticket_lock_acquire(&cspace->lock);

    /* 查找能力 */
    cap = cspace_lookup_locked(cspace, slot);
    if (cap == NULL)
    {
        ticket_lock_release(&cspace->lock);
        return -(int32_t)ENOENT;
    }

    /* 检查能力状态 */
    if (cap->state != CAP_STATE_VALID)
    {
        ticket_lock_release(&cspace->lock);
        return -(int32_t)EACCES;
    }

    /* 设置继承标志 */
    cap->inheritable = inheritable;

    /* 内存屏障确保多核可见 */
    barrier();

    /* 释放 CSpace 锁 */
    ticket_lock_release(&cspace->lock);

    return KERNEL_OK;
}

/**
 * @brief 查询能力继承标志
 *
 * @details 查询指定能力的继承标志。
 *
 * @param cspace_root    CSpace 根能力槽
 * @param slot           能力槽索引
 * @param inheritable    输出继承标志
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOENT 能力不存在
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_get_inheritable(cap_slot_t cspace_root,
                                     cap_slot_t slot,
                                     bool *inheritable)
{
    cspace_t *cspace;
    cap_t *cap;

    /* 参数验证 */
    if ((slot == CAP_SLOT_INVALID) || (inheritable == NULL))
    {
        return -(int32_t)EINVAL;
    }

    /* 查找 CSpace */
    cspace = cspace_find(cspace_root);
    if (cspace == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 获取 CSpace 锁 */
    ticket_lock_acquire(&cspace->lock);

    /* 查找能力 */
    cap = cspace_lookup_locked(cspace, slot);
    if (cap == NULL)
    {
        ticket_lock_release(&cspace->lock);
        return -(int32_t)ENOENT;
    }

    /* 读取继承标志 */
    *inheritable = cap->inheritable;

    /* 释放 CSpace 锁 */
    ticket_lock_release(&cspace->lock);

    return KERNEL_OK;
}