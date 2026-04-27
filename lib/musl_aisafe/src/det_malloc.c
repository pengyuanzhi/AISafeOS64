/**
 * @file    det_malloc.c
 * @brief   确定性内存分配器实现
 * @author  AISafe64 Team
 * @date    2026-04-27
 * @version 1.2
 *
 * @details 使用固定大小内存池 + 位图管理的确定性内存分配器。
 *          - 固定 4MB 内存池，无动态扩展
 *          - 256 字节内存块，位图跟踪使用状态
 *          - 分配时按顺序查找第一个空闲块（确定性）
 *          - 支持跨块分配（大块请求）
 *          - 8 字节对齐保证
 *          - 每个分配带头部记录块数，支持正确释放
 *
 * @par 确定性保证
 * 相同的分配/释放序列始终产生相同的内存布局，
 * 满足 ISO 26262 ASIL-D 对确定性行为的要求。
 *
 * @note MISRA-C:2012 合规
 * @note 无递归、无动态内存分配（除内存池本身）
 * @warning 内存池大小固定，不支持运行时调整
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================
 * 常量定义
 * ============================================================================== */

/** @brief 内存池总大小：4MB */
#define DET_POOL_SIZE       (4UL * 1024UL * 1024UL)

/** @brief 内存块大小：256 字节 */
#define DET_BLOCK_SIZE      256UL

/** @brief 总块数 */
#define DET_BLOCK_COUNT     (DET_POOL_SIZE / DET_BLOCK_SIZE)

/** @brief 位图数组大小（每个 uint64_t 管理 64 个块） */
#define DET_BITMAP_SIZE     ((DET_BLOCK_COUNT + 63UL) / 64UL)

/* ==============================================================================
 * 分配头部
 *
 * 每个分配的第一个块开头存储 DetAllocHeader_t，
 * 记录分配的块数，用于 free/realloc 时正确释放。
 * 头部占 8 字节，剩余 248 字节给用户数据。
 * ============================================================================== */

/** @brief 分配魔数，用于验证指针合法性 */
#define DET_ALLOC_MAGIC     0xDFA110CLU

/**
 * @brief 分配头部结构（8 字节对齐）
 */
typedef struct
{
    uint32_t magic;     /**< @brief 魔数，用于验证指针合法性 */
    uint32_t blocks;    /**< @brief 分配的块数 */
} DetAllocHeader_t;

/** @brief 头部大小（必须是 8 字节对齐） */
#define DET_HEADER_SIZE     sizeof(DetAllocHeader_t)

/* ==============================================================================
 * 内存池（静态分配）
 * ============================================================================== */

/**
 * @brief 内存池存储
 * @note MISRA 偏差：使用 __attribute__((aligned)) 编译器扩展
 *       理由：ARM64 ABI 要求内存块对齐，标准 C11 无等价机制
 */
static uint8_t s_pool[DET_POOL_SIZE]
    __attribute__((aligned(DET_BLOCK_SIZE)));

/** @brief 块使用状态位图（1 = 已使用，0 = 空闲） */
static uint64_t s_block_used[DET_BITMAP_SIZE];

/** @brief 初始化标志（0 = 未初始化，1 = 已初始化） */
static uint32_t s_initialized;

/* ==============================================================================
 * 内部辅助函数
 * ============================================================================== */

/**
 * @brief 计算需要的块数（含头部）
 * @param size 用户请求的字节数
 * @return 需要的块数
 */
static size_t blocks_needed(size_t size)
{
    return (DET_HEADER_SIZE + size + DET_BLOCK_SIZE - 1UL) / DET_BLOCK_SIZE;
}

/**
 * @brief 查找连续空闲块
 * @param count 需要的连续块数
 * @return 起始块索引，失败返回 DET_BLOCK_COUNT
 */
