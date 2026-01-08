/**
 * @file errno.h
 * @brief AISafe64 RTOS - POSIX 错误码定义
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details POSIX.1-2008 错误码和 errno 变量
 *          - 标准错误码定义
 *          - 线程局部 errno 变量
 *          - strerror() 函数声明
 *
 * @note 遵循 POSIX.1-2008 标准
 * @note 与 Linux kernel 和 glibc 兼容
 */

#ifndef ERRNO_H
#define ERRNO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief POSIX 标准错误码
     *
     * @note 这些值必须与 Linux kernel 保持一致
     * @note 参见：include/uapi/asm-generic/errno.h
     *
     * 常用错误码：
     * - EPERM (1): Operation not permitted
     * - ENOENT (2): No such file or directory
     * - EINTR (4): Interrupted system call
     * - EIO (5): I/O error
     * - ENXIO (6): No such device or address
     * - EAGAIN (11): Resource temporarily unavailable
     * - ENOMEM (12): Out of memory
     * - EACCES (13): Permission denied
     * - EBUSY (16): Device or resource busy
     * - EEXIST (17): File exists
     * - EINVAL (22): Invalid argument
     * - ENOSPC (28): No space left on device
     * - EOVERFLOW (75): Value too large
     * - ENOTSUP (95): Not supported
     * - ETIMEDOUT (110): Connection timed out
     */
