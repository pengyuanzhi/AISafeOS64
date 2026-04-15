/**
 * @file    main.c
 * @brief   PathManager 路径管理器服务（完整版）
 * @author  AISafe64 Team
 * @date    2026-04-14
 * @version 4.0
 *
 * @details 用户态路径管理器：
 *          - 服务注册/发现
 *          - 名字解析（服务名→IPC端点映射）
 *          - 服务健康检查
 *          - 设备路径命名空间
 *          - 挂载点管理
 *          - 服务依赖管理
 *          - 服务属性管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-024, API-001~006
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/service.h>
#include <kernel/config.h>
#include <kernel/syscall.h>
#include <kernel/errno.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* ========================================================================
 * IPC 消息协议（从 kernel/arch/arm64/entry.c 复制）
 * ======================================================================== */

/**
 * @brief 服务间 IPC 消息格式
 */
typedef struct
{
    uint32_t type;     /**< @brief 消息类型 */
    uint32_t len;      /**< @brief 数据长度 */
    uint64_t data[4];  /**< @brief 数据负载 */
} service_msg_t;

/* ========================================================================
 * 路径表常量
 * ======================================================================== */

/** @brief 最大路径条目数 */
#define MAX_PATH_ENTRIES    128U

/** @brief 最大服务健康检查记录数 */
#define MAX_HEALTH_RECORDS  64U

/** @brief 最大挂载数 */
#define MAX_MOUNTS         32U

/** @brief 健康检查超时（毫秒） */
#define HEALTH_CHECK_TIMEOUT_MS    5000U

/** @brief 最大依赖服务数 */
#define MAX_DEPENDENCIES    8U

/** @brief 最大设备路径数 */
#define MAX_DEVICE_PATHS   64U

/** @brief 设备路径最大长度 */
#define DEVICE_PATH_MAX    64U

/* ========================================================================
 * 挂载点管理
 * ======================================================================== */

/**
 * @brief 挂载点类型
 */
typedef enum
{
    MOUNT_TYPE_FS = 0U,       /**< @brief 文件系统挂载 */
    MOUNT_TYPE_DEV,           /**< @brief 设备挂载 */
    MOUNT_TYPE_PROC,          /**< @brief proc 挂载 */
    MOUNT_TYPE_SYS,           /**< @brief sys 挂载 */
    MOUNT_TYPE_TMP            /**< @brief tmp 挂载 */
} mount_type_t;

/**
 * @brief 挂载点条目
 */
typedef struct
{
    char            source[PATH_NAME_MAX]; /**< @brief 源路径 */
    char            target[PATH_NAME_MAX]; /**< @brief 目标路径 */
    mount_type_t    type;               /**< @brief 挂载类型 */
    uint32_t        flags;              /**< @brief 挂载标志 */
    kobj_id_t       fs_endpoint;        /**< @brief FS 服务端点 */
    bool            active;             /**< @brief 活跃标记 */
} mount_entry_t;

/* ========================================================================
 * 设备路径命名空间
 * ======================================================================== */

/**
 * @brief 设备路径条目
 */
typedef struct
{
    char            path[DEVICE_PATH_MAX]; /**< @brief 设备路径 */
    uint32_t        major;               /**< @brief 主设备号 */
    uint32_t        minor;               /**< @brief 次设备号 */
    uint32_t        flags;               /**< @brief 设备标志 */
    bool            active;              /**< @brief 活跃标记 */
} device_path_t;

/* ========================================================================
 * 服务健康状态
 * ======================================================================== */

/**
 * @brief 服务健康状态
 */
typedef enum
{
    HEALTH_UNKNOWN = 0U,    /**< @brief 未知状态 */
    HEALTH_HEALTHY,         /**< @brief 健康 */
    HEALTH_DEGRADED,        /**< @brief 降级 */
    HEALTH_UNHEALTHY,       /**< @brief 不健康 */
    HEALTH_DEAD             /**< @brief 已死亡 */
} health_state_t;

/**
 * @brief 服务健康检查记录
 */
