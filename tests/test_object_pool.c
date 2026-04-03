/**
 * @file    test_object_pool.c
 * @brief   AISafe64 RTOS - 内核对象池（Souls 分配器）单元测试（宿主机）
 * @author  AISafe64 Team
 * @date    2026-04-02
 * @version 1.0
 *
 * @details 内核对象池宿主机自包含测试
 *          测试与内核 object_pool.c 一致的逻辑：
 *          - 初始化（空闲栈填充、状态验证）
 *          - 分配（O(1) 弹出、池耗尽）
 *          - 释放（O(1) 压入、无效指针检测）
 *          - 所有权检查
 *          - 计数查询
 *          - 遍历已分配对象（泄漏检测）
 *          - 压力测试
 *
 * @note 对应需求: KR-021（对象池分配器）、KR-022（泄漏检测）、TF-001
 */

#include "mock_kernel.h"

/* ========================================================================
 * 内核对象池实现（宿主机自包含版本）
 *
 * @details 此实现与 kernel/mm/object_pool.c 逻辑完全一致，
 *          使用 mock_kernel.h 中的 TicketLock 和原子操作。
 * ======================================================================== */

#define OBJ_POOL_FOREACH_MAX_CAPACITY 1024U
#define OBJ_POOL_FOREACH_BITMAP_WORDS ((OBJ_POOL_FOREACH_MAX_CAPACITY + 31U) / 32U)

/**
 * @brief 初始化对象池
 * @details 与 kernel/mm/object_pool.c 一致
 */
static kernel_status_t object_pool_init(object_pool_t *pool,
                                        uint8_t *buffer,
                                        uint32_t obj_size,
                                        uint32_t capacity,
                                        uint32_t *free_stack)
{
    uint32_t i;

    if (pool == NULL)      { return -(int32_t)EINVAL; }
    if (buffer == NULL)    { return -(int32_t)EINVAL; }
    if (free_stack == NULL){ return -(int32_t)EINVAL; }
    if (obj_size == 0U)    { return -(int32_t)EINVAL; }
    if (capacity == 0U)    { return -(int32_t)EINVAL; }

    pool->buffer      = buffer;
    pool->obj_size    = obj_size;
    pool->capacity    = capacity;
    pool->free_stack  = free_stack;
    pool->free_count  = capacity;
    pool->alloc_count = 0U;

    ticket_lock_init(&pool->lock);

    /* 填充空闲栈：从 capacity-1 到 0 */
    for (i = 0U; i < capacity; i++)
    {
        pool->free_stack[i] = (capacity - 1U) - i;
    }

    (void)memset(buffer, 0, (size_t)(obj_size * capacity));

    return KERNEL_OK;
}

/**
 * @brief 从对象池分配一个对象
 * @details 与 kernel/mm/object_pool.c 一致
 */
static void *object_pool_alloc(object_pool_t *pool)
{
    uint32_t index;
    void *obj;

    if (pool == NULL)             { return NULL; }
    if (pool->buffer == NULL)     { return NULL; }
    if (pool->free_stack == NULL) { return NULL; }

    ticket_lock_acquire(&pool->lock);

    if (pool->free_count == 0U)
    {
        ticket_lock_release(&pool->lock);
        return NULL;
    }

    pool->free_count--;
    index = pool->free_stack[pool->free_count];
    pool->alloc_count++;

    ticket_lock_release(&pool->lock);

    obj = (void *)&pool->buffer[(uint64_t)index * (uint64_t)pool->obj_size];
    (void)memset(obj, 0, (size_t)pool->obj_size);

    return obj;
}

/**
 * @brief 释放对象到对象池
 * @details 与 kernel/mm/object_pool.c 一致
 */
static void object_pool_free(object_pool_t *pool, void *obj)
{
    uintptr_t obj_addr;
    uintptr_t buf_addr;
    uintptr_t offset;
    uint32_t index;

    if (pool == NULL)             { return; }
    if (obj == NULL)              { return; }
    if (pool->buffer == NULL)     { return; }
    if (pool->free_stack == NULL) { return; }

    obj_addr = (uintptr_t)obj;
    buf_addr = (uintptr_t)pool->buffer;
    offset   = obj_addr - buf_addr;

    if (offset >= ((uintptr_t)pool->obj_size * (uintptr_t)pool->capacity))
    {
        return;
    }

    if ((offset % (uintptr_t)pool->obj_size) != 0U)
    {
        return;
    }

    index = (uint32_t)(offset / (uintptr_t)pool->obj_size);

    ticket_lock_acquire(&pool->lock);

    if (pool->free_count >= pool->capacity)
    {
        ticket_lock_release(&pool->lock);
        return;
    }

    pool->free_stack[pool->free_count] = index;
    pool->free_count++;

    ticket_lock_release(&pool->lock);
}