static size_t find_free_blocks(size_t count)
{
    size_t start;
    size_t consecutive;
    size_t bitmap_idx;
    size_t bit_idx;
    size_t block_idx;
    size_t limit;

    /* 防止无符号整数下溢：count 不能超过总块数 */
    if (count == 0UL)
    {
        return DET_BLOCK_COUNT;
    }

    if (count > DET_BLOCK_COUNT)
    {
        return DET_BLOCK_COUNT;
    }

    limit = DET_BLOCK_COUNT - count;

    for (start = 0UL; start <= limit; start++)
    {
        consecutive = 0UL;

        for (block_idx = start; block_idx < (start + count); block_idx++)
        {
            bitmap_idx = block_idx / 64UL;
            bit_idx = block_idx % 64UL;

            if ((s_block_used[bitmap_idx] & (1ULL << bit_idx)) != 0ULL)
            {
                break;
            }

            consecutive++;
        }

        if (consecutive == count)
        {
            return start;
        }
    }

    return DET_BLOCK_COUNT;
}

/**
 * @brief 标记块为已使用
 * @param start 起始块索引
 * @param count 块数
 */
static void mark_blocks_used(size_t start, size_t count)
{
    size_t i;
    for (i = 0UL; i < count; i++)
    {
        size_t block_idx = start + i;
        size_t bitmap_idx = block_idx / 64UL;
        size_t bit_idx = block_idx % 64UL;
        s_block_used[bitmap_idx] |= (1ULL << bit_idx);
    }
}

/**
 * @brief 标记块为空闲
 * @param start 起始块索引
 * @param count 块数
 */
static void mark_blocks_free(size_t start, size_t count)
{
    size_t i;
    for (i = 0UL; i < count; i++)
    {
        size_t block_idx = start + i;
        size_t bitmap_idx = block_idx / 64UL;
        size_t bit_idx = block_idx % 64UL;
        s_block_used[bitmap_idx] &= ~(1ULL << bit_idx);
    }
}

/**
 * @brief 从块索引获取用户数据指针
 * @param block_idx 块索引
 * @return 用户数据指针（跳过头部）
 */
static void *block_to_user_ptr(size_t block_idx)
{
    return (void *)&s_pool[block_idx * DET_BLOCK_SIZE + DET_HEADER_SIZE];
}

/**
 * @brief 从用户数据指针获取块索引
 * @param ptr 用户数据指针
 * @return 块索引，无效返回 DET_BLOCK_COUNT
 */
static size_t user_ptr_to_block_idx(const void *ptr)
{
    uintptr_t pool_start = (uintptr_t)s_pool;
    uintptr_t ptr_val = (uintptr_t)ptr;
    size_t offset;
    size_t block_idx;

    if (ptr_val < (pool_start + DET_HEADER_SIZE))
    {
        return DET_BLOCK_COUNT;
    }

    /* 用户指针 = 池起始 + block_idx * BLOCK_SIZE + HEADER_SIZE */
    /* 所以 block_idx = (ptr_val - pool_start - HEADER_SIZE) / BLOCK_SIZE */
    offset = (size_t)(ptr_val - pool_start);

    if (offset < DET_HEADER_SIZE)
    {
        return DET_BLOCK_COUNT;
    }

    block_idx = (offset - DET_HEADER_SIZE) / DET_BLOCK_SIZE;

    if (block_idx >= DET_BLOCK_COUNT)
    {
        return DET_BLOCK_COUNT;
    }

    return block_idx;
}

/**
 * @brief 从用户指针获取分配头部
 * @param ptr 用户数据指针
 * @return 分配头部指针，无效返回 NULL
 */
static DetAllocHeader_t *get_header(const void *ptr)
{
    size_t block_idx;
    DetAllocHeader_t *header;

    block_idx = user_ptr_to_block_idx(ptr);
    if (block_idx >= DET_BLOCK_COUNT)
    {
        return NULL;
    }

    header = (DetAllocHeader_t *)&s_pool[block_idx * DET_BLOCK_SIZE];

    if (header->magic != DET_ALLOC_MAGIC)
    {
        return NULL;
    }

    return header;
}

/* ==============================================================================
 * 公共接口实现
 * ============================================================================== */

/**
 * @brief 初始化确定性内存分配器
 * @return 0 成功，-1 失败
 */
int det_malloc_init(void)
{
    memset(s_block_used, 0, sizeof(s_block_used));
    s_initialized = 1U;

    return 0;
}

/**
 * @brief 销毁确定性内存分配器
 */
void det_malloc_destroy(void)
{
    memset(s_block_used, 0, sizeof(s_block_used));
    s_initialized = 0U;
}

/**
 * @brief 确定性 malloc
 * @param size 请求的字节数
 * @return 分配的内存指针，失败返回 NULL
 */
