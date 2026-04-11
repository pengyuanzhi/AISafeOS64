/**
 * @file    exit.c
 * @brief   exit 实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 调用 atexit 注册的处理函数，然后通过系统调用退出
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdlib.h>

/* 声明 atexit 内部接口 */
extern atexit_fn atexit_get_func(int index);
extern int atexit_get_count(void);

/**
 * @brief 正常终止程序
 * @param status 退出状态码
 */
void exit(int status)
{
    int count = atexit_get_count();
    int i;

    /* 按注册相反顺序调用 atexit 处理函数 */
    for (i = count - 1; i >= 0; i--)
    {
        atexit_fn fn = atexit_get_func(i);
        if (fn != NULL)
        {
            fn();
        }
    }

    /* 使用 _exit 系统调用退出 */
#if defined(__aarch64__)
    __asm__ volatile(
        "mov x0, %0\n"
        "mov x8, #94\n"
        "svc #0\n"
        :: "r"((long)status)
        : "x0", "x8"
    );
#elif defined(__x86_64__)
    __asm__ volatile(
        "movl %0, %%edi\n"
        "movl $231, %%eax\n"  /* exit_group */
        "syscall\n"
        :: "r"(status)
        : "rax", "rdi"
    );
#else
    /* 回退：使用标准库 _exit */
    extern void _exit(int);
    _exit(status);
#endif

    /* 不会到达 */
    for (;;) {}
}
