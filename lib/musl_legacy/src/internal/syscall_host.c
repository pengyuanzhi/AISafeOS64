/**
 * @file    syscall_host.c
 * @brief   系统调用桩函数宿主机实现（测试用）
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 提供系统调用桩函数的宿主机模拟实现，
 *          用于在 x86_64 上运行单元测试。
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdint.h>
#include <unistd.h>
#include <string.h>

/**
 * @brief 宿主机 getpid 模拟
 */
int64_t syscall0(uint32_t nr)
{
    if (nr == 0x0008U)  /* SYS_THREAD_GET_ID */
    {
        return (int64_t)getpid();
    }
    return -1;
}

/**
 * @brief 宿主机 1 参数系统调用模拟
 */
int64_t syscall1(uint32_t nr, uint64_t a0)
{
    (void)nr;
    (void)a0;
    return 0;
}

/**
 * @brief 宿主机 2 参数系统调用模拟
 */
int64_t syscall2(uint32_t nr, uint64_t a0, uint64_t a1)
{
    if (nr == 0x0500U)  /* SYS_DEBUG_PRINT */
    {
        /* 在宿主机上直接 write 到 stdout */
        ssize_t ret = write(1, (const void *)(uintptr_t)a0, (size_t)a1);
        return (int64_t)ret;
    }
    return -1;
}

/**
 * @brief 宿主机 3 参数系统调用模拟
 */
int64_t syscall3(uint32_t nr, uint64_t a0, uint64_t a1, uint64_t a2)
{
    (void)nr;
    (void)a0;
    (void)a1;
    (void)a2;
    return -1;
}

/**
 * @brief 宿主机 5 参数系统调用模拟
 */
int64_t syscall5(uint32_t nr, uint64_t a0, uint64_t a1,
                 uint64_t a2, uint64_t a3, uint64_t a4)
{
    (void)nr;
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    return -1;
}
