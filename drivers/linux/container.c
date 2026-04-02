/**
 * @file    container.c
 * @brief   Linux 驱动容器（LDC）管理框架
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details Linux 驱动容器管理：
 *          - 在用户态运行 Linux LTS 驱动
 *          - IC2 通道与原生服务通信
 *          - 设备路径注册与重定向
 *          - 驱动崩溃隔离与重启
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: DR-006~008
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/ipc_ic2.h>
#include <kernel/service.h>
#include <kernel/driver_framework.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 容器常量
 * ======================================================================== */

/** @brief 最大容器数量 */
#define LDC_MAX_CONTAINERS         4U

/** @brief 宸器名最大长度 */
#define LDC_NAME_MAX              32U

/** @brief 容器内最大设备数 */
#define LDC_MAX_DEVICES          8U

/** @brief 命令超时（毫秒） */
#define LDC_CMD_TIMEOUT_MS        5000U

/* ========================================================================
 * 容器状态
 * ======================================================================== */

/**
 * @brief Linux 驱动容器状态
 */
typedef enum
{
    LDC_STATE_STOPPED = 0U,   /**< @brief 已停止 */
    LDC_STATE_STARTING,       /**< @brief 启动中 */
    LDC_STATE_RUNNING,        /**< @brief 运行中 */
    LDC_STATE_CRASHED,        /**< @brief 已崩溃 */
    LDC_STATE_RESTARTING      /**< @brief 重启中 */
} ldc_state_t;

/**
 * @brief 容器内设备状态
 */
typedef enum
{
    LDC_DEV_UNUSED = 0U,     /**< @brief 未使用 */
    LDC_DEV_INITIALIZING,     /**< @brief 初始化中 */
    LDC_DEV_READY,            /**< @brief 就绪 */
    LDC_DEV_ERROR              /**< @brief 错误 */
} ldc_dev_state_t;

/* ========================================================================
 * 容器内设备描述符
 * ======================================================================== */

/**
 * @brief 容器内设备描述符
 */
typedef struct
{
    uint32_t          device_id;       /**< @brief 设备 ID */
    char              path[PATH_NAME_MAX]; /**< @brief 设备路径 */
    ldc_dev_state_t   state;          /**< @brief 设备状态 */
    uint32_t          ic2_channel;     /**< @brief 关联的 IC2 通道 ID */
    uint32_t          crash_count;     /**< @brief 崩溃计数 */
} ldc_device_t;

/* ========================================================================
 * Linux 驱动容器描述符
 * ======================================================================== */

/**
 * @brief Linux 驱动容器描述符
 */
typedef struct
{
    uint32_t        container_id;              /**< @brief 容器 ID */
    char            name[LDC_NAME_MAX];        /**< @brief 容器名称 */
    ldc_state_t     state;                     /**< @brief 容器状态 */
    uint32_t        linux_version;             /**< @brief Linux 内核版本 */
    uint32_t        pid;                       /**< @brief 容器进程 PID */
    ldc_device_t    devices[LDC_MAX_DEVICES]; /**< @brief 设备列表 */
    uint32_t        device_count;              /**< @brief 设备数量 */
    uint32_t        ic2_base_channel;         /**< @brief IC2 基础通道 ID */
    uint32_t        mem_pool_size;             /**< @brief 内存池大小（KB） */
    uint32_t        restart_count;             /**< @brief 重启计数 */
    bool            auto_restart;              /**< @brief 自动重启标志 */
} ldc_container_t;

/* ========================================================================
 * 全局状态
 * ======================================================================== */

/** @brief 容器实例池 */
static ldc_container_t s_containers[LDC_MAX_CONTAINERS];

/** @brief 容器使用标记 */
static bool s_container_used[LDC_MAX_CONTAINERS];

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 安全字符串复制
 */
static void ldc_strcpy(char *dst, const char *src, uint32_t n)
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

/* ========================================================================
 * 容器管理
 * ======================================================================== */

