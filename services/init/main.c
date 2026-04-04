/**
 * @file    main.c
 * @brief   init 服务入口
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 3.0
 *
 * @details init 服务是内核启动后创建的第一个用户态服务。
 *          其职责包括：
 *          - 系统启动编排
 *          - 服务启动顺序管理（依赖图）
 *          - 服务监控和自动重启
 *          - 系统状态报告
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-024, API-001~006
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/syscall.h>
#include <kernel/service.h>
#include <kernel/config.h>
#include <stdint.h>
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

/** @brief 服务名最大长度 */
#define INIT_SVC_NAME_MAX        32U

/** @brief 启动消息最大长度 */
#define INIT_MSG_MAX             128U

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
    INIT_SVC_RESTARTING     /**< @brief 正在重启 */
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

/* ========================================================================
 * 外部函数声明
 * ======================================================================== */

extern int sys_debug_print(const char *str, uint64_t len);
extern void sys_thread_yield(void);

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 安全字符串复制
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
 */
static void init_print(const char *msg)
{
    if (msg != NULL)
    {
        (void)sys_debug_print(msg, (uint64_t)init_strlen(msg));
    }
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
        return -(int32_t)22;
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
        return -(int32_t)12;
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
 * @brief 检测循环依赖
 *
 * @param svc_idx    起始服务索引
 * @param visited    已访问位图
 *
 * @return true 存在循环，false 无循环
 */
static bool init_detect_cycle(uint32_t svc_idx, bool visited[INIT_MAX_SERVICES])
{
    uint32_t i;
    uint32_t dep_idx;

    if (svc_idx >= INIT_MAX_SERVICES)
    {
        return false;
    }

    if (visited[svc_idx])
    {
        return true;
    }

    visited[svc_idx] = true;

    for (i = 0U; i < s_services[svc_idx].dep_count; i++)
    {
        dep_idx = s_services[svc_idx].deps[i];

        if (init_detect_cycle(dep_idx, visited))
        {
            return true;
        }
    }

    visited[svc_idx] = false;

    return false;
}

/**
 * @brief 检查整个依赖图是否存在循环
 *
 * @return true 存在循环，false 无循环
 */
static bool init_has_dependency_cycle(void)
{
    uint32_t i;
    bool visited[INIT_MAX_SERVICES];

    (void)memset(visited, 0, sizeof(visited));

    for (i = 0U; i < INIT_MAX_SERVICES; i++)
    {
        if (s_services[i].in_use)
        {
            if (init_detect_cycle(i, visited))
            {
                return true;
            }
        }
    }

    return false;
}

/* ========================================================================
 * 服务启动
 * ======================================================================== */

/**
 * @brief 启动单个服务
 *
 * @param svc_idx 服务索引
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t init_start_service(uint32_t svc_idx)
{
    init_service_desc_t *svc;

    if (svc_idx >= INIT_MAX_SERVICES)
    {
        return -(int32_t)22;
    }

    svc = &s_services[svc_idx];

    if (!svc->in_use)
    {
        return -(int32_t)2;
    }

    if (svc->state == INIT_SVC_RUNNING)
    {
        return KERNEL_OK;
    }

    if (!init_deps_satisfied(svc_idx))
    {
        return -(int32_t)11;
    }

    svc->state = INIT_SVC_STARTING;

    /*
     * 实际实现中：
     * 1. 调用 sys_process_create() 创建服务进程
     * 2. 通过 PathManager 注册服务端点
     * 3. 等待服务就绪通知
     */

    svc->state = INIT_SVC_RUNNING;
    svc->start_time = 0ULL;
    s_active_count++;

    return KERNEL_OK;
}

/**
 * @brief 按依赖顺序启动所有服务
 *
 * @details 使用拓扑排序算法，按依赖关系依次启动服务
 */
static void init_start_all_services(void)
{
    uint32_t i;
    bool progress;
    uint32_t iterations;

    iterations = 0U;

    for (;;)
    {
        progress = false;

        for (i = 0U; i < INIT_MAX_SERVICES; i++)
        {
            if (!s_services[i].in_use)
            {
                continue;
            }

            if (s_services[i].state != INIT_SVC_STOPPED)
            {
                continue;
            }

            if (init_deps_satisfied(i))
            {
                (void)init_start_service(i);
                progress = true;
            }
        }

        if (!progress)
        {
            break;
        }

        iterations++;
        if (iterations > INIT_MAX_SERVICES)
        {
            break;
        }
    }
}

/* ========================================================================
 * 服务监控和自动重启
 * ======================================================================== */

/**
 * @brief 处理服务失败
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

    switch (svc->policy)
    {
        case INIT_POLICY_CRITICAL:
            /* 关键服务失败，系统停止 */
            s_system_state = SYSTEM_STATE_SHUTTING_DOWN;
            init_print("[init] CRITICAL service failed, shutting down\n");
            break;

        case INIT_POLICY_RESTART:
            if (svc->restart_count < svc->max_restarts)
            {
                svc->state = INIT_SVC_RESTARTING;
                svc->restart_count++;

                /* 延迟后重新启动 */
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
            }
            break;

        case INIT_POLICY_OPTIONAL:
        default:
            svc->state = INIT_SVC_FAILED;
            break;
    }
}

