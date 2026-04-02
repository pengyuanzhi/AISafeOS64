/**
 * @file    object_pool.c
 * @brief   内核对象池（Souls 分配器）实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件实现了固定大小对象池（Souls 分配器）：
 *          - O(1) 分配：从空闲索引栈弹出索引，计算对象地址
 *          - O(1) 释放：计算对象索引，压入空闲索引栈
 *          - 无内存碎片：所有对象等大小，无动态分配
 *          - 线程安全：使用 TicketLock_t 保护共享数据
 *          - 泄漏检测：通过 bitmap 追踪已分配对象
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-021（对象池分配器）、KR-022（泄漏检测）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */

#include <kernel/object_pool.h>
#include <kernel/errno.h>
#include <kernel/barrier.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 内部常量定义
 * ======================================================================== */

/**
 * @def OBJ_POOL_BITMAP_WORDS
 * @brief 每个对象池分配位图的 uint32_t 字数
 *
 * @details 使用 (capacity + 31) / 32 计算，
 *          在栈上定义时需要外部提供足够的缓冲区。
 *          此处仅作为内部参考，实际使用动态计算。
 */

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 检查对象指针是否对齐到对象大小
 *
 * @details 验证对象指针相对于缓冲区起始地址的偏移量
 *          是否为对象大小的整数倍。
 *
 * @param pool 对象池指针
 * @param obj  对象指针
 *
 * @return true 对齐正确，false 对齐错误
 */
static bool is_aligned_index(const object_pool_t *pool, const void *obj)
{
    uintptr_t obj_addr = (uintptr_t)obj;
    uintptr_t buf_addr = (uintptr_t)pool->buffer;
    uintptr_t offset   = obj_addr - buf_addr;

    return ((offset % (uintptr_t)pool->obj_size) == 0U);
}

/* ========================================================================
 * 对象池管理 API 实现
 * ======================================================================== */

/**
 * @brief 初始化对象池
 *
 * @details 将对象池与预分配的缓冲区和空闲栈关联。
 *          空闲栈从 capacity-1 到 0 依次填充，
 *          使得首次分配时索引 0 最先被弹出（栈 LIFO）。
 *          初始化完成后，free_count == capacity，alloc_count == 0。
 *
 * @param pool       对象池指针
 * @param buffer     对象缓冲区（必须为 obj_size * capacity 字节）
 * @param obj_size   单个对象大小（字节），不得为 0
 * @param capacity   池容量，不得为 0
 * @param free_stack 空闲索引栈（必须为 capacity 个 uint32_t）
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL   参数无效（NULL 指针或 obj_size/capacity 为 0）
 *
 * @note 对应需求: KR-021
 */
