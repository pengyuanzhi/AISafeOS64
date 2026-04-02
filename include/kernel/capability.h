/**
 * @file    capability.h
 * @brief   能力描述符和操作接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了微内核的能力（Capability）描述符和操作接口：
 *          - 能力描述符（Cap_t）结构
 *          - 权限位定义（读/写/执行/转发/撤销）
 *          - 能力操作（Copy/Move/Revoke/Delete）
 *          - 能力状态管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-013~016
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_CAPABILITY_H
#define KERNEL_CAPABILITY_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/kobject.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 权限位定义
 * ======================================================================== */

/** @brief 能力权限：读取 */
#define CAP_RIGHT_READ      (1U << 0U)

/** @brief 能力权限：写入 */
#define CAP_RIGHT_WRITE     (1U << 1U)

/** @brief 能力权限：执行 */
#define CAP_RIGHT_EXECUTE   (1U << 2U)

/** @brief 能力权限：转发（可复制给其他 CSpace） */
#define CAP_RIGHT_GRANT     (1U << 3U)

/** @brief 能力权限：撤销（可撤销派生的子能力） */
#define CAP_RIGHT_REVOKE    (1U << 4U)

/** @brief 所有权限位掩码 */
#define CAP_RIGHT_ALL       (CAP_RIGHT_READ | CAP_RIGHT_WRITE | \
                             CAP_RIGHT_EXECUTE | CAP_RIGHT_GRANT | \
                             CAP_RIGHT_REVOKE)

/** @brief 无权限 */
#define CAP_RIGHT_NONE      0U

/* ========================================================================
 * 能力槽索引类型
 * ======================================================================== */

/** @brief 能力槽索引（在 CSpace 中的位置） */
typedef uint32_t cap_slot_t;

/** @brief 无效能力槽 */
#define CAP_SLOT_INVALID    ((cap_slot_t)0xFFFFFFFFU)

/* ========================================================================
 * 能力状态
 * ======================================================================== */

/**
 * @brief 能力状态枚举
 */
typedef enum
{
    CAP_STATE_FREE = 0U,        /**< @brief 空闲（未使用） */
    CAP_STATE_VALID,            /**< @brief 有效（已分配） */
    CAP_STATE_REVOKED           /**< @brief 已撤销 */
} cap_state_t;

/* ========================================================================
 * 能力描述符
 * ======================================================================== */

/**
 * @brief 能力描述符
 *
 * @details 描述一个进程对某个内核对象的访问权限。
 *          能力是内核对象的代理，用户态通过能力槽索引引用能力。
 *          支持父子关系追踪，用于级联撤销。
 *
 * @note 对应需求: KR-013
 */
typedef struct
{
    cap_state_t     state;          /**< @brief 能力状态 */
    kobj_type_t     kobj_type;      /**< @brief 指向的内核对象类型 */
    uint8_t         rights;         /**< @brief 权限位 */
    uint16_t        badge;          /**< @brief 标识（用于 IPC 连接） */
    kobj_id_t       kobj_id;        /**< @brief 指向的内核对象 ID */
    cap_slot_t      parent_slot;    /**< @brief 父能力槽索引（CAP_SLOT_INVALID 表示根能力） */
    cap_slot_t      cspace_root;    /**< @brief 所属 CSpace 的根能力槽 */
    struct list_head children;      /**< @brief 子能力链表 */
    struct list_head sibling;       /**< @brief 兄弟链表节点 */
} cap_t;

/* ========================================================================
 * 能力操作 API
 * ======================================================================== */

/**
 * @brief 初始化能力子系统
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: KR-013
 */
kernel_status_t capability_subsys_init(void);

/**
 * @brief 复制能力（可降权）
 *
 * @details 将源能力复制到目标槽位，权限可降级但不能提升。
 *          源能力必须具有 GRANT 权限才能执行复制。
 *
 * @param src_cspace  源 CSpace 的根能力槽
 * @param src_slot    源能力槽索引
 * @param dest_cspace 目标 CSpace 的根能力槽（可与源相同）
 * @param dest_slot   目标能力槽索引
 * @param rights_mask 权限掩码（降权后的权限，0 表示保持原权限）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EACCES 权限不足（源能力无 GRANT 权限）
 * @return -ENOENT 源能力不存在
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_copy(cap_slot_t src_cspace,
                          cap_slot_t src_slot,
                          cap_slot_t dest_cspace,
                          cap_slot_t dest_slot,
                          uint8_t rights_mask);

/**
 * @brief 移动能力
 *
 * @details 将能力从源槽移动到目标槽，源槽被清空。
 *          源能力必须具有 GRANT 权限。
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
                          cap_slot_t dest_slot);

/**
 * @brief 撤销能力（级联）
 *
 * @details 撤销指定能力及其所有派生子能力。
 *          源能力必须具有 REVOKE 权限。
 *          被撤销的能力状态变为 CAP_STATE_REVOKED。
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
kernel_status_t cap_revoke(cap_slot_t cspace_root, cap_slot_t slot);

/**
 * @brief 删除能力
 *
 * @details 删除指定能力（不级联）。
 *          如果能力有子能力，删除前会先解除父子关系。
 *
 * @param cspace_root CSpace 根能力槽
 * @param slot        要删除的能力槽索引
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: KR-015
 */
kernel_status_t cap_delete(cap_slot_t cspace_root, cap_slot_t slot);

/**
 * @brief 查找能力并验证权限
 *
 * @param cspace_root CSpace 根能力槽
 * @param slot        能力槽索引
 * @param required_rights 需要的权限位
 * @param out_cap     输出能力指针（可为 NULL）
 *
 * @return KERNEL_OK 成功
 * @return -ENOENT 能力不存在
 * @return -EACCES 权限不足
 *
 * @note 对应需求: KR-013
 */
kernel_status_t cap_validate(cap_slot_t cspace_root,
                              cap_slot_t slot,
                              uint8_t required_rights,
                              cap_t **out_cap);

/**
 * @brief 获取能力指向的内核对象类型
 *
 * @param cspace_root CSpace 根能力槽
 * @param slot        能力槽索引
 *
 * @return 内核对象类型，无效返回 KOBJ_TYPE_COUNT
 */
kobj_type_t cap_get_object_type(cap_slot_t cspace_root, cap_slot_t slot);

#endif /* KERNEL_CAPABILITY_H */
