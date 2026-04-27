/**
 * @file    test_musl_det_malloc.c
 * @brief   确定性内存分配器单元测试
 * @author  AISafe64 Team
 * @date    2026-04-27
 * @version 1.0
 *
 * @details 测试确定性内存分配器（det_malloc）的正确性和确定性：
 *          - 确定性分配：相同分配序列产生相同布局
 *          - 内存池固定大小：避免碎片
 *          - 分配失败处理：池耗尽时返回 NULL
 *          - 释放后内存可重用
 *          - 对齐要求（8 字节对齐）
 *
 * @note TDD REFACTOR 阶段：所有测试通过，持续保持 GREEN
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ==============================================================================
 * 测试框架（自定义，与项目现有测试风格一致）
 * ============================================================================== */
static int s_pass = 0;
static int s_fail = 0;
static int s_total = 0;

#define TEST_ASSERT(cond, msg) do { \
    s_total++; \
    if (cond) { \
        s_pass++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        s_fail++; \
        printf("  [FAIL] %s\n", msg); \
    } \
} while (0)

/* ==============================================================================
 * 确定性内存分配器接口声明
 *
 * 这些函数将在 det_malloc.c 中实现，替换标准 malloc/free/realloc/calloc。
 * 宿主机测试中我们直接链接这些函数。
 * ============================================================================== */

/**
 * @brief 初始化确定性内存分配器
 * @return 0 成功，-1 失败
 */
int det_malloc_init(void);

/**
 * @brief 销毁确定性内存分配器（释放资源）
 */
void det_malloc_destroy(void);

/**
 * @brief 确定性 malloc
 * @param size 请求的字节数
 * @return 分配的内存指针，失败返回 NULL
 */
void *det_malloc(size_t size);

/**
 * @brief 确定性 free
 * @param ptr 要释放的内存指针
 */
void det_free(void *ptr);

/**
 * @brief 确定性 calloc
 * @param nmemb 元素数量
 * @param size 每个元素大小
 * @return 分配的零初始化内存指针，失败返回 NULL
 */
void *det_calloc(size_t nmemb, size_t size);

/**
 * @brief 确定性 realloc
 * @param ptr 原始指针
 * @param size 新大小
 * @return 重新分配的内存指针，失败返回 NULL
 */
void *det_realloc(void *ptr, size_t size);

/**
 * @brief 获取分配器统计信息
 * @param total_pool_size 输出：内存池总大小
 * @param used_blocks 输出：已使用块数
 * @param free_blocks 输出：空闲块数
 */
void det_malloc_stats(size_t *total_pool_size, size_t *used_blocks, size_t *free_blocks);

/* ==============================================================================
 * 测试用例
 * ============================================================================== */

/**
 * @brief 测试 1：基本分配和释放
 */
static void test_basic_alloc_free(void)
{
    printf("\n[测试 1] 基本分配和释放\n");

    /* 初始化分配器 */
    int ret = det_malloc_init();
    TEST_ASSERT(ret == 0, "分配器初始化成功");

    /* 分配 64 字节 */
    void *p1 = det_malloc(64);
    TEST_ASSERT(p1 != NULL, "分配 64 字节成功");

    /* 写入数据验证可用 */
    memset(p1, 0xAA, 64);
    uint8_t *bytes = (uint8_t *)p1;
    int all_aa = 1;
    size_t i;
    for (i = 0; i < 64; i++)
    {
        if (bytes[i] != 0xAA)
        {
            all_aa = 0;
            break;
        }
    }
    TEST_ASSERT(all_aa, "写入并验证 64 字节数据");

    /* 释放 */
    det_free(p1);
    TEST_ASSERT(1, "释放 64 字节成功（无崩溃）");

    det_malloc_destroy();
}

/**
 * @brief 测试 2：8 字节对齐要求
 */
