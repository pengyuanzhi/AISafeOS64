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
 * 派生深度限制
 * ======================================================================== */

/** @brief 能力派生最大深度 */
#define CAP_MAX_DERIVE_DEPTH    8U

/* ========================================================================
 * 对象类型权限矩阵
 * ======================================================================== */

/**
 * @brief 对象类型权限规则
 *
 * @details 定义每种内核对象类型的合法权限集合。
 *          allowed_rights: 该类型允许拥有的权限位
 *          mandatory_rights: 该类型必须拥有的权限位
 */
typedef struct
{
    kobj_type_t type;               /**< @brief 内核对象类型 */
    uint8_t     allowed_rights;     /**< @brief 允许的权限位集合 */
    uint8_t     mandatory_rights;   /**< @brief 强制要求的权限位集合 */
} cap_type_rights_t;

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
    uint8_t         derive_depth;   /**< @brief 派生深度（根能力为 0，每派生一次加 1） */
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

/**
 * @brief 更新能力的 Badge 值
 *
 * @details 修改指定能力的 badge 字段。调用者必须具有 WRITE 权限。
 *
 * @param cspace_root CSpace 根能力槽
 * @param slot        能力槽索引
 * @param new_badge   新的 Badge 值
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOENT 能力不存在
 * @return -EACCES 权限不足
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_badge_update(cap_slot_t cspace_root,
                                   cap_slot_t slot,
                                   uint16_t new_badge);

/**
 * @brief 检查权限是否可以从父能力派生
 *
 * @details 验证请求的权限是否为父能力权限的子集，
 *          且父能力具有 GRANT 权限。
 *
 * @param cspace_root     CSpace 根能力槽
 * @param slot            父能力槽索引
 * @param request_rights  请求的权限位
 *
 * @return KERNEL_OK 可以派生
 * @return -ENOENT 父能力不存在
 * @return -EACCES 权限不足
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_rights_derive_check(cap_slot_t cspace_root,
                                          cap_slot_t slot,
                                          uint8_t request_rights);

/* ========================================================================
 * 能力铸造（Mint）与派生（Derive）
 * ======================================================================== */

/**
 * @brief 铸造新能力
 *
 * @details 从内核对象创建一个新的能力，插入到指定 CSpace 的 slot 中。
 *          这是内核创建能力的唯一入口。
 *          需要 CSpace 根能力具有 WRITE+GRANT 权限。
 *
 * @param cspace_root 目标 CSpace 的根能力槽
 * @param slot        目标能力槽索引
 * @param obj_type    内核对象类型
 * @param obj_id      内核对象 ID
 * @param rights      权限位
 * @param badge       标识值
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EACCES 权限不足
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_mint(cap_slot_t cspace_root,
                          cap_slot_t slot,
                          kobj_type_t obj_type,
                          kobj_id_t obj_id,
                          uint8_t rights,
                          uint16_t badge);

/**
 * @brief 派生子能力（严格降权）
 *
 * @details 比 cap_copy 更严格的降权操作：
 *          - 子能力权限必须是父能力的严格子集
 *          - 子能力权限必须严格小于父能力
 *          - 父能力必须具有 GRANT 权限
 *          - 建立父子关系用于级联撤销
 *
 * @param src_cspace  源 CSpace 的根能力槽
 * @param src_slot    源能力槽索引
 * @param dest_cspace 目标 CSpace 的根能力槽
 * @param dest_slot   目标能力槽索引
 * @param new_rights  新权限位（必须为源权限的严格子集）
 * @param badge       新标识值
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效或权限未严格降权
 * @return -EACCES 权限不足
 * @return -ENOENT 源能力不存在
 *
 * @note 对应需求: KR-014
 */
kernel_status_t cap_derive(cap_slot_t src_cspace,
                            cap_slot_t src_slot,
                            cap_slot_t dest_cspace,
                            cap_slot_t dest_slot,
                            uint8_t new_rights,
                            uint16_t badge);

/* ========================================================================
 * 能力信息查询
 * ======================================================================== */

/**
 * @brief 能力信息结构（用户可见）
 *
 * @details 用于 cap_get_info() 返回给用户的能力元数据，
 *          不暴露内核内部指针。
 */