/**
 * @brief 检查对象是否属于此池
 */
static bool object_pool_owns(object_pool_t *pool, const void *obj)
{
    uintptr_t obj_addr;
    uintptr_t buf_addr;
    uintptr_t offset;
    uintptr_t total_size;

    if (pool == NULL)         { return false; }
    if (obj == NULL)          { return false; }
    if (pool->buffer == NULL) { return false; }

    obj_addr   = (uintptr_t)obj;
    buf_addr   = (uintptr_t)pool->buffer;
    offset     = obj_addr - buf_addr;
    total_size = (uintptr_t)pool->obj_size * (uintptr_t)pool->capacity;

    if (offset >= total_size) { return false; }
    if ((offset % (uintptr_t)pool->obj_size) != 0U) { return false; }

    return true;
}

/**
 * @brief 获取对象池空闲数量
 */
static uint32_t object_pool_free_count(const object_pool_t *pool)
{
    if (pool == NULL) { return 0U; }
    return pool->free_count;
}

/**
 * @brief 获取对象池已使用数量
 */
static uint32_t object_pool_used_count(const object_pool_t *pool)
{
    if (pool == NULL) { return 0U; }
    return (pool->capacity - pool->free_count);
}

/**
 * @brief 遍历所有已分配的对象（泄漏检测）
 * @details 与 kernel/mm/object_pool.c 一致
 */
