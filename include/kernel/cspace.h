/**
 * @file    cspace.h
 * @brief   能力空间（CSpace）管理接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了能力空间（CSpace）管理接口：
 *          - CSpace 创建/销毁
 *          - 能力表管理（插入/查找/移除）
 *          - 根能力管理
 *          - CSpace 树状结构
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-016
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_CSPACE_H
#define KERNEL_CSPACE_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/capability.h>
#include <kernel/kobject.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * CSpace 配置常量
 * ======================================================================== */

/** @brief 默认 CSpace 容量（能力槽数量） */
#define CSPACE_DEFAULT_CAPACITY     64U

/** @brief 最大 CSpace 容量 */
#define CSPACE_MAX_CAPACITY         256U

/** @brief CSpace 静态池最大数量 */
#define CONFIG_MAX_CSPACES          32U

/* ========================================================================
 * CSpace 结构
 * ======================================================================== */

/**
 * @brief 能力空间（CSpace）
 *
 * @details 每个进程拥有独立的 CSpace，管理该进程的所有能力。
 *          CSpace 本身也是内核对象（KOBJ_CSPACE），受能力保护。
 *          能力表使用静态数组实现，通过 cap_slot_t 索引。
 *
 * @note 对应需求: KR-016
 */
typedef struct
{
    KObjHeader_t    header;         /**< @brief 内核对象公共头部 */
    cap_t          *cap_table;      /**< @brief 能力表（能力描述符数组） */
    uint32_t        capacity;       /**< @brief 能力表容量 */
    uint32_t        used_count;     /**< @brief 已使用能力槽数量 */
    cap_slot_t      root_slot;      /**< @brief 根能力槽（指向自身） */
    cap_slot_t      free_head;      /**< @brief 空闲链表头（使用 cap_t.sibling 作为链表节点） */
    struct list_head child_cspaces; /**< @brief 子 CSpace 链表 */
    struct list_head cspace_node;   /**< @brief CSpace 链表节点 */
    TicketLock_t    lock;           /**< @brief CSpace 锁 */
} cspace_t;

/* ========================================================================
 * CSpace 管理 API
 * ======================================================================== */

/**
 * @brief 初始化 CSpace 子系统
 *
 * @details 初始化 CSpace 静态池和相关数据结构。
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: KR-016
 */
kernel_status_t cspace_subsys_init(void);

/**
 * @brief 创建新的能力空间
 *
 * @details 分配一个 CSpace，初始化能力表。
 *          第 0 号槽为根能力（指向 CSpace 自身）。
 *
 * @param capacity 能力表容量
 * @param[out] out_cspace 输出 CSpace 指针
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOMEM 内存不足
 *
 * @note 对应需求: KR-016
 */
kernel_status_t cspace_create(uint32_t capacity, cspace_t **out_cspace);

/**
 * @brief 销毁能力空间
 *
 * @details 撤销所有能力，释放能力表和 CSpace 结构。
 *
 * @param cspace 要销毁的 CSpace
 *
 * @note 对应需求: KR-016
 */
void cspace_destroy(cspace_t *cspace);

/**
 * @brief 在 CSpace 中分配一个空闲能力槽
 *
 * @param cspace CSpace 指针
 * @param[out] out_slot 输出分配的槽索引
 *
 * @return KERNEL_OK 成功
 * @return -ENOMEM 无空闲槽
 */
kernel_status_t cspace_alloc_slot(cspace_t *cspace, cap_slot_t *out_slot);

/**
 * @brief 释放能力槽到空闲链表
 *
 * @param cspace CSpace 指针
 * @param slot   要释放的槽索引
 */
void cspace_free_slot(cspace_t *cspace, cap_slot_t slot);

/**
 * @brief 在指定槽位插入能力
 *
 * @param cspace   CSpace 指针
 * @param slot     目标槽索引
 * @param kobj_type 内核对象类型
 * @param kobj_id  内核对象 ID
 * @param rights   权限位
 * @param badge    标识
 * @param parent_slot 父能力槽（CAP_SLOT_INVALID 表示根能力）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t cspace_insert_cap(cspace_t *cspace,
                                   cap_slot_t slot,
                                   kobj_type_t kobj_type,
                                   kobj_id_t kobj_id,
                                   uint8_t rights,
                                   uint16_t badge,
                                   cap_slot_t parent_slot);

/**
 * @brief 查找能力
 *
 * @param cspace CSpace 指针
 * @param slot   能力槽索引
 *
 * @return 能力指针，未找到返回 NULL
 */
cap_t *cspace_lookup(cspace_t *cspace, cap_slot_t slot);

/**
 * @brief 从槽索引解析 CSpace
 *
 * @param cspace_root CSpace 根能力槽索引
 *
 * @return CSpace 指针，无效返回 NULL
 */
cspace_t *cspace_from_root(cap_slot_t cspace_root);

/**
 * @brief 获取当前线程的 CSpace
 *
 * @return 当前 CSpace 指针
 */
cspace_t *cspace_get_current(void);

#endif /* KERNEL_CSPACE_H */
