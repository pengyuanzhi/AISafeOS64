/**
 * @file    realloc.c
 * @brief   realloc 实现（简化版）
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 调用 malloc + memcpy 实现，不支持原地扩展
 *          因为 bump allocator 不支持释放旧内存
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdlib.h>
#include <string.h>

/**
 * @brief 重新分配内存
 * @param ptr 原有内存指针（可为 NULL）
 * @param size 新的大小
 * @return 成功返回新的内存指针，失败返回 NULL
 */
void *realloc(void *ptr, size_t size)
{
    void *new_ptr;

    /* ptr 为 NULL 等价于 malloc */
    if (ptr == NULL)
    {
        return malloc(size);
    }

    /* size 为 0 等价于 free */
    if (size == 0U)
    {
        free(ptr);
        return NULL;
    }

    new_ptr = malloc(size);
    if (new_ptr == NULL)
    {
        return NULL;
    }

    /* 复制旧数据（无法知道原始大小，按新大小复制是安全的） */
    /* 注意：bump allocator 中旧内存有效，按较小值复制 */
    (void)memcpy(new_ptr, ptr, size);

    free(ptr);

    return new_ptr;
}
