/**
 * @file    _exit.c
 * @brief   _exit() 实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 立即终止进程，不调用 atexit 处理函数
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <unistd.h>
#include <aisafe/syscall.h>

/**
 * @brief 立即终止进程
 *
 * @param status 退出状态码
 */
void _exit(int status)
{
    (void)syscall1(SYS_THREAD_EXIT, (uint64_t)(int64_t)status);

    /* 不会到达 */
    for (;;) {}
}
