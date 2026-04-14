/**
 * @file    process_impl.c
 * @brief   用户态进程管理 API 实现
 * @author  AISafe64 Team
 * @date    2026-04-14
 * @version 3.0
 *
 * @details 实现 POSIX 风格的进程管理接口：
 *          - fork/exec/waitpid/exit
 *          - kill 信号发送
 *          - setrlimit/getrlimit 资源限制
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-024, API-001~004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/process.h>
#include <kernel/syscall.h>
#include <kernel/errno.h>
#include <kernel/types.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 全局状态
 * ======================================================================== */

uint64_t s_proc_endpoint_id = 0ULL;
pid_t s_current_pid = 1;
pid_t s_parent_pid = 0;

/* ========================================================================
 * IPC 通信函数
 * ======================================================================== */

/**
 * @brief 进程管理 IPC 通信
 */
static int32_t proc_communicate(uint32_t msg_type, uint64_t *data)
{
    int32_t ret;
    uint64_t ipc_data[4U];

    if (data == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 如果还没有端点，创建一个 */
    if (s_proc_endpoint_id == 0ULL)
    {
        s_proc_endpoint_id = (uint64_t)syscall2(SYS_EP_CREATE, 0ULL, 0ULL);
        if ((int64_t)s_proc_endpoint_id < 0)
        {
            return -(int32_t)ENOMEM;
        }
    }

    /* 构造 IPC 消息 */
    ipc_data[0] = (uint64_t)msg_type;
    ipc_data[1] = data[0];
    ipc_data[2] = data[1];
    ipc_data[3] = data[2];

    /* 发送请求到 ProcessManager */
    ret = syscall3(SYS_MSG_SEND, s_proc_endpoint_id,
                   ipc_data[0], ipc_data[1]);

    if (ret < 0)
    {
        return ret;
    }

    /* 等待 ProcessManager 回复 */
    ret = syscall3(SYS_MSG_RECV, s_proc_endpoint_id,
                   (uint64_t)(uintptr_t)&ipc_data, sizeof(ipc_data));

    if (ret >= 0)
    {
        /* 返回结果 */
        ret = (int32_t)((int64_t)ipc_data[0]);
    }

    return ret;
}

/* ========================================================================
 * 进程管理 API 实现
 * ======================================================================== */

/**
 * @brief fork - 创建子进程
 */
int fork(void)
{
    int32_t ret;
    uint64_t data[4U];
    pid_t child_pid;

    /* 获取当前 PID */
    s_current_pid = (pid_t)syscall0(SYS_THREAD_GET_ID);

    /* 调用 ProcessManager fork */
    data[0] = (uint64_t)s_current_pid;
    data[1] = (uint64_t)&child_pid;
    data[2] = 0ULL;
    data[3] = 0ULL;

    ret = proc_communicate(PROC_MSG_FORK, data);

    if (ret < 0)
    {
        return ret;
    }

    /* 在子进程中返回 0 */
    if (child_pid == 0)
    {
        return 0;
    }

    /* 在父进程中返回子进程 PID */
    return child_pid;
}

/**
 * @brief exec - 替换进程映像
 */
int exec(const char *name, const char **argv)
{
    int32_t ret;
    uint64_t data[4U];

    if (name == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 调用 ProcessManager exec */
    data[0] = (uint64_t)s_current_pid;
    data[1] = (uint64_t)name;
    data[2] = (argv != NULL) ? (uint64_t)argv : 0ULL;
    data[3] = 0ULL;

    ret = proc_communicate(PROC_MSG_EXEC, data);

    return ret;
}

/**
 * @brief waitpid - 等待子进程状态变化
 */
pid_t waitpid(pid_t pid, int *status, int options)
{
    int32_t ret;
    uint64_t data[4U];
    int32_t child_status;

    if ((pid <= 0) && (pid != -1))
    {
        return -(int32_t)EINVAL;
    }

    data[0] = (uint64_t)s_current_pid;
    data[1] = (uint64_t)pid;
    data[2] = (uint64_t)&child_status;
    data[3] = (uint64_t)options;

    ret = proc_communicate(PROC_MSG_WAITPID, data);

    if (ret < 0)
    {
        return (pid_t)ret;
    }

    if (ret == 0)
    {
        /* WNOHANG 且无状态变化 */
        return 0;
    }

    /* 返回子进程 PID 和状态 */
    if (status != NULL)
    {
        *status = child_status;
    }

    return (pid_t)ret;
}

/**
 * @brief exit - 退出当前进程
 */
void exit(int status)
{
    int32_t ret;
    uint64_t data[4U];

    /* 调用 ProcessManager exit */
    data[0] = (uint64_t)s_current_pid;
    data[1] = (uint64_t)status;
    data[2] = 0ULL;
    data[3] = 0ULL;

    ret = proc_communicate(PROC_MSG_EXIT, data);

    /* 退出当前线程 */
    syscall0(SYS_THREAD_EXIT);

    /* 不应该到达这里 */
    for (;;)
    {
        syscall0(SYS_THREAD_YIELD);
    }
}

/**
 * @brief kill - 发送信号到目标进程
 */
int kill(pid_t pid, int sig)
{
    int32_t ret;
    uint64_t data[4U];

    if (pid <= 0)
    {
        return -(int32_t)EINVAL;
    }

    if ((sig <= 0) || (sig > 31))
    {
        return -(int32_t)EINVAL;
    }

    data[0] = (uint64_t)pid;
    data[1] = (uint64_t)sig;
    data[2] = 0ULL;
    data[3] = 0ULL;

    ret = proc_communicate(PROC_MSG_SIGNAL, data);

    return ret;
}

/**
 * @brief setrlimit - 设置资源限制
 */
int setrlimit(int resource, const rlimit_t *rlim)
{
    int32_t ret;
    uint64_t data[4U];

    if ((resource < 0) || (resource >= RLIMIT_AS))
    {
        return -(int32_t)EINVAL;
    }

    if (rlim == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (rlim->rlim_cur > rlim->rlim_max)
    {
        return -(int32_t)EINVAL;
    }

    data[0] = (uint64_t)s_current_pid;
    data[1] = (uint64_t)resource;
    data[2] = rlim->rlim_cur;
    data[3] = rlim->rlim_max;

    ret = proc_communicate(PROC_MSG_RLIMIT_SET, data);

    return ret;
}

/**
 * @brief getrlimit - 获取资源限制
 */
int getrlimit(int resource, rlimit_t *rlim)
{
    int32_t ret;
    uint64_t data[4U];

    if ((resource < 0) || (resource >= RLIMIT_AS))
    {
        return -(int32_t)EINVAL;
    }

    if (rlim == NULL)
    {
        return -(int32_t)EINVAL;
    }

    data[0] = (uint64_t)s_current_pid;
    data[1] = (uint64_t)resource;
    data[2] = (uint64_t)rlim;
    data[3] = 0ULL;

    ret = proc_communicate(PROC_MSG_RLIMIT_GET, data);

    return ret;
}
