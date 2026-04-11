/**
 * @file    errno.h
 * @brief   错误码接口
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 提供 errno 线程局部存储接口：
 *          - errno 宏（通过 __errno_location() 获取地址）
 *          - __errno_location() 函数声明
 *          - POSIX 标准错误码定义
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef AISAFE_ERRNO_H
#define AISAFE_ERRNO_H

/* ========================================================================
 * errno 接口
 * ======================================================================== */

/**
 * @brief 获取 errno 存储位置
 * @return 指向当前线程 errno 值的指针
 */
int *__errno_location(void);

/**
 * @brief errno 宏 — 通过函数获取线程局部存储地址
 */
#define errno (*__errno_location())

/* ========================================================================
 * POSIX 标准错误码
 * ======================================================================== */

#define EPERM           1       /**< @brief 操作不允许 */
#define ENOENT          2       /**< @brief 文件或目录不存在 */
#define ESRCH           3       /**< @brief 没有该进程 */
#define EINTR           4       /**< @brief 系统调用被中断 */
#define EIO             5       /**< @brief I/O 错误 */
#define ENXIO           6       /**< @brief 设备或地址不存在 */
#define E2BIG           7       /**< @brief 参数列表过长 */
#define ENOEXEC         8       /**< @brief 执行格式错误 */
#define EBADF           9       /**< @brief 错误的文件描述符 */
#define ECHILD          10      /**< @brief 没有子进程 */
#define EAGAIN          11      /**< @brief 资源暂时不可用 */
#define ENOMEM          12      /**< @brief 内存不足 */
#define EACCES          13      /**< @brief 权限被拒绝 */
#define EFAULT          14      /**< @brief 错误的地址 */
#define EBUSY           16      /**< @brief 设备或资源忙 */
#define EEXIST          17      /**< @brief 文件已存在 */
#define EXDEV           18      /**< @brief 跨设备链接 */
#define ENODEV          19      /**< @brief 设备不存在 */
#define ENOTDIR         20      /**< @brief 不是目录 */
#define EISDIR          21      /**< @brief 是目录 */
#define EINVAL          22      /**< @brief 无效的参数 */
#define ENFILE          23      /**< @brief 系统打开文件表溢出 */
#define EMFILE          24      /**< @brief 进程打开文件数过多 */
#define ENOTTY          25      /**< @brief 不适当的 I/O 控制操作 */
#define ESPIPE          29      /**< @brief 非法寻址 */
#define EROFS           30      /**< @brief 只读文件系统 */
#define EMLINK          31      /**< @brief 链接过多 */
#define EPIPE           32      /**< @brief 管道破裂 */
#define EDOM            33      /**< @brief 数学参数超出定义域 */
#define ERANGE          34      /**< @brief 结果过大 */
#define EDEADLK         35      /**< @brief 资源死锁避免 */
#define ENAMETOOLONG    36      /**< @brief 文件名过长 */
#define ENOLCK          37      /**< @brief 没有可用的锁 */
#define ENOSYS          38      /**< @brief 功能未实现 */
#define ENOTEMPTY       39      /**< @brief 目录非空 */
#define ELOOP           40      /**< @brief 符号链接层级过多 */
#define ENOMSG          42      /**< @brief 没有指定类型的消息 */
#define EIDRM           43      /**< @brief 标识符被删除 */
#define EOVERFLOW       75      /**< @brief 值过大 */
#define ETIMEDOUT       116     /**< @brief 操作超时 */

#endif /* AISAFE_ERRNO_H */
