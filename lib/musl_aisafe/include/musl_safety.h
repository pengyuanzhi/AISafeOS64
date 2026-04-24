/**
 * @file    musl_safety.h
 * @brief   AISafeOS64 musl 功能安全包装接口
 * @version 2.0
 *
 * @details 对标准 musl 的功能安全改造：
 *          - 参数验证：每个 syscall 路径添加参数边界检查
 *          - 确定性：替换不确定行为
 *          - 错误路径覆盖：补齐 musl 未覆盖的边界条件
 *          - 审计日志：关键 syscall 路径添加安全审计点
 *          - MISRA 包装：对 musl 公共 API 提供符合 MISRA 的薄包装
 *
 * @note MISRA-C:2012 合规
 */

#ifndef AISAFE_MUSL_SAFETY_H
#define AISAFE_MUSL_SAFETY_H

#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>

/* ========================================================================
 * 权限位定义（sys/stat.h 的子集）
 * ======================================================================== */
/* 注意：这些宏由 musl_upstream 提供，无需重复定义 */

/* ========================================================================
 * 类型定义
 * ======================================================================== */

#ifndef mode_t
typedef unsigned int mode_t;
#endif

/* ========================================================================
 * 参数验证接口
 * ======================================================================== */

/**
 * @brief 验证指针参数合法性
 * @param ptr 指针参数
 * @param size 大小参数（字节）
 * @return 0 表示合法，-EINVAL 表示非法
 * @note 检查指针非空、对齐、大小不超过限制
 */
int musl_validate_pointer(const void *ptr, size_t size);

/**
 * @brief 验证文件描述符合法性
 * @param fd 文件描述符
 * @return 0 表示合法，-EBADF 表示非法
 * @note 检查 fd >= 0 且在系统允许范围内
 */
int musl_validate_fd(int fd);

/**
 * @brief 验证字符串参数合法性
 * @param str 字符串指针
 * @param max_len 最大长度限制
 * @return 0 表示合法，-EINVAL 表示非法
 * @note 检查指针非空、字符串以 NULL 结尾、长度不超过限制
 */
int musl_validate_string(const char *str, size_t max_len);

/**
 * @brief 验证长度参数合法性
 * @param len 长度参数
 * @param max_len 最大允许值
 * @return 0 表示合法，-EINVAL 表示非法
 * @note 检查 len <= max_len
 */
int musl_validate_size(size_t len, size_t max_len);

/**
 * @brief 验证权限参数合法性
 * @param mode 权限模式
 * @return 0 表示合法，-EINVAL 表示非法
 * @note 检查权限模式位合法性
 */
int musl_validate_mode(mode_t mode);

/* ========================================================================
 * 审计日志接口
 * ======================================================================== */

/**
 * @brief 审计日志输出函数（格式化）
 * @param fmt 格式化字符串
 * @param ... 变参
 * @return 0 表示成功，负值表示失败
 * @note 通过内核调试接口输出
 */
int musl_audit_log_printf(const char *fmt, ...);

/**
 * @brief 初始化审计日志系统
 * @return 0 表示成功，负值表示失败
 */
int musl_audit_log_init(void);

/**
 * @brief 记录系统调用审计日志
 * @param syscall_nr 系统调用号
 * @param ret 返回值
 * @param arg1 参数 1（通常是 fd 或标志）
 * @param arg2 参数 2（通常是指针或长度）
 * @param arg3 参数 3（通常是模式或长度）
 * @return 0 表示成功，负值表示失败
 */
int musl_audit_log_syscall(int syscall_nr, long ret, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3);

/**
 * @brief 记录安全事件审计日志
 * @param event_type 事件类型（如 ACCESS_VIOLATION, BUFFER_OVERFLOW）
 * @param severity 严重级别（0=INFO, 1=WARN, 2=ERROR, 3=FATAL）
 * @param msg 事件描述
 * @return 0 表示成功，负值表示失败
 */
int musl_audit_log_event(int event_type, int severity, const char *msg);

/* ========================================================================
 * 审计事件类型定义
 * ======================================================================== */

#define AISAFE_AUDIT_EVENT_SYSCALL    0  /* 系统调用审计 */
#define AISAFE_AUDIT_EVENT_PARAM      1  /* 参数验证失败 */
#define AISAFE_AUDIT_EVENT_OVERFLOW   2  /* 缓冲区溢出 */
#define AISAFE_AUDIT_EVENT_VIOLATION  3  /* 访问违规 */

/* ========================================================================
 * 安全级别定义
 * ======================================================================== */

#define AISAFE_AUDIT_INFO    0  /* 信息 */
#define AISAFE_AUDIT_WARN    1  /* 警告 */
#define AISAFE_AUDIT_ERROR   2  /* 错误 */
#define AISAFE_AUDIT_FATAL   3  /* 致命 */

/* ========================================================================
 * 系统调用号定义（用于审计日志）
 * ======================================================================== */

#define SYS_OPEN    2
#define SYS_WRITE   4
#define SYS_READ    3
#define SYS_CLOSE   6
#define SYS_MMAP    9
#define SYS_MUNMAP  11
#define SYS_MPROTECT 10
#define SYS_CLONE   56
#define SYS_EXECVE  59
#define SYS_EXIT    60
#define SYS_CHMOD   90
#define SYS_CHOWN   92

#endif /* AISAFE_MUSL_SAFETY_H */
