/**
 * @file mm.h
 * @brief AISafe64 RTOS - 内存管理头文件
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 内存管理接口
 *          - 物理页分配器
 *          - 内核堆分配器
 */

#ifndef MM_H
#define MM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 页大小定义
 */
#define PAGE_SIZE 4096U /**< 4KB页 */
#define PAGE_SHIFT 12U  /**< 页偏移量 */
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

/**
 * @brief 物理内存配置
 */
#define PHYS_MEMORY_SIZE (1024 * 1024 * 1024) /**< 1GB物理内存 */
#define PHYS_MEMORY_BASE 0x40000000UL         /**< 物理内存基址 */
#define MAX_PAGES (PHYS_MEMORY_SIZE / PAGE_SIZE)

/**
 * @brief 页分配器初始化
 * @param base_addr 物理内存基址
 * @param size 内存大小（字节）
 * @return 成功返回0，失败返回负错误码
 */
int page_allocator_init(uint64_t base_addr, uint64_t size);

/**
 * @brief 分配一个物理页
 * @return 页物理地址，失败返回0
 */
uint64_t page_alloc(void);

/**
 * @brief 释放一个物理页
 * @param addr 页物理地址
 */
void page_free(uint64_t addr);

/**
 * @brief 内核堆初始化
 * @param start 堆起始地址
 * @param size 堆大小
 * @return 成功返回0，失败返回负错误码
 */
int kheap_init(void *start, uint64_t size);

/**
 * @brief 内核内存分配
 * @param size 大小（字节）
 * @return 内存指针，失败返回NULL
 */
void *kmalloc(uint64_t size);

/**
 * @brief 内核内存释放
 * @param ptr 内存指针
 */
void kfree(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* MM_H */
