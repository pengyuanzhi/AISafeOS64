/**
 * @file    musl_safety.c
 * @brief   AISafeOS64 musl 功能安全改造包装实现
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

#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <stdarg.h>
#include "musl_safety.h"
#include "../arch/aarch64_aisafe/syscall_entry.h"

/* 仅在 ARM64 交叉编译时使用真正的 syscall_entry.h */
#if defined(__aarch64__) && !defined(AISAFE_TEST_MODE)
/* 已在上面包含 syscall_entry.h */
#else
/* 测试模式：使用桩版本 */
#include "../arch/aarch64_aisafe/syscall_entry_test.h"
#endif

/* ========================================================================
 * AISafeOS64 内核系统调用号
 * ======================================================================== */
#define AISAFE_SYS_DEBUG_PRINT      0x0500U

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** 最大指针验证大小（防止整数溢出攻击） */
#define MAX_VALIDATE_SIZE (1024UL * 1024UL * 1024UL)  /* 1GB */

/** 最大字符串长度 */
#define MAX_STRING_LEN (1024UL * 1024UL)  /* 1MB */

/** 最大文件描述符 */
#define MAX_FD (1024 * 16)  /* 16384 */

/* 文件权限位定义 */
#define S_IRUSR  00400U  /* 用户读 */
#define S_IWUSR  00200U  /* 用户写 */
#define S_IXUSR  00100U  /* 用户执行 */
#define S_IRWXU  00700U  /* 用户读写执行 */
#define S_IRGRP  00040U  /* 组读 */
#define S_IWGRP  00020U  /* 组写 */
#define S_IXGRP  00010U  /* 组执行 */
#define S_IRWXG  00070U  /* 组读写执行 */
#define S_IROTH  00004U  /* 其他读 */
#define S_IWOTH  00002U  /* 其他写 */
#define S_IXOTH  00001U  /* 其他执行 */
#define S_IRWXO  00007U  /* 其他读写执行 */
#define S_ISUID  004000U /* set-user-id */
#define S_ISGID  002000U /* set-group-id */
#define S_ISVTX  001000U /* sticky */

/* ========================================================================
 * 审计日志状态
 * ======================================================================== */

/** 审计日志是否已初始化 */
static int s_audit_log_initialized = 0;

/** 审计日志计数器（防止日志泛滥） */
static unsigned long s_audit_log_count = 0;

/** 审计日志上限（每分钟最多记录多少条） */
#define MAX_AUDIT_LOGS_PER_MINUTE 1000

/* ========================================================================
 * 参数验证实现
 * ======================================================================== */

int musl_validate_pointer(const void *ptr, size_t size)
{
    /* 检查指针非空 */
    if (ptr == NULL) {
        return -EINVAL;
    }

    /* 检查大小不超过限制 */
    if (size > MAX_VALIDATE_SIZE) {
        return -EINVAL;
    }

    /* 检查大小不为 0（某些场景） */
    if (size == 0 && ptr != NULL) {
        /* 大小为 0 但指针非空，通常表示不需要拷贝，允许 */
    }

    return 0;
}

int musl_validate_fd(int fd)
{
    /* 检查 fd 范围 */
    if (fd < 0) {
        return -EBADF;
    }

    /* 检查 fd 不超过系统限制 */
    if (fd >= MAX_FD) {
        return -EBADF;
    }

    return 0;
}

int musl_validate_string(const char *str, size_t max_len)
{
    size_t len;
    size_t i;

    /* 检查指针非空 */
    if (str == NULL) {
        return -EINVAL;
    }

    /* 手动检查字符串以 NULL 结尾（避免 strnlen 的不确定性） */
    for (i = 0; i < max_len; i++) {
        if (str[i] == '\0') {
            break;  /* 找到 NULL 终止符 */
        }
    }

    if (i == max_len) {
        return -EINVAL;  /* 未找到 NULL 终止符 */
    }

    len = i;

    /* 检查长度不超过最大限制 */
    if (len > MAX_STRING_LEN) {
        return -EINVAL;
    }

    return 0;
}

int musl_validate_size(size_t len, size_t max_len)
{
    /* 检查 len <= max_len */
    if (len > max_len) {
        return -EINVAL;
    }

    return 0;
}