static void test_alignment(void)
{
    printf("\n[测试 2] 8 字节对齐要求\n");

    det_malloc_init();

    /* 分配不同大小，验证对齐 */
    void *p1 = det_malloc(1);
    void *p2 = det_malloc(3);
    void *p3 = det_malloc(7);
    void *p4 = det_malloc(13);
    void *p5 = det_malloc(100);

    TEST_ASSERT(p1 != NULL, "分配 1 字节成功");
    TEST_ASSERT(p2 != NULL, "分配 3 字节成功");
    TEST_ASSERT(p3 != NULL, "分配 7 字节成功");
    TEST_ASSERT(p4 != NULL, "分配 13 字节成功");
    TEST_ASSERT(p5 != NULL, "分配 100 字节成功");

    /* 验证所有返回地址 8 字节对齐 */
    TEST_ASSERT(((uintptr_t)p1 & 7U) == 0U, "p1 8字节对齐");
    TEST_ASSERT(((uintptr_t)p2 & 7U) == 0U, "p2 8字节对齐");
    TEST_ASSERT(((uintptr_t)p3 & 7U) == 0U, "p3 8字节对齐");
    TEST_ASSERT(((uintptr_t)p4 & 7U) == 0U, "p4 8字节对齐");
    TEST_ASSERT(((uintptr_t)p5 & 7U) == 0U, "p5 8字节对齐");

    det_free(p1);
    det_free(p2);
    det_free(p3);
    det_free(p4);
    det_free(p5);

    det_malloc_destroy();
}

/**
 * @brief 测试 3：确定性分配（相同序列产生相同布局）
 */
static void test_determinism(void)
{
    printf("\n[测试 3] 确定性分配（相同序列产生相同布局）\n");

    uintptr_t addrs_run1[8];
    uintptr_t addrs_run2[8];

    /* 第一次运行 */
    det_malloc_init();
    size_t i;
    for (i = 0; i < 8; i++)
    {
        void *p = det_malloc(64);
        TEST_ASSERT(p != NULL, "运行1: 分配成功");
        addrs_run1[i] = (uintptr_t)p;
    }
    for (i = 0; i < 8; i++)
    {
        det_free((void *)addrs_run1[i]);
    }
    det_malloc_destroy();

    /* 第二次运行（完全相同的分配序列） */
    det_malloc_init();
    for (i = 0; i < 8; i++)
    {
        void *p = det_malloc(64);
        TEST_ASSERT(p != NULL, "运行2: 分配成功");
        addrs_run2[i] = (uintptr_t)p;
    }
    for (i = 0; i < 8; i++)
    {
        det_free((void *)addrs_run2[i]);
    }
    det_malloc_destroy();

    /* 验证两次运行的地址完全相同 */
    int all_same = 1;
    for (i = 0; i < 8; i++)
    {
        if (addrs_run1[i] != addrs_run2[i])
        {
            all_same = 0;
            printf("    差异: run1[%zu]=0x%lx, run2[%zu]=0x%lx\n",
                   i, (unsigned long)addrs_run1[i],
                   i, (unsigned long)addrs_run2[i]);
            break;
        }
    }
    TEST_ASSERT(all_same, "两次运行产生完全相同的地址布局");
}

/**
 * @brief 测试 4：分配失败处理（池耗尽）
 */
static void test_pool_exhaustion(void)
{
    printf("\n[测试 4] 分配失败处理（池耗尽）\n");

    det_malloc_init();

    /* 获取内存池统计信息 */
    size_t pool_size = 0;
    size_t used = 0;
    size_t free_cnt = 0;
    det_malloc_stats(&pool_size, &used, &free_cnt);

    printf("    内存池大小: %zu 字节\n", pool_size);
    printf("    初始空闲块: %zu\n", free_cnt);
    TEST_ASSERT(pool_size > 0, "内存池大小大于 0");
    TEST_ASSERT(free_cnt > 0, "初始有可用空闲块");

    /* 每块 256 字节，4MB 池 = 16384 块 */
    /* 尝试分配超过池容量 */
    void *ptrs[17000]; /* 超过最大块数 */
    size_t alloc_count = 0;

    for (alloc_count = 0; alloc_count < 17000; alloc_count++)
    {
        ptrs[alloc_count] = det_malloc(256);
        if (ptrs[alloc_count] == NULL)
        {
            break;
        }
    }

    printf("    成功分配: %zu 块\n", alloc_count);
    TEST_ASSERT(alloc_count > 0, "至少分配了一些块");
    TEST_ASSERT(alloc_count < 17000, "最终分配失败（池耗尽）");

    /* 验证统计信息：0 空闲块 */
    det_malloc_stats(&pool_size, &used, &free_cnt);
    TEST_ASSERT(free_cnt == 0, "池耗尽后空闲块为 0");

    /* 再次分配应返回 NULL */
    void *overflow = det_malloc(256);
    TEST_ASSERT(overflow == NULL, "池耗尽后分配返回 NULL");

    /* 释放所有已分配的块 */
    size_t j;
    for (j = 0; j < alloc_count; j++)
    {
        det_free(ptrs[j]);
    }

    det_malloc_destroy();
}

