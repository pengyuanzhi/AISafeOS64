/**
 * @file    test_fs_svc.c
 * @brief   文件系统 SVC 直通测试（freestanding）
 */

#include <stdint.h>

#define SYS_OPEN    0x0680U
#define SYS_CLOSE   0x0681U
#define SYS_READ    0x0682U
#define SYS_WRITE   0x0683U

static inline long svc3(uint64_t x8, uint64_t x0, uint64_t x1, uint64_t x2)
{
    register uint64_t r8 asm("x8") = x8;
    register uint64_t r0 asm("x0") = x0;
    register uint64_t r1 asm("x1") = x1;
    register uint64_t r2 asm("x2") = x2;
    asm volatile("svc #0" : "+r"(r0) : "r"(r8), "r"(r1), "r"(r2) : "memory");
    return (long)r0;
}

static inline long svc2(uint64_t x8, uint64_t x0, uint64_t x1)
{
    return svc3(x8, x0, x1, 0);
}

static inline long svc1(uint64_t x8, uint64_t x0)
{
    return svc3(x8, x0, 0, 0);
}

void _start(void)
{
    /* 测试 1: 写到 stdout（fd=1） */
    svc3(SYS_WRITE, 1, (uint64_t)"=== FS test ===\n", 16);

    /* 测试 2: 创建并写入文件 */
    long fd = svc3(SYS_OPEN, (uint64_t)"/test.txt", 0x42U, 0); /* O_RDWR | O_CREAT */
    if (fd >= 0)
    {
        svc3(SYS_WRITE, (uint64_t)fd, (uint64_t)"Hello RAMFS!\n", 13);
        svc3(SYS_WRITE, 1, (uint64_t)"write ok\n", 9);
        svc1(SYS_CLOSE, (uint64_t)fd);
    }
    else
    {
        svc3(SYS_WRITE, 1, (uint64_t)"open failed\n", 12);
    }

    /* 测试 3: 重新打开并读取 */
    fd = svc3(SYS_OPEN, (uint64_t)"/test.txt", 0x2U, 0); /* O_RDWR */
    if (fd >= 0)
    {
        char buf[32];
        int i;
        for (i = 0; i < 31; i++) buf[i] = 0;
        long n = svc3(SYS_READ, (uint64_t)fd, (uint64_t)buf, 31);
        if (n > 0)
        {
            svc3(SYS_WRITE, 1, (uint64_t)"read: ", 6);
            svc3(SYS_WRITE, 1, (uint64_t)buf, (uint64_t)n);
        }
        svc1(SYS_CLOSE, (uint64_t)fd);
    }

    svc3(SYS_WRITE, 1, (uint64_t)"=== done ===\n", 13);

    for (;;)
    {
        asm volatile("wfe");
    }
}
