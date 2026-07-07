/**
 * @file    init_svc.c
 * @brief   最小 init 服务（freestanding，不依赖 musl/crt0）
 * @author  AISafe64 Team
 * @date    2026-07-07
 * @version 1.0
 *
 * @details init 服务是内核启动后创建的第一个用户态进程。
 *          职责：
 *          1. 创建 IPC 端点（服务注册点）
 *          2. 通过 SVC 等待其他服务的连接请求
 *          3. 当前为最小验证版本（创建端点 + 打印状态 + 循环等待）
 *
 *          后续扩展：
 *          - 通过 IPC 启动 fs/net/proc 等子服务
 *          - 服务依赖图管理
 *          - 心跳监控和自动重启
 *
 * @note    纯 SVC 系统调用，不依赖 musl libc 或 crt0
 *
 * @revision history
 * v1.0 2026-07-07 初始版本
 */

#include <stdint.h>

/* 系统调用号 */
#define SYS_DEBUG_PRINT    0x0800U
#define SYS_EP_CREATE      0x010AU
#define SYS_MSG_RECV       0x0105U
#define SYS_MSG_REPLY      0x0106U
#define SYS_PROCESS_GETPID 0x0503U
#define SYS_CLOCK_GETTIME  0x0704U
#define SYS_NANOSLEEP      0x0703U

/* SVC 内联函数 */
static inline long svc0(uint64_t nr)
{
    register uint64_t x8 asm("x8") = nr;
    register uint64_t x0 asm("x0");
    asm volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return (long)x0;
}

static inline long svc1(uint64_t nr, uint64_t a0)
{
    register uint64_t x8 asm("x8") = nr;
    register uint64_t r0 asm("x0") = a0;
    asm volatile("svc #0" : "+r"(r0) : "r"(x8) : "memory");
    return (long)r0;
}

static inline long svc3(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2)
{
    register uint64_t x8 asm("x8") = nr;
    register uint64_t r0 asm("x0") = a0;
    register uint64_t r1 asm("x1") = a1;
    register uint64_t r2 asm("x2") = a2;
    asm volatile("svc #0" : "+r"(r0) : "r"(x8), "r"(r1), "r"(r2) : "memory");
    return (long)r0;
}

static void print(const char *msg)
{
    uint32_t len = 0U;
    while (msg[len] != '\0')
    {
        len++;
    }
    svc3(SYS_DEBUG_PRINT, (uint64_t)msg, (uint64_t)len, 0);
}

static void print_dec(uint32_t val)
{
    char buf[12];
    int i = 10;
    buf[i] = '\0';
    if (val == 0U)
    {
        buf[--i] = '0';
    }
    else
    {
        while ((val > 0U) && (i > 0))
        {
            i--;
            buf[i] = (char)('0' + (val % 10U));
            val /= 10U;
        }
    }
    print(&buf[i]);
}

/**
 * @brief init 服务入口
 *
 * @details 不接收参数，不返回（循环等待消息）。
 */
void _start(void)
{
    uint32_t pid;
    long ep_id;

    print("init: starting\n");

    /* 获取自己的 PID */
    pid = (uint32_t)svc0(SYS_PROCESS_GETPID);
    print("init: pid=");
    print_dec(pid);
    print("\n");

    /* 创建 IPC 端点（服务注册点） */
    ep_id = svc1(SYS_EP_CREATE, 0U);
    if (ep_id >= 0)
    {
        print("init: endpoint created, id=");
        print_dec((uint32_t)ep_id);
        print("\n");
    }
    else
    {
        print("init: endpoint create failed\n");
    }

    /* 主循环：等待消息 */
    print("init: waiting for messages...\n");

    for (;;)
    {
        /* 简化：每秒打印心跳 */
        svc1(SYS_NANOSLEEP, 1000000000ULL);

        /* 打印当前时间 */
        uint64_t ns = svc0(SYS_CLOCK_GETTIME);
        print("init: tick ns=");
        print_dec((uint32_t)(ns / 1000000ULL));
        print("ms\n");
    }
}
