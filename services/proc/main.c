/**
 * @file    main.c
 * @brief   ProcessManager 进程管理器服务
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 3.0
 *
 * @details 用户态进程管理器：完整进程生命周期管理
 *          - fork/exec 语义的进程创建
 *          - 进程状态机（ready/running/blocked/zombie）
 *          - 信号处理框架
 *          - 资源限制（rlimit）
 *          - waitpid/exit 机制
 *          - 通过 IPC 消息与内核交互
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-024, API-001~004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/service.h>
#include <kernel/config.h>
#include <kernel/errno.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大信号编号 */
#define PROC_SIG_MAX                31U

/** @brief 最大挂起信号数 */
#define PROC_SIG_PENDING_MAX        64U

/** @brief 最大资源限制类型数 */
#define PROC_RLIMIT_MAX             8U

/** @brief 进程表扩展消息类型 */
#define PROC_MSG_FORK               0x0014U
#define PROC_MSG_EXEC               0x0015U
#define PROC_MSG_WAITPID            0x0016U
#define PROC_MSG_EXIT               0x0017U
#define PROC_MSG_SIGNAL             0x0018U
#define PROC_MSG_RLIMIT_SET         0x0019U
#define PROC_MSG_RLIMIT_GET         0x001AU
#define PROC_MSG_STATE_SET          0x001BU

/* ========================================================================
 * 信号定义
 * ======================================================================== */

/** @brief POSIX 信号编号 */
#define SIGHUP      1U    /**< @brief 终端挂起 */
#define SIGINT      2U    /**< @brief 中断（Ctrl+C） */
#define SIGQUIT     3U    /**< @brief 退出（Ctrl+\） */
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

/* ========================================================================
 * 数据结构
 * ======================================================================== */

/**
 * @brief 资源限制描述符
 */
typedef struct
{
    uint64_t    cur;              /**< @brief 软限制（当前值） */
    uint64_t    max;              /**< @brief 硬限制（最大值） */
} rlimit_t;

/**
 * @brief 信号处理动作类型
 */
typedef enum
{
    SIG_ACT_DEFAULT = 0U,  /**< @brief 默认处理 */
    SIG_ACT_IGNORE,        /**< @brief 忽略信号 */
    SIG_ACT_HANDLER        /**< @brief 自定义处理函数 */
} sig_action_t;

/**
 * @brief 信号处理描述符
 */
typedef struct
{
    sig_action_t    action;     /**< @brief 处理动作 */
    uint64_t        flags;      /**< @brief 信号处理标志 */
} sig_handler_t;

/* ========================================================================
 * 扩展进程描述符（含信号、rlimit）
 * ======================================================================== */

/**
 * @brief 扩展进程描述符
 *
 * @details 在基础 process_desc_t 上扩展信号处理和资源限制
 */
typedef struct
{
    /* 基础信息 */
    process_desc_t  base;           /**< @brief 基础进程描述符 */
    uint32_t        exit_code;      /**< @brief 退出码 */
    tick_t          start_time;     /**< @brief 启动时间 */
    tick_t          end_time;       /**< @brief 结束时间 */

    /* 信号处理 */
    uint32_t        sig_pending[2U]; /**< @brief 挂起信号位图（64位） */
    uint32_t        sig_blocked[2U]; /**< @brief 阻塞信号位图 */
    sig_handler_t   sig_handlers[PROC_SIG_MAX]; /**< @brief 信号处理表 */

    /* 资源限制 */
    rlimit_t        rlimits[PROC_RLIMIT_MAX]; /**< @brief 资源限制表 */

    /* 进程关系 */
    uint32_t        child_count;    /**< @brief 子进程计数 */
    uint32_t        pgrp;           /**< @brief 进程组 ID */
    uint32_t        session;        /**< @brief 会话 ID */
} proc_entry_t;

/* ========================================================================
 * 全局状态
 * ======================================================================== */

static proc_entry_t s_procs[MAX_PROCESSES];
static uint32_t s_next_pid;
static uint32_t s_active_count;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 根据进程 ID 查找进程条目索引
 *
 * @param pid 进程 ID
 *
 * @return 进程表索引，MAX_PROCESSES 表示未找到
 */
static uint32_t proc_find_index(uint32_t pid)
{
    uint32_t i;

    for (i = 0U; i < MAX_PROCESSES; i++)
    {
        if ((s_procs[i].base.pid == pid) &&
            (s_procs[i].base.state != PROC_STATE_EMPTY))
        {
            return i;
        }
    }

    return MAX_PROCESSES;
}

