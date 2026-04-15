/**
 * @file    crt0.c
 * @brief   C 运行时初始化（用户态程序启动）
 * @version 1.0
 *
 * C 运行时初始化流程：
 * - 初始化标准输入输出（stdin, stdout, stderr）
 * - 重定向到内核系统调用
 * - 设置 errno 初始化
 *
 * @note MISRA-C:2012 合规
 * @note AISafeOS64 用户态服务使用
 */

#include <stddef.h>
#include <stdint.h>
#include <kernel/syscall.h>
#include <kernel/errno.h>

/* ========================================================================
 * 标准文件描述符
 * ======================================================================== */

#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

/* ========================================================================
 * 标准 IO 文件描述符（仅占位）
 * ======================================================================== */

/**
 * @brief 占位：标准输入
 * @note 实际使用内核系统调用
 */
extern int stdin_fd;

/**
 * @brief 占位：标准输出
 * @note 实际使用内核系统调用
 */
extern int stdout_fd;

/**
 * @brief 占位：标准错误
 * @note 实际使用内核系统调用
 */
extern int stderr_fd;

/* ========================================================================
 * 全局变量
 * ======================================================================== */

/**
 * @brief errno 指针
 */
extern int *__errno_location(void);

/**
 * @brief exit code（main 返回值）
 */
static int s_exit_code = 0;

/* ========================================================================
 * 标准 IO 重定向到内核系统调用
 * ======================================================================== */

/**
 * @brief 占位：read 实现
 *
 * @param fd 文件描述符（0-2）
 * @param buf 缓冲区
 * @param count 字节数
 * @return 读取的字节数，-1 表示错误
 */
int read(int fd, void *buf, size_t count)
{
    (void)fd;
    (void)buf;
    (void)count;

    /* TODO: 实现标准输入读取 */
    /* 使用内核系统调用 SYS_READ */
    return -1;
}

/**
 * @brief 占位：write 实现
 *
 * @param fd 文件描述符（1-2）
 * @param buf 数据缓冲区
 * @param count 字节数
 * @return 写入的字节数，-1 表示错误
 */
int write(int fd, const void *buf, size_t count)
{
    (void)fd;
    (void)buf;
    (void)count;

    /* TODO: 实现标准输出写入 */
    /* 使用内核系统调用 SYS_WRITE 或 SYS_DEBUG_PRINT */
    return -1;
}

/**
 * @brief 占位：close 实现
 *
 * @param fd 文件描述符
 * @return 0 成功，-1 表示错误
 */
int close(int fd)
{
    (void)fd;

    /* TODO: 实现文件关闭 */
    /* 使用内核系统调用 SYS_CLOSE */
    return 0;
}

/**
 * @brief 占位：isatty 实现
 *
 * @param fd 文件描述符
 * @return 1 如果是终端，0 如果不是
 */
int isatty(int fd)
{
    /* 标准输入输出/标准错误总是终端 */
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO)
    {
        return 1;
    }

    return 0;
}

/* ========================================================================
 * exit 实现
 * ======================================================================== */

/**
 * @brief 退出程序并返回退出代码
 *
 * @param exit_code 退出代码
 */
void exit(int exit_code)
{
    s_exit_code = exit_code;

    /* 调用内核系统调用来退出 */
    int64_t ret = syscall0(SYS_THREAD_EXIT);

    /* 如果系统调用失败（预期行为），死循环 */
    if (ret < 0)
    {
        for (;;)
        {
            __asm__ volatile("wfi");
        }
    }
}

/* ========================================================================
 * exit 的封装版本（main 使用）
 * ======================================================================== */

/**
 * @brief 退出程序（main 函数专用）
 *
 * @param exit_code 退出代码
 */
static void _exit_wrapper(int exit_code)
{
    s_exit_code = exit_code;

    /* 调用内核系统调用来退出 */
    int64_t ret = syscall0(SYS_THREAD_EXIT);

    /* 如果系统调用失败，死循环 */
    if (ret < 0)
    {
        for (;;)
        {
            __asm__ volatile("wfi");
        }
    }
}

/* ========================================================================
 * C 运行时初始化
 * ======================================================================== */

/**
 * @brief C 运行时初始化函数
 *
 * 调用时机：
 * - 在 main() 函数之前调用
 * - 初始化标准 IO
 * - 设置 errno
 *
 * @return 0 成功
 */
int __libc_init(void)
{
    /* 初始化 errno */
    int *errno_ptr = __errno_location();
    if (errno_ptr != NULL)
    {
        *errno_ptr = 0;
    }

    /* TODO: 初始化标准 IO 文件描述符 */
    /* 可以在这里设置 stdin_fd, stdout_fd, stderr_fd */

    return 0;
}

/* ========================================================================
 * 全局退出代码获取
 * ======================================================================== */

/**
 * @brief 获取 main 函数的退出代码
 *
 * @return main 函数的返回值
 */
int __libc_exit_code(void)
{
    return s_exit_code;
}
