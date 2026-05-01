/**
 * @file    shm.h
 * @brief   共享内存管理接口
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @details 本文件定义了共享内存管理接口：
 *          - 共享内存创建（shm_create）
 *          - 共享内存销毁（shm_destroy）
 *          - 共享内存映射（shm_map）
 *          - 共享内存取消映射（shm_unmap）
 *          - 共享内存获取（shm_get）
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.2.1 - 零拷贝 IPC 实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_MM_SHM_H
#define KERNEL_MM_SHM_H

#include <kernel/types.h>
#include <kernel/kobject.h>
#include <kernel/spinlock.h>
#include <stdint.h>

/* ========================================================================
 * 映射标志
 * ======================================================================== */

/** @brief 共享映射（多个进程共享同一物理内存） */
#define SHM_MAP_SHARED      (1U << 0U)

/** @brief 私有映射（写时复制） */
#define SHM_MAP_PRIVATE     (1U << 1U)

/* ========================================================================
 * 共享内存对象结构
 * ======================================================================== */

/**
 * @brief 共享内存对象
 *
 * @details 共享内存对象描述了一块可以由多个进程共享的物理内存区域。
 */
typedef struct
{
    KObjHeader_t header;          /**< @brief 内核对象头部 */
    uint64_t     size;            /**< @brief 共享内存大小（字节） */
    uint64_t     phys_addr;       /**< @brief 物理基地址 */
    uint32_t     ref_count;       /**< @brief 映射引用计数 */
    struct list_head mappings;     /**< @brief 映射链表 */
    TicketLock_t lock;            /**< @brief 自旋锁 */
} shm_t;

/* ========================================================================
 * 共享内存映射结构
 * ======================================================================== */

/**
 * @brief 共享内存映射
 *
 * @brief 描述一个共享内存对象的映射实例。
 */
typedef struct shm_mapping
{
    struct list_head list;        /**< @brief 链表节点 */
    kobj_id_t       vm_space_id;  /**< @brief 虚拟地址空间 ID */
    uint64_t         vaddr;        /**< @brief 虚拟基地址 */
    uint64_t         size;        /**< @brief 映射大小 */
    uint32_t         flags;       /**< @brief 映射标志 */
} shm_mapping_t;

/* ========================================================================
 * 共享内存管理 API
 * ======================================================================== */

/**
 * @brief 初始化共享内存子系统
 *
 * @return KERNEL_OK 成功
 * @return -ENOMEM 内存不足
 */
kernel_status_t shm_subsys_init(void);

/**
 * @brief 创建共享内存对象
 *
 * @param size 共享内存大小（字节）
 * @param id   输出：共享内存对象 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOMEM 内存不足
 */
kernel_status_t shm_create(uint64_t size, kobj_id_t *id);

/**
 * @brief 销毁共享内存对象
 *
 * @param id 共享内存对象 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EBUSY  仍有映射存在
 */
kernel_status_t shm_destroy(kobj_id_t id);

/**
 * @brief 获取共享内存对象
 *
 * @param id 共享内存对象 ID
 *
 * @return 共享内存对象指针
 * @return NULL 未找到
 */
shm_t *shm_get(kobj_id_t id);

/**
 * @brief 映射共享内存到用户空间
 *
 * @param id        共享内存对象 ID
 * @param vm_space_id 虚拟地址空间 ID
 * @param vaddr     虚拟基地址（0 表示自动分配）
 * @param size      映射大小（0 表示整个共享内存）
 * @param flags     映射标志（SHM_MAP_SHARED / SHM_MAP_PRIVATE）
 * @param out_vaddr 输出：实际虚拟基地址
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOMEM 内存不足
 * @return -EPERM  权限不足
 */
kernel_status_t shm_map(kobj_id_t id, kobj_id_t vm_space_id,
                        uint64_t vaddr, uint64_t size, uint32_t flags,
                        uint64_t *out_vaddr);

/**
 * @brief 取消映射共享内存
 *
 * @param id        共享内存对象 ID
 * @param vm_space_id 虚拟地址空间 ID
 * @param vaddr     虚拟基地址
 * @param size      映射大小（0 表示取消所有映射）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t shm_unmap(kobj_id_t id, kobj_id_t vm_space_id,
                          uint64_t vaddr, uint64_t size);

#endif /* KERNEL_MM_SHM_H */