/**
 * @brief 检查进程是否为指定进程的子进程
 *
 * @param pid      待检查进程 ID
 * @param parent   父进程 ID
 *
 * @return true 是子进程，false 不是
 */
static bool proc_is_child(uint32_t pid, uint32_t parent)
{
    uint32_t idx = proc_find_index(pid);

    if (idx < MAX_PROCESSES)
    {
        return (s_procs[idx].base.parent_pid == parent);
    }

    return false;
}

/* ========================================================================
 * 初始化
 * ======================================================================== */

/**
 * @brief 初始化进程管理器
 */
static void proc_init(void)
{
    uint32_t i;
    uint32_t j;

    (void)memset(s_procs, 0, sizeof(s_procs));

    for (i = 0U; i < MAX_PROCESSES; i++)
    {
        s_procs[i].base.pid = 0U;
        s_procs[i].base.state = PROC_STATE_EMPTY;
        s_procs[i].exit_code = 0U;
        s_procs[i].child_count = 0U;
        s_procs[i].pgrp = 0U;
        s_procs[i].session = 0U;

        /* 初始化信号处理为默认动作 */
        for (j = 0U; j < PROC_SIG_MAX; j++)
        {
            s_procs[i].sig_handlers[j].action = SIG_ACT_DEFAULT;
            s_procs[i].sig_handlers[j].flags = 0U;
        }

        /* 初始化挂起/阻塞信号位图为空 */
        s_procs[i].sig_pending[0U] = 0U;
        s_procs[i].sig_pending[1U] = 0U;
        s_procs[i].sig_blocked[0U] = 0U;
        s_procs[i].sig_blocked[1U] = 0U;

        /* 初始化资源限制为默认值 */
        for (j = 0U; j < PROC_RLIMIT_MAX; j++)
        {
            s_procs[i].rlimits[j].cur = 0xFFFFFFFFFFFFFFFFULL;
            s_procs[i].rlimits[j].max = 0xFFFFFFFFFFFFFFFFULL;
        }

        /* 文件描述符限制 */
        s_procs[i].rlimits[RLIMIT_NOFILE].cur = 1024U;
        s_procs[i].rlimits[RLIMIT_NOFILE].max = 4096U;

        /* 栈大小限制 */
        s_procs[i].rlimits[RLIMIT_STACK].cur = (uint64_t)CONFIG_STACK_SIZE_DEFAULT;
        s_procs[i].rlimits[RLIMIT_STACK].max = (uint64_t)CONFIG_STACK_SIZE_MAX;
    }

    s_next_pid = 1U;
    s_active_count = 0U;
}

/* ========================================================================
 * 进程创建（fork 语义）
 * ======================================================================== */

/**
 * @brief fork 操作 - 创建父进程的完整副本
 *
 * @param parent_pid 父进程 ID
 * @param[out] child_pid 子进程 ID 输出
 *
 * @return 0 成功，负数表示错误
 *
 * @note 子进程继承父进程的信号处理和资源限制
 */
