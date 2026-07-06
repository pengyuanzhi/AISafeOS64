/**
 * @file    process.h
 * @brief   用户态进程管理 API
 * @author  AISafe64 Team
 * @date    2026-04-14
 * @version 1.0
 *
 * @details 提供 POSIX 风格的进程管理接口，通过 IPC 与 ProcessManager 服务通信。
 *          所有函数都是用户态可调用的，通过 SVC 系统调用与内核交互。
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-024, API-001~004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <kernel/types.h>
#include <kernel/errno.h>
#include <kernel/syscall.h>
#include <stdbool.h>
#include <stdint.h>

struct list_head;

/* ========================================================================
 * 进程描述符
 * ======================================================================== */

/**
 * @brief 进程描述符
 *
 * @details 进程 = 地址空间 + 线程组 + 资源限额。
 *          一个进程包含多个线程，共享 vmspace 和 cspace。
 */
typedef struct process
{
    uint32_t          pid;            /**< @brief 进程 ID */
    uint32_t          parent_pid;     /**< @brief 父进程 ID */
    void             *vmspace;        /**< @brief 地址空间（vm_space_t*） */
    void             *cspace;         /**< @brief 能力空间 */
    struct list_head *thread_list;    /**< @brief 线程组链表头指针 */
    uint32_t          thread_count;   /**< @brief 线程数 */
    int32_t           exit_status;    /**< @brief 退出状态 */
    bool              in_use;         /**< @brief 是否活跃 */
} process_t;

/* ========================================================================
 * 进程 ID 类型
 * ======================================================================== */

/**
 * @brief 进程 ID 类型
 */
typedef int32_t pid_t;

/**
 * @brief 无效进程 ID
 */
#define INVALID_PID  ((pid_t)(-1))

/* ========================================================================
 * 信号定义
 * ======================================================================== */

/**
 * @brief POSIX 信号编号
 */
#define SIGHUP      1U    /**< @brief 终端挂起 */
#define SIGINT      2U    /**< @brief 中断（Ctrl+C） */
#define SIGQUIT     3U    /**< @brief 退出（Ctrl+\\） */
#define SIGILL      4U    /**< @brief 非法指令 */
#define SIGTRAP     5U    /**< @brief 断点陷阱 */
#define SIGABRT     6U    /**< @brief 异常终止 */
#define SIGKILL     9U    /**< @brief 强制终止（不可捕获） */
#define SIGSEGV     11U   /**< @brief 段错误 */
#define SIGPIPE     13U   /**< @brief 管道破裂 */
#define SIGALRM     14U   /**< @brief 定时器超时 */
#define SIGTERM     15U   /**< @brief 终止 */
#define SIGCHLD     17U   /**< @brief 子进程状态变化 */
#define SIGSTOP     19U   /**< @brief 停止（不可捕获） */
#define SIGCONT     18U   /**< @brief 继续 */
#define SIGUSR1     10U   /**< @brief 用户定义信号1 */
#define SIGUSR2     12U   /**< @brief 用户定义信号2 */

/* ========================================================================
 * waitpid 选项
 * ======================================================================== */

/**
 * @brief WNOHANG - 非阻塞等待
 */
#define WNOHANG    1U

/* ========================================================================
 * 资源限制类型
 * ======================================================================== */

/**
 * @brief 资源限制类型枚举
 */
typedef enum
{
    RLIMIT_CPU = 0U,       /**< @brief CPU 时间限制（秒） */
    RLIMIT_FSIZE,          /**< @brief 文件大小限制（字节） */
    RLIMIT_DATA,           /**< @brief 数据段大小限制 */
    RLIMIT_STACK,          /**< @brief 栈大小限制 */
    RLIMIT_CORE,           /**< @brief 核心转储大小限制 */
    RLIMIT_RSS,            /**< @brief 驻留集大小限制 */
    RLIMIT_NOFILE,         /**< @brief 最大打开文件数 */
    RLIMIT_AS              /**< @brief 地址空间大小限制 */
} rlimit_resource_t;

/**
 * @brief 资源限制描述符
 */
typedef struct
{
    uint64_t    rlim_cur;   /**< @brief 软限制（当前值） */
    uint64_t    rlim_max;   /**< @brief 硬限制（最大值） */
} rlimit_t;

/* ========================================================================
 * 内部 SVC 调用辅助
 * ======================================================================== */

/**
 * @brief 执行 SVC 系统调用
 *
 * @param frame 系统调用帧
 */
extern void svc_call(syscall_frame_t *frame);

/* ========================================================================
 * 进程管理 API
 * ======================================================================== */

