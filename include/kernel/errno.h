/**
 * @file    errno.h
 * @brief   POSIX 错误码定义
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 微内核 RTOS POSIX 兼容错误码定义
 *          - 所有错误码定义为带 U 后缀的宏常量（MISRA-C:2012 合规）
 *          - 覆盖 POSIX.1 标准核心错误码
 *          - 内核错误码使用格式说明见下方 @ref errno_convention
 *
 * @note    MISRA-C:2012 合规
 * @note    所有错误码均为正整数，调用者需取负值返回
 *
 * @par errno_convention 内核错误码使用约定
 * @code
 * // 系统调用：成功返回 0 或正数，失败返回负错误码
 * long sys_read(int fd, void *buf, size_t count)
 * {
 *     if (buf == NULL)
 *     {
 *         return -(int32_t)EINVAL;   // 返回 -22
 *     }
 *     // ...
 *     return bytes_read;              // 成功返回读取字节数
 * }
 * @endcode
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_ERRNO_H
#define KERNEL_ERRNO_H

/* ============================================================================
 * POSIX 标准错误码（错误号 1 ~ 43）
 * ============================================================================ */

/**
 * @def EPERM
 * @brief 操作不允许（Operation not permitted）
 *
 * @details 尝试执行需要特权的操作，但调用者没有足够权限
 */
#define EPERM           1U

/**
 * @def ENOENT
 * @brief 文件或目录不存在（No such file or directory）
 *
 * @details 指定的路径名引用了一个不存在的文件或目录
 */
#define ENOENT          2U

/**
 * @def ESRCH
 * @brief 没有该进程（No such process）
 *
 * @details 指定的进程或线程不存在
 */
#define ESRCH           3U

/**
 * @def EINTR
 * @brief 系统调用被中断（Interrupted system call）
 *
 * @details 系统调用执行过程中被信号中断
 */
#define EINTR           4U

/**
 * @def EIO
 * @brief 输入/输出错误（I/O error）
 *
 * @details 底层 I/O 子系统报告的物理 I/O 错误
 */
#define EIO             5U

/**
 * @def ENXIO
 * @brief 设备或地址不存在（No such device or address）
 *
 * @details I/O 请求指向了不存在的设备或子设备
 */
#define ENXIO           6U

/**
 * @def E2BIG
 * @brief 参数列表过长（Argument list too long）
 *
 * @details exec 系列函数的参数列表过长
 */
#define E2BIG           7U

/**
 * @def ENOEXEC
 * @brief 执行格式错误（Exec format error）
 *
 * @details 请求执行的文件格式不正确
 */
#define ENOEXEC         8U

/**
 * @def EBADF
 * @brief 错误的文件描述符（Bad file descriptor）
 *
 * @details 文件描述符引用了未打开的文件或不可用于该操作的文件
 */
#define EBADF           9U

/**
 * @def ECHILD
 * @brief 没有子进程（No child processes）
 *
 * @details wait 或 waitpid 调用时没有子进程可等待
 */
#define ECHILD         10U

/**
 * @def EAGAIN
 * @brief 资源暂时不可用（Resource temporarily unavailable）
 *
 * @details 非阻塞操作无法立即完成，稍后重试可能成功
 *          与 EWOULDBLOCK 等价（定义相同值）
 */
#define EAGAIN         11U

/**
 * @def ENOMEM
 * @brief 内存不足（Not enough space）
 *
 * @details 内存分配失败，可用内存不足以满足请求
 */
#define ENOMEM         12U

/**
 * @def EACCES
 * @brief 权限被拒绝（Permission denied）
 *
 * @details 尝试以被文件权限禁止的方式访问文件
 */
#define EACCES         13U

/**
 * @def EFAULT
 * @brief 错误的地址（Bad address）
 *
 * @details 指针参数指向了不可访问的地址空间
 */
#define EFAULT         14U

/**
 * @def ENOTBLK
 * @brief 需要块设备（Block device required）
 *
 * @details 需要块设备但提供了非块设备
 */
#define ENOTBLK        15U

/**
 * @def EBUSY
 * @brief 设备或资源忙（Device or resource busy）
 *
 * @details 尝试使用系统正在使用的资源
 */
#define EBUSY          16U

/**
 * @def EEXIST
 * @brief 文件已存在（File exists）
 *
 * @details 尝试创建已存在的文件
 */
#define EEXIST         17U

/**
 * @def EXDEV
 * @brief 跨设备链接（Cross-device link）
 *
 * @details 尝试将文件链接到不同设备上的文件系统
 */
#define EXDEV          18U

/**
 * @def ENODEV
 * @brief 设备不存在（No such device）
 *
 * @details 尝试在不存在的设备上操作
 */