typedef struct
{
    kobj_type_t obj_type;       /**< @brief 内核对象类型 */
    uint8_t     rights;         /**< @brief 权限位 */
    uint16_t    badge;          /**< @brief 标识值 */
    kobj_id_t   obj_id;         /**< @brief 内核对象 ID */
    cap_state_t state;          /**< @brief 能力状态 */
    uint32_t    child_count;    /**< @brief 子能力数量 */
} cap_info_t;

/**
 * @brief 获取能力信息
 *
 * @details 查询指定能力的元数据，返回给调用者。
 *          不暴露内核内部指针，仅返回安全信息。
 *
 * @param cspace_root CSpace 根能力槽
 * @param slot        能力槽索引
 * @param info        输出信息结构（调用者分配）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOENT 能力不存在
 *
 * @note 对应需求: KR-013
 */
kernel_status_t cap_get_info(cap_slot_t cspace_root,
                              cap_slot_t slot,
                              cap_info_t *info);

/* ========================================================================
 * 内联权限检查（性能关键路径）
 * ======================================================================== */

/**
 * @brief 内联权限检查
 *
 * @details 检查能力是否具有所需的全部权限位。
 *          用于内核热路径（IPC 调用、内存访问等）。
 *
 * @param cap      能力指针（必须非 NULL）
 * @param required 所需权限位
 *
 * @return true 具备所需权限
 * @return false 权限不足
 *
 * @note 对应需求: KR-013
 */
static inline bool cap_rights_check(const cap_t *cap, uint8_t required)
{
    return ((cap->rights & required) == required);
}

/* ========================================================================
 * 线程迁移时能力上下文同步
 * ======================================================================== */

/**
 * @brief 线程迁移时的能力上下文同步
 *
 * @details 当线程从一个 CPU 迁移到另一个 CPU 时调用。
 *          确保迁移后的能力访问安全。
 *
 * @param thread_id 迁移的线程 ID
 * @param old_cpu   原 CPU 编号
 * @param new_cpu   新 CPU 编号
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: KR-013, MP-005
 */
kernel_status_t cap_migrate_context(uint32_t thread_id,
                                      uint32_t old_cpu,
                                      uint32_t new_cpu);

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
kernel_status_t cap_validate_rights_for_type(kobj_type_t type, uint8_t rights);

/* ========================================================================
 * 能力系统完整性自检
 * ======================================================================== */

/**
 * @brief 能力系统完整性自检结果
 */
typedef struct
{
    uint32_t total_caps;        /**< @brief 检查的能力总数 */
    uint32_t passed_checks;     /**< @brief 通过的检查数 */
    uint32_t failed_checks;     /**< @brief 失败的检查数 */
} cap_integrity_result_t;

/**
 * @brief 执行 CSpace 能力完整性自检
 *
 * @details 遍历指定 CSpace 的所有能力，检查以下不变式：
 *          a. 所有 VALID 能力的 parent_slot 指向 VALID 父能力或 CAP_SLOT_INVALID
 *          b. 所有 children 链表中的子能力确实以当前能力为 parent
 *          c. derive_depth 单调递增（子 >= 父 + 1）
 *          d. rights 单调递减（子权限是父权限的子集）
 *          e. 权限符合类型权限矩阵
 *
 *          在 CSpace 锁保护下执行，多核安全。
 *
 * @param cspace_root CSpace 根能力槽
 * @param result      输出检查结果
 *
 * @return KERNEL_OK 自检完成（不代表全部通过，查看 result）
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: KR-013
 */
kernel_status_t cap_integrity_check(cap_slot_t cspace_root,
                                      cap_integrity_result_t *result);

/* ========================================================================
 * 能力系统形式化验证条件注册
 * ======================================================================== */

/**
 * @brief 注册能力系统形式化验证不变式
 *
 * @details 向形式化验证框架注册 8 个能力系统核心不变式条件。
 *          应在 capability_subsys_init() 中调用。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: SE-007, SE-008
 */
kernel_status_t fv_register_cap_invariants(void);

#endif /* KERNEL_CAPABILITY_H */