static int32_t proc_fork(uint32_t parent_pid, uint32_t *child_pid)
{
    uint32_t parent_idx;
    uint32_t child_idx;
    uint32_t j;

    if (child_pid == NULL)
    {
        return -(int32_t)EINVAL;
    }

    parent_idx = proc_find_index(parent_pid);
    if (parent_idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    if (s_active_count >= MAX_PROCESSES)
    {
        return -(int32_t)ENOMEM;
    }

    /* 查找空闲槽位 */
    child_idx = MAX_PROCESSES;
    for (j = 0U; j < MAX_PROCESSES; j++)
    {
        if (s_procs[j].base.state == PROC_STATE_EMPTY)
        {
            child_idx = j;
            break;
        }
    }

    if (child_idx >= MAX_PROCESSES)
    {
        return -(int32_t)ENOMEM;
    }

    /* 复制父进程信息 */
    s_procs[child_idx] = s_procs[parent_idx];

    /* 设置子进程特有字段 */
    s_procs[child_idx].base.pid = s_next_pid++;
    s_procs[child_idx].base.parent_pid = parent_pid;
    s_procs[child_idx].base.state = PROC_STATE_RUNNING;
    s_procs[child_idx].base.thread_count = 0U;
    s_procs[child_idx].exit_code = 0U;
    s_procs[child_idx].child_count = 0U;

    /* 清除挂起信号（不继承） */
    s_procs[child_idx].sig_pending[0U] = 0U;
    s_procs[child_idx].sig_pending[1U] = 0U;

    /* 继承阻塞信号掩码（已通过结构体复制获取） */
    /* 继承信号处理表（已通过结构体复制获取） */
    /* 继承资源限制（已通过结构体复制获取） */

    /* 更新父进程子进程计数 */
    s_procs[parent_idx].child_count++;

    s_active_count++;

    *child_pid = s_procs[child_idx].base.pid;

    return 0;
}

/* ========================================================================
 * 进程执行（exec 语义）
 * ======================================================================== */

/**
 * @brief exec 操作 - 替换进程映像
 *
 * @param pid        进程 ID
 * @param name       新进程名
 * @param cspace_id 新 CSpace ID
 * @param vspace_id 新 VSpace ID
 * @param endpoint_id 新 IPC 端点 ID
 *
 * @return 0 成功，负数表示错误
 *
 * @note exec 保留 pid、父进程关系、信号处理（重置为默认）
 */
static int32_t proc_exec(uint32_t pid, const char *name,
                         kobj_id_t cspace_id, kobj_id_t vspace_id,
                         kobj_id_t endpoint_id)
{
    uint32_t idx;
    uint32_t j;

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    /* 更新进程资源 */
    s_procs[idx].base.cspace_id = cspace_id;
    s_procs[idx].base.vspace_id = vspace_id;
    s_procs[idx].base.endpoint_id = endpoint_id;

    /* 更新进程名 */
    if (name != NULL)
    {
        for (j = 0U; (j < (PROC_NAME_MAX - 1U)) && (name[j] != '\0'); j++)
        {
            s_procs[idx].base.name[j] = name[j];
        }
        s_procs[idx].base.name[j] = '\0';
    }

    /* 重置信号处理为默认（POSIX 语义） */
    for (j = 0U; j < PROC_SIG_MAX; j++)
    {
        s_procs[j].sig_handlers[j].action = SIG_ACT_DEFAULT;
        s_procs[idx].sig_handlers[j].flags = 0U;
    }

    /* 清除挂起信号 */
    s_procs[idx].sig_pending[0U] = 0U;
    s_procs[idx].sig_pending[1U] = 0U;

    /* 重置线程计数 */
    s_procs[idx].base.thread_count = 0U;

    return 0;
}

/* ========================================================================
 * 基本进程创建（兼容旧接口）
 * ======================================================================== */

static int32_t proc_create(uint32_t parent_pid, const char *name,
                            kobj_id_t cspace_id, kobj_id_t vspace_id,
                            kobj_id_t endpoint_id)
{
    uint32_t i;
    uint32_t j;

    if (s_active_count >= MAX_PROCESSES)
    {
        return -(int32_t)ENOMEM;
    }

    for (i = 0U; i < MAX_PROCESSES; i++)
    {
        if (s_procs[i].base.state == PROC_STATE_EMPTY)
        {
            s_procs[i].base.pid = s_next_pid++;
            s_procs[i].base.parent_pid = parent_pid;
            s_procs[i].base.state = PROC_STATE_RUNNING;
            s_procs[i].base.thread_count = 0U;
            s_procs[i].base.cspace_id = cspace_id;
            s_procs[i].base.vspace_id = vspace_id;
            s_procs[i].base.endpoint_id = endpoint_id;
            s_procs[i].exit_code = 0U;
            s_procs[i].child_count = 0U;
            s_procs[i].pgrp = s_procs[i].base.pid;
            s_procs[i].session = s_procs[i].base.pid;

            if (name != NULL)
            {
                for (j = 0U; (j < (PROC_NAME_MAX - 1U)) && (name[j] != '\0'); j++)
                {
                    s_procs[i].base.name[j] = name[j];
                }
                s_procs[i].base.name[j] = '\0';
            }

            s_active_count++;

            return (int32_t)s_procs[i].base.pid;
        }
    }

    return -(int32_t)ENOMEM;
}

/* ========================================================================
 * 销毁进程
 * ======================================================================== */

static int32_t proc_destroy(uint32_t pid)
{
    uint32_t idx;

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ENOENT;
    }

    s_procs[idx].base.state = PROC_STATE_ZOMBIE;
    s_active_count--;

    return 0;
}

