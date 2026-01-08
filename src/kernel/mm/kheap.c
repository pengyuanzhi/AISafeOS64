/**
 * @file kheap.c
 * @brief AISafe64 RTOS - 内核堆分配器
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 内核堆分配器（简化版）
 *          - 首次适配算法（First Fit）
 *          - 最小分配单元：16字节
 *          - 支持合并空闲块
 *
 * @note MISRA-C:2012合规
 * @note 后续扩展为slab分配器
 */

#include "mm.h"
#include "types.h"
#include <string.h>

/**
 * @brief 堆块头
 */
typedef struct HeapBlock {
    uint64_t size;                  /**< 块大小（包含头部） */
    bool used;                      /**< 是否使用 */
    struct HeapBlock *next;         /**< 下一个块 */
} HeapBlock_t;

/**
 * @brief 堆管理器
 */
typedef struct {
    void *start;                    /**< 堆起始地址 */
    uint64_t size;                  /**< 堆大小 */
    HeapBlock_t *first_block;        /**< 第一个块 */
    uint64_t total_allocated;        /**< 总分配大小 */
    uint64_t total_free;             /**< 总空闲大小 */
} HeapManager_t;

/**
 * @brief 全局堆管理器
 */
static HeapManager_t g_heap;

/**
 * @brief 最小分配单元
 */
#define HEAP_MIN_ALIGN  16U
#define HEAP_MIN_SIZE    (sizeof(HeapBlock_t) + HEAP_MIN_ALIGN)

/**
 * @brief 内核堆初始化
 * @param start 堆起始地址
 * @param size 堆大小
 * @return 成功返回0，失败返回负错误码
 *
 * @details 初始化内核堆分配器
 *          - 验证参数
 *          - 创建初始空闲块
 */
int kheap_init(void *start, uint64_t size) {
    /* 参数验证 */
    if (start == NULL) {
        return -ERROR_INVALID_PARAM;
    }

    if (size < HEAP_MIN_SIZE) {
        return -ERROR_INVALID_PARAM;
    }

    /* 检查对齐 */
    if (((uint64_t)start & (HEAP_MIN_ALIGN - 1UL)) != 0UL) {
        return -ERROR_INVALID_PARAM;
    }

    /* 初始化堆管理器 */
    g_heap.start = start;
    g_heap.size = size;
    g_heap.total_allocated = 0UL;
    g_heap.total_free = size;

    /* 创建初始空闲块 */
    HeapBlock_t *block = (HeapBlock_t *)start;
    block->size = size;
    block->used = false;
    block->next = NULL;

    g_heap.first_block = block;

    return ERROR_SUCCESS;
}

/**
 * @brief 内核内存分配
 * @param size 大小（字节）
 * @return 内存指针，失败返回NULL
 *
 * @details 首次适配算法
 *          - 遍历空闲块列表
 *          - 找到第一个合适的块
 *          - 如果需要，分割块
 */
void *kmalloc(uint64_t size) {
    /* 参数验证 */
    if (size == 0UL) {
        return NULL;
    }

    /* 对齐到16字节 */
    size = (size + (HEAP_MIN_ALIGN - 1UL)) & ~(HEAP_MIN_ALIGN - 1UL);

    /* 至少分配HEAP_MIN_SIZE */
    if (size < HEAP_MIN_SIZE) {
        size = HEAP_MIN_SIZE;
    }

    /* 遍历块列表 */
    HeapBlock_t *prev = NULL;
    HeapBlock_t *block = g_heap.first_block;

    while (block != NULL) {
        /* 跳过已使用的块 */
        if (block->used) {
            prev = block;
            block = block->next;
            continue;
        }

        /* 检查块大小是否足够 */
        if (block->size < size) {
            prev = block;
            block = block->next;
            continue;
        }

        /* 找到合适的块 */
        /* 检查是否需要分割 */
        if (block->size >= (size + sizeof(HeapBlock_t) + HEAP_MIN_ALIGN)) {
            /* 分割块 */
            HeapBlock_t *new_block = (HeapBlock_t *)((uint8_t *)block + size);
            new_block->size = block->size - size;
            new_block->used = false;
            new_block->next = block->next;

            block->size = size;
            block->next = new_block;
        }

        /* 标记为已使用 */
        block->used = true;

        /* 更新统计 */
        g_heap.total_allocated += block->size;
        g_heap.total_free -= block->size;

        /* 返回内存指针（跳过块头） */
        return (void *)((uint8_t *)block + sizeof(HeapBlock_t));
    }

    /* 无可用内存 */
    return NULL;
}

/**
 * @brief 内核内存释放
 * @param ptr 内存指针
 *
 * @details 释放内存并尝试合并相邻空闲块
 *          - 验证指针
 *          - 标记为空闲
 *          - 合并相邻空闲块
 */
void kfree(void *ptr) {
    /* 参数验证 */
    if (ptr == NULL) {
        return;
    }

    /* 获取块头 */
    HeapBlock_t *block = (HeapBlock_t *)((uint8_t *)ptr - sizeof(HeapBlock_t));

    /* 验证块 */
    if (!block->used) {
        /* 重复释放 */
        return;
    }

    /* 标记为空闲 */
    block->used = false;

    /* 更新统计 */
    g_heap.total_allocated -= block->size;
    g_heap.total_free += block->size;

    /* 尝试合并下一个块 */
    if ((block->next != NULL) && (!block->next->used)) {
        /* 合并块 */
        block->size += block->next->size;
        block->next = block->next->next;
    }

    /* 尝试合并上一个块（需要遍历） */
    HeapBlock_t *prev_block = g_heap.first_block;
    while (prev_block != NULL) {
        if ((prev_block->next == block) && (!prev_block->used)) {
            /* 合并块 */
            prev_block->size += block->size;
            prev_block->next = block->next;
            break;
        }

        if (prev_block->next == block) {
            break;
        }

        prev_block = prev_block->next;
    }
}
