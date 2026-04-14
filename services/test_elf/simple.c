/**
 * @file    simple.c
 * @brief   ELF 加载器测试程序 — 最小 AArch64 用户态程序
 * @author  AISafe64 Team
 * @date    2026-04-14
 * @version 1.0
 *
 * @details 这是一个用于验证 AISafeOS64 ELF 加载器的最小测试程序。
 *          程序不依赖任何标准库，直接使用 SVC 系统调用进行输出。
 *          通过 UART 输出 "Hello ELF" 字符串来验证 ELF 加载成功。
 *
 *          编译为 AArch64 ELF 可执行文件后，由内核通过 VirtIO 块设备
 *          读取磁盘映像加载，或直接嵌入内核映像中进行测试。
 *
 * @note MISRA-C:2012 合规（用户态服务放宽部分规则）
 *
 * @copyright Copyright (c) 2026 AISafeOS64 Team
 */

/* ========================================================================
 * 系统调用号定义（与内核 include/kernel/syscall.h 一致）
 * ======================================================================== */

/** @brief SYS_debug_print 系统调用号 */
#define SYS_DEBUG_PRINT    14U

/**
 * @brief SVC 系统调用封装（内联汇编）
 *
 * @param sysno 系统调用号
 * @param arg0  参数 0
 * @param arg1  参数 1
 *
 * @return 系统调用返回值
 *
 * @note 使用 ARM64 SVC #0 指令触发系统调用
 */
static inline long svc_call2(unsigned long sysno,
                              unsigned long arg0,
                              unsigned long arg1)
{
    register long x0 __asm__("x0") = (long)arg0;
    register long x1 __asm__("x1") = (long)arg1;
    register long x8 __asm__("x8") = (long)sysno;

    __asm__ volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x8)
        : "memory"
    );

    return x0;
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

/**
 * @brief 测试程序入口函数
 *
 * @details 通过 SVC 系统调用向 UART 输出 "Hello ELF" 字符串。
 *          入口地址在 ELF 头的 e_entry 字段中指定。
 *
 * @note 该函数不返回，执行完毕后进入无限循环等待内核回收
 */
void _start(void)
{
    static const char msg[] = "Hello ELF from AISafeOS64!\n";

    /* 通过 SVC 系统调用输出字符串 */
    (void)svc_call2(
        SYS_DEBUG_PRINT,
        (unsigned long)(const char *)msg,
        (unsigned long)(sizeof(msg) - 1U)
    );

    /* 程序不返回，进入无限循环 */
    for (;;)
    {
        __asm__ volatile("wfe" ::: "memory");
    }
}