#define EPERM 1             /**< Operation not permitted */
#define ENOENT 2            /**< No such file or directory */
#define ESRCH 3             /**< No such process */
#define EINTR 4             /**< Interrupted system call */
#define EIO 5               /**< I/O error */
#define ENXIO 6             /**< No such device or address */
#define E2BIG 7             /**< Argument list too long */
#define ENOEXEC 8           /**< Exec format error */
#define EBADF 9             /**< Bad file number */
#define ECHILD 10           /**< No child processes */
#define EAGAIN 11           /**< Resource temporarily unavailable (would block) */
#define ENOMEM 12           /**< Out of memory */
#define EACCES 13           /**< Permission denied */
#define EFAULT 14           /**< Bad address */
#define ENOTBLK 15          /**< Block device required */
#define EBUSY 16            /**< Device or resource busy */
#define EEXIST 17           /**< File exists */
#define EXDEV 18            /**< Cross-device link */
#define ENODEV 19           /**< No such device */
#define ENOTDIR 20          /**< Not a directory */
#define EISDIR 21           /**< Is a directory */
#define EINVAL 22           /**< Invalid argument */
#define ENFILE 23           /**< File table overflow */
#define EMFILE 24           /**< Too many open files */
#define ENOTTY 25           /**< Not a typewriter */
#define ETXTBSY 26          /**< Text file busy */
#define EFBIG 27            /**< File too large */
#define ENOSPC 28           /**< No space left on device */
#define ESPIPE 29           /**< Illegal seek */
#define EROFS 30            /**< Read-only file system */
#define EMLINK 31           /**< Too many links */
#define EPIPE 32            /**< Broken pipe */
#define EDOM 33             /**< Math argument out of domain of func */
#define ERANGE 34           /**< Math result not representable */
#define EDEADLK 35          /**< Resource deadlock would occur */
#define ENAMETOOLONG 36     /**< File name too long */
#define ENOLCK 37           /**< No record locks available */
#define ENOSYS 38           /**< Function not implemented */
#define ENOTEMPTY 39        /**< Directory not empty */
#define ELOOP 40            /**< Too many symbolic links encountered */
#define EWOULDBLOCK EAGAIN  /**< Operation would block (same as EAGAIN) */
#define ENOMSG 42           /**< No message of desired type */
#define EIDRM 43            /**< Identifier removed */
#define ECHRNG 44           /**< Channel number out of range */
#define EL2NSYNC 45         /**< Level 2 not synchronized */
#define EL3HLT 46           /**< Level 3 halted */
#define EL3RST 47           /**< Level 3 reset */
#define ELNRNG 48           /**< Link number out of range */
#define EUNATCH 49          /**< Protocol driver not attached */
#define ENOCSI 50           /**< No CSI structure available */
#define EL2HLT 51           /**< Level 2 halted */
#define EBADE 52            /**< Invalid exchange */
#define EBADR 53            /**< Invalid request descriptor */
#define EXFULL 54           /**< Exchange full */
#define ENOANO 55           /**< No anode */
#define EBADRQC 56          /**< Invalid request code */
#define EBADSLT 57          /**< Invalid slot */
#define EDEADLOCK EDEADLK   /**< File locking deadlock error */
#define EBFONT 59           /**< Bad font file format */
#define ENOSTR 60           /**< Device not a stream */
#define ENODATA 61          /**< No data available */
#define ETIME 62            /**< Timer expired */
#define ENOSR 63            /**< Out of streams resources */
#define ENONET 64           /**< Machine is not on the network */
#define ENOPKG 65           /**< Package not installed */
#define EREMOTE 66          /**< Object is remote */
#define ENOLINK 67          /**< Link has been severed */
#define EADV 68             /**< Advertise error */
#define ESRMNT 69           /**< Srmount error */
#define ECOMM 70            /**< Communication error on send */
#define EPROTO 71           /**< Protocol error */
#define EMULTIHOP 72        /**< Multihop attempted */
#define EDOTDOT 73          /**< RFS specific error */
#define EBADMSG 74          /**< Not a data message */
#define EOVERFLOW 75        /**< Value too large for defined data type */
#define ENOTUNIQ 76         /**< Name not unique on network */
#define EBADFD 77           /**< File descriptor in bad state */
#define EREMCHG 78          /**< Remote address changed */
#define ELIBACC 79          /**< Can not access a needed shared library */
#define ELIBBAD 80          /**< Accessing a corrupted shared library */
#define ELIBSCN 81          /**< .lib section in a.out corrupted */
#define ELIBMAX 82          /**< Attempting to link in too many shared libraries */
#define ELIBEXEC 83         /**< Cannot exec a shared library directly */
#define EILSEQ 84           /**< Illegal byte sequence */
#define ERESTART 85         /**< Interrupted system call should be restarted */
#define ESTRPIPE 86         /**< Streams pipe error */
#define EUSERS 87           /**< Too many users */
#define ENOTSOCK 88         /**< Socket operation on non-socket */
#define EDESTADDRREQ 89     /**< Destination address required */
#define EMSGSIZE 90         /**< Message too long */
#define EPROTOTYPE 91       /**< Protocol wrong type for socket */
#define ENOPROTOOPT 92      /**< Protocol not available */
#define EPROTONOSUPPORT 93  /**< Protocol not supported */
#define ESOCKTNOSUPPORT 94  /**< Socket type not supported */
#define EOPNOTSUPP 95       /**< Operation not supported on transport endpoint */
#define EPFNOSUPPORT 96     /**< Protocol family not supported */
#define EAFNOSUPPORT 97     /**< Address family not supported by protocol */
#define EADDRINUSE 98       /**< Address already in use */
#define EADDRNOTAVAIL 99    /**< Cannot assign requested address */
#define ENETDOWN 100        /**< Network is down */
#define ENETUNREACH 101     /**< Network is unreachable */
#define ENETRESET 102       /**< Network dropped connection on reset */
#define ECONNABORTED 103    /**< Software caused connection abort */
#define ECONNRESET 104      /**< Connection reset by peer */
#define ENOBUFS 105         /**< No buffer space available */
#define EISCONN 106         /**< Transport endpoint is already connected */
#define ENOTCONN 107        /**< Transport endpoint is not connected */
#define ESHUTDOWN 108       /**< Cannot send after transport endpoint shutdown */
#define ETOOMANYREFS 109    /**< Too many references: cannot splice */
#define ETIMEDOUT 110       /**< Connection timed out */
#define ECONNREFUSED 111    /**< Connection refused */
#define EHOSTDOWN 112       /**< Host is down */
#define EHOSTUNREACH 113    /**< No route to host */
#define EALREADY 114        /**< Operation already in progress */
#define EINPROGRESS 115     /**< Operation now in progress */
#define ESTALE 116          /**< Stale file handle */
#define EUCLEAN 117         /**< Structure needs cleaning */
#define ENOTNAM 118         /**< Not a XENIX named type file */
#define ENAVAIL 119         /**< No XENIX semaphores available */
#define EISNAM 120          /**< Is a named type file */
#define EREMOTEIO 121       /**< Remote I/O error */
#define EDQUOT 122          /**< Quota exceeded */
#define ENOMEDIUM 123       /**< No medium found */
#define EMEDIUMTYPE 124     /**< Wrong medium type */
#define ECANCELED 125       /**< Operation canceled */
#define ENOKEY 126          /**< Required key not available */
#define EKEYEXPIRED 127     /**< Key has expired */
#define EKEYREVOKED 128     /**< Key has been revoked */
#define EKEYREJECTED 129    /**< Key was rejected by service */
#define EOWNERDEAD 130      /**< Owner died */
#define ENOTRECOVERABLE 131 /**< State not recoverable */
#define ERFKILL 132         /**< Operation not possible due to RF-kill */
#define EHWPOISON 133       /**< Memory page has hardware error */

    /**
     * @brief 线程局部 errno 变量
     *
     * @details 每个线程都有自己的 errno 副本
     *          - 由系统调用和库函数设置
     *          - 初始值为 0（无错误）
     *          - 线程安全（无需加锁）
     *
     * @note 使用 __thread 存储类（线程局部存储）
     * @note 符合 POSIX 和 C11 标准
     *
     * @code
     * if (open("file.txt") < 0) {
     *     printf("Error: %s\n", strerror(errno));
     * }
     * @endcode
     */
    extern __thread int errno;

    /**
     * @brief 将错误码转换为可读字符串
     * @param errnum 错误码（通常是 errno 的值）
     * @return 指向错误描述字符串的指针
     *
     * @details 返回静态缓冲区，线程安全
     *          - 不可重入（不要在信号处理函数中使用）
     *          - 返回的字符串不应被修改
     *          - 下次调用会覆盖之前的字符串
     *
     * @note POSIX.1-2008 标准函数
     * @note 线程安全（每个线程独立的缓冲区）
     *
     * @code
     * if (semaphore_wait(sem) < 0) {
     *     printf("Error: %s\n", strerror(errno));
     * }
     * @endcode
     */
    char *strerror(int errnum);

    /**
     * @brief 打印错误消息到 stderr
     * @param s 错误消息前缀
     *
     * @details 输出格式： "s: error-description\n"
     *          - 如果 s 为 NULL 或空，只输出错误描述
     *          - 依赖 errno 的当前值
     *
     * @note POSIX.1-2008 标准函数
     *
     * @code
     * if (semaphore_wait(sem) < 0) {
     *     perror("semaphore_wait");
     *     // 输出: semaphore_wait: Resource temporarily unavailable
     * }
     * @endcode
     */
    void perror(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* ERRNO_H */
