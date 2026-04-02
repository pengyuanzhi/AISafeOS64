/**
 * @file    object_pool.h
 * @brief   内核对象池（Souls 分配器）接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了固定大小对象池（Souls 分配器）接口：
 *          - O(1) 分配和释放
 *          - 无内存碎片
 *          - 无动态分配
 *          - 支持泄漏检测
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-021（对象池分配器）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_OBJECT_POOL_H
#define KERNEL_OBJECT_POOL_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/spinlock.h>
#include <kernel/list.h>
#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * 对象池结构
 * ======================================================================== */

/**
 * @brief 内核对象池（Souls 分配器）
 *
 * @details 固定大小对象池，使用空闲索引栈管理空闲槽位。
 *          分配 O(1)（栈弹出），释放 O(1)（栈压入）。
 *          无碎片、无动态分配。
 *
 * @note 对应需求: KR-021
 */
typedef struct
{
    uint8_t         *buffer;        /**< @brief 内存缓冲区（对象数组） */
    uint32_t         obj_size;      /**< @brief 单个对象大小（字节） */
    uint32_t         capacity;      /**< @brief 池容量（最大对象数） */
    uint32_t        *free_stack;    /**< @brief 空闲索引栈 */
    uint32_t         free_count;    /**< @brief 空闲计数（栈顶指针） */
    uint32_t         alloc_count;   /**< @brief 累计分配数量 */
    TicketLock_t     lock;          /**< @brief 池锁 */
} object_pool_t;

/* ========================================================================
 * 对象池管理 API
 * ======================================================================== */

/**
 * @brief 初始化对象池
 *
 * @details 将对象池与预分配的缓冲区和空闲栈关联。
 *          调用前，buffer 和 free_stack 必须已分配好。
 *
 * @param pool     对象池指针
 * @param buffer   对象缓冲区（必须为 obj_size * capacity 字节）
 * @param obj_size 单个对象大小（字节）
 * @param capacity 池容量
 * @param free_stack 牍闲索引栈（必须为 capacity 个 uint32_t）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 *
 * @note 对应需求: KR-021
 */
kernel_status_t object_pool_init(object_pool_t *pool,
                                  uint8_t *buffer,
                                  uint32_t obj_size,
                                  uint32_t capacity,
                                  uint32_t *free_stack);

/**
 * @brief 从对象池分配一个对象
 *
 * @details O(1) 操作：从空闲栈弹出一个索引，返回对应槽位地址。
 *
 * @param pool 对象池指针
 *
 * @return 分配的对象指针，池满返回 NULL
 *
 * @note 对应需求: KR-021
 */
void *object_pool_alloc(object_pool_t *pool);

/**
 * @brief 释放对象到对象池
 *
 * @details O(1) 操作：计算索引，压入空闲栈。
 *
 * @param pool 对象池指针
 * @param obj  要释放的对象指针
 *
 * @note 对应需求: KR-021
 */
void object_pool_free(object_pool_t *pool, void *obj);

/**
 * @brief 检查对象是否属于此池
 *
 * @param pool 对象池指针
 * @param obj  对象指针
 *
 * @return true 属于此池
 */
bool object_pool_owns(object_pool_t *pool, const void *obj);

/**
 * @brief 获取对象池空闲数量
 *
 * @param pool 对象池指针
 *
 * @return 空闲对象数量
 */
uint32_t object_pool_free_count(const object_pool_t *pool);

/**
 * @brief 获取对象池使用数量
 *
 * @param pool 对象池指针
 *
 * @return 已分配对象数量
 */
uint32_t object_pool_used_count(const object_pool_t *pool);

/**
 * @brief 遍历所有已分配的对象
 *
 * @details 遍历对象池，对每个已分配的对象调用回调函数。
 *          用于泄漏检测（KR-022）。
 *
 * @param pool     对象池指针
 * @param callback 回调函数
 * @param arg      回调参数
 */
void object_pool_foreach(object_pool_t *pool,
                          void (*callback)(void *obj, void *arg),
                          void *arg);

#endif /* KERNEL_OBJECT_POOL_H */
