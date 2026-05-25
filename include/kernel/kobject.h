/**
 * @file    kobject.h
 * @brief   内核对象统一类型系统接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了微内核的统一内核对象类型系统：
 *          - 内核对象公共头部（KObjHeader_t）
 *          - 14 种内核对象类型标识（新增 FD/INODE/MEMORY_REGION）
 *          - 原子引用计数
 *          - 对象生命周期管理 API
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-017~022
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_KOBJECT_H
#define KERNEL_KOBJECT_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <stdint.h>

/* ========================================================================
 * 内核对象类型枚举
 * ======================================================================== */

/**
 * @brief 内核对象类型标识
 *
 * @details 每种内核对象都有唯一的类型标识，
 *          用于运行时类型检查和安全转换。
 *
 * @note 对应需求: KR-017
 */
typedef enum
{
    KOBJ_THREAD = 0U,          /**< @brief 线程 */
    KOBJ_ENDPOINT,             /**< @brief IPC 端点 */
    KOBJ_NOTIFICATION,         /**< @brief 通知对象 */
    KOBJ_CSPACE,               /**< @brief 能力空间 */
    KOBJ_VM_SPACE,             /**< @brief 虚拟地址空间 */
    KOBJ_PAGE_FRAME,           /**< @brief 页帧 */
    KOBJ_INTERRUPT,            /**< @brief 中断对象 */
    KOBJ_DEVICE,               /**< @brief 设备对象 */
    KOBJ_CHANNEL,              /**< @brief IPC 通道 */
    KOBJ_CONNECTION,           /**< @brief IPC 连接 */
    KOBJ_SHM,                  /**< @brief 共享内存对象 */
    KOBJ_FD,                   /**< @brief 文件描述符 */
    KOBJ_INODE,                /**< @brief 文件 Inode */
    KOBJ_MEMORY_REGION,        /**< @brief 内存区域 */
    KOBJ_TYPE_COUNT            /**< @brief 类型总数（用于边界检查） */
} kobj_type_t;

/* ========================================================================
 * 内核对象公共头部
 * ======================================================================== */

/**
 * @brief 内核对象公共头部
 *
 * @details 所有内核对象结构体必须以此头部为首成员。
 *          提供统一的类型标识、引用计数和对象 ID。
 *
 * @note 对应需求: KR-017（统一类型系统）、KR-018（引用计数）
 *
 * @par 示例
 * @code
 * typedef struct
 * {
 *     KObjHeader_t header;   // 必须为首成员
 *     // ... 特定类型字段
 * } MyObject_t;
 * @endcode
 */
typedef struct
{
    kobj_type_t     type;           /**< @brief 对象类型 */
    kobj_id_t       id;             /**< @brief 对象 ID（全局唯一） */
    volatile int32_t ref_count;      /**< @brief 原子引用计数 */
    kobj_id_t       parent_id;      /**< @brief 父对象 ID（KR-019 依赖追踪） */
    struct list_head children;       /**< @brief 子对象链表 */
    struct list_head sibling;        /**< @brief 兄弟链表节点 */
    struct list_head global_node;    /**< @brief 全局对象链表节点 */
} KObjHeader_t;

/* ========================================================================
 * 引用计数操作
 * ======================================================================== */

/**
 * @brief 增加对象引用计数
 *
 * @param obj 内核对象头部指针
 *
 * @return 增加后的引用计数
 *
 * @note 原子操作，多核安全
 * @note 对应需求: KR-018
 */
int32_t kobj_ref_inc(KObjHeader_t *obj);

/**
 * @brief 减少对象引用计数
 *
 * @details 引用计数归零时触发对象销毁回调。
 *          销毁回调负责释放对象资源和级联处理子对象。
 *
 * @param obj 内核对象头部指针
 *
 * @return 减少后的引用计数
 *
 * @note 原子操作，多核安全
 * @note 对应需求: KR-018, KR-019
 */
int32_t kobj_ref_dec(KObjHeader_t *obj);

/**
 * @brief 获取对象引用计数
 *
 * @param obj 内核对象头部指针
 *
 * @return 当前引用计数
 */
int32_t kobj_ref_count(const KObjHeader_t *obj);

/* ========================================================================
 * 内核对象生命周期管理 API
 * ======================================================================== */

/**
 * @brief 初始化内核对象子系统
 *
 * @details 初始化全局对象表、对象池和链表。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: KR-017
 */
kernel_status_t kobject_subsys_init(void);

/**
 * @brief 初始化内核对象头部
 *
 * @param obj       对象头部指针
 * @param type      对象类型
 * @param id        对象 ID
 * @param parent_id 父对象 ID（KOBJ_ID_INVALID 表示无父对象）
 *
 * @note 对应需求: KR-017, KR-018
 */
void kobj_header_init(KObjHeader_t *obj,
                       kobj_type_t type,
                       kobj_id_t id,
                       kobj_id_t parent_id);

/**
 * @brief 添加子对象
 *
 * @details 建立父子关系，用于级联销毁。
 *
 * @param parent 父对象
 * @param child  子对象
 *
 * @note 对应需求: KR-019
 */
void kobj_add_child(KObjHeader_t *parent, KObjHeader_t *child);

/**
 * @brief 移除子对象
 *
 * @param parent 父对象
 * @param child  子对象
 */
void kobj_remove_child(KObjHeader_t *parent, KObjHeader_t *child);

/**
 * @brief 销毁对象（内部调用）
 *
 * @details 引用计数归零时由 kobj_ref_dec 调用。
 *          遍历并级联销毁所有子对象，然后释放对象。
 *
 * @param obj 要销毁的对象
 *
 * @note 对应需求: KR-019
 */
void kobj_destroy(KObjHeader_t *obj);

/**
 * @brief 根据类型和 ID 查找内核对象
 *
 * @param type 对象类型
 * @param id   对象 ID
 *
 * @return 对象头部指针，未找到返回 NULL
 */
KObjHeader_t *kobj_find(kobj_type_t type, kobj_id_t id);

/**
 * @brief 检查对象类型是否匹配
 *
 * @param obj  对象指针
 * @param type 期望的类型
 *
 * @return true 类型匹配
 */
bool kobj_check_type(const KObjHeader_t *obj, kobj_type_t type);

/* ========================================================================
 * 泄漏检测（KR-022）
 * ======================================================================== */

/**
 * @brief 枚举所有活跃内核对象
 *
 * @param type  要枚举的类型（KOBJ_TYPE_COUNT 表示所有类型）
 * @param count 输出活跃对象数量
 *
 * @note 对应需求: KR-022
 */
void kobj_enum_active(kobj_type_t type, uint32_t *count);

/**
 * @brief 检测孤立对象（无父对象且无引用）
 *
 * @param count 输出孤立对象数量
 *
 * @note 对应需求: KR-022
 */
void kobj_detect_orphans(uint32_t *count);

#endif /* KERNEL_KOBJECT_H */
