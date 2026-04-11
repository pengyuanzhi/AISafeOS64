/**
 * @file    getpid.c
 * @brief   getpid() 实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 通过 SYS_THREAD_GET_ID 获取当前线程/进程 ID
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <unistd.h>
#include <aisafe/syscall.h>

/**
 * @brief 获取进程 ID
 *
 * @details 在微内核中，线程 ID 等同于进程 ID（单线程进程模型）
 *
 * @return 当前线程/进程 ID
 */
pid_t getpid(void)
{
    return (pid_t)syscall0(SYS_THREAD_GET_ID);
}