/* ========================================================================
 * 获取进程信息
 * ======================================================================== */

static int32_t proc_get_info(uint32_t pid, process_desc_t *info_out)
{
    uint32_t idx;

    if (info_out == NULL)
    {
        return -(int32_t)EINVAL;
    }

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ENOENT;
    }

    (void)memcpy(info_out, &s_procs[idx].base, sizeof(process_desc_t));

    return 0;
}

/* ========================================================================
 * 进程退出
 * ======================================================================== */

/**
 * @brief 进程退出处理
 *
 * @param pid       退出进程 ID
 * @param exit_code 退出码
 *
 * @return 0 成功，负数表示错误
 *
 * @note 将进程状态设为 ZOMBIE，向父进程发送 SIGCHLD
 */
static int32_t proc_exit(uint32_t pid, uint32_t exit_code)
{
    uint32_t idx;
    uint32_t parent_idx;

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    /* 设置退出状态 */
    s_procs[idx].base.state = PROC_STATE_ZOMBIE;
    s_procs[idx].exit_code = exit_code;
    s_active_count--;

    /* 向父进程发送 SIGCHLD 信号 */
    if (s_procs[idx].base.parent_pid != 0U)
    {
        parent_idx = proc_find_index(s_procs[idx].base.parent_pid);
        if (parent_idx < MAX_PROCESSES)
        {
            /* 设置 SIGCHLD 挂起位（信号17） */
            s_procs[parent_idx].sig_pending[0U] |= (1U << SIGCHLD);
        }
    }

    return 0;
}

/* ========================================================================
 * 进程等待（waitpid）
 * ======================================================================== */

/**
 * @brief 等待子进程状态变化
 *
 * @param parent_pid  父进程 ID
 * @param child_pid   指定子进程 ID（0 表示任意子进程）
 * @param[out] status  子进程退出码
 * @param options      等待选项（WNOHANG=1 表示非阻塞）
 *
 * @return 成功返回子进程 PID，0 表示无状态变化，负数表示错误
 */
static int32_t proc_waitpid(uint32_t parent_pid, uint32_t child_pid,
                            uint32_t *status, uint32_t options)
{
    uint32_t i;
    uint32_t found_pid;
    bool has_children;

    if (status == NULL)
    {
        return -(int32_t)EINVAL;
    }

    has_children = false;

    if (child_pid != 0U)
    {
        /* 等待指定子进程 */
        if (!proc_is_child(child_pid, parent_pid))
        {
            return -(int32_t)ECHILD;
        }

        i = proc_find_index(child_pid);
        if (i >= MAX_PROCESSES)
        {
            return -(int32_t)ECHILD;
        }

        has_children = true;

        if (s_procs[i].base.state == PROC_STATE_ZOMBIE)
        {
            *status = s_procs[i].exit_code;
            found_pid = s_procs[i].base.pid;

            /* 回收僵尸进程 */
            s_procs[i].base.state = PROC_STATE_EMPTY;
            s_procs[i].base.pid = 0U;

            return (int32_t)found_pid;
        }
    }
    else
    {
        /* 等待任意子进程 */
        for (i = 0U; i < MAX_PROCESSES; i++)
        {
            if ((s_procs[i].base.parent_pid == parent_pid) &&
                (s_procs[i].base.state != PROC_STATE_EMPTY))
            {
                has_children = true;

                if (s_procs[i].base.state == PROC_STATE_ZOMBIE)
                {
                    *status = s_procs[i].exit_code;
                    found_pid = s_procs[i].base.pid;

                    /* 回收僵尸进程 */
                    s_procs[i].base.state = PROC_STATE_EMPTY;
                    s_procs[i].base.pid = 0U;

                    return (int32_t)found_pid;
                }
            }
        }
    }

    if (!has_children)
    {
        return -(int32_t)ECHILD;
    }

    /* 无僵尸子进程 */
    if ((options & 1U) != 0U)
    {
        /* WNOHANG：非阻塞，立即返回 0 */
        return 0;
    }

    /* 阻塞等待：实际实现中应阻塞，此处返回 -EAGAIN 表示需重试 */
    return -(int32_t)EAGAIN;
}

/* ========================================================================
 * 状态机管理
 * ======================================================================== */

