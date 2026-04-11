/**
 * @file    malloc.c
 * @brief   malloc/free 实现（bump allocator）
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 使用 64KB 静态内存池的 bump allocator：
 *          - 分配时指针单调递增，不回收
 *          - 16 字节对齐
 *          - free() 为空操作
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 堆内存池大小（64KB） */
#define HEAP_SIZE 65536U

/** @brief 对齐粒度（16 字节） */
#define ALIGNMENT 16U

/** @brief 对齐掩码 */
#define ALIGN_MASK (ALIGNMENT - 1U)

/* ========================================================================
 * 静态内存池
 * ======================================================================== */

/** @brief 静态内存池 */
static char s_heap[HEAP_SIZE];

/** @brief 当前分配偏移 */
static size_t s_offset = 0U;

/* ========================================================================
 * 公共接口
 * ======================================================================== */

/**
 * @brief 分配内存
 * @param size 请求的字节数
 * @return 成功返回分配的内存指针，失败返回 NULL
 */
void *malloc(size_t size)
{
    /* 对齐到 16 字节 */
    size_t aligned = (size + ALIGN_MASK) & ~(size_t)ALIGN_MASK;
    void *ptr;

    /* 检查溢出 */
    if (size == 0U)
    {
        return NULL;
    }

    /* 检查是否有足够空间 */
    if ((HEAP_SIZE - s_offset) < aligned)
    {
        return NULL;
    }

    ptr = (void *)&s_heap[s_offset];
    s_offset += aligned;

    return ptr;
}

/**
 * @brief 释放内存（bump allocator 不回收）
 * @param ptr 要释放的内存指针（可为 NULL）
 */
void free(void *ptr)
{
    (void)ptr; /* bump allocator 不回收 */
}