static void object_pool_foreach(object_pool_t *pool,
                                 void (*callback)(void *obj, void *arg),
                                 void *arg)
{
    uint32_t bitmap[OBJ_POOL_FOREACH_BITMAP_WORDS];
    uint32_t bitmap_words;
    uint32_t i;
    uint32_t index;
    uint32_t word_idx;
    uint32_t bit_idx;

    if (pool == NULL)             { return; }
    if (callback == NULL)         { return; }
    if (pool->buffer == NULL)     { return; }
    if (pool->free_stack == NULL) { return; }
    if (pool->capacity > OBJ_POOL_FOREACH_MAX_CAPACITY) { return; }

    bitmap_words = (pool->capacity + 31U) / 32U;

    ticket_lock_acquire(&pool->lock);

    (void)memset(bitmap, 0, (size_t)(bitmap_words * sizeof(uint32_t)));

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

    ticket_lock_release(&pool->lock);

    for (i = 0U; i < pool->capacity; i++)
    {
        word_idx = i / 32U;
        bit_idx  = i % 32U;

        if ((bitmap[word_idx] & (1UL << bit_idx)) == 0U)
        {
            void *obj = (void *)&pool->buffer[(uint64_t)i * (uint64_t)pool->obj_size];
            callback(obj, arg);
        }
    }
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试 1: 初始化成功，空闲数=总容量
 */
static void test_init_basic(void)
{
    object_pool_t pool;
    uint8_t buffer[128];
    uint32_t stack[16];

    kernel_status_t ret = object_pool_init(&pool, buffer, 8U, 16U, stack);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(pool.free_count, 16U);
    TEST_ASSERT_EQ(pool.alloc_count, 0U);
    TEST_ASSERT_EQ(pool.capacity, 16U);
    TEST_ASSERT_EQ(pool.obj_size, 8U);
}

/**
 * @brief 测试 2: 分配一个对象，空闲数减 1
 */
static void test_alloc_one(void)
{
    object_pool_t pool;
    uint8_t buffer[128];
    uint32_t stack[16];

    object_pool_init(&pool, buffer, 8U, 16U, stack);

    void *obj = object_pool_alloc(&pool);
    TEST_ASSERT_NOT_NULL(obj);
    TEST_ASSERT_EQ(pool.free_count, 15U);
    TEST_ASSERT_EQ(pool.alloc_count, 1U);
}

/**
 * @brief 测试 3: 分配全部对象成功
 */
static void test_alloc_all(void)
{
    object_pool_t pool;
    uint8_t buffer[64];
    uint32_t stack[8];
    void *objs[8];
    uint32_t i;

    object_pool_init(&pool, buffer, 8U, 8U, stack);

    for (i = 0U; i < 8U; i++)
    {
        objs[i] = object_pool_alloc(&pool);
        TEST_ASSERT_NOT_NULL(objs[i]);
    }

    TEST_ASSERT_EQ(pool.free_count, 0U);
}

/**
 * @brief 测试 4: 池耗尽后分配返回 NULL
 */
static void test_alloc_exhaust(void)
{
    object_pool_t pool;
    uint8_t buffer[64];
    uint32_t stack[8];
    uint32_t i;

    object_pool_init(&pool, buffer, 8U, 8U, stack);

    for (i = 0U; i < 8U; i++)
    {
        object_pool_alloc(&pool);
    }

    void *obj = object_pool_alloc(&pool);
    TEST_ASSERT_NULL(obj);
}

/**
 * @brief 测试 5: 释放一个对象，空闲数恢复
 */
static void test_free_basic(void)
{
    object_pool_t pool;
    uint8_t buffer[128];
    uint32_t stack[16];

    object_pool_init(&pool, buffer, 8U, 16U, stack);

    void *obj = object_pool_alloc(&pool);
    TEST_ASSERT_EQ(pool.free_count, 15U);

    object_pool_free(&pool, obj);
    TEST_ASSERT_EQ(pool.free_count, 16U);
}

/**
 * @brief 测试 6: 释放全部已分配对象
 */
static void test_free_all(void)
{
    object_pool_t pool;
    uint8_t buffer[64];
    uint32_t stack[8];
    void *objs[8];
    uint32_t i;

    object_pool_init(&pool, buffer, 8U, 8U, stack);

    for (i = 0U; i < 8U; i++)
    {
        objs[i] = object_pool_alloc(&pool);
    }
    TEST_ASSERT_EQ(pool.free_count, 0U);

    for (i = 0U; i < 8U; i++)
    {
        object_pool_free(&pool, objs[i]);
    }
    TEST_ASSERT_EQ(pool.free_count, 8U);
}

/**
 * @brief 测试 7: 释放非本池指针静默忽略
 */
static void test_free_invalid(void)
{
    object_pool_t pool;
    uint8_t buffer[128];
    uint32_t stack[16];
    uint8_t foreign[8];

    object_pool_init(&pool, buffer, 8U, 16U, stack);

    /* 释放不属于池的指针应静默忽略 */
    object_pool_free(&pool, &foreign[0]);
    /* 不应崩溃，空闲数不变 */
    TEST_ASSERT_EQ(pool.free_count, 16U);
}

/**
 * @brief 测试 8: 释放后可再次分配
 */
static void test_alloc_after_free(void)
{
    object_pool_t pool;
    uint8_t buffer[64];
    uint32_t stack[4];

    object_pool_init(&pool, buffer, 16U, 4U, stack);

    void *obj1 = object_pool_alloc(&pool);
    TEST_ASSERT_NOT_NULL(obj1);

    object_pool_free(&pool, obj1);
    TEST_ASSERT_EQ(pool.free_count, 4U);

    void *obj2 = object_pool_alloc(&pool);
    TEST_ASSERT_NOT_NULL(obj2);
    TEST_ASSERT_EQ(pool.free_count, 3U);
}

/**
 * @brief 测试 9: NULL 参数安全检查
 */
static void test_init_null(void)
{
    object_pool_t pool;
    uint8_t buffer[64];
    uint32_t stack[4];

    TEST_ASSERT_EQ(object_pool_init(NULL, buffer, 8U, 4U, stack), -(int32_t)EINVAL);
    TEST_ASSERT_EQ(object_pool_init(&pool, NULL, 8U, 4U, stack), -(int32_t)EINVAL);
    TEST_ASSERT_EQ(object_pool_init(&pool, buffer, 8U, 4U, NULL), -(int32_t)EINVAL);
    TEST_ASSERT_EQ(object_pool_init(&pool, buffer, 0U, 4U, stack), -(int32_t)EINVAL);
    TEST_ASSERT_EQ(object_pool_init(&pool, buffer, 8U, 0U, stack), -(int32_t)EINVAL);
}

/**
 * @brief 测试 10: 分配的对象可正确读写
 */
static void test_object_integrity(void)
{
    object_pool_t pool;
    uint8_t buffer[128];
    uint32_t stack[8];

    object_pool_init(&pool, buffer, 16U, 8U, stack);

    uint32_t *obj = (uint32_t *)object_pool_alloc(&pool);
    TEST_ASSERT_NOT_NULL(obj);

    /* 写入数据 */
    obj[0] = 0xDEADBEEFU;
    obj[1] = 0xCAFEBABEU;
    obj[2] = 0x12345678U;
    obj[3] = 0x87654321U;

    /* 读回验证 */
    TEST_ASSERT_EQ(obj[0], 0xDEADBEEFU);
    TEST_ASSERT_EQ(obj[1], 0xCAFEBABEU);
    TEST_ASSERT_EQ(obj[2], 0x12345678U);
    TEST_ASSERT_EQ(obj[3], 0x87654321U);
}

/**
 * @brief 测试 11: object_pool_owns 正确判断
 */
static void test_owns(void)
{
    object_pool_t pool;
    uint8_t buffer[128];
    uint32_t stack[8];
    uint8_t foreign[16];

    object_pool_init(&pool, buffer, 16U, 8U, stack);

    void *obj = object_pool_alloc(&pool);
    TEST_ASSERT_TRUE(object_pool_owns(&pool, obj));
    TEST_ASSERT_FALSE(object_pool_owns(&pool, &foreign[0]));
    TEST_ASSERT_FALSE(object_pool_owns(&pool, NULL));
    TEST_ASSERT_FALSE(object_pool_owns(NULL, obj));
}

/**
 * @brief 测试 12: 循环 500 次分配释放无泄漏
 */
static void test_stress_alloc_free(void)
{
    object_pool_t pool;
    uint8_t buffer[256];
    uint32_t stack[16];
    uint32_t i;

    object_pool_init(&pool, buffer, 16U, 16U, stack);

    for (i = 0U; i < 500U; i++)
    {
        void *obj = object_pool_alloc(&pool);
        TEST_ASSERT_NOT_NULL(obj);
        TEST_ASSERT_EQ(pool.free_count, 15U);

        object_pool_free(&pool, obj);
        TEST_ASSERT_EQ(pool.free_count, 16U);
    }
}

/**
 * @brief 遍历回调：计数已分配对象
 */
static void foreach_count_cb(void *obj, void *arg)
{
    (void)obj;
    uint32_t *cnt = (uint32_t *)arg;
    (*cnt)++;
}

/**
 * @brief 测试 13: 遍历已分配对象（泄漏检测基础）
 */
static void test_foreach_allocated(void)
{
    object_pool_t pool;
    uint8_t buffer[128];
    uint32_t stack[8];
    uint32_t count = 0U;
    uint32_t i;

    object_pool_init(&pool, buffer, 16U, 8U, stack);

    /* 分配 3 个对象 */
    for (i = 0U; i < 3U; i++)
    {
        (void)object_pool_alloc(&pool);
    }

    /* 遍历已分配对象 */
    object_pool_foreach(&pool, foreach_count_cb, &count);

    TEST_ASSERT_EQ(count, 3U);
}

/**
 * @brief 测试 14: free_count / used_count 查询
 */
static void test_count_queries(void)
{
    object_pool_t pool;
    uint8_t buffer[128];
    uint32_t stack[8];

    object_pool_init(&pool, buffer, 16U, 8U, stack);

    TEST_ASSERT_EQ(object_pool_free_count(&pool), 8U);
    TEST_ASSERT_EQ(object_pool_used_count(&pool), 0U);

    void *obj = object_pool_alloc(&pool);
    (void)obj;

    TEST_ASSERT_EQ(object_pool_free_count(&pool), 7U);
    TEST_ASSERT_EQ(object_pool_used_count(&pool), 1U);

    /* NULL 查询安全 */
    TEST_ASSERT_EQ(object_pool_free_count(NULL), 0U);
    TEST_ASSERT_EQ(object_pool_used_count(NULL), 0U);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("=== 内核对象池（Souls 分配器）测试 ===\n\n");

    test_init_basic();
    test_alloc_one();
    test_alloc_all();
    test_alloc_exhaust();
    test_free_basic();
    test_free_all();
    test_free_invalid();
    test_alloc_after_free();
    test_init_null();
    test_object_integrity();
    test_owns();
    test_stress_alloc_free();
    test_foreach_allocated();
    test_count_queries();

    TEST_SUMMARY("test_object_pool");

    return TEST_RESULT();
}