/**
 * @brief 创建 Linux 驱动容器
 *
 * @param name          容器名称
 * @param linux_version Linux 内核版本编码（如 0x050A00 = 5.10）
 * @param mem_pool_size 内存池大小（KB）
 * @param auto_restart  自动重启标志
 *
 * @return 成功返回容器 ID，失败返回负错误码
 */
int32_t ldc_create(const char *name, uint32_t linux_version,
                   uint32_t mem_pool_size, bool auto_restart)
{
    uint32_t i;
    ldc_container_t *c;

    if (name == NULL)
    {
        return -(int32_t)22; /* -EINVAL */
    }

    /* 查找空闲容器槽 */
    for (i = 0U; i < LDC_MAX_CONTAINERS; i++)
    {
        if (!s_container_used[i])
        {
            break;
        }
    }

    if (i >= LDC_MAX_CONTAINERS)
    {
        return -(int32_t)12; /* -ENOMEM */
    }

    c = &s_containers[i];

    (void)memset(c, 0, sizeof(ldc_container_t));

    c->container_id = i;
    ldc_strcpy(c->name, name, LDC_NAME_MAX);
    c->state = LDC_STATE_STOPPED;
    c->linux_version = linux_version;
    c->pid = 0U;
    c->device_count = 0U;
    c->ic2_base_channel = 0U;
    c->mem_pool_size = mem_pool_size;
    c->restart_count = 0U;
    c->auto_restart = auto_restart;

    s_container_used[i] = true;

    return (int32_t)i;
}

/**
 * @brief 启动容器
 *
 * @param container_id 容器 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t ldc_start(uint32_t container_id)
{
    ldc_container_t *c;

    if (container_id >= LDC_MAX_CONTAINERS)
    {
        return -(int32_t)22;
    }

    if (!s_container_used[container_id])
    {
        return -(int32_t)2;
    }

    c = &s_containers[container_id];

    if (c->state == LDC_STATE_RUNNING)
    {
        return KERNEL_OK;
    }

    c->state = LDC_STATE_STARTING;

    /*
     * 实际实现中：
     * 1. 分配容器进程地址空间
     * 2. 加载 Linux 内核镜像到容器内存
     * 3. 创建 IC2 通道用于通信
     * 4. 启动容器进程
     * 此处为框架实现
     */

    c->state = LDC_STATE_RUNNING;

    return KERNEL_OK;
}

/**
 * @brief 停止容器
 *
 * @param container_id 容器 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t ldc_stop(uint32_t container_id)
{
    ldc_container_t *c;
    uint32_t i;

    if (container_id >= LDC_MAX_CONTAINERS)
    {
        return -(int32_t)22;
    }

    c = &s_containers[container_id];

    if (c->state == LDC_STATE_STOPPED)
    {
        return KERNEL_OK;
    }

    /* 停止所有设备 */
    for (i = 0U; i < c->device_count; i++)
    {
        c->devices[i].state = LDC_DEV_UNUSED;
    }

    c->state = LDC_STATE_STOPPED;

    return KERNEL_OK;
}

/**
 * @brief 销毁容器
 *
 * @param container_id 容器 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t ldc_destroy(uint32_t container_id)
{
    if (container_id >= LDC_MAX_CONTAINERS)
    {
        return -(int32_t)22;
    }

    if (!s_container_used[container_id])
    {
        return -(int32_t)2;
    }

    /* 先停止 */
    (void)ldc_stop(container_id);

    s_container_used[container_id] = false;

    return KERNEL_OK;
}

/* ========================================================================
 * 设备管理
 * ======================================================================== */

/**
 * @brief 向容器添加设备
 *
 * @param container_id 容器 ID
 * @param device_path  设备路径
 *
 * @return 成功返回设备 ID，失败返回负错误码
 */