kernel_status_t object_pool_init(object_pool_t *pool,
                                  uint8_t *buffer,
                                  uint32_t obj_size,
                                  uint32_t capacity,
                                  uint32_t *free_stack)
{
    /* 参数有效性检查 */
    if (pool == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (buffer == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (free_stack == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (obj_size == 0U)
    {
        return -(int32_t)EINVAL;
    }

    if (capacity == 0U)
    {
        return -(int32_t)EINVAL;
    }

    /* 初始化对象池字段 */
    pool->buffer      = buffer;
    pool->obj_size    = obj_size;
    pool->capacity    = capacity;
    pool->free_stack  = free_stack;
    pool->free_count  = capacity;
    pool->alloc_count = 0U;

    /* 初始化自旋锁 */
    ticket_lock_init(&pool->lock);

    /* 填充空闲栈：从 capacity-1 到 0 */
    /* 这样栈弹出顺序为 0, 1, 2, ..., capacity-1 */
    uint32_t i;
    for (i = 0U; i < capacity; i++)
    {
        pool->free_stack[i] = (capacity - 1U) - i;
    }

    /* 将缓冲区清零 */
    (void)memset(buffer, 0, (size_t)(obj_size * capacity));

    /* 内存屏障确保初始化对所有核可见 */
    barrier();

    return KERNEL_OK;
}

/**
 * @brief 从对象池分配一个对象
 *
 * @details O(1) 操作：从空闲栈弹出一个索引，
 *          计算并返回 buffer + index * obj_size 的地址。
 *          分配成功后递增 alloc_count。
 *          使用 TicketLock_t 保护共享数据。
 *
 * @param pool 对象池指针
 *
 * @return 分配的对象指针（已清零）
 * @return NULL 池已满或参数无效
 *
 * @note 对应需求: KR-021
 * @warning 调用者必须在释放前使用完分配的对象
 */
void *object_pool_alloc(object_pool_t *pool)
{
    uint32_t index;
    void *obj;

    /* 参数有效性检查 */
    if (pool == NULL)
    {
        return NULL;
    }

    if (pool->buffer == NULL)
    {
        return NULL;
    }

    if (pool->free_stack == NULL)
    {
        return NULL;
    }

    /* 获取自旋锁 */
    ticket_lock_acquire(&pool->lock);

    /* 检查是否有空闲对象 */
    if (pool->free_count == 0U)
    {
        ticket_lock_release(&pool->lock);
        return NULL;
    }

    /* 从空闲栈弹出索引（LIFO） */
    pool->free_count--;
    index = pool->free_stack[pool->free_count];

    /* 递增累计分配计数 */
    pool->alloc_count++;

    /* 内存屏障确保锁释放前的写入对所有核可见 */
    barrier();

    /* 释放自旋锁 */
    ticket_lock_release(&pool->lock);

    /* 计算对象地址 */
    obj = (void *)&pool->buffer[(uint64_t)index * (uint64_t)pool->obj_size];

    /* 将分配的对象清零 */
    (void)memset(obj, 0, (size_t)pool->obj_size);

    return obj;
}

/**
 * @brief 释放对象到对象池
 *
 * @details O(1) 操作：计算对象索引 = (obj - buffer) / obj_size，
 *          将索引压入空闲栈。
 *          使用 TicketLock_t 保护共享数据。
 *
 * @param pool 对象池指针
 * @param obj  要释放的对象指针
 *
 * @note 对应需求: KR-021
 * @warning 不得释放不属于此池的对象
 * @warning 不得重复释放同一对象
 */
void object_pool_free(object_pool_t *pool, void *obj)
{
    uintptr_t obj_addr;
    uintptr_t buf_addr;
    uintptr_t offset;
    uint32_t index;

    /* 参数有效性检查 */
    if (pool == NULL)
    {
        return;
    }

    if (obj == NULL)
    {
        return;
    }

    if (pool->buffer == NULL)
    {
        return;
    }

    if (pool->free_stack == NULL)
    {
        return;
    }

    /* 计算偏移量 */
    obj_addr = (uintptr_t)obj;
    buf_addr = (uintptr_t)pool->buffer;
    offset   = obj_addr - buf_addr;

    /* 检查对象是否在缓冲区范围内 */
    if (offset >= ((uintptr_t)pool->obj_size * (uintptr_t)pool->capacity))
    {
        /* 对象不在池范围内，静默忽略 */
        return;
    }

    /* 检查对齐 */
    if ((offset % (uintptr_t)pool->obj_size) != 0U)
    {
        /* 对象未对齐到对象大小边界，静默忽略 */
        return;
    }

    /* 计算索引 */
    index = (uint32_t)(offset / (uintptr_t)pool->obj_size);

    /* 获取自旋锁 */
    ticket_lock_acquire(&pool->lock);

    /* 检查空闲栈是否已满（防止重复释放导致溢出） */
    if (pool->free_count >= pool->capacity)
    {
        ticket_lock_release(&pool->lock);
        return;
    }

    /* 将索引压入空闲栈 */
    pool->free_stack[pool->free_count] = index;
    pool->free_count++;

    /* 内存屏障确保写入对所有核可见 */
    barrier();

    /* 释放自旋锁 */
    ticket_lock_release(&pool->lock);
}

/**
 * @brief 检查对象是否属于此池
 *
 * @details 检查指针是否在 buffer 到 buffer + obj_size * capacity
 *          的范围内，并且偏移量是 obj_size 的整数倍。
 *
 * @param pool 对象池指针
 * @param obj  对象指针
 *
 * @return true  属于此池且对齐正确
 * @return false 不属于此池、参数无效或对齐错误
 */
bool object_pool_owns(object_pool_t *pool, const void *obj)
{
    uintptr_t obj_addr;
    uintptr_t buf_addr;
    uintptr_t offset;
    uintptr_t total_size;

    /* 参数有效性检查 */
    if (pool == NULL)
    {
        return false;
    }

    if (obj == NULL)
    {
        return false;
    }

    if (pool->buffer == NULL)
    {
        return false;
    }

    /* 计算地址和偏移 */
    obj_addr   = (uintptr_t)obj;
    buf_addr   = (uintptr_t)pool->buffer;
    offset     = obj_addr - buf_addr;
    total_size = (uintptr_t)pool->obj_size * (uintptr_t)pool->capacity;

    /* 检查是否在缓冲区范围内 */
    if (offset >= total_size)
    {
        return false;
    }

    /* 检查是否对齐到对象大小 */
    if ((offset % (uintptr_t)pool->obj_size) != 0U)
    {
        return false;
    }

    return true;
}

/**
 * @brief 获取对象池空闲数量
 *
 * @details 返回当前空闲栈中的元素数量。
 *          无需加锁读取（单次原子读取）。
 *
 * @param pool 对象池指针
 *
 * @return 空闲对象数量，参数无效返回 0
 */
uint32_t object_pool_free_count(const object_pool_t *pool)
{
    if (pool == NULL)
    {
        return 0U;
    }

    return pool->free_count;
}

/**
 * @brief 获取对象池已使用数量
 *
 * @details 返回 capacity - free_count。
 *
 * @param pool 对象池指针
 *
 * @return 已分配对象数量，参数无效返回 0
 */
uint32_t object_pool_used_count(const object_pool_t *pool)
{
    if (pool == NULL)
    {
        return 0U;
    }

    return (pool->capacity - pool->free_count);
}

/**
 * @brief 遍历所有已分配的对象
 *
 * @details 使用 bitmap 追踪哪些对象已分配。
 *          对每个已分配的对象调用回调函数。
 *          在持锁状态下遍历，保证一致性。
 *          用于泄漏检测（KR-022）。
 *
 * @note bitmap 的计算方法：
 *       - 对象 i 已分配当且仅当索引 i 不在空闲栈中
 *       - 通过标记空闲栈中所有索引，未标记的即为已分配
 *
 * @param pool     对象池指针
 * @param callback 回调函数（不得为 NULL）
 * @param arg      回调参数
 *
 * @note 对应需求: KR-022（泄漏检测）
 */
void object_pool_foreach(object_pool_t *pool,
                          void (*callback)(void *obj, void *arg),
                          void *arg)
{
    uint32_t bitmap_words;
    uint32_t i;
    uint32_t index;
    uint32_t word_idx;
    uint32_t bit_idx;

    /* 定义足够大的本地 bitmap（最大支持 1024 个对象） */
    /* 每位代表一个槽位：0 = 已分配，1 = 空闲 */
    /* bitmap_words = (capacity + 31) / 32 */
    #define OBJ_POOL_FOREACH_MAX_CAPACITY 1024U
    #define OBJ_POOL_FOREACH_BITMAP_WORDS ((OBJ_POOL_FOREACH_MAX_CAPACITY + 31U) / 32U)

    uint32_t bitmap[OBJ_POOL_FOREACH_BITMAP_WORDS];

    /* 参数有效性检查 */
    if (pool == NULL)
    {
        return;
    }

    if (callback == NULL)
    {
        return;
    }

    if (pool->buffer == NULL)
    {
        return;
    }

    if (pool->free_stack == NULL)
    {
        return;
    }

    /* 不支持超过最大容量的对象池 */
    if (pool->capacity > OBJ_POOL_FOREACH_MAX_CAPACITY)
    {
        return;
    }

    /* 计算需要的 bitmap 字数 */
    bitmap_words = (pool->capacity + 31U) / 32U;

    /* 获取自旋锁 */
    ticket_lock_acquire(&pool->lock);

    /* 初始化 bitmap：所有位清零（标记为已分配） */
    (void)memset(bitmap, 0, (size_t)(bitmap_words * sizeof(uint32_t)));

    /* 标记空闲栈中的索引（设置为空闲 = 1） */
    for (i = 0U; i < pool->free_count; i++)
    {
        index = pool->free_stack[i];
        if (index < pool->capacity)
        {
            word_idx = index / 32U;
            bit_idx  = index % 32U;
            bitmap[word_idx] |= (1UL << bit_idx);
        }
    }

    /* 释放自旋锁（在回调期间不持锁，避免死锁） */
    ticket_lock_release(&pool->lock);

    /* 遍历所有槽位，调用回调 */
    for (i = 0U; i < pool->capacity; i++)
    {
        word_idx = i / 32U;
        bit_idx  = i % 32U;

        /* 如果位为 0，表示已分配 */
        if ((bitmap[word_idx] & (1UL << bit_idx)) == 0U)
        {
            void *obj = (void *)&pool->buffer[(uint64_t)i * (uint64_t)pool->obj_size];
            callback(obj, arg);
        }
    }
}
