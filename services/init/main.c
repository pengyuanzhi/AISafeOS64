/**
 * @file    main.c
 * @brief   init 服务入口 - 系统启动编排与服务监控
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 4.0
 *
 * @details init 服务是内核启动后创建的第一个用户态服务。
 *          其职责包括：
 *          - 系统启动编排（按依赖图启动所有服务）
 *          - 服务启动顺序管理（解析服务依赖关系，拓扑排序）
 *          - 服务监控和自动重启（心跳检测 + 崩溃重启）
 *          - 系统状态报告（运行中/失败/等待中的服务列表）
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-024, API-001~006
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/syscall.h>
#include <kernel/service.h>
#include <kernel/config.h>
#include <kernel/errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ========================================================================
 * Init 服务常量
 * ======================================================================== */

/** @brief 最大管理服务数 */
#define INIT_MAX_SERVICES        16U

/** @brief 最大服务依赖数 */
#define INIT_MAX_DEPS            8U

/** @brief 最大重启次数 */
#define INIT_MAX_RESTARTS        3U

/** @brief 重启间隔（毫秒） */
#define INIT_RESTART_DELAY_MS    5000U

/** @brief 心跳超时（毫秒） */
#define INIT_HEARTBEAT_TIMEOUT_MS  30000U

/** @brief 服务名最大长度 */
#define INIT_SVC_NAME_MAX        32U

/** @brief 启动消息最大长度 */
#define INIT_MSG_MAX             128U

/** @brief 状态报告缓冲区大小 */
#define INIT_REPORT_BUF_SIZE     512U

/** @brief 启动阶段超时（毫秒） */
#define INIT_BOOT_TIMEOUT_MS     60000U

/** @brief 心跳检查间隔（毫秒） */
#define INIT_MONITOR_INTERVAL_MS 1000U

/* ========================================================================
 * 服务描述定义
 * ======================================================================== */

/**
 * @brief 服务状态
 */
typedef enum
{
    INIT_SVC_STOPPED = 0U,  /**< @brief 已停止 */
    INIT_SVC_STARTING,      /**< @brief 正在启动 */
    INIT_SVC_RUNNING,       /**< @brief 运行中 */
    INIT_SVC_FAILED,        /**< @brief 失败 */
    INIT_SVC_RESTARTING,    /**< @brief 正在重启 */
    INIT_SVC_WAITING,       /**< @brief 等待依赖 */
    INIT_SVC_STOPPING       /**< @brief 正在停止 */
} init_svc_state_t;

/**
 * @brief 服务启动策略
 */
typedef enum
{
    INIT_POLICY_CRITICAL = 0U, /**< @brief 关键服务（失败则系统停止） */
    INIT_POLICY_OPTIONAL,      /**< @brief 可选服务（失败后跳过） */
    INIT_POLICY_RESTART        /**< @brief 可重启服务（失败后自动重启） */
} init_restart_policy_t;

/**
 * @brief 服务描述
 */
typedef struct
{
    uint32_t              service_id;     /**< @brief 服务 ID */
    char                  name[INIT_SVC_NAME_MAX]; /**< @brief 服务名 */
    uint32_t              protocol;       /**< @brief 服务协议号 */
    init_svc_state_t      state;          /**< @brief 当前状态 */
    init_restart_policy_t policy;         /**< @brief 重启策略 */
    uint32_t              restart_count;  /**< @brief 已重启次数 */
    uint32_t              max_restarts;   /**< @brief 最大重启次数 */
    uint32_t              dep_count;      /**< @brief 依赖数量 */
    uint32_t              deps[INIT_MAX_DEPS]; /**< @brief 依赖服务索引 */
    uint64_t              start_time;     /**< @brief 启动时间 */
    uint64_t              last_heartbeat; /**< @brief 最后心跳时间 */
    uint32_t              fail_count;     /**< @brief 累计失败次数 */
    bool                  in_use;         /**< @brief 使用标记 */
} init_service_desc_t;

/**
 * @brief 系统状态
 */
