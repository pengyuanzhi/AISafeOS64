/**
 * @file    kmalloc.c
 * @brief   内核通用内存分配器实现
 * @author  AISafe64 Team
 * @date    2026-06-10
 * @version 1.0
 *
 * @details 基于空闲链表（Free List）的内核内存分配器：
 *          - 首次适配（First Fit）搜索策略
 *          - 16 字节对齐保证（ARM64 ABI）
 *          - 分配块头部包含大小和魔数
 *          - 释放时合并相邻空闲块
 *          - 分配统计跟踪
 *
 * @note    MISRA-C:2012 合规
 * @note    禁止递归，使用迭代合并空闲块
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/mm/kmalloc.h>
#include <kernel/string.h>
#include <kernel/errno.h>

/* ========================================================================
 * 分配块头部结构
 * ======================================================================== */

/**
 * @brief 分配块头部
 *
 * @details 每个分配块前面都有一个 BlockHeader，用于跟踪：
 *          - 块大小（含头部）
 *          - 分配状态
 *          - 魔数（用于损坏检测）
 *          - 前后指针（空闲链表）
 */
typedef struct block_header
{
    size_t size;                /**< @brief 块总大小（含头部），单位：字节 */
    uint32_t magic;             /**< @brief 魔数：KMALLOC_MAGIC 或 0 */
    uint32_t is_free;           /**< @brief 空闲标志：1=空闲，0=已分配 */
    struct block_header *next;  /**< @brief 下一个块指针 */
    struct block_header *prev;  /**< @brief 上一个块指针 */
} block_header_t;

/* ========================================================================
 * 对齐辅助宏
 * ======================================================================== */

/**
 * @def ALIGN_UP
 * @brief 向上对齐到指定对齐边界
 */
#define ALIGN_UP(x, align) (((x) + ((align) - 1U)) & ~((align) - 1U))

/**
 * @def BLOCK_HEADER_SIZE
 * @brief 分配块头部大小（对齐后）
 */
#define BLOCK_HEADER_SIZE  ALIGN_UP(sizeof(block_header_t), KMALLOC_ALIGN)

/**
 * @def MIN_BLOCK_SIZE
 * @brief 最小块大小（头部 + 最小对齐）
 */
#define MIN_BLOCK_SIZE     (BLOCK_HEADER_SIZE + KMALLOC_MIN_SIZE)

/* ========================================================================
 * 内核堆区域
 * ======================================================================== */

/**
 * @brief 内核堆区域（使用链接脚本定义的 heap 段）
 *
 * @details 使用 __heap_start ~ __heap_start + KMALLOC_HEAP_SIZE 作为内核堆。
 *          避免在 BSS 段中放置巨型静态数组。
 *
 *          宿主机单元测试（TEST_HOST_MODE）链接真实 kmalloc.c 实现但无法
 *          使用内核链接脚本，因此改用静态缓冲区作为堆区域。
 */

#ifdef TEST_HOST_MODE
/* 宿主机测试模式：使用静态缓冲区替代链接脚本符号 */
#define KMALLOC_HOST_HEAP_SIZE  (4U * 1024U * 1024U)  /**< @brief 宿主机测试堆 4MB（BSS 段，不占文件空间） */
static uint8_t s_host_heap[KMALLOC_HOST_HEAP_SIZE];   /**< @brief 宿主机测试堆缓冲区 */
#else
/** @brief 链接脚本定义的堆起始地址（仅内核构建时使用） */
extern char __heap_start[];
#endif

/** @brief 堆基地址指针（运行时从链接符号获取） */
static uint8_t *s_heap_base = NULL;

/* ========================================================================
 * 分配器全局状态
 * ======================================================================== */

/**
 * @brief 分配器状态
 */
static struct
{
    block_header_t *first;      /**< @brief 空闲链表头指针 */
    kmalloc_stats_t stats;      /**< @brief 分配统计 */
    uint32_t initialized;       /**< @brief 初始化标志 */
} s_kmalloc_state;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 分裂空闲块
 *
 * @param block 要分裂的空闲块
 * @param size  需要的大小（含头部）
 *
 * @details 如果剩余空间足够大（>= MIN_BLOCK_SIZE），
 *          将块分裂为两部分：前部分配，后部空闲
 */
