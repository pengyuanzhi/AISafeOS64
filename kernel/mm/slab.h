/**
 * @file    slab.h
 * @brief   Slab 分配器接口
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @details Slab 分配器接口定义：
 *          - Slab 分配器创建/销毁
 *          - 对象分配/释放
 *          - 内存池管理
 *          - 统计信息查询
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.3 - 内存管理优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef AISAFE64_SLAB_H
#define AISAFE64_SLAB_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/types.h>
#include <kernel/spinlock.h>

/* ========================================================================
 * 配置
 * ========================================================================
 *
 * P0-2 修复说明：
 *   头文件原将 lock 字段声明为 void*（8 字节），而 slab.c 把它当作
 *   TicketLock_t（12 字节）使用，导致 ticket_lock_acquire/release
 *   越界写入相邻字段，造成内存踩踏。现已统一为 TicketLock_t 类型。
 *
 * 内存管理层次（自底向上）：
 *   phys_mem (buddy page allocator)
 *       ↓ phys_mem_alloc_pages 取页
 *   slab (对象缓存，本文件，从 buddy 取页存放对象)
 *       ↓ slab_alloc 取对象
 *   kmalloc (通用分配，独立空闲链表堆，用于无法预知大小的杂项分配)
 *
 * 层级修正说明：
 *   原 slab_create / slab_alloc 调用 kmalloc 获取页，导致层级倒置
 *   （slab 依赖 kmalloc，而 kmalloc 与 phys_mem 无关）。现改为直接从
 *   phys_mem 取页，避免循环依赖，恢复标准三层结构。
 *   kmalloc 仍保留独立的 __heap_start 堆，服务杂项小对象分配。
 */

#ifndef SLAB_OBJECT_SIZE
#define SLAB_OBJECT_SIZE  64  /* 对象大小 */
#endif

#ifndef SLAB_MAX_OBJ
#define SLAB_MAX_OBJ      8   /* 每个 Slab 最大对象数 */
#endif

#ifndef SLAB_CACHE_COUNT
#define SLAB_CACHE_COUNT  64  /* 缓存数量 */
#endif

/* ========================================================================
 * 数据结构
 * ======================================================================== */

/**
 * @brief Slab 缓存
 */
typedef struct slab_cache slab_cache_t;

/**
 * @brief Slab 缓存结构
 */
struct slab_cache
{
    void        *pool;        /**< @brief 内存池指针（phys_mem 页的线性映射虚拟地址） */
    size_t      pool_size;    /**< @brief 内存池大小（字节，请求值） */
    paddr_t     pool_paddr;   /**< @brief 内存池物理地址（释放时回传给 phys_mem） */
    uint32_t    pool_order;   /**< @brief 内存池 buddy 阶数（释放时回传给 phys_mem） */
    void        *slabs;       /**< @brief Slab 链表 */
    size_t      num_slabs;    /**< @brief Slab 节点数量 */
    size_t      alloc_count;  /**< @brief 已分配对象数量 */
    TicketLock_t lock;        /**< @brief 自旋锁（P0-2：原 void* 改为 TicketLock_t，避免越界踩踏） */
};

/* ========================================================================
 * 接口函数
 * ======================================================================== */

/**
 * @brief 创建 Slab 分配器
 *
 * @param cache Slab 缓存指针
 * @param pool_size 内存池大小（字节）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOMEM 内存不足
 */
int32_t slab_create(slab_cache_t *cache, size_t pool_size);

/**
 * @brief 销毁 Slab 分配器
 *
 * @param cache Slab 缓存指针
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
int32_t slab_destroy(slab_cache_t *cache);

/**
 * @brief 分配对象
 *
 * @param cache Slab 缓存指针
 *
 * @return 对象指针 成功
 * @return NULL 失败
 */
void* slab_alloc(slab_cache_t *cache);

/**
 * @brief 释放对象
 *
 * @param cache Slab 缓存指针
 * @param ptr 对象指针
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOSYS 对象未找到
 */
int32_t slab_free(slab_cache_t *cache, void *ptr);

/**
 * @brief 获取已分配对象数
 *
 * @param cache Slab 缓存指针
 *
 * @return 已分配对象数
 */
size_t slab_get_alloc_count(slab_cache_t *cache);

/**
 * @brief 初始化 Slab 分配器系统
 *
 * @return KERNEL_OK 成功
 * @return -ENOMEM 内存不足
 */
int32_t slab_system_init(void);

/**
 * @brief 关闭 Slab 分配器系统
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
int32_t slab_system_shutdown(void);

#endif /* AISAFE64_SLAB_H */