typedef enum
{
    SYSTEM_STATE_BOOTING = 0U,  /**< @brief 启动中 */
    SYSTEM_STATE_RUNNING,       /**< @brief 运行中 */
    SYSTEM_STATE_DEGRADED,      /**< @brief 降级模式 */
    SYSTEM_STATE_SHUTTING_DOWN  /**< @brief 关机中 */
} system_state_t;

/**
 * @brief 系统状态报告
 */
typedef struct
{
    uint32_t     total_services;   /**< @brief 总服务数 */
    uint32_t     running_count;    /**< @brief 运行中服务数 */
    uint32_t     failed_count;     /**< @brief 失败服务数 */
    uint32_t     waiting_count;    /**< @brief 等待中服务数 */
    uint32_t     stopped_count;    /**< @brief 已停止服务数 */
    system_state_t sys_state;      /**< @brief 系统状态 */
    uint64_t     boot_time;        /**< @brief 启动耗时（毫秒） */
} init_system_report_t;

/* ========================================================================
 * 全局状态
 * ======================================================================== */

/** @brief 服务描述表 */
static init_service_desc_t s_services[INIT_MAX_SERVICES];

/** @brief 系统状态 */
static system_state_t s_system_state;

/** @brief 活跃服务计数 */
static uint32_t s_active_count;

/** @brief 初始化标志 */
static bool s_initialized;

/** @brief 启动开始时间 */
static uint64_t s_boot_start_time;

/* ========================================================================
 * 外部函数声明
 * ======================================================================== */

extern int sys_debug_print(const char *str, uint64_t len);
extern void sys_thread_yield(void);
extern uint64_t sys_get_timestamp(void);

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 安全字符串复制
 *
 * @param dst 目标缓冲区
 * @param src 源字符串
 * @param n   目标缓冲区大小
 */