static void split_block(block_header_t *block, size_t size)
{
    block_header_t *new_block;
    size_t remaining;

    if (block == NULL)
    {
        return;
    }

    remaining = block->size - size;

    if (remaining >= MIN_BLOCK_SIZE)
    {
        /* 创建新的空闲块 */
        new_block = (block_header_t *)((uint8_t *)block + size);
        new_block->size    = remaining;
        new_block->magic   = KMALLOC_MAGIC;
        new_block->is_free = 1U;

        /* 链接到链表 */
        new_block->next = block->next;
        new_block->prev = block;

        if (block->next != NULL)
        {
            block->next->prev = new_block;
        }

        block->next = new_block;
        block->size = size;
    }

    /* 不再分裂则保持原大小 */
}

/**
 * @brief 合并相邻的空闲块
 *
 * @param block 起始块（已释放）
 *
 * @details 向后合并后续空闲块，再向前合并前驱空闲块。
 *          使用迭代方式避免递归（MISRA 16.1）。
 */
static void merge_free_blocks(block_header_t *block)
{
    block_header_t *current;

    if (block == NULL)
    {
        return;
    }

    /* 向后合并：迭代合并后续空闲块 */
    current = block;
    for (;;)
    {
        if (current->next == NULL)
        {
            break;
        }

        if (current->next->is_free != 1U)
        {
            break;
        }

        if (current->next->magic != KMALLOC_MAGIC)
        {
            break;
        }

        /* 检查物理连续性 */
        if ((uint8_t *)current + current->size != (uint8_t *)current->next)
        {
            break;
        }

        /* 合并 */
        current->size += current->next->size;
        current->next = current->next->next;

        if (current->next != NULL)
        {
            current->next->prev = current;
        }
    }

    /* 向前合并：迭代合并前驱空闲块 */
    current = block;
    for (;;)
    {
        if (current->prev == NULL)
        {
            break;
        }

        if (current->prev->is_free != 1U)
        {
            break;
        }

        if (current->prev->magic != KMALLOC_MAGIC)
        {
            break;
        }

        /* 检查物理连续性 */
        if ((uint8_t *)current->prev + current->prev->size != (uint8_t *)current)
        {
            break;
        }

        /* 合并 */
        current->prev->size += current->size;
        current->prev->next = current->next;

        if (current->next != NULL)
        {
            current->next->prev = current->prev;
        }

        current = current->prev;
    }
}

/* ========================================================================
 * 公共接口实现
 * ======================================================================== */

/**
 * @brief 初始化内核内存分配器
 */
int32_t kmalloc_init(void)
{
    block_header_t *first_block;

    /* 获取堆基地址：内核模式从链接符号，宿主机测试模式从静态缓冲区 */
#ifdef TEST_HOST_MODE
    s_heap_base = s_host_heap;
    const size_t heap_size = KMALLOC_HOST_HEAP_SIZE;
#else
    s_heap_base = (uint8_t *)(uintptr_t)__heap_start;
    const size_t heap_size = KMALLOC_HEAP_SIZE;
#endif

    /* 初始化堆区域 */
    (void)kernel_memset(s_heap_base, 0, heap_size);

    /* 设置第一个空闲块 */
    first_block = (block_header_t *)s_heap_base;
    first_block->size    = heap_size;
    first_block->magic   = KMALLOC_MAGIC;
    first_block->is_free = 1U;
    first_block->next    = NULL;
    first_block->prev    = NULL;

    /* 初始化分配器状态 */
    s_kmalloc_state.first = first_block;
    s_kmalloc_state.initialized = 1U;

    /* 清零统计 */
    (void)kernel_memset(&s_kmalloc_state.stats, 0, sizeof(kmalloc_stats_t));

    return 0;
}

/**
 * @brief 分配内核内存
 */
