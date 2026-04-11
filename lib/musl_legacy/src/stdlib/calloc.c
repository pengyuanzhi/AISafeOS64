/**
 * @file    calloc.c
 * @brief   calloc 实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 调用 malloc + memset 实现内存分配并清零
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdlib.h>
#include <string.h>

/**
 * @brief 分配并清零内存
 * @param nmemb 元素数量
 * @param size 每个元素大小
 * @return 成功返回分配的内存指针，失败返回 NULL
 */
void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *ptr;

    /* 检查乘法溢出 */
    if ((nmemb != 0U) && (total / nmemb != size))
    {
        return NULL;
    }

    ptr = malloc(total);
    if (ptr != NULL)
    {
        (void)memset(ptr, 0, total);
    }

    return ptr;
}