static void init_strcpy(char *dst, const char *src, uint32_t n)
{
    uint32_t i;

    if ((dst == NULL) || (src == NULL) || (n == 0U))
    {
        return;
    }

    for (i = 0U; (i < (n - 1U)) && (src[i] != '\0'); i++)
    {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/**
 * @brief 安全字符串长度
 *
 * @param s 输入字符串
 *
 * @return 字符串长度
 */
static uint32_t init_strlen(const char *s)
{
    uint32_t len = 0U;

    if (s == NULL)
    {
        return 0U;
    }

    while (s[len] != '\0')
    {
        len++;
        if (len >= INIT_MSG_MAX)
        {
            break;
        }
    }

    return len;
}

/**
 * @brief 调试打印
 *
 * @param msg 消息字符串
 */
static void init_print(const char *msg)
{
    if (msg != NULL)
    {
        (void)sys_debug_print(msg, (uint64_t)init_strlen(msg));
    }
}

/**
 * @brief 将服务状态转换为字符串
 *
 * @param state 服务状态
 *
 * @return 状态字符串
 */
static const char *init_state_to_str(init_svc_state_t state)
{
    const char *str;

    switch (state)
    {
        case INIT_SVC_STOPPED:
            str = "STOPPED";
            break;
        case INIT_SVC_STARTING:
            str = "STARTING";
            break;
        case INIT_SVC_RUNNING:
            str = "RUNNING";
            break;
        case INIT_SVC_FAILED:
            str = "FAILED";
            break;
        case INIT_SVC_RESTARTING:
            str = "RESTARTING";
            break;
        case INIT_SVC_WAITING:
            str = "WAITING";
            break;
        case INIT_SVC_STOPPING:
            str = "STOPPING";
            break;
        default:
            str = "UNKNOWN";
            break;
    }

    return str;
}

/**
 * @brief 将系统状态转换为字符串
 *
 * @param state 系统状态
 *
 * @return 状态字符串
 */
static const char *init_sys_state_to_str(system_state_t state)
{
    const char *str;

    switch (state)
    {
        case SYSTEM_STATE_BOOTING:
            str = "BOOTING";
            break;
        case SYSTEM_STATE_RUNNING:
            str = "RUNNING";
            break;
        case SYSTEM_STATE_DEGRADED:
            str = "DEGRADED";
            break;
        case SYSTEM_STATE_SHUTTING_DOWN:
            str = "SHUTTING_DOWN";
            break;
        default:
            str = "UNKNOWN";
            break;
    }

    return str;
}

/* ========================================================================
 * 服务注册
 * ======================================================================== */

/**
 * @brief 注册服务到 init 管理
 *
 * @param name      服务名
 * @param protocol  服务协议号
 * @param policy    重启策略
 * @param deps      依赖服务索引数组
 * @param dep_count 依赖数量
 *
 * @return 成功返回服务索引，负数表示错误
 */
static int32_t init_register_service(const char *name, uint32_t protocol,
                                       init_restart_policy_t policy,
                                       const uint32_t *deps,
                                       uint32_t dep_count)
{
    uint32_t i;
    uint32_t j;

    if ((name == NULL) || (dep_count > INIT_MAX_DEPS))
    {
        return -(int32_t)EINVAL;
    }

    for (i = 0U; i < INIT_MAX_SERVICES; i++)
    {
        if (!s_services[i].in_use)
        {
            break;
        }
    }

    if (i >= INIT_MAX_SERVICES)
    {
        return -(int32_t)ENOMEM;
    }

    (void)memset(&s_services[i], 0, sizeof(init_service_desc_t));

    s_services[i].service_id = i;
    init_strcpy(s_services[i].name, name, INIT_SVC_NAME_MAX);
    s_services[i].protocol = protocol;
    s_services[i].state = INIT_SVC_STOPPED;
    s_services[i].policy = policy;
    s_services[i].restart_count = 0U;
    s_services[i].max_restarts = INIT_MAX_RESTARTS;
    s_services[i].dep_count = dep_count;
    s_services[i].start_time = 0ULL;
    s_services[i].last_heartbeat = 0ULL;
    s_services[i].fail_count = 0U;
    s_services[i].in_use = true;

    if ((deps != NULL) && (dep_count > 0U))
    {
        for (j = 0U; j < dep_count; j++)
        {
            s_services[i].deps[j] = deps[j];
        }
    }

    return (int32_t)i;
}

/* ========================================================================
 * 依赖图检查
 * ======================================================================== */

/**
 * @brief 检查服务依赖是否满足
 *
 * @details 遍历指定服务的所有依赖项，检查每个依赖是否已处于
 *          INIT_SVC_RUNNING 状态。仅当所有依赖均已运行时返回 true。
 *
 * @param svc_idx 服务索引
 *
 * @return true 所有依赖都已运行，false 存在未满足的依赖
 */
static bool init_deps_satisfied(uint32_t svc_idx)
{
    uint32_t i;
    uint32_t dep_idx;
    init_service_desc_t *svc;

    if (svc_idx >= INIT_MAX_SERVICES)
    {
        return false;
    }

    svc = &s_services[svc_idx];

    if (svc->dep_count == 0U)
    {
        return true;
    }

    for (i = 0U; i < svc->dep_count; i++)
    {
        dep_idx = svc->deps[i];

        if (dep_idx >= INIT_MAX_SERVICES)
        {
            return false;
        }

        if (!s_services[dep_idx].in_use)
        {
            return false;
        }

        if (s_services[dep_idx].state != INIT_SVC_RUNNING)
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief 检测循环依赖（深度优先搜索）
 *
 * @param svc_idx    起始服务索引
 * @param visited    已访问位图
 * @param on_stack   当前搜索栈上的节点
 *
 * @return true 存在循环，false 无循环
 */
static bool init_detect_cycle_dfs(uint32_t svc_idx,
                                    bool visited[INIT_MAX_SERVICES],
                                    bool on_stack[INIT_MAX_SERVICES])
{
    uint32_t i;
    uint32_t dep_idx;

    if (svc_idx >= INIT_MAX_SERVICES)
    {
        return false;
    }

    if (visited[svc_idx])
    {
        if (on_stack[svc_idx])
        {
            return true;
        }
        return false;
    }

    visited[svc_idx] = true;
    on_stack[svc_idx] = true;

    for (i = 0U; i < s_services[svc_idx].dep_count; i++)
    {
        dep_idx = s_services[svc_idx].deps[i];

        if (dep_idx < INIT_MAX_SERVICES)
        {
            if (init_detect_cycle_dfs(dep_idx, visited, on_stack))
            {
                return true;
            }
        }
    }

    on_stack[svc_idx] = false;

    return false;
}

/**
 * @brief 检查整个依赖图是否存在循环
 *
 * @details 使用标准的 DFS 环检测算法（三色标记法），
 *          检查所有已注册服务的依赖关系中是否存在循环。
 *
 * @return true 存在循环，false 无循环
 */
static bool init_has_dependency_cycle(void)
{
    uint32_t i;
    bool visited[INIT_MAX_SERVICES];
    bool on_stack[INIT_MAX_SERVICES];

    (void)memset(visited, 0, sizeof(visited));
    (void)memset(on_stack, 0, sizeof(on_stack));

    for (i = 0U; i < INIT_MAX_SERVICES; i++)
    {
        if (s_services[i].in_use)
        {
            if (init_detect_cycle_dfs(i, visited, on_stack))
            {
                return true;
            }
        }
    }

    return false;
}

/**
 * @brief 计算服务的启动优先级（拓扑层级）
 *
 * @details 无依赖的服务层级为 0，依赖其他服务的层级为
 *          max(依赖层级) + 1。层级越低越先启动。
 *
 * @param svc_idx 服务索引
 * @param levels  各服务层级缓存
 * @param depth   当前递归深度（防止异常）
 *
 * @return 启动层级
 */
static uint32_t init_calc_level(uint32_t svc_idx,
                                  uint32_t levels[INIT_MAX_SERVICES],
                                  uint32_t depth)
{
    uint32_t i;
    uint32_t dep_idx;
    uint32_t max_dep_level;
    uint32_t dep_level;

    if ((svc_idx >= INIT_MAX_SERVICES) || (depth > INIT_MAX_SERVICES))
    {
        return 0U;
    }

    if (levels[svc_idx] != 0U)
    {
        return levels[svc_idx];
    }

    if (s_services[svc_idx].dep_count == 0U)
    {
        levels[svc_idx] = 1U;
        return 1U;
    }

    max_dep_level = 0U;

    for (i = 0U; i < s_services[svc_idx].dep_count; i++)
    {
        dep_idx = s_services[svc_idx].deps[i];

        if (dep_idx < INIT_MAX_SERVICES)
        {
            dep_level = init_calc_level(dep_idx, levels, depth + 1U);
            if (dep_level > max_dep_level)
            {
                max_dep_level = dep_level;
            }
        }
    }

    levels[svc_idx] = max_dep_level + 1U;
    return levels[svc_idx];
}

/* ========================================================================
 * 服务启动
 * ======================================================================== */

/**
 * @brief 启动单个服务
 *
 * @details 执行服务启动流程：
 *          1. 验证参数和状态
 *          2. 检查依赖是否满足
 *          3. 设置状态为 STARTING
 *          4. 调用内核创建服务进程
 *          5. 更新状态为 RUNNING
 *
 * @param svc_idx 服务索引
 *
 * @return KERNEL_OK 成功，负数表示错误
 */
static kernel_status_t init_start_service(uint32_t svc_idx)
{
    init_service_desc_t *svc;

    if (svc_idx >= INIT_MAX_SERVICES)
    {
        return -(int32_t)EINVAL;
    }

    svc = &s_services[svc_idx];

    if (!svc->in_use)
    {
        return -(int32_t)ESRCH;
    }

    if (svc->state == INIT_SVC_RUNNING)
    {
        return KERNEL_OK;
    }

    if (!init_deps_satisfied(svc_idx))
    {
        svc->state = INIT_SVC_WAITING;
        return -(int32_t)EAGAIN;
    }

    svc->state = INIT_SVC_STARTING;
    init_print("[init] Starting service: ");
    init_print(svc->name);
    init_print("\n");

    /*
     * 实际实现中：
     * 1. 调用 sys_process_create() 创建服务进程
     * 2. 通过 PathManager 注册服务端点
     * 3. 等待服务就绪通知（IPC 回复）
     * 4. 设置心跳时间戳
     */

    svc->state = INIT_SVC_RUNNING;
    svc->start_time = sys_get_timestamp();
    svc->last_heartbeat = svc->start_time;
    s_active_count++;

    init_print("[init] Service started: ");
    init_print(svc->name);
    init_print("\n");

    return KERNEL_OK;
}

/**
 * @brief 按依赖顺序启动所有服务（拓扑排序）
 *
 * @details 使用拓扑排序算法：
 *          1. 计算每个服务的启动层级（依赖深度）
 *          2. 按层级从低到高依次启动
 *          3. 同层级内按注册顺序启动
 *          4. 重复直到所有可启动的服务都已启动
 *
 *          这种算法保证了：
 *          - 被依赖的服务一定先于依赖者启动
 *          - 同层级服务可以并行启动
 *          - 检测到无法满足的依赖会标记为 WAITING
 */
static void init_start_all_services(void)
{
    uint32_t levels[INIT_MAX_SERVICES];
    uint32_t max_level;
    uint32_t current_level;
    uint32_t i;
    uint32_t started;
    uint32_t total_registered;

    (void)memset(levels, 0, sizeof(levels));

    /* 计算每个服务的启动层级 */
    max_level = 0U;
    total_registered = 0U;

    for (i = 0U; i < INIT_MAX_SERVICES; i++)
    {
        if (s_services[i].in_use)
        {
            total_registered++;
            (void)init_calc_level(i, levels, 0U);
            if (levels[i] > max_level)
            {
                max_level = levels[i];
            }
        }
    }

    init_print("[init] Dependency graph resolved, max depth: ...\n");

    /* 按层级依次启动 */
    started = 0U;

    for (current_level = 1U; current_level <= max_level; current_level++)
    {
        for (i = 0U; i < INIT_MAX_SERVICES; i++)
        {
            if (!s_services[i].in_use)
            {
                continue;
            }

            if (levels[i] != current_level)
            {
                continue;
            }

            if (s_services[i].state == INIT_SVC_STOPPED)
            {
                (void)init_start_service(i);

                if (s_services[i].state == INIT_SVC_RUNNING)
                {
                    started++;
                }
            }
        }
    }

    /* 检查未能启动的服务 */
    for (i = 0U; i < INIT_MAX_SERVICES; i++)
    {
        if (s_services[i].in_use)
        {
            if (s_services[i].state == INIT_SVC_STOPPED)
            {
                s_services[i].state = INIT_SVC_WAITING;
            }
        }
    }
}

/* ========================================================================
 * 服务停止
 * ======================================================================== */

/**
 * @brief 停止单个服务
 *
 * @param svc_idx 服务索引
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t init_stop_service(uint32_t svc_idx)
{
    init_service_desc_t *svc;

    if (svc_idx >= INIT_MAX_SERVICES)
    {
        return -(int32_t)EINVAL;
    }

    svc = &s_services[svc_idx];

    if (!svc->in_use)
    {
        return -(int32_t)ESRCH;
    }

    if (svc->state == INIT_SVC_STOPPED)
    {
        return KERNEL_OK;
    }

    svc->state = INIT_SVC_STOPPING;

    init_print("[init] Stopping service: ");
    init_print(svc->name);
    init_print("\n");

    /*
     * 实际实现中：
     * 1. 向服务进程发送 SIGTERM 等效信号
     * 2. 等待服务优雅退出（超时后强制终止）
     * 3. 从 PathManager 注销服务端点
     * 4. 回收服务进程资源
     */

    svc->state = INIT_SVC_STOPPED;
    if (s_active_count > 0U)
    {
        s_active_count--;
    }

    return KERNEL_OK;
}

/**
 * @brief 按逆依赖顺序停止所有服务
 */
static void init_stop_all_services(void)
{
    int32_t i;

    init_print("[init] Stopping all services\n");

    /* 逆序停止（后启动的先停止） */
    for (i = (int32_t)(INIT_MAX_SERVICES - 1U); i >= 0; i--)
    {
        if (s_services[i].in_use && (s_services[i].state != INIT_SVC_STOPPED))
        {
            (void)init_stop_service((uint32_t)i);
        }
    }
}

/* ========================================================================
 * 服务监控和自动重启
 * ======================================================================== */

/**
 * @brief 处理服务失败
 *
 * @details 根据服务的重启策略处理失败：
 *          - CRITICAL：标记系统为关机状态
 *          - RESTART：在最大重启次数内尝试重启
 *          - OPTIONAL：标记为失败，继续运行
 *
 * @param svc_idx 服务索引
 */
static void init_handle_failure(uint32_t svc_idx)
{
    init_service_desc_t *svc;

    if (svc_idx >= INIT_MAX_SERVICES)
    {
        return;
    }

    svc = &s_services[svc_idx];

    svc->fail_count++;

    switch (svc->policy)
    {
        case INIT_POLICY_CRITICAL:
            /* 关键服务失败，系统停止 */
            init_print("[init] CRITICAL service failed: ");
            init_print(svc->name);
            init_print(", shutting down system\n");
            s_system_state = SYSTEM_STATE_SHUTTING_DOWN;
            break;

        case INIT_POLICY_RESTART:
            if (svc->restart_count < svc->max_restarts)
            {
                svc->state = INIT_SVC_RESTARTING;
                svc->restart_count++;

                init_print("[init] Restarting service: ");
                init_print(svc->name);
                init_print(" (attempt ");
                /* 简化：不打印数字 */
                init_print(")\n");

                svc->state = INIT_SVC_STOPPED;

                if (init_deps_satisfied(svc_idx))
                {
                    (void)init_start_service(svc_idx);
                }
            }
            else
            {
                svc->state = INIT_SVC_FAILED;
                s_system_state = SYSTEM_STATE_DEGRADED;

                init_print("[init] Service permanently failed: ");
                init_print(svc->name);
                init_print(", system degraded\n");
            }
            break;

        case INIT_POLICY_OPTIONAL:
        default:
            svc->state = INIT_SVC_FAILED;
            init_print("[init] Optional service failed: ");
            init_print(svc->name);
            init_print("\n");
            break;
    }
}

/**
 * @brief 检查服务心跳
 *
 * @details 检查每个运行中服务的心跳时间戳，
 *          超时未更新的服务将被标记为失败。
 */
static void init_check_heartbeats(void)
{
    uint32_t i;
    uint64_t now;
    init_service_desc_t *svc;

    now = sys_get_timestamp();

    for (i = 0U; i < INIT_MAX_SERVICES; i++)
    {
        svc = &s_services[i];

        if (!svc->in_use)
        {
            continue;
        }

        if (svc->state != INIT_SVC_RUNNING)
        {
            continue;
        }

        if (svc->last_heartbeat == 0ULL)
        {
            /* 尚未收到首次心跳，跳过 */
            continue;
        }

        if ((now - svc->last_heartbeat) > (uint64_t)INIT_HEARTBEAT_TIMEOUT_MS)
        {
            init_print("[init] Heartbeat timeout for service: ");
            init_print(svc->name);
            init_print("\n");

            svc->state = INIT_SVC_FAILED;
            if (s_active_count > 0U)
            {
                s_active_count--;
            }
        }
    }
}

/**
 * @brief 服务健康监控循环
 *
 * @details 执行一轮服务健康检查：
 *          1. 检查所有已失败服务并处理
 *          2. 检查心跳超时
 *          3. 尝试启动等待中的服务
 */
static void init_monitor_services(void)
{
    uint32_t i;

    /* 检查心跳超时 */
    init_check_heartbeats();

    /* 处理已失败的服务 */
    for (i = 0U; i < INIT_MAX_SERVICES; i++)
    {
        if (!s_services[i].in_use)
        {
            continue;
        }

        if (s_services[i].state == INIT_SVC_FAILED)
        {
            if (s_active_count > 0U)
            {
                s_active_count--;
            }
            init_handle_failure(i);
        }
    }

    /* 尝试启动等待依赖的服务 */
    for (i = 0U; i < INIT_MAX_SERVICES; i++)
    {
        if (!s_services[i].in_use)
        {
            continue;
        }

        if (s_services[i].state == INIT_SVC_WAITING)
        {
            if (init_deps_satisfied(i))
            {
                (void)init_start_service(i);
            }
        }
    }
}

/* ========================================================================
 * 系统状态报告
 * ======================================================================== */

/**
 * @brief 获取系统状态报告
 *
 * @param[out] report 报告结构体指针
 */
static void init_get_report(init_system_report_t *report)
{
    uint32_t i;

    if (report == NULL)
    {
        return;
    }

    (void)memset(report, 0, sizeof(init_system_report_t));

    for (i = 0U; i < INIT_MAX_SERVICES; i++)
    {
        if (!s_services[i].in_use)
        {
            continue;
        }

        report->total_services++;

        switch (s_services[i].state)
        {
            case INIT_SVC_RUNNING:
                report->running_count++;
                break;
            case INIT_SVC_FAILED:
                report->failed_count++;
                break;
            case INIT_SVC_WAITING:
                report->waiting_count++;
                break;
            case INIT_SVC_STOPPED:
            case INIT_SVC_STOPPING:
                report->stopped_count++;
                break;
            case INIT_SVC_STARTING:
            case INIT_SVC_RESTARTING:
                /* 启动/重启中计为等待 */
                report->waiting_count++;
                break;
            default:
                break;
        }
    }

    report->sys_state = s_system_state;
    report->boot_time = sys_get_timestamp() - s_boot_start_time;
}

/**
 * @brief 打印系统状态报告
 */
static void init_print_report(void)
{
    uint32_t i;
    init_system_report_t report;

    init_get_report(&report);

    init_print("[init] === System Status Report ===\n");
    init_print("[init] State: ");
    init_print(init_sys_state_to_str(report.sys_state));
    init_print("\n");

    /* 打印各服务状态 */
    init_print("[init] Services:\n");

    for (i = 0U; i < INIT_MAX_SERVICES; i++)
    {
        if (!s_services[i].in_use)
        {
            continue;
        }

        init_print("  - ");
        init_print(s_services[i].name);
        init_print(" [");
        init_print(init_state_to_str(s_services[i].state));
        init_print("]\n");
    }
}

/* ========================================================================
 * 服务注册表初始化
 * ======================================================================== */

/**
 * @brief 注册所有系统服务
 *
 * @details 定义服务启动依赖图：
 *
 *          层级 1（无依赖）:
 *            path_manager (0)
 *
 *          层级 2（依赖层级 1）:
 *            mem_manager (1)  ← path_manager
 *
 *          层级 3（依赖层级 2）:
 *            proc_manager (2) ← mem_manager
 *            dev_manager  (3) ← mem_manager
 *            fs_service   (4) ← mem_manager
 *            security     (6) ← mem_manager
 *
 *          层级 4（依赖层级 3）:
 *            net_service  (5) ← mem_manager + fs_service
 *            vmm_service  (7) ← mem_manager + security
 */
static void init_register_all_services(void)
{
    /* 层级 1: PathManager - 无依赖 */
    (void)init_register_service("path_manager", SERVICE_PATH_MANAGER,
                                  INIT_POLICY_CRITICAL, NULL, 0U);

    /* 层级 2: MemoryManager - 依赖 PathManager */
    {
        uint32_t deps[1U] = {0U};
        (void)init_register_service("mem_manager", SERVICE_MEM_MANAGER,
                                      INIT_POLICY_CRITICAL, deps, 1U);
    }

    /* 层级 3: ProcessManager - 依赖 MemoryManager */
    {
        uint32_t deps[1U] = {1U};
        (void)init_register_service("proc_manager", SERVICE_PROC_MANAGER,
                                      INIT_POLICY_CRITICAL, deps, 1U);
    }

    /* 层级 3: DeviceManager - 依赖 MemoryManager */
    {
        uint32_t deps[1U] = {1U};
        (void)init_register_service("dev_manager", SERVICE_DEV_MANAGER,
                                      INIT_POLICY_RESTART, deps, 1U);
    }

    /* 层级 3: Filesystem - 依赖 MemoryManager */
    {
        uint32_t deps[1U] = {1U};
        (void)init_register_service("fs_service", SERVICE_FS_MANAGER,
                                      INIT_POLICY_RESTART, deps, 1U);
    }

    /* 层级 4: Network - 依赖 MemoryManager + FS */
    {
        uint32_t deps[2U] = {1U, 4U};
        (void)init_register_service("net_service", SERVICE_NET_MANAGER,
                                      INIT_POLICY_RESTART, deps, 2U);
    }

    /* 层级 3: Security - 依赖 MemoryManager */
    {
        uint32_t deps[1U] = {1U};
        (void)init_register_service("security", SERVICE_SECURITY_MANAGER,
                                      INIT_POLICY_CRITICAL, deps, 1U);
    }

    /* 层级 4: VMM - 依赖 MemoryManager + Security */
    {
        uint32_t deps[2U] = {1U, 6U};
        (void)init_register_service("vmm_service", SERVICE_VMM_MANAGER,
                                      INIT_POLICY_OPTIONAL, deps, 2U);
    }
}

/* ========================================================================
 * init 服务入口
 * ======================================================================== */

/**
 * @brief init 服务主入口
 *
 * @details 启动流程：
 *          1. 初始化服务注册表
 *          2. 注册所有系统服务
 *          3. 验证依赖图（检测循环依赖）
 *          4. 按依赖拓扑顺序启动所有服务
 *          5. 进入监控主循环（心跳检测 + 故障恢复）
 *          6. 收到关机信号时按逆序停止所有服务
 *
 * @return 0 正常退出，1 异常退出
 */
int main(void)
{
    /* 打印启动信息 */
    init_print("[init] AISafeOS64 init service started\n");

    /* 记录启动时间 */
    s_boot_start_time = sys_get_timestamp();

    /* 步骤 1：初始化服务注册表 */
    (void)memset(s_services, 0, sizeof(s_services));
    s_system_state = SYSTEM_STATE_BOOTING;
    s_active_count = 0U;
    s_initialized = true;

    /* 步骤 2：注册所有服务 */
    init_register_all_services();

    /* 步骤 3：验证依赖图 */
    if (init_has_dependency_cycle())
    {
        init_print("[init] FATAL: Dependency cycle detected, cannot boot\n");
        s_system_state = SYSTEM_STATE_SHUTTING_DOWN;
        for (;;)
        {
            sys_thread_yield();
        }
        return 1;
    }

    init_print("[init] Dependency graph validated, starting services\n");

    /* 步骤 4：按依赖顺序启动所有服务 */
    init_start_all_services();

    /* 步骤 5：标记系统为运行状态 */
    s_system_state = SYSTEM_STATE_RUNNING;
    init_print("[init] All services started, system running\n");

    /* 打印初始状态报告 */
    init_print_report();

    /* 步骤 6：主循环 - 服务健康监控 */
    for (;;)
    {
        /* 执行一轮健康监控 */
        init_monitor_services();

        /* 检查是否需要关机 */
        if (s_system_state == SYSTEM_STATE_SHUTTING_DOWN)
        {
            init_print("[init] System shutting down\n");
            init_stop_all_services();
            break;
        }

        /* 定期打印状态报告 */
        /* TODO: 基于定时器周期性打印 */

        sys_thread_yield();
    }

    return 0;
}
