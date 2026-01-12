/**
 * @file syscall.h
 * @brief AISafe64 RTOS - 系统调用接口
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 系统调用接口定义
 *          - 系统调用号定义
 *          - 系统调用函数原型
 *          - 用户空间系统调用包装宏
 *
 * @note ARMv8-A SVC指令系统调用
 *       - 系统调用号通过x8寄存器传递
 *       - 参数通过x0-x7寄存器传递
 *       - 返回值通过x0寄存器返回
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief 系统调用号定义
 * @details 遵循Linux系统调用约定（部分）
 */
#define SYS_WRITE 1      /**< 写控制台 */
#define SYS_READ 2       /**< 读控制台 */
#define SYS_EXIT 3       /**< 退出任务 */
#define SYS_GETPID 4     /**< 获取任务ID */
#define SYS_YIELD 5      /**< 让出CPU */
#define SYS_SLEEP 6      /**< 睡眠指定时间 */
#define SYS_MALLOC 7     /**< 分配内存 */
#define SYS_FREE 8       /**< 释放内存 */
#define SYS_GETTIME 9    /**< 获取系统时间 */
#define SYS_SCHED_SET 10 /**< 设置调度参数 */
#define SYS_SCHED_GET 11 /**< 获取调度参数 */

/**
 * @brief 系统调用错误码
 */
#define SYS_SUCCESS 0      /**< 成功 */
#define SYS_ERROR_INVAL -1 /**< 无效参数 */
#define SYS_ERROR_NOMEM -2 /**< 内存不足 */
#define SYS_ERROR_NOSYS -3 /**< 系统调用未实现 */
#define SYS_ERROR_FAULT -4 /**< 内存错误 */

    /**
     * @brief 系统调用处理函数
     * @param syscall_nr 系统调用号
     * @param params 参数数组指针（指向栈上的参数）
     * @return 系统调用返回值
     *
     * @details 由start.S的SVC异常处理调用
     *          - 验证系统调用号
     *          - 调用对应的系统调用实现
     *          - 返回结果到用户空间
     */
    int64_t syscall_handler(uint64_t syscall_nr, uint64_t *params);

/**
 * @brief 用户空间系统调用包装宏
 */
#define SYSCALL_DECL(name, nr) static inline int64_t sys_##name

/**
 * @brief 执行系统调用（内联汇编）
 */
#define SYSCALL_INVOKE(nr, ...)                                                      \
    ({                                                                               \
        register uint64_t x8 asm("x8") = (nr);                                       \
        register int64_t x0 asm("x0");                                               \
        asm volatile("svc #0" : "=r"(x0) : "r"(x8), ##__VA_ARGS__ : "memory", "cc"); \
        x0;                                                                          \
    })

    /**
     * @brief 系统调用包装函数（用户空间）
     */

    /**
     * @brief 写控制台
     * @param buf 缓冲区指针
     * @param count 字节数
     * @return 成功写入的字节数，失败返回负错误码
     */
    static inline int64_t sys_write(const void *buf, uint64_t count)
    {
        register uint64_t x0 asm("x0") = (uint64_t)buf;
        register uint64_t x1 asm("x1") = count;
        register uint64_t x8 asm("x8") = SYS_WRITE;

        asm volatile("svc #0" : "=r"(x0) : "r"(x0), "r"(x1), "r"(x8) : "memory", "cc");

        return (int64_t)x0;
    }

    /**
     * @brief 读控制台
     * @param buf 缓冲区指针
     * @param count 最大读取字节数
     * @return 成功读取的字节数，失败返回负错误码
     */
    static inline int64_t sys_read(void *buf, uint64_t count)
    {
        register uint64_t x0 asm("x0") = (uint64_t)buf;
        register uint64_t x1 asm("x1") = count;
        register uint64_t x8 asm("x8") = SYS_READ;

        asm volatile("svc #0" : "=r"(x0) : "r"(x0), "r"(x1), "r"(x8) : "memory", "cc");

        return (int64_t)x0;
    }

    /**
     * @brief 退出当前任务
     * @param exit_code 退出码
     * @return 不返回
     */
    static inline void sys_exit(int exit_code) __attribute__((noreturn));
    static inline void sys_exit(int exit_code)
    {
        register uint64_t x0 asm("x0") = (uint64_t)exit_code;
        register uint64_t x8 asm("x8") = SYS_EXIT;

        asm volatile("svc #0" : : "r"(x0), "r"(x8) : "memory", "cc");

        /* 永不返回 */
        for (;;)
        {
            __asm__ volatile("wfe");
        }
    }

    /**
     * @brief 获取当前任务ID
     * @return 任务ID
     */
    static inline int64_t sys_getpid(void)
    {
        register uint64_t x8 asm("x8") = SYS_GETPID;
        register int64_t x0 asm("x0");

        asm volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory", "cc");

        return x0;
    }

    /**
     * @brief 让出CPU
     * @return 成功返回0
     */
    static inline int64_t sys_yield(void)
    {
        register uint64_t x8 asm("x8") = SYS_YIELD;
        register int64_t x0 asm("x0");

        asm volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory", "cc");

        return x0;
    }

    /**
     * @brief 睡眠指定时间
     * @param ms 毫秒数
     * @return 成功返回0，失败返回负错误码
     */
    static inline int64_t sys_sleep(uint64_t ms)
    {
        register uint64_t x0 asm("x0") = ms;
        register uint64_t x8 asm("x8") = SYS_SLEEP;
        register int64_t ret asm("x0");

        asm volatile("svc #0" : "=r"(ret) : "r"(x0), "r"(x8) : "memory", "cc");

        return ret;
    }

    /**
     * @brief 分配内存
     * @param size 字节数
     * @return 内存指针，失败返回NULL
     */
    static inline void *sys_malloc(uint64_t size)
    {
        register uint64_t x0 asm("x0") = size;
        register uint64_t x8 asm("x8") = SYS_MALLOC;
        register void *ret asm("x0");

        asm volatile("svc #0" : "=r"(ret) : "r"(x0), "r"(x8) : "memory", "cc");

        return ret;
    }

    /**
     * @brief 释放内存
     * @param ptr 内存指针
     * @return 成功返回0，失败返回负错误码
     */
    static inline int64_t sys_free(void *ptr)
    {
        register uint64_t x0 asm("x0") = (uint64_t)ptr;
        register uint64_t x8 asm("x8") = SYS_FREE;
        register int64_t ret asm("x0");

        asm volatile("svc #0" : "=r"(ret) : "r"(x0), "r"(x8) : "memory", "cc");

        return ret;
    }

    /**
     * @brief 获取系统时间（毫秒）
     * @return 系统运行时间（毫秒）
     */
    static inline int64_t sys_gettime(void)
    {
        register uint64_t x8 asm("x8") = SYS_GETTIME;
        register int64_t x0 asm("x0");

        asm volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory", "cc");

        return x0;
    }

#ifdef __cplusplus
}
#endif

#endif /* SYSCALL_H */