/**
 * @brief fork - 创建子进程
 *
 * @details 创建当前进程的完整副本：
 *          - 子进程继承父进程的内存映像（写时复制）
 *          - 子进程继承父进程的文件描述符
 *          - 子进程继承信号处理设置
 *          - 子进程继承资源限制
 *
 * @return 子进程 PID（父进程中），0（子进程中），负数表示错误
 *
 * @note MISRA-C:2012 Rule 8.2 - 不要重新定义标准函数
 */
int fork(void);

/**
 * @brief exec - 替换进程映像
 *
 * @details 用新的程序替换当前进程映像：
 *          - PID、父进程关系保持不变
 *          - 内存映像被新程序替换
 *          - 信号处理重置为默认
 *          - 文件描述符保持打开（除非设置 FD_CLOEXEC）
 *
 * @param name 进程名
 * @param argv 参数数组
 *
 * @return 0 成功，负数表示错误
 */
int exec(const char *name, const char **argv);

/**
 * @brief waitpid - 等待子进程状态变化
 *
 * @details 阻塞等待指定子进程状态变化：
 *          - 子进程退出
 *          - 子进程停止（SIGSTOP）
 *          - 子进程继续（SIGCONT）
 *
 * @param pid     子进程 PID（-1 表示任意子进程）
 * @param status 退出状态输出
 * @param options 等待选项（WNOHANG = 非阻塞）
 *
 * @return 子进程 PID（成功），0（WNOHANG 且无状态变化），负数表示错误
 */
pid_t waitpid(pid_t pid, int *status, int options);

/**
 * @brief exit - 退出当前进程
 *
 * @details 终止当前进程：
 *          - 关闭所有文件描述符
 *          - 向父进程发送 SIGCHLD
 *          - 子进程变为僵尸态，等待父进程回收
 *
 * @param status 退出状态
 *
 * @note 此函数不会返回
 */
void exit(int status);

/**
 * @brief kill - 发送信号到目标进程
 *
 * @details 向指定进程发送信号：
 *          - SIGKILL/SIGSTOP 不可被捕获或忽略
 *          - 其他信号可以被捕获或忽略
 *          - 信号加入目标进程的挂起队列
 *
 * @param pid 目标进程 PID
 * @param sig 信号编号（1-31）
 *
 * @return 0 成功，负数表示错误
 */
int kill(pid_t pid, int sig);

/**
 * @brief setrlimit - 设置资源限制
 *
 * @details 设置进程的资源限制：
 *          - 软限制不能超过硬限制
 *          - 只有 root 可以提高硬限制
 *          - RLIM_INFINITY 表示无限制
 *
 * @param resource 资源类型
 * @param rlim     资源限制
 *
 * @return 0 成功，负数表示错误
 */
int setrlimit(int resource, const rlimit_t *rlim);

/**
 * @brief getrlimit - 获取资源限制
 *
 * @details 获取进程的资源限制
 *
 * @param resource 资源类型
 * @param rlim     资源限制输出
 *
 * @return 0 成功，负数表示错误
 */
int getrlimit(int resource, rlimit_t *rlim);

/* ========================================================================
 * 内部 IPC 消息类型
 * ======================================================================== */

/** @brief ProcessManager 服务协议号 */
#define SERVICE_PROC_MANAGER  0x0001U

/** @brief 创建进程 */
#define PROC_MSG_CREATE        0x0010U

/** @brief 销毁进程 */
#define PROC_MSG_DESTROY       0x0011U

/** @brief 获取进程信息 */
#define PROC_MSG_GET_INFO      0x0012U

/** @brief fork 操作 */
#define PROC_MSG_FORK          0x0014U

/** @brief exec 操作 */
#define PROC_MSG_EXEC          0x0015U

/** @brief waitpid 操作 */
#define PROC_MSG_WAITPID       0x0016U

/** @brief exit 操作 */
#define PROC_MSG_EXIT          0x0017U

/** @brief 发送信号 */
#define PROC_MSG_SIGNAL        0x0018U

/** @brief 设置资源限制 */
#define PROC_MSG_RLIMIT_SET    0x0019U

/** @brief 获取资源限制 */
#define PROC_MSG_RLIMIT_GET    0x001AU

/** @brief 设置进程状态 */
#define PROC_MSG_STATE_SET     0x001BU

/* ========================================================================
 * 内部 ProcessManager 通信函数
 * ======================================================================== */

/**
 * @brief 与 ProcessManager 通信
 *
 * @param msg_type 消息类型
 * @param data     内联数据（最多4个 uint64_t）
 *
 * @return 处理结果
 */

#endif /* KERNEL_PROCESS_H */