/**
 * @brief 设置进程状态
 *
 * @param pid   进程 ID
 * @param state 目标状态
 *
 * @return 0 成功，负数表示错误
 *
 * @note 状态转换校验：确保仅允许合法转换
 */
static int32_t proc_set_state(uint32_t pid, proc_state_t state)
{
    uint32_t idx;
    proc_state_t cur;

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    cur = s_procs[idx].base.state;

    /* 状态转换校验 */
    switch (cur)
    {
        case PROC_STATE_LOADING:
            /* 加载中 -> 运行中 */
            if (state != PROC_STATE_RUNNING)
            {
                return -(int32_t)EINVAL;
            }
            break;

        case PROC_STATE_RUNNING:
            /* 运行中 -> 阻塞/僵尸 */
            if ((state != PROC_STATE_BLOCKED) &&
                (state != PROC_STATE_ZOMBIE))
            {
                return -(int32_t)EINVAL;
            }
            break;

        case PROC_STATE_BLOCKED:
            /* 阻塞 -> 运行中/僵尸 */
            if ((state != PROC_STATE_RUNNING) &&
                (state != PROC_STATE_ZOMBIE))
            {
                return -(int32_t)EINVAL;
            }
            break;

        case PROC_STATE_ZOMBIE:
            /* 僵尸 -> 空槽（被回收） */
            if (state != PROC_STATE_EMPTY)
            {
                return -(int32_t)EINVAL;
            }
            break;

        case PROC_STATE_EMPTY:
        default:
            return -(int32_t)EINVAL;
    }

    s_procs[idx].base.state = state;

    return 0;
}

/* ========================================================================
 * 信号处理
 * ======================================================================== */

/**
 * @brief 发送信号到目标进程
 *
 * @param target_pid 目标进程 ID
 * @param sig        信号编号（1-31）
 *
 * @return 0 成功，负数表示错误
 *
 * @note SIGKILL 和 SIGSTOP 不可被阻塞或忽略
 */