typedef struct
{
    uint32_t      service_id;       /**< @brief 服务 ID */
    health_state_t state;           /**< @brief 健康状态 */
    uint64_t      last_heartbeat;   /**< @brief 最后心跳时间 */
    uint32_t      fail_count;       /**< @brief 连续失败计数 */
    uint32_t      check_interval_ms;/**< @brief 检查间隔（毫秒） */
    uint32_t      total_heartbeats; /**< @brief 总心跳数 */
    uint32_t      total_failures;   /**< @brief 总失败数 */
    bool          active;           /**< @brief 活跃标记 */
} health_record_t;

/* ========================================================================
 * 服务发现扩展
 * ======================================================================== */

/**
 * @brief 服务属性
 */
typedef struct
{
    uint32_t      version;          /**< @brief 服务版本号 */
    uint32_t      priority;         /**< @brief 优先级 */
    uint32_t      dependency_count; /**< @brief 依赖数量 */
    uint32_t      dependencies[MAX_DEPENDENCIES]; /**< @brief 依赖服务列表 */
    uint64_t      start_time;       /**< @brief 启动时间 */
    uint32_t      restart_count;    /**< @brief 重启次数 */
    uint32_t      pid;              /**< @brief 进程 ID */
    uint32_t      uptime;           /**< @brief 运行时间（秒） */
    uint32_t      flags;            /**< @brief 服务标志 */
} service_attrs_t;

/**
 * @brief 扩展路径条目
 */
typedef struct
{
    char            path[PATH_NAME_MAX]; /**< @brief 路径名 */
    path_type_t     type;           /**< @brief 条目类型 */
    uint32_t        service_id;     /**< @brief 服务/设备 ID */
    kobj_id_t       endpoint_id;    /**< @brief 服务端点 ID */
    uint32_t        flags;          /**< @brief 标志 */
    service_attrs_t attrs;          /**< @brief 服务属性 */
} ext_path_entry_t;

/* ========================================================================
 * 全局状态
 * ======================================================================== */

/** @brief 路径条目表 */
static ext_path_entry_t s_path_table[MAX_PATH_ENTRIES];

/** @brief 已注册路径计数 */
static uint32_t s_path_count;

/** @brief 挂载点表 */
static mount_entry_t s_mounts[MAX_MOUNTS];

/** @brief 已挂载数量 */
static uint32_t s_mount_count;

/** @brief 设备路径表 */
static device_path_t s_devices[MAX_DEVICE_PATHS];

/** @brief 设备路径计数 */
static uint32_t s_device_count;

/** @brief 健康检查记录 */
static health_record_t s_health[MAX_HEALTH_RECORDS];

/** @brief 初始化标志 */
static bool s_initialized;

/** @brief 服务启动时间（用于计算 uptime） */
static uint64_t s_start_time;

/** @brief 唯一服务 ID 生成器 */
static uint32_t s_next_service_id;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 路径名匹配
 */
static bool path_match(const char *a, const char *b)
{
    uint32_t j;

    for (j = 0U; j < PATH_NAME_MAX; j++)
    {
        if (a[j] != b[j])
        {
            return false;
        }
        if (a[j] == '\0')
        {
            return true;
        }
    }

    return true;
}

/**
 * @brief 生成唯一服务 ID
 */
static uint32_t generate_service_id(void)
{
    s_next_service_id++;
    return s_next_service_id;
}

/**
 * @brief 获取当前时间（简化版，使用 tick） */
static uint64_t get_current_time(void)
{
    return s_next_service_id;  /* 简化：使用计数器模拟时间 */
}

/* ========================================================================
 * 初始化
 * ======================================================================== */