void *kmalloc(size_t size)
{
    block_header_t *current;
    size_t total_size;
    void *result;

    /* 参数验证 */
    if (size == 0U)
    {
        return NULL;
    }

    /* 未初始化检查 */
    if (s_kmalloc_state.initialized != 1U)
    {
        (void)kmalloc_init();
    }

    /* 大小限制 */
    if (size > KMALLOC_MAX_SIZE)
    {
        return NULL;
    }

    /* 计算实际分配大小（对齐 + 头部） */
    total_size = ALIGN_UP(size, KMALLOC_ALIGN) + BLOCK_HEADER_SIZE;

    /* 首次适配搜索 */
    current = s_kmalloc_state.first;
    while (current != NULL)
    {
        if ((current->is_free == 1U) && (current->size >= total_size))
        {
            /* 找到合适的空闲块 */
            break;
        }
        current = current->next;
    }

    if (current == NULL)
    {
        /* 没有找到足够大的空闲块 */
        return NULL;
    }

    /* 分裂块（如果剩余空间足够大） */
    split_block(current, total_size);

    /* 标记为已分配 */
    current->is_free = 0U;
    current->magic   = KMALLOC_MAGIC;

    /* 更新统计 */
    s_kmalloc_state.stats.total_allocs++;
    s_kmalloc_state.stats.current_allocs++;
    s_kmalloc_state.stats.total_bytes   += size;
    s_kmalloc_state.stats.current_bytes += size;

    if (s_kmalloc_state.stats.current_bytes > s_kmalloc_state.stats.peak_bytes)
    {
        s_kmalloc_state.stats.peak_bytes = s_kmalloc_state.stats.current_bytes;
    }

    /* 返回数据区域（跳过头部） */
    result = (void *)((uint8_t *)current + BLOCK_HEADER_SIZE);

    return result;
}

/**
 * @brief 分配清零的内核内存
 */
void *kzalloc(size_t size)
{
    void *ptr;

    ptr = kmalloc(size);
    if (ptr != NULL)
    {
        (void)kernel_memset(ptr, 0, size);
    }

    return ptr;
}

/**
 * @brief 释放内核内存
 */
void kfree(void *ptr)
{
    block_header_t *block;

    if (ptr == NULL)
    {
        return;
    }

    /* 获取块头部 */
    block = (block_header_t *)((uint8_t *)ptr - BLOCK_HEADER_SIZE);

    /* 验证魔数 */
    if (block->magic != KMALLOC_MAGIC)
    {
        /* 无效的指针或已释放 */
        return;
    }

    /* 检查是否已经释放（双重释放检测） */
    if (block->is_free == 1U)
    {
        /* 双重释放，忽略 */
        return;
    }

    /* 标记为空闲 */
    block->is_free = 1U;

    /* 更新统计 */
    if (s_kmalloc_state.stats.current_allocs > 0U)
    {
        s_kmalloc_state.stats.current_allocs--;
    }

    if (block->size > BLOCK_HEADER_SIZE)
    {
        size_t data_size = block->size - BLOCK_HEADER_SIZE;
        if (s_kmalloc_state.stats.current_bytes >= data_size)
        {
            s_kmalloc_state.stats.current_bytes -= data_size;
        }
    }

    s_kmalloc_state.stats.total_frees++;

    /* 合并相邻空闲块 */
    merge_free_blocks(block);
}

/**
 * @brief 安全释放内核内存（清零后释放）
 */
void kfree_secure(void *ptr, size_t size)
{
    if (ptr == NULL)
    {
        return;
    }

    /* 清零数据区域 */
    if (size > 0U)
    {
        (void)kernel_memset(ptr, 0, size);
    }

    /* 释放 */
    kfree(ptr);
}

/**
 * @brief 获取内存分配统计信息
 */
int32_t kmalloc_get_stats(kmalloc_stats_t *stats)
{
    if (stats == NULL)
    {
        return -(int32_t)EINVAL;
    }

    *stats = s_kmalloc_state.stats;

    return 0;
}
