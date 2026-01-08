/**
 * @file page.c
 * @brief AISafe64 RTOS - 物理页分配器
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 物理页分配器（简化版）
 *          - 位图管理（1位=1页）
 *          - 4KB页大小
 *          - 最大支持1GB内存（256K页）
 *
 * @note MISRA-C:2012合规
 * @note 后续扩展为伙伴系统
 */

#include "mm.h"
#include "types.h"

/**
 * @brief 页分配器结构
 */
typedef struct {
    uint64_t base_addr;       /**< 物理内存基址 */
    uint64_t total_pages;     /**< 总页数 */
    uint64_t free_pages;      /**< 空闲页数 */
    uint64_t allocated_pages; /**< 已分配页数 */
    uint32_t *bitmap;         /**< 页位图（每32位管理32页） */
} PageAllocator_t;

/**
 * @brief 全局页分配器
 */
static PageAllocator_t g_page_allocator;

/**
 * @brief 页位图（静态分配）
 * @details 最多支持1GB内存（256K页）
 */
#define MAX_BITMAP_WORDS (MAX_PAGES / 32U)
static uint32_t g_page_bitmap[MAX_BITMAP_WORDS];

/**
 * @brief 页分配器初始化
 * @param base_addr 物理内存基址
 * @param size 内存大小（字节）
 * @return 成功返回0，失败返回负错误码
 *
 * @details 初始化页分配器
 *          - 验证参数
 *          - 初始化位图
 *          - 标记所有页为空闲
 */
int page_allocator_init(uint64_t base_addr, uint64_t size) {
    /* 参数验证 */
    if (base_addr == 0UL) {
        return -ERROR_INVALID_PARAM;
    }

    if (size < PAGE_SIZE) {
        return -ERROR_INVALID_PARAM;
    }

    /* 检查对齐 */
    if ((base_addr & (PAGE_SIZE - 1UL)) != 0UL) {
        return -ERROR_INVALID_PARAM;
    }

    /* 计算页数 */
    uint64_t pages = size / PAGE_SIZE;
    if (pages > MAX_PAGES) {
        return -ERROR_INVALID_PARAM;
    }

    /* 初始化分配器结构 */
    g_page_allocator.base_addr = base_addr;
    g_page_allocator.total_pages = pages;
    g_page_allocator.free_pages = pages;
    g_page_allocator.allocated_pages = 0UL;

    /* 初始化位图 */
    g_page_allocator.bitmap = g_page_bitmap;
    for (uint64_t i = 0UL; i < bitmap_words; i++) {
        g_page_allocator.bitmap[i] = 0xFFFFFFFFU;
    }

    /* 标记最后可能不完整的字 */
    uint64_t remaining_pages = pages % 32U;
    if (remaining_pages != 0UL) {
        uint32_t mask = (1U << remaining_pages) - 1U;
        g_page_allocator.bitmap[bitmap_words - 1] = mask;
    }

    return ERROR_SUCCESS;
}

/**
 * @brief 分配一个物理页
 * @return 页物理地址，失败返回0
 *
 * @details 从位图中查找第一个空闲页
 *          - O(N)时间复杂度（后续优化）
 *          - 标记页为已分配
 */
uint64_t page_alloc(void) {
    /* 查找第一个空闲页 */
    for (uint64_t word_idx = 0UL; word_idx < g_page_allocator.total_pages / 32U; word_idx++) {
        uint32_t word = g_page_allocator.bitmap[word_idx];

        if (word != 0U) {
            /* 找到空闲页 */
            uint32_t page_idx = 0U;

            /* 查找第一个0位（空闲页） */
            for (uint32_t bit = 0U; bit < 32U; bit++) {
                if ((word & (1U << bit)) != 0U) {
                    page_idx = bit;
                    break;
                }
            }

            /* 计算页号 */
            uint64_t page_nr = word_idx * 32U + page_idx;

            /* 检查是否超出范围 */
            if (page_nr >= g_page_allocator.total_pages) {
                return 0UL;  /* 无可用页 */
            }

            /* 标记为已分配 */
            g_page_allocator.bitmap[word_idx] &= ~(1U << page_idx);

            /* 更新统计 */
            g_page_allocator.free_pages--;
            g_page_allocator.allocated_pages++;

            /* 返回物理地址 */
            return g_page_allocator.base_addr + (page_nr * PAGE_SIZE);
        }
    }

    return 0UL;  /* 无可用页 */
}

/**
 * @brief 释放一个物理页
 * @param addr 页物理地址
 *
 * @details 将页标记为空闲
 *          - 验证地址对齐
 *          - 验证地址范围
 */
void page_free(uint64_t addr) {
    /* 参数验证 */
    if (addr == 0UL) {
        return;
    }

    /* 检查对齐 */
    if ((addr & (PAGE_SIZE - 1UL)) != 0UL) {
        return;
    }

    /* 计算页号 */
    uint64_t page_nr = (addr - g_page_allocator.base_addr) / PAGE_SIZE;

    /* 检查范围 */
    if (page_nr >= g_page_allocator.total_pages) {
        return;
    }

    /* 计算位图索引 */
    uint64_t word_idx = page_nr / 32U;
    uint32_t page_idx = (uint32_t)(page_nr % 32U);

    /* 检查页是否已分配 */
    uint32_t mask = g_page_allocator.bitmap[word_idx] & (1U << page_idx);
    if (mask != 0U) {
        /* 页未分配，重复释放 */
        return;
    }

    /* 标记为空闲 */
    g_page_allocator.bitmap[word_idx] |= (1U << page_idx);

    /* 更新统计 */
    g_page_allocator.free_pages++;
    g_page_allocator.allocated_pages--;
}