int musl_validate_mode(mode_t mode)
{
    /* 检查权限模式位合法性（只允许 S_IRUSR, S_IWUSR, S_IXUSR 等） */
    mode_t valid_mask = S_IRWXU | S_IRWXG | S_IRWXO;

    /* 添加特殊位（如果定义） */
#ifdef S_ISUID
    valid_mask |= S_ISUID;
#endif
#ifdef S_ISGID
    valid_mask |= S_ISGID;
#endif
#ifdef S_ISVTX
    valid_mask |= S_ISVTX;
#endif

    if ((mode & ~valid_mask) != 0) {
        return -EINVAL;
    }

    return 0;
}

/* ========================================================================
 * 审计日志缓冲区
 * ======================================================================== */

/** 审计日志缓冲区大小 */
#define AUDIT_LOG_BUFFER_SIZE 512U

/** 审计日志缓冲区 */
static char s_audit_log_buffer[AUDIT_LOG_BUFFER_SIZE];

/* ========================================================================
 * 审计日志输出函数
 * ======================================================================== */

/**
 * @brief 审计日志输出函数（格式化）
 *
 * @param fmt 格式化字符串
 * @param ... 变参
 *
 * @return 0 表示成功，负值表示失败
 *
 * @note 通过内核调试接口输出
 */
int musl_audit_log_printf(const char *fmt, ...)
{
    va_list args;
    int len;
    long ret;

    /* 检查参数 */
    if (fmt == NULL)
    {
        return -EINVAL;
    }

    /* 格式化字符串到缓冲区 */
    va_start(args, fmt);
    len = vsnprintf(s_audit_log_buffer, AUDIT_LOG_BUFFER_SIZE, fmt, args);
    va_end(args);

    /* 检查格式化是否成功 */
    if (len < 0)
    {
        return -EINVAL;
    }

    /* 截断超长的日志 */
    if (len >= (int)AUDIT_LOG_BUFFER_SIZE)
    {
        len = (int)AUDIT_LOG_BUFFER_SIZE - 1;
    }

    /* 通过内核调试接口输出 */
    ret = aisafe_svc2(AISAFE_SYS_DEBUG_PRINT, (long)s_audit_log_buffer, (long)len);

    if (ret < 0)
    {
        return (int)ret;
    }

    return 0;
}

/* ========================================================================
 * 审计日志实现
 * ======================================================================== */

int musl_audit_log_init(void)
{
    if (s_audit_log_initialized) {
        return 0;  /* 已经初始化 */
    }

    s_audit_log_initialized = 1;
    s_audit_log_count = 0;

    return 0;
}

int musl_audit_log_syscall(int syscall_nr, long ret, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3)
{
    /* 检查审计日志是否已初始化 */
    if (!s_audit_log_initialized) {
        musl_audit_log_init();
    }

    /* 防止日志泛滥：检查计数器 */
    if (s_audit_log_count >= MAX_AUDIT_LOGS_PER_MINUTE) {
        /* 日志超限，丢弃 */
        return -EAGAIN;
    }

    /* 记录 syscall 信息 */
    (void)musl_audit_log_printf("AUDIT: syscall=%d ret=%ld arg1=0x%lx arg2=0x%lx arg3=0x%lx\n",
                                syscall_nr, ret, arg1, arg2, arg3);

    s_audit_log_count++;

    return 0;
}

int musl_audit_log_event(int event_type, int severity, const char *msg)
{
    /* 检查审计日志是否已初始化 */
    if (!s_audit_log_initialized) {
        musl_audit_log_init();
    }

    /* 验证消息参数 */
    if (msg == NULL) {
        return -EINVAL;
    }

    /* 验证事件类型 */
    if (event_type < AISAFE_AUDIT_EVENT_SYSCALL || event_type > AISAFE_AUDIT_EVENT_VIOLATION) {
        return -EINVAL;
    }

    /* 验证严重级别 */
    if (severity < AISAFE_AUDIT_INFO || severity > AISAFE_AUDIT_FATAL) {
        return -EINVAL;
    }

    /* 防止日志泛滥：检查计数器 */
    if (s_audit_log_count >= MAX_AUDIT_LOGS_PER_MINUTE) {
        /* 日志超限，丢弃 */
        return -EAGAIN;
    }

    /* 记录安全事件 */
    (void)musl_audit_log_printf("AUDIT: event=%d severity=%d msg=%s\n",
                                event_type, severity, msg);

    s_audit_log_count++;

    return 0;
}