int32_t ldc_add_device(uint32_t container_id, const char *device_path)
{
    ldc_container_t *c;
    ldc_device_t *dev;
    uint32_t dev_id;

    if (container_id >= LDC_MAX_CONTAINERS)
    {
        return -(int32_t)22;
    }

    if (device_path == NULL)
    {
        return -(int32_t)22;
    }

    c = &s_containers[container_id];

    if (c->state == LDC_STATE_STOPPED)
    {
        return -(int32_t)22;
    }

    if (c->device_count >= LDC_MAX_DEVICES)
    {
        return -(int32_t)12;
    }

    dev_id = c->device_count;
    dev = &c->devices[dev_id];

    dev->device_id = dev_id;
    ldc_strcpy(dev->path, device_path, PATH_NAME_MAX);
    dev->state = LDC_DEV_INITIALIZING;
    dev->ic2_channel = 0U;
    dev->crash_count = 0U;

    c->device_count++;

    /* 创建 IC2 通道用于此设备的快速通信 */
    dev->ic2_channel = 0U; /* 实际通过 ic2_channel_create 创建 */

    dev->state = LDC_DEV_READY;

    return (int32_t)dev_id;
}

/* ========================================================================
 * I/O 重定向
 * ======================================================================== */

/**
 * @brief 通过容器执行 I/O 操作
 *
 * @details 将 I/O 请求通过 IC2 通道发送到 Linux 驱动容器，
 *          等待容器内 Linux 驱动处理后返回结果
 *
 * @param container_id 容器 ID
 * @param device_id    设备 ID
 * @param cmd          I/O 命令
 * @param buf          数据缓冲区
 * @param size         数据大小
 *
 * @return 实际传输字节数，负数表示错误
 */
int32_t ldc_io_request(uint32_t container_id, uint32_t device_id,
                        uint32_t cmd, void *buf, uint32_t size)
{
    ldc_container_t *c;
    ldc_device_t *dev;

    if (container_id >= LDC_MAX_CONTAINERS)
    {
        return -(int32_t)22;
    }

    c = &s_containers[container_id];

    if (c->state != LDC_STATE_RUNNING)
    {
        return -(int32_t)22;
    }

    if (device_id >= c->device_count)
    {
        return -(int32_t)22;
    }

    dev = &c->devices[device_id];

    if (dev->state != LDC_DEV_READY)
    {
        return -(int32_t)22;
    }

    /*
     * 实际实现中：
     * 1. 构造 I/O 请求包头（length/type/flags/seq）
     * 2. 通过 IC2 通道发送包头 + 数据
     * 3. 等待回复（同步或异步）
     * 4. 返回结果
     */
    (void)cmd;
    (void)buf;

    (void)cmd;
    (void)buf;

    return (int32_t)size;
}

/* ========================================================================
 * 崩溃处理
 * ======================================================================== */

/**
 * @brief 处理容器崩溃
 *
 * @param container_id 容器 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t ldc_handle_crash(uint32_t container_id)
{
    ldc_container_t *c;

    if (container_id >= LDC_MAX_CONTAINERS)
    {
        return -(int32_t)22;
    }

    c = &s_containers[container_id];

    c->state = LDC_STATE_CRASHED;

    /* 标记所有设备为错误状态 */
    uint32_t i;
    for (i = 0U; i < c->device_count; i++)
    {
        c->devices[i].state = LDC_DEV_ERROR;
        c->devices[i].crash_count++;
    }

    /* 自动重启 */
    if (c->auto_restart)
    {
        c->state = LDC_STATE_RESTARTING;
        c->restart_count++;
        (void)ldc_start(container_id);
    }

    return KERNEL_OK;
}

/**
 * @brief 获取容器描述符
 *
 * @param container_id 容器 ID
 *
 * @return 容器描述符指针
 */
ldc_container_t *ldc_get_container(uint32_t container_id)
{
    if (container_id >= LDC_MAX_CONTAINERS)
    {
        return NULL;
    }

    if (!s_container_used[container_id])
    {
        return NULL;
    }

    return &s_containers[container_id];
}
