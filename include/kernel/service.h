/**
 * @file    service.h
 * @brief   用户态核心服务接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了用户态核心服务（ProcessManager、MemoryManager、
 *          PathManager）的接口和数据结构。
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-024, API-001~004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_SERVICE_H
#define KERNEL_SERVICE_H

#include <kernel/types.h>
#include <kernel/kobject.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 服务协议号
 * ======================================================================== */

/** @brief 进程管理器服务协议号 */
#define SERVICE_PROC_MANAGER       0x0001U

/** @brief 内存管理器服务协议号 */
#define SERVICE_MEM_MANAGER        0x0002U

/** @brief 路径管理器服务协议号 */
#define SERVICE_PATH_MANAGER       0x0003U

/** @brief 设备管理器服务协议号 */
#define SERVICE_DEV_MANAGER        0x0004U

/** @brief 文件系统服务协议号 */
#define SERVICE_FS_MANAGER         0x0005U

/** @brief 网络服务协议号 */
#define SERVICE_NET_MANAGER        0x0006U

/** @brief 安全服务协议号 */
#define SERVICE_SECURITY_MANAGER   0x0007U

/** @brief VMM 服务协议号 */
#define SERVICE_VMM_MANAGER        0x0008U

/* ========================================================================
 * 进程管理器消息类型
 * ======================================================================== */

/** @brief 创建进程 */
#define PROC_MSG_CREATE            0x0010U

/** @brief 销毁进程 */
#define PROC_MSG_DESTROY           0x0011U

/** @brief 获取进程信息 */
#define PROC_MSG_GET_INFO          0x0012U

/** @brief 列举进程 */
#define PROC_MSG_LIST              0x0013U

/* ========================================================================
 * 内存管理器消息类型
 * ======================================================================== */

/** @brief 分配物理页 */
#define MEM_MSG_ALLOC_PAGE         0x0020U

/** @brief 释放物理页 */
#define MEM_MSG_FREE_PAGE          0x0021U

/** @brief 映射虚拟地址 */
#define MEM_MSG_MAP                0x0022U

/** @brief 解除映射 */
#define MEM_MSG_UNMAP              0x0023U

/** @brief 共享内存创建 */
#define MEM_MSG_SHARE_CREATE       0x0024U

/** @brief 共享内存映射 */
#define MEM_MSG_SHARE_MAP          0x0025U

/* ========================================================================
 * 路径管理器消息类型
 * ======================================================================== */

/** @brief 注册服务 */
#define PATH_MSG_REGISTER          0x0030U

/** @brief 注销服务 */
#define PATH_MSG_UNREGISTER        0x0031U

/** @brief 查找服务 */
#define PATH_MSG_LOOKUP            0x0032U

/** @brief 枚举路径 */
#define PATH_MSG_ENUMERATE         0x0033U

/* ========================================================================
 * 进程描述符
 * ======================================================================== */

/** @brief 最大进程数 */
#define MAX_PROCESSES              64U

/** @brief 进程名最大长度 */
#define PROC_NAME_MAX              32U

/** @brief 进程最大线程数 */
#define PROC_MAX_THREADS           32U

/**
 * @brief 进程状态
 */
typedef enum
{
    PROC_STATE_EMPTY = 0U,       /**< @brief 空槽 */
    PROC_STATE_LOADING,          /**< @brief 加载中 */
    PROC_STATE_RUNNING,          /**< @brief 运行中 */
    PROC_STATE_BLOCKED,          /**< @brief 阻塞 */
    PROC_STATE_ZOMBIE            /**< @brief 僵尸态（等待父进程回收） */
} proc_state_t;

/**
 * @brief 进程描述符
 */
typedef struct
{
    uint32_t        pid;              /**< @brief 进程 ID */
    uint32_t        parent_pid;       /**< @brief 父进程 ID */
    proc_state_t    state;            /**< @brief 进程状态 */
    uint32_t        thread_count;     /**< @brief 线程计数 */
    char            name[PROC_NAME_MAX]; /**< @brief 进程名 */
    kobj_id_t       cspace_id;        /**< @brief CSpace 对象 ID */
    kobj_id_t       vspace_id;        /**< @brief VMSpace 对象 ID */
    kobj_id_t       endpoint_id;      /**< @brief IPC 端点 ID */
} process_desc_t;

/* ========================================================================
 * 路径条目
 * ======================================================================== */

/** @brief 路径名最大长度 */
#define PATH_NAME_MAX              64U

/**
 * @brief 路径条目类型
 */
typedef enum
{
    PATH_TYPE_SERVICE = 0U,      /**< @brief 服务路径 */
    PATH_TYPE_DEVICE,            /**< @brief 设备路径 */
    PATH_TYPE_MOUNT_POINT        /**< @brief 挂载点 */
} path_type_t;

/**
 * @brief 路径条目
 */
typedef struct
{
    char            path[PATH_NAME_MAX]; /**< @brief 路径名 */
    path_type_t     type;           /**< @brief 条目类型 */
    uint32_t        service_id;     /**< @brief 服务/设备 ID */
    kobj_id_t       endpoint_id;    /**< @brief 服务端点 ID */
    uint32_t        flags;          /**< @brief 标志 */
} path_entry_t;

#endif /* KERNEL_SERVICE_H */