#define ENODEV         19U

/**
 * @def ENOTDIR
 * @brief 不是目录（Not a directory）
 *
 * @details 路径名的某个组件不是目录
 */
#define ENOTDIR        20U

/**
 * @def EISDIR
 * @brief 是目录（Is a directory）
 *
 * @details 对目录执行了只适用于文件的操作
 */
#define EISDIR         21U

/**
 * @def EINVAL
 * @brief 无效的参数（Invalid argument）
 *
 * @details 函数接收到不合法的参数值
 *          这是内核中最常用的错误码之一
 */
#define EINVAL         22U

/**
 * @def ENFILE
 * @brief 系统打开文件表溢出（Too many open files in system）
 *
 * @details 系统范围内打开的文件总数已达上限
 */
#define ENFILE         23U

/**
 * @def EMFILE
 * @brief 进程打开文件数过多（Too many open files）
 *
 * @details 单个进程打开的文件描述符数量已达上限
 */
#define EMFILE         24U

/**
 * @def ENOTTY
 * @brief 不适当的 I/O 控制操作（Inappropriate I/O control operation）
 *
 * @details 对设备执行了不支持的 ioctl 操作
 */
#define ENOTTY         25U

/* ETXTBSY=26 和 EFBIG=27 在嵌入式 RTOS 中较少使用，此处不定义 */
/* ENOSPC=28 通常由文件系统模块自行定义 */

/**
 * @def ESPIPE
 * @brief 非法寻址（Illegal seek）
 *
 * @details 尝试在管道或 FIFO 上执行 lseek 操作
 */
#define ESPIPE         29U

/**
 * @def EROFS
 * @brief 只读文件系统（Read-only file system）
 *
 * @details 尝试在只读文件系统上执行写操作
 */
#define EROFS          30U

/**
 * @def EMLINK
 * @brief 链接过多（Too many links）
 *
 * @details 文件的硬链接数量已达上限
 */
#define EMLINK         31U

/**
 * @def EPIPE
 * @brief 管道破裂（Broken pipe）
 *
 * @details 向没有读取者的管道写入数据
 */
#define EPIPE          32U

/**
 * @def EDOM
 * @brief 数学参数超出定义域（Domain error）
 *
 * @details 数学函数的参数超出其定义域
 */
#define EDOM           33U

/**
 * @def ERANGE
 * @brief 结果过大（Result too large）
 *
 * @details 数学函数的结果超出可表示的范围
 */
#define ERANGE         34U

/**
 * @def EDEADLK
 * @brief 资源死锁避免（Resource deadlock avoided）
 *
 * @details 检测到可能导致死锁的资源分配请求
 */
#define EDEADLK        35U

/**
 * @def ENAMETOOLONG
 * @brief 文件名过长（Filename too long）
 *
 * @details 路径名组件长度超过系统限制
 */
#define ENAMETOOLONG   36U

/**
 * @def ENOLCK
 * @brief 没有可用的锁（No locks available）
 *
 * @details 系统锁表已满，无法分配新的记录锁
 */
#define ENOLCK         37U

/**
 * @def ENOSYS
 * @brief 功能未实现（Function not implemented）
 *
 * @details 请求的系统调用或功能尚未实现
 *          常用于存根函数返回
 */
#define ENOSYS         38U

/**
 * @def ENOTEMPTY
 * @brief 目录非空（Directory not empty）
 *
 * @details 尝试删除仍包含文件的目录
 */
#define ENOTEMPTY      39U

/**
 * @def ELOOP
 * @brief 符号链接层级过多（Too many levels of symbolic links）
 *
 * @details 解析路径名时遇到过多的符号链接嵌套
 */
#define ELOOP          40U

/**
 * @def ENOMSG
 * @brief 没有指定类型的消息（No message of the desired type）
 *
 * @details 消息队列中没有符合请求类型的消息
 */
#define ENOMSG         42U

/**
 * @def EIDRM
 * @brief 标识符已被删除（Identifier removed）
 *
 * @details 操作的 IPC 标识符已从系统中删除
 */
#define EIDRM          43U

/* ============================================================================
 * 扩展错误码
 * ============================================================================ */

/**
 * @def EOVERFLOW
 * @brief 值过大（Value too large for defined data type）
 *
 * @details 要存储的值超过了目标数据类型能表示的范围
 */
#define EOVERFLOW      75U

/**
 * @def ETIMEDOUT
 * @brief 连接/操作超时（Operation timed out）
 *
 * @details 在指定的超时时间内操作未完成
 *          常用于 IPC 操作和锁获取的超时等待
 */
#define ETIMEDOUT     116U

#endif /* KERNEL_ERRNO_H */