/**
 * @brief 服务健康监控循环
 */
static void init_monitor_services(void)
{
    uint32_t i;

    for (i = 0U; i < INIT_MAX_SERVICES; i++)
    {
        if (!s_services[i].in_use)
        {
            continue;
        }

        if (s_services[i].state == INIT_SVC_FAILED)
        {
            init_handle_failure(i);
        }
    }
}

/* ========================================================================
 * 系统状态报告
 * ======================================================================== */

/**
 * @brief 获取系统状态报告
 *
 * @param total      总服务数输出
 * @param running    运行中服务数输出
 * @param failed     失败服务数输出
 * @param sys_state  系统状态输出
 */
static void init_get_report(uint32_t *total, uint32_t *running,
                              uint32_t *failed, system_state_t *sys_state)
{
    uint32_t i;
    uint32_t t = 0U;
    uint32_t r = 0U;
    uint32_t f = 0U;

    for (i = 0U; i < INIT_MAX_SERVICES; i++)
    {
        if (s_services[i].in_use)
        {
            t++;
            if (s_services[i].state == INIT_SVC_RUNNING)
            {
                r++;
            }
            if (s_services[i].state == INIT_SVC_FAILED)
            {
                f++;
            }
        }
    }

    if (total != NULL)
    {
        *total = t;
    }
    if (running != NULL)
    {
        *running = r;
    }
    if (failed != NULL)
    {
        *failed = f;
    }
    if (sys_state != NULL)
    {
        *sys_state = s_system_state;
    }
}

/* ========================================================================
 * 服务注册表初始化
 * ======================================================================== */

/**
 * @brief 注册所有系统服务
 */
static void init_register_all_services(void)
{
    /*
     * 服务启动依赖图：
     *
     * path_manager (0) ← mem_manager (1) ← proc_manager (2)
     *                                      ← dev_manager (3)
     *                   ← fs_service (4) ← net_service (5)
     *                   ← security (6) ← vmm_service (7)
     */

    /* 0: PathManager - 无依赖 */
    (void)init_register_service("path_manager", SERVICE_PATH_MANAGER,
                                  INIT_POLICY_CRITICAL, NULL, 0U);

    /* 1: MemoryManager - 依赖 PathManager */
    {
        uint32_t deps[1U] = {0U};
        (void)init_register_service("mem_manager", SERVICE_MEM_MANAGER,
                                      INIT_POLICY_CRITICAL, deps, 1U);
    }

    /* 2: ProcessManager - 依赖 MemoryManager */
    {
        uint32_t deps[1U] = {1U};
        (void)init_register_service("proc_manager", SERVICE_PROC_MANAGER,
                                      INIT_POLICY_CRITICAL, deps, 1U);
    }

    /* 3: DeviceManager - 依赖 MemoryManager */
    {
        uint32_t deps[1U] = {1U};
        (void)init_register_service("dev_manager", SERVICE_DEV_MANAGER,
                                      INIT_POLICY_RESTART, deps, 1U);
    }

    /* 4: Filesystem - 依赖 MemoryManager */
    {
        uint32_t deps[1U] = {1U};
        (void)init_register_service("fs_service", 0x0005U,
                                      INIT_POLICY_RESTART, deps, 1U);
    }

    /* 5: Network - 依赖 FS */
    {
        uint32_t deps[2U] = {1U, 4U};
        (void)init_register_service("net_service", 0x0006U,
                                      INIT_POLICY_RESTART, deps, 2U);
    }

    /* 6: Security - 依赖 MemoryManager */
    {
        uint32_t deps[1U] = {1U};
        (void)init_register_service("security", 0x0007U,
                                      INIT_POLICY_CRITICAL, deps, 1U);
    }

    /* 7: VMM - 依赖 Security + MemoryManager */
    {
        uint32_t deps[2U] = {1U, 6U};
        (void)init_register_service("vmm_service", 0x0008U,
                                      INIT_POLICY_OPTIONAL, deps, 2U);
    }
}

/* ========================================================================
 * init 服务入口
 * ======================================================================== */

int main(void)
{
    /* 打印启动信息 */
    init_print("[init] AISafeOS64 init service started\n");

    /* 初始化服务注册表 */
    (void)memset(s_services, 0, sizeof(s_services));
    s_system_state = SYSTEM_STATE_BOOTING;
    s_active_count = 0U;
    s_initialized = true;

    /* 注册所有服务 */
    init_register_all_services();

    /* 检查依赖图 */
    if (init_has_dependency_cycle())
    {
        init_print("[init] ERROR: dependency cycle detected\n");
        s_system_state = SYSTEM_STATE_SHUTTING_DOWN;
        for (;;)
        {
            sys_thread_yield();
        }
        return 1;
    }

    /* 按依赖顺序启动所有服务 */
    init_print("[init] Starting services...\n");
    init_start_all_services();
    s_system_state = SYSTEM_STATE_RUNNING;
    init_print("[init] System running\n");

    /* 主循环：监控服务健康 */
    for (;;)
    {
        init_monitor_services();

        if (s_system_state == SYSTEM_STATE_SHUTTING_DOWN)
        {
            init_print("[init] System shutting down\n");
            break;
        }

        sys_thread_yield();
    }

    return 0;
}