static int32_t proc_signal_send(uint32_t target_pid, uint32_t sig)
{
    uint32_t idx;
    uint32_t word;
    uint32_t bit;

    if ((sig == 0U) || (sig > PROC_SIG_MAX))
    {
        return -(int32_t)EINVAL;
    }

    idx = proc_find_index(target_pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    /* SIGKILL 直接终止进程 */
    if (sig == SIGKILL)
    {
        return proc_exit(target_pid, 0x80U | sig);
    }

    /* SIGSTOP 停止进程 */
    if (sig == SIGSTOP)
    {
        if (s_procs[idx].base.state == PROC_STATE_RUNNING)
        {
            s_procs[idx].base.state = PROC_STATE_BLOCKED;
        }
        return 0;
    }

    /* 将信号加入挂起队列 */
    word = sig >> 5U;
    bit = sig & 0x1FU;
    if (word < 2U)
    {
        s_procs[idx].sig_pending[word] |= (1U << bit);
    }

    return 0;
}

/**
 * @brief 设置信号处理动作
 *
 * @param pid     进程 ID
 * @param sig     信号编号
 * @param action  处理动作类型
 * @param flags   信号处理标志
 *
 * @return 0 成功，负数表示错误
 */
static int32_t proc_signal_action(uint32_t pid, uint32_t sig,
                                   sig_action_t action, uint64_t flags)
{
    uint32_t idx;

    if ((sig == 0U) || (sig > PROC_SIG_MAX))
    {
        return -(int32_t)EINVAL;
    }

    /* SIGKILL 和 SIGSTOP 不可设置处理动作 */
    if ((sig == SIGKILL) || (sig == SIGSTOP))
    {
        return -(int32_t)EINVAL;
    }

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    s_procs[idx].sig_handlers[sig - 1U].action = action;
    s_procs[idx].sig_handlers[sig - 1U].flags = flags;

    return 0;
}

/* ========================================================================
 * 资源限制
 * ======================================================================== */

/**
 * @brief 设置资源限制
 *
 * @param pid      进程 ID
 * @param resource 资源类型
 * @param cur      软限制
 * @param max      硬限制
 *
 * @return 0 成功，负数表示错误
 *
 * @note 软限制不得超过硬限制
 */
static int32_t proc_rlimit_set(uint32_t pid, uint32_t resource,
                                uint64_t cur, uint64_t max)
{
    uint32_t idx;

    if (resource >= PROC_RLIMIT_MAX)
    {
        return -(int32_t)EINVAL;
    }

    if (cur > max)
    {
        return -(int32_t)EINVAL;
    }

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    s_procs[idx].rlimits[resource].cur = cur;
    s_procs[idx].rlimits[resource].max = max;

    return 0;
}

/**
 * @brief 获取资源限制
 *
 * @param pid      进程 ID
 * @param resource 资源类型
 * @param[out] cur 软限制输出
 * @param[out] max 硬限制输出
 *
 * @return 0 成功，负数表示错误
 */
static int32_t proc_rlimit_get(uint32_t pid, uint32_t resource,
                                uint64_t *cur, uint64_t *max)
{
    uint32_t idx;

    if (resource >= PROC_RLIMIT_MAX)
    {
        return -(int32_t)EINVAL;
    }

    if ((cur == NULL) || (max == NULL))
    {
        return -(int32_t)EINVAL;
    }

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    *cur = s_procs[idx].rlimits[resource].cur;
    *max = s_procs[idx].rlimits[resource].max;

    return 0;
}

/* ========================================================================
 * IPC 消息处理
 * ======================================================================== */

/**
 * @brief 处理进程管理器 IPC 请求
 *
 * @param msg_type 消息类型
 * @param data     内联数据（最多4个 uint64_t）
 *
 * @return 处理结果
 */
static int32_t proc_handle_message(uint32_t msg_type, uint64_t *data)
{
    int32_t result = -(int32_t)EINVAL;

    switch (msg_type)
    {
        case PROC_MSG_CREATE:
            result = proc_create(
                (uint32_t)data[0U],
                (const char *)(uintptr_t)data[1U],
                (kobj_id_t)data[2U],
                (kobj_id_t)data[3U],
                (kobj_id_t)data[4U]
            );
            break;

        case PROC_MSG_DESTROY:
            result = proc_destroy((uint32_t)data[0U]);
            break;

        case PROC_MSG_GET_INFO:
            result = proc_get_info(
                (uint32_t)data[0U],
                (process_desc_t *)(uintptr_t)data[1U]
            );
            break;

        case PROC_MSG_FORK:
            result = proc_fork(
                (uint32_t)data[0U],
                (uint32_t *)(uintptr_t)data[1U]
            );
            break;

        case PROC_MSG_EXEC:
            result = proc_exec(
                (uint32_t)data[0U],
                (const char *)(uintptr_t)data[1U],
                (kobj_id_t)data[2U],
                (kobj_id_t)data[3U],
                (kobj_id_t)data[4U]
            );
            break;

        case PROC_MSG_EXIT:
            result = proc_exit(
                (uint32_t)data[0U],
                (uint32_t)data[1U]
            );
            break;

        case PROC_MSG_WAITPID:
            result = proc_waitpid(
                (uint32_t)data[0U],
                (uint32_t)data[1U],
                (uint32_t *)(uintptr_t)data[2U],
                (uint32_t)data[3U]
            );
            break;

        case PROC_MSG_SIGNAL:
            result = proc_signal_send(
                (uint32_t)data[0U],
                (uint32_t)data[1U]
            );
            break;

        case PROC_MSG_RLIMIT_SET:
            result = proc_rlimit_set(
                (uint32_t)data[0U],
                (uint32_t)data[1U],
                data[2U],
                data[3U]
            );
            break;

        case PROC_MSG_RLIMIT_GET:
            result = proc_rlimit_get(
                (uint32_t)data[0U],
                (uint32_t)data[1U],
                (uint64_t *)(uintptr_t)data[2U],
                (uint64_t *)(uintptr_t)data[3U]
            );
            break;

        case PROC_MSG_STATE_SET:
            result = proc_set_state(
                (uint32_t)data[0U],
                (proc_state_t)data[1U]
            );
            break;

        default:
            result = -(int32_t)ENOSYS;
            break;
    }

    return result;
}

/* ========================================================================
 * 服务主函数
 * ======================================================================== */

int main(void)
{
    proc_init();

    for (;;)
    {
        uint64_t ipc_data[4U];

        /* 通过 IPC 接收并处理请求 */
        /* 实际实现中调用 ipc_msg_receive() 获取消息 */
        /* 此处为框架循环，等待内核投递消息 */
        (void)ipc_data;

        /* proc_handle_message(msg_type, ipc_data); */
    }

    return 0;
}
