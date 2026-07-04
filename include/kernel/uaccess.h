/**
 * @file    uaccess.h
 * @brief   用户空间指针访问验证接口
 * @author  AISafe64 Team
 * @date    2026-07-03
 * @version 1.0
 *
 * @details 提供从内核安全访问用户空间内存的接口：
 *          - access_ok: 验证用户指针是否在合法用户空间范围
 *          - copy_from_user: 从用户空间复制数据到内核
 *          - copy_to_user: 从内核复制数据到用户空间
 *          - strncpy_from_user: 从用户空间安全复制字符串
 *
 *          安全原理：
 *          用户进程通过系统调用传入指针时，必须验证该指针：
 *          1. 不指向内核地址空间（>= CONFIG_KERNEL_VADDR_BASE）
 *          2. 不会因长度溢出跨越用户/内核边界
 *          否则用户态可任意读写内核内存。
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SE-006（用户指针验证）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_UACCESS_H
#define KERNEL_UACCESS_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <stdbool.h>
#include <stdint.h>

/* ========================================================================
 * 用户空间地址范围
 * ======================================================================== */

/**
 * @brief 用户空间地址上限（内核空间起始地址）
 *
 * @details VA[63]=0 为用户空间，VA[63]=1 为内核空间。
 *          CONFIG_KERNEL_VADDR_BASE = 0xFFFF000000000000。
 *         任何 >= 此值的地址属于内核空间，用户指针不得指向。
 */
#define USER_ADDR_MAX     ((vaddr_t)CONFIG_KERNEL_VADDR_BASE)

/* ========================================================================
 * 用户指针验证 API
 * ======================================================================== */

/**
 * @brief 验证用户空间指针是否合法
 *
 * @param addr 用户空间指针
 * @param size 访问长度（字节）
 *
 * @return true  指针合法（在用户空间范围内，无溢出）
 * @return false 指针非法（指向内核空间，或长度溢出）
 *
 * @note 检查项：
 *       1. addr != NULL
 *       2. addr + size 不超过 USER_ADDR_MAX
 *       3. addr + size 不回绕（无符号溢出）
 */
bool access_ok(const void *addr, uint64_t size);

/**
 * @brief 从用户空间安全复制数据到内核
 *
 * @param dst 内核目标地址
 * @param src 用户空间源地址
 * @param n   复制长度（字节）
 *
 * @return 0        成功
 * @return -EFAULT  源地址非法
 *
 * @note 先用 access_ok 验证 src，再复制。
 */
int32_t copy_from_user(void *dst, const void *src, uint64_t n);

/**
 * @brief 从内核安全复制数据到用户空间
 *
 * @param dst 用户空间目标地址
 * @param src 内核源地址
 * @param n   复制长度（字节）
 *
 * @return 0        成功
 * @return -EFAULT  目标地址非法
 */
int32_t copy_to_user(void *dst, const void *src, uint64_t n);

/**
 * @brief 从用户空间安全复制字符串
 *
 * @param dst     内核目标缓冲区
 * @param src     用户空间源字符串
 * @param maxlen  最大复制长度（含终止符）
 *
 * @return >=0     实际复制的长度（不含终止符）
 * @return -EFAULT 源地址非法
 */
int32_t strncpy_from_user(char *dst, const char *src, uint32_t maxlen);

#endif /* KERNEL_UACCESS_H */