/**
 * @brief 测试 5：释放后内存可重用
 */
static void test_free_reuse(void)
{
    printf("\n[测试 5] 释放后内存可重用\n");

    det_malloc_init();

    /* 分配 3 块 */
    void *p1 = det_malloc(256);
    void *p2 = det_malloc(256);
    void *p3 = det_malloc(256);

    TEST_ASSERT(p1 != NULL, "分配 p1 成功");
    TEST_ASSERT(p2 != NULL, "分配 p2 成功");
    TEST_ASSERT(p3 != NULL, "分配 p3 成功");

    uintptr_t orig_p2 = (uintptr_t)p2;

    /* 释放中间块 p2 */
    det_free(p2);
    TEST_ASSERT(1, "释放 p2 成功");

    /* 再次分配，应该能重用 p2 的空间 */
    void *p4 = det_malloc(256);
    TEST_ASSERT(p4 != NULL, "释放后重新分配成功");

    /* 验证重用了刚释放的空间（确定性分配器应按顺序重用） */
    TEST_ASSERT((uintptr_t)p4 == orig_p2, "重用了刚释放的 p2 空间");

    det_free(p1);
    det_free(p3);
    det_free(p4);

    det_malloc_destroy();
}

/**
 * @brief 测试 6：calloc 零初始化
 */
static void test_calloc(void)
{
    printf("\n[测试 6] calloc 零初始化\n");

    det_malloc_init();

    void *p = det_calloc(10, 32);
    TEST_ASSERT(p != NULL, "calloc(10, 32) 成功");

    /* 验证所有字节为零 */
    uint8_t *bytes = (uint8_t *)p;
    int all_zero = 1;
    size_t i;
    for (i = 0; i < 320; i++)
    {
        if (bytes[i] != 0)
        {
            all_zero = 0;
            break;
        }
    }
    TEST_ASSERT(all_zero, "calloc 返回的内存全为零");

    det_free(p);
    det_malloc_destroy();
}

/**
 * @brief 测试 7：realloc 扩展
 */
static void test_realloc(void)
{
    printf("\n[测试 7] realloc 扩展\n");

    det_malloc_init();

    /* 分配并写入数据 */
    void *p = det_malloc(64);
    TEST_ASSERT(p != NULL, "初始分配 64 字节成功");

    memset(p, 0xBB, 64);

    /* 扩展到 128 字节 */
    void *p2 = det_realloc(p, 128);
    TEST_ASSERT(p2 != NULL, "realloc 到 128 字节成功");

    /* 验证原始数据保留 */
    uint8_t *bytes = (uint8_t *)p2;
    int data_ok = 1;
    size_t i;
    for (i = 0; i < 64; i++)
    {
        if (bytes[i] != 0xBB)
        {
            data_ok = 0;
            break;
        }
    }
    TEST_ASSERT(data_ok, "realloc 后原始数据保留");

    det_free(p2);
    det_malloc_destroy();
}

/**
 * @brief 测试 8：边界情况
 */
static void test_edge_cases(void)
{
    printf("\n[测试 8] 边界情况\n");

    det_malloc_init();

    /* 分配 0 字节：应返回 NULL 或唯一指针 */
    void *p0 = det_malloc(0);
    TEST_ASSERT(p0 == NULL || p0 != NULL, "malloc(0) 不崩溃");

    /* 释放 NULL（应安全） */
    det_free(NULL);
    TEST_ASSERT(1, "free(NULL) 不崩溃");

    /* realloc NULL 等同 malloc */
    void *p_realloc_null = det_realloc(NULL, 64);
    TEST_ASSERT(p_realloc_null != NULL, "realloc(NULL, 64) 等同 malloc");

    /* realloc 到 0 等同 free */
    void *p_realloc_zero = det_realloc(p_realloc_null, 0);
    TEST_ASSERT(p_realloc_zero == NULL || p_realloc_zero != NULL,
                "realloc(ptr, 0) 不崩溃");

    /* 如果 realloc 到 0 没有释放，手动释放 */
    if (p_realloc_zero != NULL)
    {
        det_free(p_realloc_zero);
    }

    det_malloc_destroy();
}

/**
 * @brief 测试 9：大块分配（跨多个内存块）
 */
