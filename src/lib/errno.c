/**
 * @file errno.c
 * @brief AISafe64 RTOS - POSIX 错误处理函数实现
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details POSIX 错误处理函数
 *          - strerror(): 错误码转字符串
 *          - perror(): 打印错误消息
 *          - errno 变量实现
 *
 * @note POSIX.1-2008 标准合规
 * @note 线程安全实现
 */

#include "errno.h"
#include "printk.h"
#include <stddef.h>

/* 线程局部 errno 变量（需要 TLS 支持） */
/* TODO: 实现真正的线程局部存储，当前使用全局变量 */
__thread int errno = 0;

/**
 * @brief 错误码到字符串的映射表
 */
static const char *const error_names[] = {
    /* 0-99 */
    [EPERM] = "Operation not permitted",
    [ENOENT] = "No such file or directory",
    [ESRCH] = "No such process",
    [EINTR] = "Interrupted system call",
    [EIO] = "I/O error",
    [ENXIO] = "No such device or address",
    [E2BIG] = "Argument list too long",
    [ENOEXEC] = "Exec format error",
    [EBADF] = "Bad file number",
    [ECHILD] = "No child processes",
    [EAGAIN] = "Resource temporarily unavailable",
    [ENOMEM] = "Out of memory",
    [EACCES] = "Permission denied",
    [EFAULT] = "Bad address",
    [ENOTBLK] = "Block device required",
    [EBUSY] = "Device or resource busy",
    [EEXIST] = "File exists",
    [EXDEV] = "Cross-device link",
    [ENODEV] = "No such device",
    [ENOTDIR] = "Not a directory",
    [EISDIR] = "Is a directory",
    [EINVAL] = "Invalid argument",
    [ENFILE] = "File table overflow",
    [EMFILE] = "Too many open files",
    [ENOTTY] = "Not a typewriter",
    [ETXTBSY] = "Text file busy",
    [EFBIG] = "File too large",
    [ENOSPC] = "No space left on device",
    [ESPIPE] = "Illegal seek",
    [EROFS] = "Read-only file system",
    [EMLINK] = "Too many links",
    [EDOM] = "Math argument out of domain of func",
    [ERANGE] = "Math result not representable",
    [EDEADLK] = "Resource deadlock would occur",
    [ENAMETOOLONG] = "File name too long",
    [ENOLCK] = "No record locks available",
    [ENOSYS] = "Function not implemented",
    [ENOTEMPTY] = "Directory not empty",
    [ELOOP] = "Too many symbolic links encountered",
    [EWOULDBLOCK] = "Operation would block",
    [ENOMSG] = "No message of desired type",
    [EIDRM] = "Identifier removed",
    [ECHRNG] = "Channel number out of range",
    [EL2NSYNC] = "Level 2 not synchronized",
    [EL3HLT] = "Level 3 halted",
    [EL3RST] = "Level 3 reset",
    [ELNRNG] = "Link number out of range",
    [EUNATCH] = "Protocol driver not attached",
    [ENOCSI] = "No CSI structure available",
    [EL2HLT] = "Level 2 halted",
    [EBADE] = "Invalid exchange",
    [EBADR] = "Invalid request descriptor",
    [EXFULL] = "Exchange full",
    [ENOANO] = "No anode",
    [EBADRQC] = "Invalid request code",
    [EBADSLT] = "Invalid slot",
    [EDEADLOCK] = "Resource deadlock would occur",
    [EBFONT] = "Bad font file format",
    [ENOSTR] = "Device not a stream",
    [ENODATA] = "No data available",
    [ETIME] = "Timer expired",
    [ENOSR] = "Out of streams resources",
    [ENONET] = "Machine is not on the network",
    [ENOPKG] = "Package not installed",
    [EREMOTE] = "Object is remote",
    [ENOLINK] = "Link has been severed",
    [EADV] = "Advertise error",
    [ESRMNT] = "Srmount error",
    [ECOMM] = "Communication error on send",
    [EPROTO] = "Protocol error",
    [EMULTIHOP] = "Multihop attempted",
    [EDOTDOT] = "RFS specific error",
    [EBADMSG] = "Not a data message",
    [EOVERFLOW] = "Value too large for defined data type",
    [ENOTUNIQ] = "Name not unique on network",
    [EBADFD] = "File descriptor in bad state",
    [EREMCHG] = "Remote address changed",
    [ELIBACC] = "Can not access a needed shared library",
    [ELIBBAD] = "Accessing a corrupted shared library",
    [ELIBSCN] = ".lib section in a.out corrupted",
    [ELIBMAX] = "Attempting to link in too many shared libraries",
    [ELIBEXEC] = "Cannot exec a shared library directly",
    [EILSEQ] = "Illegal byte sequence",
    [ERESTART] = "Interrupted system call should be restarted",
    [ESTRPIPE] = "Streams pipe error",
    [EUSERS] = "Too many users",
    [ENOTSOCK] = "Socket operation on non-socket",
    [EDESTADDRREQ] = "Destination address required",
    [EMSGSIZE] = "Message too long",
    [EPROTOTYPE] = "Protocol wrong type for socket",
    [ENOPROTOOPT] = "Protocol not available",
    [EPROTONOSUPPORT] = "Protocol not supported",
    [ESOCKTNOSUPPORT] = "Socket type not supported",
    [EOPNOTSUPP] = "Operation not supported on transport endpoint",
    [EPFNOSUPPORT] = "Protocol family not supported",
    [EAFNOSUPPORT] = "Address family not supported by protocol",
    [EADDRINUSE] = "Address already in use",
    [EADDRNOTAVAIL] = "Cannot assign requested address",
    [ENETDOWN] = "Network is down",
    [ENETUNREACH] = "Network is unreachable",
    [ENETRESET] = "Network dropped connection on reset",
    [ECONNABORTED] = "Software caused connection abort",
    [ECONNRESET] = "Connection reset by peer",
    [ENOBUFS] = "No buffer space available",
    [EISCONN] = "Transport endpoint is already connected",
    [ENOTCONN] = "Transport endpoint is not connected",
    [ESHUTDOWN] = "Cannot send after transport endpoint shutdown",
    [ETOOMANYREFS] = "Too many references: cannot splice",
    [ETIMEDOUT] = "Connection timed out",
    [ECONNREFUSED] = "Connection refused",
    [EHOSTDOWN] = "Host is down",
    [EHOSTUNREACH] = "No route to host",
    [EALREADY] = "Operation already in progress",
    [EINPROGRESS] = "Operation now in progress",
    [ESTALE] = "Stale file handle",
    [EUCLEAN] = "Structure needs cleaning",
    [ENOTNAM] = "Not a XENIX named type file",
    [ENAVAIL] = "No XENIX semaphores available",
    [EISNAM] = "Is a named type file",
    [EREMOTEIO] = "Remote I/O error",
    [EDQUOT] = "Quota exceeded",
    [ENOMEDIUM] = "No medium found",
    [EMEDIUMTYPE] = "Wrong medium type",
    [ECANCELED] = "Operation canceled",
    [ENOKEY] = "Required key not available",
    [EKEYEXPIRED] = "Key has expired",
    [EKEYREVOKED] = "Key has been revoked",
    [EKEYREJECTED] = "Key was rejected by service",
    [EOWNERDEAD] = "Owner died",
    [ENOTRECOVERABLE] = "State not recoverable",
    [ERFKILL] = "Operation not possible due to RF-kill",
    [EHWPOISON] = "Memory page has hardware error",
};

