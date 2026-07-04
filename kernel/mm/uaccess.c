/**
 * @file    uaccess.c
 * @brief   用户空间指针访问验证实现
 * @author  AISafe64 Team
 * @date    2026-07-03
 * @version 1.0
 *
 * @details 实现用户空间指针的安全访问接口：
 *          - 地址范围验证（防止访问内核空间）
 *          - 安全数据复制（带边界检查）
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SE-006
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/uaccess.h>
#include <kernel/string.h>
#include <kernel/errno.h>

/* ========================================================================
 * 地址验证
 * ======================================================================== */

bool access_ok(const void *addr, uint64_t size)
{
    uint64_t user_addr;
    uint64_t end_addr;

    /* NULL 指针检查 */
    if (addr == NULL)
    {
        return false;
    }

    user_addr = (uint64_t)(uintptr_t)addr;

    /* 检查起始地址是否在内核空间 */
    if (user_addr >= (uint64_t)USER_ADDR_MAX)
    {
        return false;
    }

    /* 检查结束地址是否溢出或进入内核空间
     * 注意：user_addr + size 可能无符号溢出，需显式检查 */
    if (size > 0U)
    {
        end_addr = user_addr + (size - 1U);

        /* 无符号回绕检查 */
        if (end_addr < user_addr)
        {
            return false;
        }

        /* 结束地址进入内核空间 */
        if (end_addr >= (uint64_t)USER_ADDR_MAX)
        {
            return false;
        }
    }

    return true;
}

/* ========================================================================
 * 安全复制函数
 * ======================================================================== */

int32_t copy_from_user(void *dst, const void *src, uint64_t n)
{
    if ((dst == NULL) || (src == NULL))
    {
        return -EFAULT;
    }

    if (n == 0U)
    {
        return 0;
    }

    /* 验证用户源地址 */
    if (!access_ok(src, n))
    {
        return -EFAULT;
    }

    /* 安全复制（已验证边界） */
    (void)kernel_memmove(dst, src, n);

    return 0;
}

int32_t copy_to_user(void *dst, const void *src, uint64_t n)
{
    if ((dst == NULL) || (src == NULL))
    {
        return -EFAULT;
    }

    if (n == 0U)
    {
        return 0;
    }

    /* 验证用户目标地址 */
    if (!access_ok(dst, n))
    {
        return -EFAULT;
    }

    /* 安全复制（已验证边界） */
    (void)kernel_memmove(dst, src, n);

    return 0;
}

int32_t strncpy_from_user(char *dst, const char *src, uint32_t maxlen)
{
    uint32_t i;

    if ((dst == NULL) || (src == NULL) || (maxlen == 0U))
    {
        return -EFAULT;
    }

    /* 逐字节复制并检查每个字节的可访问性 */
    for (i = 0U; i < maxlen; i++)
    {
        /* 验证当前字节地址 */
        if (!access_ok(&src[i], 1U))
        {
            return -EFAULT;
        }

        dst[i] = src[i];

        /* 遇到终止符停止 */
        if (src[i] == '\0')
        {
            return (int32_t)i;
        }
    }

    /* 确保终止符 */
    if (maxlen > 0U)
    {
        dst[maxlen - 1U] = '\0';
    }

    return (int32_t)(maxlen - 1U);
}
