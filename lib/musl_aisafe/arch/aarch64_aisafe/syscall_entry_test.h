/**
 * @file    syscall_entry_test.h
 * @brief   AISafeOS64 ARM64 SVC 调用桩函数（测试版本）
 * @version 1.0
 *
 * 测试版本：模拟 SVC 调用行为，无需实际内核支持。
 */

#ifndef SYSCALL_ENTRY_TEST_H
#define SYSCALL_ENTRY_TEST_H

/**
 * @brief 模拟内核系统调用返回值
 */

/* 模拟的 PID */
static long s_test_pid = 12345;

/**
 * @brief 通用 SVC 调用桩（6 参数）
 */
static inline long aisafe_svc_call(long nr, long a0, long a1, long a2,
                                    long a3, long a4, long a5)
{
    /* 模拟 AISafeOS64 系统调用 */
    switch (nr)
    {
    /* 线程管理 */
    case 0x0001:  /* THREAD_CREATE */
        return s_test_pid;  /* 返回新线程 PID */
    case 0x0002:  /* THREAD_EXIT */
        return 0;
    case 0x0007:  /* THREAD_YIELD */
        return 0;
    case 0x0008:  /* THREAD_GET_ID */
        return s_test_pid;  /* 返回当前线程 PID */

    /* IPC 操作 */
    case 0x0100:  /* CHANNEL_CREATE */
        return 100;  /* 返回 channel id */
    case 0x0102:  /* CONNECT_ATTACH */
        return 0;
    case 0x0104:  /* MSG_SEND */
        return 0;
    case 0x0105:  /* MSG_RECV */
        return 0;
    case 0x0106:  /* MSG_REPLY */
        return 0;
    case 0x010A:  /* EP_CREATE */
        return 200;  /* 返回 endpoint id */

    /* 内存管理 */
    case 0x0202:  /* VM_MAP */
        return 0x1000000;  /* 返回映射地址 */
    case 0x0203:  /* VM_UNMAP */
        return 0;
    case 0x0204:  /* VM_PROTECT */
        return 0;

    /* 调试/信息 */
    case 0x0500:  /* DEBUG_PRINT */
        return 0;  /* 忽略输出 */

    default:
        return -ENOSYS;  /* 不支持的系统调用 */
    }
}

/** @brief 0 参数 SVC 调用桩 */
static inline long aisafe_svc0(long nr)
{
    return aisafe_svc_call(nr, 0, 0, 0, 0, 0, 0);
}

/** @brief 1 参数 SVC 调用桩 */
static inline long aisafe_svc1(long nr, long a0)
{
    return aisafe_svc_call(nr, a0, 0, 0, 0, 0, 0);
}

/** @brief 2 参数 SVC 调用桩 */
static inline long aisafe_svc2(long nr, long a0, long a1)
{
    return aisafe_svc_call(nr, a0, a1, 0, 0, 0, 0);
}

/** @brief 3 参数 SVC 调用桩 */
static inline long aisafe_svc3(long nr, long a0, long a1, long a2)
{
    return aisafe_svc_call(nr, a0, a1, a2, 0, 0, 0);
}

#endif /* SYSCALL_ENTRY_TEST_H */
