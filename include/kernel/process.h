/**
 * @file    process.h
 * @brief   进程管理子系统接口
 * @author  AISafe64 Team
 * @date    2026-07-05
 * @version 2.0
 *
 * @details 进程 = 地址空间 + 线程组 + 能力空间。
 *          提供进程创建/复制/替换/等待的完整生命周期管理。
 *
 *          POSIX 兼容接口：
 *          - process_fork: 复制当前进程（写时复制地址空间）
 *          - process_exec: 替换当前进程映像（ELF 加载）
 *          - process_wait: 等待子进程退出
 *          - process_clone: 创建新线程/进程（POSIX clone 语义）
 *
 * @note MISRA-C:2012 合规
 *
 * @revision history
 * v1.0 2026-07-05 初始版本（create/exit/wait/getpid）
 * v2.0 2026-07-07 fork/exec/clone/wait4 完整进程管理（当前版本）
 */

#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <stdbool.h>
#include <stdint.h>

struct list_head;

/**
 * @brief 进程 ID 类型（POSIX 兼容）
 */
typedef int32_t pid_t;

/**
 * @brief 无效进程 ID
 */
#define INVALID_PID  ((pid_t)(-1))

/**
 * @brief 进程描述符
 */
typedef struct process
{
    uint32_t          pid;            /**< @brief 进程 ID */
    uint32_t          parent_pid;     /**< @brief 父进程 ID */
    void             *vmspace;        /**< @brief 地址空间（vm_space_t*） */
    void             *cspace;         /**< @brief 能力空间 */
    struct list_head *thread_list;    /**< @brief 线程组链表头指针 */
    uint32_t          thread_count;   /**< @brief 线程数 */
    volatile int32_t  exit_status;    /**< @brief 退出状态 */
    volatile bool     in_use;         /**< @brief 是否活跃 */
    volatile bool     exited;         /**< @brief 是否已退出（供 wait 检查） */
} process_t;

/* ========================================================================
 * POSIX 信号编号（兼容旧代码引用）
 * ======================================================================== */

#define SIGHUP      1U
#define SIGINT      2U
#define SIGQUIT     3U
#define SIGILL      4U
#define SIGTRAP     5U
#define SIGABRT     6U
#define SIGKILL     9U
#define SIGSEGV     11U
#define SIGPIPE     13U
#define SIGALRM     14U
#define SIGTERM     15U
#define SIGCHLD     17U
#define SIGSTOP     23U
#define SIGCONT     25U
#define SIG_MAX     32U

/* ========================================================================
 * 子系统初始化
 * ======================================================================== */

/**
 * @brief 初始化进程子系统
 */
kernel_status_t process_subsys_init(void);

/* ========================================================================
 * 基础进程操作
 * ======================================================================== */

/**
 * @brief 创建新进程（空地址空间）
 *
 * @param parent_pid 父进程 PID（0=内核）
 * @param out_pid 输出进程 ID
 * @return KERNEL_OK 成功
 */
kernel_status_t process_create(uint32_t parent_pid, uint32_t *out_pid);

/**
 * @brief 进程退出
 *
 * @details 终止进程内所有线程，释放地址空间。
 *          设置 exited=true 和 exit_status 供父进程 wait。
 *
 * @param pid 进程 ID
 * @param status 退出状态码
 * @return KERNEL_OK 成功
 */
kernel_status_t process_exit(uint32_t pid, int32_t status);

/**
 * @brief 获取当前进程 ID
 * @return 当前线程所属进程的 PID
 */
uint32_t process_getpid(void);

/* ========================================================================
 * POSIX 进程管理接口
 * ======================================================================== */

/**
 * @brief fork 当前进程
 *
 * @details 复制当前进程的地址空间（写时复制），
 *          创建子进程，子进程从 fork 调用点继续执行。
 *
 * @param out_child_pid 输出子进程 PID（父进程中返回值）
 * @return KERNEL_OK 成功
 * @return -ENOMEM 内存不足
 *
 * @note 子进程中 getpid() 返回子 PID
 */
kernel_status_t process_fork(uint32_t *out_child_pid);

/**
 * @brief execve 替换当前进程映像
 *
 * @details 丢弃当前地址空间，加载新 ELF 到新地址空间。
 *          当前线程的用户态上下文被替换。
 *
 * @param elf_data ELF 文件数据
 * @param elf_size ELF 文件大小
 * @param thread_name 线程名称
 * @return 不返回（成功时 eret 到新 ELF 入口）
 * @return -EINVAL ELF 格式错误
 */
kernel_status_t process_exec(const uint8_t *elf_data, uint32_t elf_size,
                              const char *thread_name);

/**
 * @brief clone 创建新线程或进程
 *
 * @details POSIX clone 语义：
 *          - CLONE_VM（0x100）：共享地址空间（创建线程）
 *          - 无 CLONE_VM：复制地址空间（创建进程）
 *
 * @param flags clone 标志
 * @param stack 新线程栈顶（0=使用父栈）
 * @param out_tid 输出新线程/进程 ID
 * @return KERNEL_OK 成功
 */
kernel_status_t process_clone(uint64_t flags, uint64_t stack,
                               uint32_t *out_tid);

/**
 * @brief wait4 等待子进程退出
 *
 * @param pid 要等待的子进程 PID（-1=任意子进程）
 * @param out_status 输出退出状态
 * @param options 选项（WNOHANG=1 时不阻塞）
 * @return KERNEL_OK 成功（有子进程退出）
 * @return -EAGAIN 无子进程退出（WNOHANG）
 * @return -ECHILD 无子进程
 */
kernel_status_t process_wait4(int32_t pid, int32_t *out_status, uint32_t options);

#endif /* KERNEL_PROCESS_H */