void *det_malloc(size_t size)
{
    size_t needed;
    size_t start;
    DetAllocHeader_t *header;

    if (size == 0UL)
    {
        return NULL;
    }

    if (s_initialized == 0U)
    {
        return NULL;
    }

    needed = blocks_needed(size);

    start = find_free_blocks(needed);

    if (start >= DET_BLOCK_COUNT)
    {
        return NULL;
    }

    mark_blocks_used(start, needed);

    /* 写入分配头部 */
    header = (DetAllocHeader_t *)&s_pool[start * DET_BLOCK_SIZE];
    header->magic = DET_ALLOC_MAGIC;
    header->blocks = (uint32_t)needed;

    /* 返回用户数据指针（跳过头部） */
    return block_to_user_ptr(start);
}

/**
 * @brief 确定性 free
 * @param ptr 要释放的内存指针
 */
void det_free(void *ptr)
{
    size_t block_idx;
    DetAllocHeader_t *header;

    if (ptr == NULL)
    {
        return;
    }

    header = get_header(ptr);
    if (header == NULL)
    {
        return;
    }

    block_idx = user_ptr_to_block_idx(ptr);
    if (block_idx >= DET_BLOCK_COUNT)
    {
        return;
    }

    /* 清除魔数（防止双重释放） */
    header->magic = 0U;

    /* 释放指定数量的块 */
    mark_blocks_free(block_idx, (size_t)header->blocks);
}

/**
 * @brief 确定性 calloc
 * @param nmemb 元素数量
 * @param size 每个元素大小
 * @return 分配的零初始化内存指针，失败返回 NULL
 */
void *det_calloc(size_t nmemb, size_t size)
{
    size_t total;
    void *ptr;

    /* 检查乘法溢出：使用 SIZE_MAX 而非 (size_t)(-1) 避免 MISRA 10.1 偏差 */
    if (nmemb != 0UL)
    {
        if (size > SIZE_MAX / nmemb)
        {
            return NULL;
        }
    }

    total = nmemb * size;

    ptr = det_malloc(total);
    if (ptr != NULL)
    {
        memset(ptr, 0, total);
    }

    return ptr;
}

/**
 * @brief 确定性 realloc
 * @param ptr 原始指针
 * @param size 新大小
 * @return 重新分配的内存指针，失败返回 NULL
 */
void *det_realloc(void *ptr, size_t size)
{
    void *new_ptr;
    DetAllocHeader_t *header;
    size_t old_size;
    size_t copy_size;

    if (ptr == NULL)
    {
        return det_malloc(size);
    }

    if (size == 0UL)
    {
        det_free(ptr);
        return NULL;
    }

    header = get_header(ptr);
    if (header == NULL)
    {
        return NULL;
    }

    old_size = (size_t)header->blocks * DET_BLOCK_SIZE;
    if (old_size > DET_HEADER_SIZE)
    {
        old_size -= DET_HEADER_SIZE;
    }
    else
    {
        old_size = 0UL;
    }

    new_ptr = det_malloc(size);
    if (new_ptr == NULL)
    {
        return NULL;
    }

    copy_size = (size < old_size) ? size : old_size;
    memcpy(new_ptr, ptr, copy_size);

    det_free(ptr);

    return new_ptr;
}

/**
 * @brief 获取分配器统计信息
 * @param total_pool_size 输出：内存池总大小
 * @param used_blocks 输出：已使用块数
 * @param free_blocks 输出：空闲块数
 */
void det_malloc_stats(size_t *total_pool_size, size_t *used_blocks, size_t *free_blocks)
{
    size_t used = 0UL;
    size_t i;

    if (total_pool_size != NULL)
    {
        *total_pool_size = DET_POOL_SIZE;
    }

    for (i = 0UL; i < DET_BLOCK_COUNT; i++)
    {
        size_t bitmap_idx = i / 64UL;
        size_t bit_idx = i % 64UL;

        if ((s_block_used[bitmap_idx] & (1ULL << bit_idx)) != 0ULL)
        {
            used++;
        }
    }

    if (used_blocks != NULL)
    {
        *used_blocks = used;
    }

    if (free_blocks != NULL)
    {
        *free_blocks = DET_BLOCK_COUNT - used;
    }
}