static void test_large_allocation(void)
{
    printf("\n[测试 9] 大块分配（跨多个内存块）\n");

    det_malloc_init();

    /* 分配超过单个块大小（256字节）的内存 */
    void *p1 = det_malloc(512);  /* 需要 2 个块 */
    TEST_ASSERT(p1 != NULL, "分配 512 字节（2 块）成功");

    void *p2 = det_malloc(1024); /* 需要 4 个块 */
    TEST_ASSERT(p2 != NULL, "分配 1024 字节（4 块）成功");

    void *p3 = det_malloc(4096); /* 需要 16 个块 */
    TEST_ASSERT(p3 != NULL, "分配 4096 字节（16 块）成功");

    /* 验证对齐 */
    TEST_ASSERT(((uintptr_t)p1 & 7U) == 0U, "512 字节分配 8 字节对齐");
    TEST_ASSERT(((uintptr_t)p2 & 7U) == 0U, "1024 字节分配 8 字节对齐");
    TEST_ASSERT(((uintptr_t)p3 & 7U) == 0U, "4096 字节分配 8 字节对齐");

    /* 写入数据验证可用 */
    memset(p1, 0x11, 512);
    memset(p2, 0x22, 1024);
    memset(p3, 0x33, 4096);
    TEST_ASSERT(((uint8_t *)p1)[511] == 0x11, "512 字节数据验证");
    TEST_ASSERT(((uint8_t *)p2)[1023] == 0x22, "1024 字节数据验证");
    TEST_ASSERT(((uint8_t *)p3)[4095] == 0x33, "4096 字节数据验证");

    det_free(p1);
    det_free(p2);
    det_free(p3);

    det_malloc_destroy();
}

/**
 * @brief 测试 10：分配统计信息正确性
 */
static void test_stats_accuracy(void)
{
    printf("\n[测试 10] 分配统计信息正确性\n");

    det_malloc_init();

    size_t pool_size = 0;
    size_t used = 0;
    size_t free_cnt = 0;

    /* 初始状态 */
    det_malloc_stats(&pool_size, &used, &free_cnt);
    TEST_ASSERT(pool_size == 4UL * 1024UL * 1024UL, "内存池大小 = 4MB");
    TEST_ASSERT(used == 0, "初始已使用块 = 0");
    size_t initial_free = free_cnt;
    printf("    初始空闲块: %zu\n", initial_free);

    /* 分配 100 字节：header(8) + 100 = 108 → 1 块 */
    void *p1 = det_malloc(100);
    det_malloc_stats(&pool_size, &used, &free_cnt);
    TEST_ASSERT(used == 1, "分配 100 字节后 used = 1");
    TEST_ASSERT(free_cnt == initial_free - 1, "空闲块减少 1");

    /* 分配 1024 字节：header(8) + 1024 = 1032 → 5 块 */
    void *p2 = det_malloc(1024);
    det_malloc_stats(&pool_size, &used, &free_cnt);
    TEST_ASSERT(used == 6, "分配 100+1024 字节后 used = 6");

    /* 释放 p1（1 块） */
    det_free(p1);
    det_malloc_stats(&pool_size, &used, &free_cnt);
    TEST_ASSERT(used == 5, "释放 p1 后 used = 5");
    TEST_ASSERT(free_cnt == initial_free - 5, "空闲块恢复 1");

    det_free(p2);
    det_malloc_destroy();
}

/* ==============================================================================
 * 主函数
 * ============================================================================== */
int main(void)
{
    printf("\n========== 确定性内存分配器测试 ==========\n");
    printf("TDD REFACTOR 阶段：验证所有测试通过\n");

    test_basic_alloc_free();
    test_alignment();
    test_determinism();
    test_pool_exhaustion();
    test_free_reuse();
    test_calloc();
    test_realloc();
    test_edge_cases();
    test_large_allocation();
    test_stats_accuracy();

    /* 结果汇总 */
    printf("\n========== 测试结果 ==========\n");
    printf("通过: %d\n", s_pass);
    printf("失败: %d\n", s_fail);
    printf("总计: %d\n", s_total);

    if (s_fail == 0)
    {
        printf("结果: ALL PASSED\n");
    }
    else
    {
        printf("结果: FAILED (%d 个测试失败)\n", s_fail);
    }
    printf("==============================\n\n");

    return (s_fail > 0) ? 1 : 0;
}
