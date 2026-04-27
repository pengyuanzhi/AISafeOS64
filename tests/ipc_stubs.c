/**
 * @file    ipc_stubs.c
 * @brief   用户态服务 IPC 测试桩函数
 * @version 1.0
 *
 * @details 为用户提供态服务测试框架的 IPC 桩函数实现。
 *          这些桩函数模拟用户态服务的 IPC 调用，
 *          用于测试服务之间的通信。
 *
 * @note MISRA-C:2012 合规
 * @note 这些是测试桩函数，仅用于宿主机测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * IPC 消息类型定义
 * ======================================================================== */

/** @brief IPC 消息类型 */
typedef enum
{
    IPC_MSG_REQ = 0,    /**< @brief 请求消息 */
    IPC_MSG_REP = 1,    /**< @brief 回复消息 */
    IPC_MSG_PULSE = 2   /**< @brief Pulse 消息 */
} ipc_msg_type_t;

/** @brief 服务类型 */
typedef enum
{
    SVC_FS = 0,       /**< @brief 文件系统服务 */
    SVC_PROC = 1,      /**< @brief 进程管理服务 */
    SVC_MEM = 2,       /**< @brief 内存服务 */
    SVC_NET = 3,       /**< @brief 网络协议栈服务 */
    SVC_PATH = 4,      /**< @brief 路径服务 */
    SVC_INIT = 5,       /**< @brief 初始化服务 */
    SVC_SECURITY = 6,   /**< @brief 安全服务 */
    SVC_VMM = 7        /**< @brief 虚拟内存管理服务 */
} service_type_t;

/** @brief 文件系统请求类型 */
typedef enum
{
    FS_REQ_OPEN = 0,
    FS_REQ_CLOSE,
    FS_REQ_READ,
    FS_REQ_WRITE,
    FS_REQ_MKDIR,
    FS_REQ_UNLINK,
    FS_REQ_LSTAT
} fs_req_type_t;

/** @brief 进程管理请求类型 */
typedef enum
{
    PROC_REQ_CREATE = 0,
    PROC_REQ_DESTROY,
    PROC_REQ_STATUS,
    PROC_REQ_SET_PRIORITY
} proc_req_type_t;

/** @brief 内存服务请求类型 */
typedef enum
{
    MEM_REQ_STATS = 0,
    MEM_REQ_MAP_QUERY,
    MEM_REQ_SET_LIMITS
} mem_req_type_t;

/** @brief 网络服务请求类型 */
typedef enum
{
    NET_REQ_SOCKET_CREATE = 0,
    NET_REQ_TCP_CONNECT,
    NET_REQ_UDP_SEND_RECV
} net_req_type_t;

/** @brief 路径服务请求类型 */
typedef enum
{
    PATH_REQ_REGISTER = 0,
    PATH_REQ_DISCOVER,
    PATH_REQ_RESOLVE
} path_req_type_t;

/** @brief 初始化服务请求类型 */
typedef enum
{
    INIT_REQ_STARTUP = 0,
    INIT_REQ_DEPENDENCY_GRAPH,
    INIT_REQ_AUTO_RESTART
} init_req_type_t;

/** @brief 安全服务请求类型 */
typedef enum
{
    SECURITY_REQ_CAP_VERIFY = 0,
    SECURITY_REQ_ACCESS_CONTROL,
    SECURITY_REQ_AUDIT_LOG
} security_req_type_t;

/** @brief 虚拟内存管理请求类型 */
typedef enum
{
    VMM_REQ_CREATE = 0,
    VMM_REQ_MAP,
    VMM_REQ_VIRTUALIZATION
} vmm_req_type_t;

/* ========================================================================
 * IPC 消息结构
 * ======================================================================== */

/** @brief IPC 消息头部 */
typedef struct
{
    uint32_t type;        /**< @brief 消息类型（ipc_msg_type_t） */
    uint32_t service;     /**< @brief 目标服务类型（service_type_t） */
    uint32_t req_type;    /**< @brief 请求类型（服务特定） */
    uint32_t length;      /**< @brief 消息数据长度（字节） */
} ipc_msg_header_t;

/** @brief IPC 消息数据（最大 256 字节） */
#define IPC_MSG_DATA_MAX  256U

typedef struct
{
    ipc_msg_header_t header;
    uint8_t data[IPC_MSG_DATA_MAX];
} ipc_msg_t;

/* ========================================================================
 * 桩函数状态
 * ======================================================================== */

/** @brief IPC 初始化状态 */
static bool s_ipc_initialized = false;

/** @brief 服务可用状态 */
static bool s_service_available[8] =
{
    true,  /* FS */
    true,  /* PROC */
    true,  /* MEM */
    true,  /* NET */
    true,  /* PATH */
    true,  /* INIT */
    true,  /* SECURITY */
    true   /* VMM */
};

/* ========================================================================
 * IPC 桩函数 API
 * ======================================================================== */

/**
 * @brief 初始化 IPC 子系统
 *
 * @return 0 表示成功，负值表示失败
 */
int ipc_stubs_init(void)
{
    if (s_ipc_initialized)
    {
        return 0;  /* 已经初始化 */
    }

    s_ipc_initialized = true;
    return 0;
}

/**
 * @brief 发送 IPC 消息
 *
 * @param service 目标服务类型
 * @param req_type 请求类型
 * @param data 消息数据（可为 NULL）
 * @param data_len 数据长度
 * @param reply 回复缓冲区（可为 NULL）
 * @param reply_len 回复缓冲区大小
 *
 * @return 0 表示成功，负值表示失败
 */
int ipc_stubs_send(service_type_t service,
                   uint32_t req_type,
                   const void *data,
                   size_t data_len,
                   void *reply,
                   size_t reply_len)
{
    /* 检查参数 */
    if ((int)service < 0 || service > SVC_VMM)
    {
        return -1;
    }

    /* 检查服务是否可用 */
    if (!s_service_available[service])
    {
        return -2;
    }

    /* 检查数据长度 */
    if (data_len > IPC_MSG_DATA_MAX)
    {
        return -3;
    }

    /* TODO: 实际发送 IPC 消息（目前只是模拟） */
    printf("  [IPC] 发送: service=%d, req_type=%d, data_len=%zu\n",
           service, req_type, data_len);

    return 0;
}

/**
 * @brief 接收 IPC 消息
 *
 * @param msg 消息缓冲区
 * @param msg_len 缓冲区大小
 *
 * @return 0 表示成功，负值表示失败
 */
int ipc_stubs_receive(ipc_msg_t *msg, size_t msg_len)
{
    /* 检查参数 */
    if (msg == NULL)
    {
        return -1;
    }

    if (msg_len < sizeof(ipc_msg_header_t))
    {
        return -2;
    }

    /* TODO: 实际接收 IPC 消息（目前只是模拟） */
    return 0;
}

/**
 * @brief 设置服务可用状态
 *
 * @param service 服务类型
 * @param available 是否可用
 */
void ipc_stubs_set_service_available(service_type_t service, bool available)
{
    if ((int)service >= 0 && service <= SVC_VMM)
    {
        s_service_available[service] = available;
    }
}

/**
 * @brief 反初始化 IPC 子系统
 *
 * @return 0 表示成功
 */
int ipc_stubs_cleanup(void)
{
    s_ipc_initialized = false;
    return 0;
}
