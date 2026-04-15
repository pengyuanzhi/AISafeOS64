/**
 * @file    boot_simple.c
 * @brief   AISafeOS64 musl 简化启动函数（最小实现）
 * @version 1.0
 *
 * 最小化的 musl 启动函数：
 * - __init_tls: 静态 TLS 区域（简化版）
 * - __libc_free: 内存释放（空实现）
 * - __sysinfo: 已在 syscall_dispatch.c 中设置
 *
 * @note Phase 3: 简化版本，后续可逐步完善
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * TLS (Thread-Local Storage) 简化实现
 * ======================================================================== */

/**
 * @brief 静态 TLS 区域（单线程）
 *
 * AISafeOS64 初始阶段只支持单线程用户态服务。
 * errno 存储在 TLS 区域，实现线程安全。
 */
static char s_tls_buffer[4096] __attribute__((aligned(16)));

/** TLS 区域的 errno 指针 */
static int *s_errno_ptr = NULL;

/**
 * @brief 简化的 TLS 初始化
 * @param a 传递 TLS 大小（忽略）
 * @return TLS 区域指针
 */
void *__init_tls(size_t *a)
{
    (void)a;  /* 忽略参数 */

    /* 设置 errno 指针 */
    s_errno_ptr = (int *)&s_tls_buffer[0];
    *s_errno_ptr = 0;

    return s_tls_buffer;
}

/* ========================================================================
 * 内存管理简化实现
 * ======================================================================== */

/**
 * @brief musl 内存释放接口（空实现）
 * @note 当前使用 bump allocator，不支持 free
 */
void __libc_free(void *ptr)
{
    (void)ptr;  /* 忽略指针 */
    /* 空实现：bump allocator 不支持 free */
}

/* ========================================================================
 * 启动函数
 * ======================================================================== */


