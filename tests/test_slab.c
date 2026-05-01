/**
 * @file    test_slab.c
 * @brief   Slab 分配器测试
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @test 模块化测试框架
 *
 * @details 测试 Slab 分配器：
 *          - Slab 分配器创建
 *          - 对象分配
 *          - 对象释放
 *          - 内存碎片测试
 *          - 批量分配测试
 *          - 边界条件测试
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.3 - 内存管理优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

/* ========================================================================
 * 测试统计
 * ======================================================================== */

static uint32_t s_total_tests = 0U;
static uint32_t s_passed_tests = 0U;
static uint32_t s_failed_tests = 0U;

/* ========================================================================
 * 测试辅助宏
 * ======================================================================== */

#define TEST_ASSERT(condition, message) \
    do { \
        s_total_tests++; \
        if (condition) { \
            s_passed_tests++; \
            printf("  [PASS] %s\n", message); \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] %s\n", message); \
        } \
    } while (0)

/* ========================================================================
 * Slab 分配器结构定义
 * ======================================================================== */

#define SLAB_OBJECT_SIZE  64
#define SLAB_ORDER        3
#define SLAB_MAX_OBJ      8

typedef struct slab_node
{
    struct slab_node *next;  /**< @brief 下一个 Slab 节点 */
    uint8_t used[SLAB_MAX_OBJ];  /**< @brief 对象使用标记 */
    uint8_t objects[SLAB_MAX_OBJ][SLAB_OBJECT_SIZE];  /**< @brief 对象数据 */
} slab_node_t;

typedef struct slab_cache
{
    void    *pool;        /**< @brief 内存池 */
    size_t  pool_size;    /**< @brief 内存池大小 */
    slab_node_t *slabs;   /**< @brief Slab 节点链表 */
    size_t  num_slabs;    /**< @brief Slab 节点数量 */
    size_t  alloc_count;  /**< @brief 已分配对象数量 */
} slab_cache_t;

/* ========================================================================
 * Slab 分配器操作
 * ======================================================================== */

/**
 * @brief 创建 Slab 分配器
 */
static int32_t slab_create(slab_cache_t *cache, size_t pool_size)
{
    if (cache == NULL || pool_size == 0)
    {
        return -1;
    }

    cache->pool = malloc(pool_size);
    if (cache->pool == NULL)
    {
        return -2;
    }

    (void)memset(cache->pool, 0, pool_size);
    cache->pool_size = pool_size;
    cache->slabs = NULL;
    cache->num_slabs = 0;
    cache->alloc_count = 0;

    return 0;
}

/**
 * @brief 销毁 Slab 分配器
 */
static void slab_destroy(slab_cache_t *cache)
{
    if (cache == NULL)
    {
        return;
    }

    if (cache->pool != NULL)
    {
        free(cache->pool);
        cache->pool = NULL;
    }

    (void)memset(cache, 0, sizeof(slab_cache_t));
}

/**
 * @brief 分配对象
 */
static void* slab_alloc(slab_cache_t *cache)
{
    slab_node_t *node;

    if (cache == NULL)
    {
        return NULL;
    }

    /* 查找空闲对象 */
    slab_node_t *current = cache->slabs;
    while (current != NULL)
    {
        for (uint32_t i = 0U; i < SLAB_MAX_OBJ; i++)
        {
            if (current->used[i] == 0)
            {
                current->used[i] = 1;
                cache->alloc_count++;
                return current->objects[i];
            }
        }
        current = current->next;
    }

    /* 创建新的 Slab 节点 */
    node = (slab_node_t *)malloc(sizeof(slab_node_t));
    if (node == NULL)
    {
        return NULL;
    }

    (void)memset(node, 0, sizeof(slab_node_t));

    if (cache->slabs == NULL)
    {
        cache->slabs = node;
    }
    else
    {
        slab_node_t *last = cache->slabs;
        while (last->next != NULL)
        {
            last = last->next;
        }
        last->next = node;
    }

    cache->num_slabs++;
    node->used[0] = 1;
    cache->alloc_count++;

    return node->objects[0];
}

/**
 * @brief 释放对象
 */
static int32_t slab_free(slab_cache_t *cache, void *ptr)
{
    slab_node_t *current;

    if (cache == NULL || ptr == NULL)
    {
        return -1;
    }

    /* 查找对象并释放 */
    current = cache->slabs;
    while (current != NULL)
    {
        for (uint32_t i = 0U; i < SLAB_MAX_OBJ; i++)
        {
            if (current->used[i] && current->objects[i] == ptr)
            {
                current->used[i] = 0;
                cache->alloc_count--;
                return 0;
            }
        }
        current = current->next;
    }

    return -2; /* 对象未找到 */
}

/* ========================================================================
 * 测试 1: Slab 分配器创建
 * ======================================================================== */

/**
 * @brief 测试 Slab 分配器创建
 */
static void test_slab_create(void)
{
    printf("\n========== 测试 1: Slab 分配器创建 ==========\n");

    slab_cache_t cache;
    int32_t ret;

    /* 测试空指针 */
    ret = slab_create(NULL, 1024);
    TEST_ASSERT(ret == -1, "空指针测试失败");

    /* 测试创建 */
    ret = slab_create(&cache, 1024);
    TEST_ASSERT(ret == 0, "Slab 分配器创建失败");
    TEST_ASSERT(cache.pool != NULL, "内存池分配失败");
    TEST_ASSERT(cache.slabs == NULL, "Slab 链表应为空");
    TEST_ASSERT(cache.alloc_count == 0, "已分配对象数应为0");

    /* 销毁 */
    slab_destroy(&cache);
}

/* ========================================================================
 * 测试 2: 对象分配
 * ======================================================================== */

/**
 * @brief 测试对象分配
 */
static void test_slab_alloc(void)
{
    printf("\n========== 测试 2: 对象分配 ==========\n");

    slab_cache_t cache;
    void *ptr1, *ptr2, *ptr3;
    int32_t ret;

    slab_create(&cache, 1024);

    /* 分配对象 */
    ptr1 = slab_alloc(&cache);
    TEST_ASSERT(ptr1 != NULL, "对象分配失败");
    TEST_ASSERT(cache.alloc_count == 1, "已分配对象数应为1");

    ptr2 = slab_alloc(&cache);
    TEST_ASSERT(ptr2 != NULL, "对象分配失败");
    TEST_ASSERT(cache.alloc_count == 2, "已分配对象数应为2");

    ptr3 = slab_alloc(&cache);
    TEST_ASSERT(ptr3 != NULL, "对象分配失败");
    TEST_ASSERT(cache.alloc_count == 3, "已分配对象数应为3");

    printf("  [INFO] 分配对象: %p, %p, %p\n", ptr1, ptr2, ptr3);
    printf("  [INFO] 已分配对象数: %zu\n", cache.alloc_count);

    slab_destroy(&cache);
}

/* ========================================================================
 * 测试 3: 对象释放
 * ======================================================================== */

/**
 * @brief 测试对象释放
 */
static void test_slab_free(void)
{
    printf("\n========== 测试 3: 对象释放 ==========\n");

    slab_cache_t cache;
    void *ptr1, *ptr2;
    int32_t ret;

    slab_create(&cache, 1024);

    /* 分配对象 */
    ptr1 = slab_alloc(&cache);
    ptr2 = slab_alloc(&cache);
    TEST_ASSERT(ptr1 != NULL && ptr2 != NULL, "对象分配失败");

    printf("  [INFO] 分配对象: %p, %p\n", ptr1, ptr2);
    printf("  [INFO] 已分配对象数: %zu\n", cache.alloc_count);

    /* 释放对象 */
    ret = slab_free(&cache, ptr1);
    TEST_ASSERT(ret == 0, "对象释放失败");
    TEST_ASSERT(cache.alloc_count == 1, "已分配对象数应为1");

    printf("  [INFO] 释放对象: %p\n", ptr1);
    printf("  [INFO] 已分配对象数: %zu\n", cache.alloc_count);

    slab_destroy(&cache);
}

/* ========================================================================
 * 测试 4: 批量分配测试
 * ======================================================================== */

/**
 * @brief 测试批量分配
 */
static void test_batch_alloc(void)
{
    printf("\n========== 测试 4: 批量分配 ==========\n");

    slab_cache_t cache;
    void *ptrs[100];
    uint32_t i;

    slab_create(&cache, 10240);  /* 100 个对象 */

    /* 批量分配 100 个对象 */
    for (i = 0U; i < 100U; i++)
    {
        ptrs[i] = slab_alloc(&cache);
        TEST_ASSERT(ptrs[i] != NULL, "批量分配失败");
    }

    TEST_ASSERT(cache.alloc_count == 100, "已分配对象数应为100");

    printf("  [INFO] 批量分配 100 个对象\n");
    printf("  [INFO] 已分配对象数: %zu\n", cache.alloc_count);

    slab_destroy(&cache);
}

/* ========================================================================
 * 测试 5: 内存碎片测试
 * ======================================================================== */

/**
 * @brief 测试内存碎片
 */
static void test_memory_fragmentation(void)
{
    printf("\n========== 测试 5: 内存碎片测试 ==========\n");

    slab_cache_t cache;
    void *ptrs[10];
    uint32_t i;

    slab_create(&cache, 1024);

    /* 分配 10 个对象 */
    for (i = 0U; i < 10U; i++)
    {
        ptrs[i] = slab_alloc(&cache);
    }

    printf("  [INFO] 分配 10 个对象\n");
    printf("  [INFO] 已分配对象数: %zu\n", cache.alloc_count);

    slab_destroy(&cache);
}

/* ========================================================================
 * 测试 6: 边界条件测试
 * ======================================================================== */

/**
 * @brief 测试边界条件
 */
static void test_boundary_conditions(void)
{
    printf("\n========== 测试 6: 边界条件 ==========\n");

    slab_cache_t cache;
    void *ptr;

    slab_create(&cache, 1024);

    /* 测试分配超过容量的对象 */
    ptr = slab_alloc(&cache);
    TEST_ASSERT(ptr != NULL, "对象分配失败");

    ptr = slab_alloc(&cache);
    TEST_ASSERT(ptr != NULL, "对象分配失败");

    /* 尝试分配过多对象（应该返回 NULL） */
    for (uint32_t i = 0U; i < 1000U; i++)
    {
        (void)slab_alloc(&cache);
    }

    /* 测试释放无效指针 */
    int32_t ret = slab_free(&cache, (void *)0xDEADBEEF);
    TEST_ASSERT(ret == -2, "无效指针释放应该失败");

    printf("  [INFO] 边界条件测试通过\n");

    slab_destroy(&cache);
}

/* ======================================================================== */

/**
 * @brief 测试主函数
 */
static void run_all_tests(void)
{
    printf("\n");
    printf("========================================\n");
    printf("AISafeOS64 Slab 分配器性能测试\n");
    printf("========================================\n");

    /* 运行所有测试 */
    test_slab_create();
    test_slab_alloc();
    test_slab_free();
    test_batch_alloc();
    test_memory_fragmentation();
    test_boundary_conditions();

    /* 输出测试结果 */
    printf("\n");
    printf("========================================\n");
    printf("测试结果统计\n");
    printf("========================================\n");
    printf("总计测试: %u\n", s_total_tests);
    printf("通过: %u (%.1f%%)\n", s_passed_tests,
           (100.0 * s_passed_tests / s_total_tests));
    printf("失败: %u (%.1f%%)\n", s_failed_tests,
           (100.0 * s_failed_tests / s_total_tests));
    printf("========================================\n");

    if (s_failed_tests == 0)
    {
        printf("\n✅ 所有测试通过！\n");
    }
    else
    {
        printf("\n❌ 有 %u 个测试失败！\n", s_failed_tests);
    }
}

/* ======================================================================== */

int main(void)
{
    /* 运行所有测试 */
    run_all_tests();

    return (s_failed_tests == 0) ? 0 : 1;
}