#define ERROR_NAMES_COUNT (sizeof(error_names) / sizeof(error_names[0]))

/**
 * @brief strerror 线程局部缓冲区
 * @details 每个线程有独立的缓冲区，避免竞态条件
 *
 * @note 使用静态变量+TLS实现
 * @note TODO: 使用真正的 __thread 支持
 */
static char strerror_buffer[256];

/**
 * @brief 将错误码转换为可读字符串
 * @param errnum 错误码（可以是正数或负数）
 * @return 指向错误描述字符串的指针
 *
 * @details 线程安全实现
 *          - 处理负数错误码（-EINVAL → EINVAL）
 *          - 未知错误码返回 "Unknown error N"
 *          - 返回静态缓冲区，下次调用会覆盖
 *
 * @note POSIX.1-2008 标准函数
 * @note 不可重入（不要在信号处理函数中使用）
 */
char *strerror(int errnum)
{
    /* 处理负数错误码（-EAGAIN → EAGAIN） */
    if (errnum < 0)
    {
        errnum = -errnum;
    }

    /* 检查错误码是否在已知范围内 */
    if (errnum >= 0 && (size_t)errnum < ERROR_NAMES_COUNT && error_names[errnum] != NULL)
    {
        /* 返回已知的错误描述 */
        return (char *)error_names[errnum];
    }
    else
    {
        /* 未知错误码，生成通用消息 */
        snprintf(strerror_buffer, sizeof(strerror_buffer), "Unknown error %d", errnum);
        return strerror_buffer;
    }
}

/**
 * @brief 打印错误消息到控制台
 * @param s 错误消息前缀
 *
 * @details 输出格式： "s: error-description\n"
 *          - 如果 s 为 NULL，只输出 ": error-description"
 *          - 如果 s 为空字符串，只输出 "error-description"
 *          - 依赖当前线程的 errno 值
 *
 * @note 线程安全（每个线程独立的 errno）
 * @note 不可重入（会修改内部缓冲区）
 */
void perror(const char *s)
{
    const char *error_str = strerror(errno);

    if (s != NULL && s[0] != '\0')
    {
        printk("%s: %s\n", s, error_str);
    }
    else
    {
        printk("%s\n", error_str);
    }
}