static kernel_status_t path_init(void)
{
    uint32_t i;

    (void)memset(s_path_table, 0, sizeof(s_path_table));
    (void)memset(s_mounts, 0, sizeof(s_mounts));
    (void)memset(s_devices, 0, sizeof(s_devices));
    (void)memset(s_health, 0, sizeof(s_health));
    s_path_count = 0U;
    s_mount_count = 0U;
    s_device_count = 0U;
    s_initialized = true;
    s_start_time = get_current_time();
    s_next_service_id = 1U;

    /* 初始化默认挂载点 */
    for (i = 0U; i < MAX_MOUNTS; i++)
    {
        s_mounts[i].active = false;
    }

    /* 初始化默认设备路径 */
    for (i = 0U; i < MAX_DEVICE_PATHS; i++)
    {
        s_devices[i].active = false;
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 服务注册
 * ======================================================================== */

/**
 * @brief 注册服务到命名空间
 *
 * @param path        服务路径（如 "/services/proc_manager"）
 * @param type        路径类型
 * @param service_id  服务 ID（0 表示自动生成）
 * @param endpoint_id IPC 端点 ID
 * @param version     服务版本
 * @param priority    优先级
 *
 * @return KERNEL_OK 成功，负数表示错误
 */
kernel_status_t path_register_service(const char *path, path_type_t type,
                                        uint32_t service_id,
                                        kobj_id_t endpoint_id,
                                        uint32_t version,
                                        uint32_t priority)
{
    uint32_t i;
    uint32_t j;

    if (!s_initialized)
    {
        return -(int32_t)EINVAL;
    }

    if (path == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (s_path_count >= MAX_PATH_ENTRIES)
    {
        return -(int32_t)ENOMEM;
    }

    /* 检查重复 */
    for (i = 0U; i < MAX_PATH_ENTRIES; i++)
    {
        if (s_path_table[i].path[0] != '\0')
        {
            if (path_match(s_path_table[i].path, path))
            {
                return -(int32_t)EEXIST;
            }
        }
    }

    /* 生成唯一服务 ID（如果未提供） */
    if (service_id == 0U)
    {
        service_id = generate_service_id();
    }

    /* 找空槽 */
    for (i = 0U; i < MAX_PATH_ENTRIES; i++)
    {
        if (s_path_table[i].path[0] == '\0')
        {
            for (j = 0U; (j < (PATH_NAME_MAX - 1U)) && (path[j] != '\0'); j++)
            {
                s_path_table[i].path[j] = path[j];
            }
            s_path_table[i].path[j] = '\0';
            s_path_table[i].type = type;
            s_path_table[i].service_id = service_id;
            s_path_table[i].endpoint_id = endpoint_id;
            s_path_table[i].flags = 0U;
            s_path_table[i].attrs.version = version;
            s_path_table[i].attrs.priority = priority;
            s_path_table[i].attrs.dependency_count = 0U;
            s_path_table[i].attrs.start_time = get_current_time();
            s_path_table[i].attrs.restart_count = 0U;
            s_path_table[i].attrs.pid = 0U;
            s_path_table[i].attrs.uptime = 0U;
            s_path_table[i].attrs.flags = 0U;

            s_path_count++;

            /* 创建健康记录 */
            for (j = 0U; j < MAX_HEALTH_RECORDS; j++)
            {
                if (!s_health[j].active)
                {
                    s_health[j].service_id = service_id;
                    s_health[j].state = HEALTH_UNKNOWN;
                    s_health[j].last_heartbeat = get_current_time();
                    s_health[j].fail_count = 0U;
                    s_health[j].check_interval_ms = HEALTH_CHECK_TIMEOUT_MS;
                    s_health[j].total_heartbeats = 0U;
                    s_health[j].total_failures = 0U;
                    s_health[j].active = true;
                    break;
                }
            }

            return KERNEL_OK;
        }
    }

    return -(int32_t)ENOMEM;
}

/**
 * @brief 注册路径（兼容旧接口）
 */
static int32_t path_register(const char *path, path_type_t type,
                              uint32_t service_id, kobj_id_t endpoint_id)
{
    return (int32_t)path_register_service(path, type, service_id,
                                            endpoint_id, 1U, 128U);
}

/* ========================================================================
 * 注销路径
 * ======================================================================== */

/**
 * @brief 注销服务
 */
kernel_status_t path_unregister_service(const char *path)
{
    uint32_t i;
    uint32_t j;

    if (path == NULL)
    {
        return -(int32_t)EINVAL;
    }

    for (i = 0U; i < MAX_PATH_ENTRIES; i++)
    {
        if ((s_path_table[i].path[0] != '\0') &&
            path_match(s_path_table[i].path, path))
        {
            /* 移除健康记录 */
            for (j = 0U; j < MAX_HEALTH_RECORDS; j++)
            {
                if (s_health[j].active &&
                    (s_health[j].service_id == s_path_table[i].service_id))
                {
                    s_health[j].active = false;
                    break;
                }
            }

            (void)memset(&s_path_table[i], 0, sizeof(ext_path_entry_t));
            if (s_path_count > 0U)
            {
                s_path_count--;
            }

            return KERNEL_OK;
        }
    }

    return -(int32_t)ENOENT;
}

/* ========================================================================
 * 服务发现（名字解析）
 * ======================================================================== */

/**
 * @brief 按路径名查找服务
 *
 * @details 将服务路径名解析为 IPC 端点 ID
 *
 * @param path     服务路径
 * @param endpoint 输出端点 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t path_resolve(const char *path, kobj_id_t *endpoint)
{
    uint32_t i;

    if ((path == NULL) || (endpoint == NULL))
    {
        return -(int32_t)EINVAL;
    }

    for (i = 0U; i < MAX_PATH_ENTRIES; i++)
    {
        if ((s_path_table[i].path[0] != '\0') &&
            path_match(s_path_table[i].path, path))
        {
            *endpoint = s_path_table[i].endpoint_id;
            return KERNEL_OK;
        }
    }

    return -(int32_t)ENOENT;
}

/**
 * @brief 按类型枚举服务
 *
 * @param type     路径类型
 * @param results  输出结果数组
 * @param max_count 最大返回数
 * @param actual   实际返回数
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t path_enumerate(path_type_t type, kobj_id_t *results,
                                 uint32_t max_count, uint32_t *actual)
{
    uint32_t i;
    uint32_t count = 0U;

    if ((results == NULL) || (actual == NULL))
    {
        return -(int32_t)EINVAL;
    }

    for (i = 0U; (i < MAX_PATH_ENTRIES) && (count < max_count); i++)
    {
        if ((s_path_table[i].path[0] != '\0') &&
            (s_path_table[i].type == type))
        {
            results[count] = s_path_table[i].endpoint_id;
            count++;
        }
    }

    *actual = count;

    return KERNEL_OK;
}

/* ========================================================================
 * 挂载点管理
 * ======================================================================== */

/**
 * @brief 挂载文件系统
 *
 * @param source       源路径
 * @param target       目标路径
 * @param type         挂载类型
 * @param fs_endpoint  FS 服务端点
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t path_mount(const char *source, const char *target,
                              mount_type_t type, kobj_id_t fs_endpoint)
{
    uint32_t i;
    uint32_t j;

    if ((source == NULL) || (target == NULL))
    {
        return -(int32_t)EINVAL;
    }

    if (s_mount_count >= MAX_MOUNTS)
    {
        return -(int32_t)ENOMEM;
    }

    /* 找空槽 */
    for (i = 0U; i < MAX_MOUNTS; i++)
    {
        if (!s_mounts[i].active)
        {
            for (j = 0U; (j < (PATH_NAME_MAX - 1U)) && (source[j] != '\0'); j++)
            {
                s_mounts[i].source[j] = source[j];
            }
            s_mounts[i].source[j] = '\0';

            for (j = 0U; (j < (PATH_NAME_MAX - 1U)) && (target[j] != '\0'); j++)
            {
                s_mounts[i].target[j] = target[j];
            }
            s_mounts[i].target[j] = '\0';

            s_mounts[i].type = type;
            s_mounts[i].flags = 0U;
            s_mounts[i].fs_endpoint = fs_endpoint;
            s_mounts[i].active = true;

            s_mount_count++;

            return KERNEL_OK;
        }
    }

    return -(int32_t)ENOMEM;
}

/**
 * @brief 卸载文件系统
 *
 * @param target 目标路径
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t path_umount(const char *target)
{
    uint32_t i;

    if (target == NULL)
    {
        return -(int32_t)EINVAL;
    }

    for (i = 0U; i < MAX_MOUNTS; i++)
    {
        if (s_mounts[i].active && path_match(s_mounts[i].target, target))
        {
            (void)memset(&s_mounts[i], 0, sizeof(mount_entry_t));
            if (s_mount_count > 0U)
            {
                s_mount_count--;
            }

            return KERNEL_OK;
        }
    }

    return -(int32_t)ENOENT;
}

/**
 * @brief 列出挂载点
 *
 * @param results  输出结果数组
 * @param max_count 最大返回数
 * @param actual   实际返回数
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t path_list_mounts(mount_entry_t *results,
                                   uint32_t max_count, uint32_t *actual)
{
    uint32_t i;
    uint32_t count = 0U;

    if ((results == NULL) || (actual == NULL))
    {
        return -(int32_t)EINVAL;
    }

    for (i = 0U; (i < MAX_MOUNTS) && (count < max_count); i++)
    {
        if (s_mounts[i].active)
        {
            (void)memcpy(&results[count], &s_mounts[i], sizeof(mount_entry_t));
            count++;
        }
    }

    *actual = count;

    return KERNEL_OK;
}

/* ========================================================================
 * 设备路径命名空间
 * ======================================================================== */

/**
 * @brief 注册设备路径
 *
 * @param path   设备路径
 * @param major  主设备号
 * @param minor  次设备号
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t path_register_device(const char *path,
                                      uint32_t major, uint32_t minor)
{
    uint32_t i;
    uint32_t j;

    if (path == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (s_device_count >= MAX_DEVICE_PATHS)
    {
        return -(int32_t)ENOMEM;
    }

    /* 找空槽 */
    for (i = 0U; i < MAX_DEVICE_PATHS; i++)
    {
        if (!s_devices[i].active)
        {
            for (j = 0U; (j < (DEVICE_PATH_MAX - 1U)) && (path[j] != '\0'); j++)
            {
                s_devices[i].path[j] = path[j];
            }
            s_devices[i].path[j] = '\0';

            s_devices[i].major = major;
            s_devices[i].minor = minor;
            s_devices[i].flags = 0U;
            s_devices[i].active = true;

            s_device_count++;

            return KERNEL_OK;
        }
    }

    return -(int32_t)ENOMEM;
}

/**
 * @brief 解析设备路径
 *
 * @param path    设备路径
 * @param major   输出主设备号
 * @param minor   输出次设备号
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t path_resolve_device(const char *path,
                                     uint32_t *major, uint32_t *minor)
{
    uint32_t i;

    if ((path == NULL) || (major == NULL) || (minor == NULL))
    {
        return -(int32_t)EINVAL;
    }

    for (i = 0U; i < MAX_DEVICE_PATHS; i++)
    {
        if (s_devices[i].active && path_match(s_devices[i].path, path))
        {
            *major = s_devices[i].major;
            *minor = s_devices[i].minor;
            return KERNEL_OK;
        }
    }

    return -(int32_t)ENOENT;
}

/* ========================================================================
 * 服务健康检查
 * ======================================================================== */

/**
 * @brief 更新服务心跳
 *
 * @param service_id 服务 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t path_heartbeat(uint32_t service_id)
{
    uint32_t i;

    if (!s_initialized)
    {
        return -(int32_t)EINVAL;
    }

    for (i = 0U; i < MAX_HEALTH_RECORDS; i++)
    {
        if (s_health[i].active && (s_health[i].service_id == service_id))
        {
            s_health[i].last_heartbeat = get_current_time();
            s_health[i].fail_count = 0U;
            s_health[i].state = HEALTH_HEALTHY;
            s_health[i].total_heartbeats++;
            return KERNEL_OK;
        }
    }

    return -(int32_t)ENOENT;
}

/**
 * @brief 检查服务健康状态
 *
 * @param service_id 服务 ID
 * @param state      输出健康状态
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t path_check_health(uint32_t service_id, health_state_t *state)
{
    uint32_t i;

    if (state == NULL)
    {
        return -(int32_t)EINVAL;
    }

    for (i = 0U; i < MAX_HEALTH_RECORDS; i++)
    {
        if (s_health[i].active && (s_health[i].service_id == service_id))
        {
            *state = s_health[i].state;
            return KERNEL_OK;
        }
    }

    return -(int32_t)ENOENT;
}

/**
 * @brief 执行健康检查扫描
 *
 * @details 检查所有注册服务的心跳状态
 */
static void path_health_scan(void)
{
    uint32_t i;
    uint64_t current_time;

    current_time = get_current_time();

    for (i = 0U; i < MAX_HEALTH_RECORDS; i++)
    {
        if (!s_health[i].active)
        {
            continue;
        }

        /* 检查心跳超时（简化：使用 fail_count 模拟） */
        if (s_health[i].fail_count > 3U)
        {
            s_health[i].state = HEALTH_DEAD;
            s_health[i].total_failures++;
        }
        else if (s_health[i].fail_count > 1U)
        {
            s_health[i].state = HEALTH_UNHEALTHY;
            s_health[i].total_failures++;
        }
        else if (s_health[i].fail_count > 0U)
        {
            s_health[i].state = HEALTH_DEGRADED;
            s_health[i].total_failures++;
        }
        else
        {
            s_health[i].state = HEALTH_HEALTHY;
        }

        /* 更新 uptime */
        {
            uint32_t idx;
            for (idx = 0U; idx < MAX_PATH_ENTRIES; idx++)
            {
                if ((s_path_table[idx].path[0] != '\0') &&
                    (s_path_table[idx].service_id == s_health[i].service_id))
                {
                    s_path_table[idx].attrs.uptime =
                        (uint32_t)(current_time - s_path_table[idx].attrs.start_time);
                    break;
                }
            }
        }
    }
}

/* ========================================================================
 * 服务属性管理
 * ======================================================================== */

/**
 * @brief 设置服务依赖
 *
 * @param path            服务路径
 * @param dependencies    依赖服务 ID 数组
 * @param dep_count       依赖数量
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t path_set_dependencies(const char *path,
                                        const uint32_t *dependencies,
                                        uint32_t dep_count)
{
    uint32_t i;
    uint32_t j;

    if ((path == NULL) || (dependencies == NULL))
    {
        return -(int32_t)EINVAL;
    }

    if (dep_count > MAX_DEPENDENCIES)
    {
        return -(int32_t)EINVAL;
    }

    for (i = 0U; i < MAX_PATH_ENTRIES; i++)
    {
        if ((s_path_table[i].path[0] != '\0') &&
            path_match(s_path_table[i].path, path))
        {
            s_path_table[i].attrs.dependency_count = dep_count;
            for (j = 0U; j < dep_count; j++)
            {
                s_path_table[i].attrs.dependencies[j] = dependencies[j];
            }
            return KERNEL_OK;
        }
    }

    return -(int32_t)ENOENT;
}

/**
 * @brief 获取服务信息
 *
 * @param path     服务路径
 * @param endpoint 输出端点 ID
 * @param version  输出版本号
 * @param state    输出健康状态
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t path_get_service_info(const char *path,
                                        kobj_id_t *endpoint,
                                        uint32_t *version,
                                        health_state_t *state)
{
    uint32_t i;

    if (path == NULL)
    {
        return -(int32_t)EINVAL;
    }

    for (i = 0U; i < MAX_PATH_ENTRIES; i++)
    {
        if ((s_path_table[i].path[0] != '\0') &&
            path_match(s_path_table[i].path, path))
        {
            if (endpoint != NULL)
            {
                *endpoint = s_path_table[i].endpoint_id;
            }
            if (version != NULL)
            {
                *version = s_path_table[i].attrs.version;
            }
            if (state != NULL)
            {
                uint32_t j;
                *state = HEALTH_UNKNOWN;
                for (j = 0U; j < MAX_HEALTH_RECORDS; j++)
                {
                    if (s_health[j].active &&
                        (s_health[j].service_id == s_path_table[i].service_id))
                    {
                        *state = s_health[j].state;
                        break;
                    }
                }
            }
            return KERNEL_OK;
        }
    }

    return -(int32_t)ENOENT;
}

/* ========================================================================
 * IPC 消息处理
 * ======================================================================== */

/**
 * @brief 处理 REGISTER 消息
 */
static int32_t handle_register(const service_msg_t *req,
                               uint8_t *reply, uint32_t reply_size)
{
    (void)reply;
    (void)reply_size;

    /* 从请求中提取参数 */
    const char *path = (const char *)(uintptr_t)req->data[0];
    path_type_t type = (path_type_t)req->data[1];
    kobj_id_t endpoint_id = (kobj_id_t)req->data[2];
    uint32_t version = (uint32_t)req->data[3];

    /* 注册服务 */
    kernel_status_t ret = path_register_service(path, type, 0U, endpoint_id,
                                                    version, 128U);

    return (int32_t)ret;
}

/**
 * @brief 处理 UNREGISTER 消息
 */
static int32_t handle_unregister(const service_msg_t *req,
                                 uint8_t *reply, uint32_t reply_size)
{
    (void)reply;
    (void)reply_size;

    /* 从请求中提取参数 */
    const char *path = (const char *)(uintptr_t)req->data[0];

    /* 注销服务 */
    kernel_status_t ret = path_unregister_service(path);

    return (int32_t)ret;
}

/**
 * @brief 处理 LOOKUP 消息
 */
static int32_t handle_lookup(const service_msg_t *req,
                             uint8_t *reply, uint32_t reply_size)
{
    (void)reply_size;

    /* 从请求中提取参数 */
    const char *path = (const char *)(uintptr_t)req->data[0];

    /* 解析服务 */
    kobj_id_t endpoint_id;
    kernel_status_t ret = path_resolve(path, &endpoint_id);

    if (ret == KERNEL_OK && reply != NULL)
    {
        /* 返回端点 ID */
        kobj_id_t *result = (kobj_id_t *)(uintptr_t)reply;
        *result = endpoint_id;
    }

    return (int32_t)ret;
}

/**
 * @brief 处理 ENUMERATE 消息
 */
static int32_t handle_enumerate(const service_msg_t *req,
                                uint8_t *reply, uint32_t reply_size)
{
    (void)req;
    (void)reply_size;

    /* 枚举所有服务 */
    kobj_id_t results[MAX_PATH_ENTRIES];
    uint32_t actual;

    kernel_status_t ret = path_enumerate(PATH_TYPE_SERVICE, results,
                                            MAX_PATH_ENTRIES, &actual);

    if (ret == KERNEL_OK && reply != NULL)
    {
        /* 返回结果 */
        (void)memcpy(reply, results, actual * sizeof(kobj_id_t));
    }

    return (int32_t)ret;
}

/* ========================================================================
 * 服务主函数
 * ======================================================================== */

int main(void)
{
    int32_t ret;
    kobj_id_t my_endpoint;

    /* 初始化 */
    (void)path_init();

    /* 创建 endpoint */
    my_endpoint = syscall2(SYS_EP_CREATE, 0ULL, 0ULL);
    if (my_endpoint <= 0)
    {
        return -1;
    }

    /* 注册自己到服务发现（简化：使用固定 endpoint） */
    (void)path_register("/services/path_manager",
                          PATH_TYPE_SERVICE, 1000, my_endpoint);

    /* 主循环 */
    for (;;)
    {
        service_msg_t req;
        uint8_t reply[128U];
        uint32_t reply_size;

        /* 执行健康检查扫描 */
        path_health_scan();

        /* 接收请求 */
        ret = syscall3(SYS_MSG_RECV, my_endpoint,
                       (uint64_t)(uintptr_t)&req, sizeof(req));
        if (ret < 0)
        {
            continue;
        }

        /* 处理请求 */
        reply_size = 0U;

        switch (req.type)
        {
            case PATH_MSG_REGISTER:
                ret = handle_register(&req, reply, sizeof(reply));
                break;

            case PATH_MSG_UNREGISTER:
                ret = handle_unregister(&req, reply, sizeof(reply));
                break;

            case PATH_MSG_LOOKUP:
                ret = handle_lookup(&req, reply, sizeof(reply));
                reply_size = sizeof(kobj_id_t);
                break;

            case PATH_MSG_ENUMERATE:
                ret = handle_enumerate(&req, reply, sizeof(reply));
                reply_size = MAX_PATH_ENTRIES * sizeof(kobj_id_t);
                break;

            default:
                ret = -(int32_t)EINVAL;
                break;
        }

        /* 回复 */
        if (ret >= 0)
        {
            (void)syscall3(SYS_MSG_REPLY, my_endpoint,
                           (uint64_t)(uintptr_t)reply, reply_size);
        }
        else
        {
            /* 错误回复 */
            (void)syscall3(SYS_MSG_REPLY, my_endpoint, 0ULL, 0ULL);
        }
    }

    return 0;
}
