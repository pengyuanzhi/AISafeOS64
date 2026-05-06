/**
 * @file    vmm_ipc_types.h
 * @brief   VMM 服务 IPC 消息类型定义
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details VMM 服务与客户端之间的 IPC 消息类型定义
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_VMM_VMM_IPC_TYPES_H
#define SERVICES_VMM_VMM_IPC_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * VMM IPC 消息类型
 * ======================================================================== */

/** @brief 创建虚拟机 */
#define VMM_IPC_CREATE_VM       1U

/** @brief 销毁虚拟机 */
#define VMM_IPC_DESTROY_VM      2U

/** @brief 启动虚拟机 */
#define VMM_IPC_START_VM        3U

/** @brief 停止虚拟机 */
#define VMM_IPC_STOP_VM         4U

/** @brief 暂停虚拟机 */
#define VMM_IPC_PAUSE_VM        5U

/** @brief 恢复虚拟机 */
#define VMM_IPC_RESUME_VM       6U

/** @brief 创建 vCPU */
#define VMM_IPC_CREATE_VCPU     7U

/** @brief 销毁 vCPU */
#define VMM_IPC_DESTROY_VCPU    8U

/** @brief 暂停 vCPU */
#define VMM_IPC_PAUSE_VCPU      9U

/** @brief 运行 vCPU */
#define VMM_IPC_RUN_VCPU        10U

/** @brief 列出所有虚拟机 */
#define VMM_IPC_LIST_VMS        11U

/** @brief 获取虚拟机信息 */
#define VMM_IPC_GET_VM_INFO     12U

/** @brief 获取虚拟机统计信息 */
#define VMM_IPC_GET_VM_STATS    13U

/** @brief 注入中断 */
#define VMM_IPC_INJECT_IRQ      14U

/** @brief 清除中断 */
#define VMM_IPC_CLEAR_IRQ       15U

/* ========================================================================
 * VMM IPC 消息头
 * ======================================================================== */

/**
 * @brief VMM IPC 消息头
 */
typedef struct
{
    uint32_t msg_type;      /**< @brief 消息类型 */
    uint32_t msg_id;        /**< @brief 消息 ID（用于匹配请求/响应） */
    uint32_t status;        /**< @brief 状态码（仅响应） */
    uint32_t reserved;      /**< @brief 保留 */
} vmm_ipc_msg_header_t;

/* ========================================================================
 * VMM IPC 请求/响应消息
 * ======================================================================== */

/**
 * @brief VMM 创建虚拟机请求
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    char name[32];          /**< @brief VM 名称 */
    uint64_t mem_size;      /**< @brief Guest 物理内存大小 */
    uint32_t num_vcpus;     /**< @brief vCPU 数量 */
} vmm_ipc_create_vm_req_t;

/**
 * @brief VMM 创建虚拟机响应
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    int32_t result;         /**< @brief 结果（>=0 为 VM ID，<0 为错误码） */
} vmm_ipc_create_vm_resp_t;

/**
 * @brief VMM 销毁虚拟机请求
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    uint32_t vm_id;         /**< @brief VM ID */
} vmm_ipc_destroy_vm_req_t;

/**
 * @brief VMM 销毁虚拟机响应
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    int32_t result;         /**< @brief 结果（0 成功，<0 错误码） */
} vmm_ipc_destroy_vm_resp_t;

/**
 * @brief VMM 启动虚拟机请求
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    uint32_t vm_id;         /**< @brief VM ID */
} vmm_ipc_start_vm_req_t;

/**
 * @brief VMM 启动虚拟机响应
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    int32_t result;         /**< @brief 结果（0 成功，<0 错误码） */
} vmm_ipc_start_vm_resp_t;

/**
 * @brief VMM 停止虚拟机请求
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    uint32_t vm_id;         /**< @brief VM ID */
} vmm_ipc_stop_vm_req_t;

/**
 * @brief VMM 停止虚拟机响应
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    int32_t result;         /**< @brief 结果（0 成功，<0 错误码） */
} vmm_ipc_stop_vm_resp_t;

/**
 * @brief VMM 暂停虚拟机请求
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    uint32_t vm_id;         /**< @brief VM ID */
} vmm_ipc_pause_vm_req_t;

/**
 * @brief VMM 暂停虚拟机响应
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    int32_t result;         /**< @brief 结果（0 成功，<0 错误码） */
} vmm_ipc_pause_vm_resp_t;

/**
 * @brief VMM 恢复虚拟机请求
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    uint32_t vm_id;         /**< @brief VM ID */
} vmm_ipc_resume_vm_req_t;

/**
 * @brief VMM 恢复虚拟机响应
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    int32_t result;         /**< @brief 结果（0 成功，<0 错误码） */
} vmm_ipc_resume_vm_resp_t;

/**
 * @brief VMM 创建 vCPU 请求
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    uint32_t vm_id;         /**< @brief VM ID */
    uint64_t entry_point;  /**< @brief 入口点地址 */
} vmm_ipc_create_vcpu_req_t;

/**
 * @brief VMM 创建 vCPU 响应
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    int32_t result;         /**< @brief 结果（>=0 为 vCPU ID，<0 为错误码） */
} vmm_ipc_create_vcpu_resp_t;

/**
 * @brief VMM 销毁 vCPU 请求
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    uint32_t vm_id;         /**< @brief VM ID */
    uint32_t vcpu_id;       /**< @brief vCPU ID */
} vmm_ipc_destroy_vcpu_req_t;

/**
 * @brief VMM 销毁 vCPU 响应
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    int32_t result;         /**< @brief 结果（0 成功，<0 错误码） */
} vmm_ipc_destroy_vcpu_resp_t;

/**
 * @brief VMM 暂停 vCPU 请求
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    uint32_t vm_id;         /**< @brief VM ID */
    uint32_t vcpu_id;       /**< @brief vCPU ID */
} vmm_ipc_pause_vcpu_req_t;

/**
 * @brief VMM 暂停 vCPU 响应
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    int32_t result;         /**< @brief 结果（0 成功，<0 错误码） */
} vmm_ipc_pause_vcpu_resp_t;

/**
 * @brief VMM 运行 vCPU 请求
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    uint32_t vm_id;         /**< @brief VM ID */
    uint32_t vcpu_id;       /**< @brief vCPU ID */
} vmm_ipc_run_vcpu_req_t;

/**
 * @brief VMM 运行 vCPU 响应
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    int32_t result;         /**< @brief 结果（0 成功，<0 错误码） */
} vmm_ipc_run_vcpu_resp_t;

/**
 * @brief VMM 列出所有虚拟机请求
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
} vmm_ipc_list_vms_req_t;

/**
 * @brief VMM 虚拟机信息
 */
typedef struct
{
    uint32_t vm_id;         /**< @brief VM ID */
    uint32_t state;         /**< @brief VM 状态 */
    bool active;            /**< @brief 活跃标志 */
    char name[32];           /**< @brief VM 名称 */
    uint64_t mem_size;      /**< @brief Guest 物理内存大小 */
    uint32_t vcpu_count;    /**< @brief vCPU 数量 */
} vmm_ipc_vm_info_t;

/**
 * @brief VMM 列出所有虚拟机响应
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    int32_t result;         /**< @brief 结果（VM 数量，<0 错误码） */
    uint32_t num_vms;       /**< @brief 虚拟机数量 */
    vmm_ipc_vm_info_t vms[4]; /**< @brief 虚拟机信息数组（最多 4 个） */
} vmm_ipc_list_vms_resp_t;

/**
 * @brief VMM 获取虚拟机信息请求
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    uint32_t vm_id;         /**< @brief VM ID */
} vmm_ipc_get_vm_info_req_t;

/**
 * @brief VMM 获取虚拟机信息响应
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    int32_t result;         /**< @brief 结果（0 成功，<0 错误码） */
    vmm_ipc_vm_info_t vm_info; /**< @brief 虚拟机信息 */
} vmm_ipc_get_vm_info_resp_t;

/**
 * @brief VMM 获取虚拟机统计信息请求
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    uint32_t vm_id;         /**< @brief VM ID */
} vmm_ipc_get_vm_stats_req_t;

/**
 * @brief VMM 虚拟机统计信息
 */
typedef struct
{
    uint64_t exit_count;    /**< @brief VM 退出次数 */
    uint64_t irq_count;     /**< @brief 中断注入次数 */
    uint64_t mmio_count;    /**< @brief MMIO 访问次数 */
    uint64_t hypercall_count; /**< @brief Hypercall 次数 */
} vmm_ipc_vm_stats_t;

/**
 * @brief VMM 获取虚拟机统计信息响应
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    int32_t result;         /**< @brief 结果（0 成功，<0 错误码） */
    vmm_ipc_vm_stats_t vm_stats; /**< @brief 虚拟机统计信息 */
} vmm_ipc_get_vm_stats_resp_t;

/**
 * @brief VMM 注入中断请求
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    uint32_t vm_id;         /**< @brief VM ID */
    uint32_t vcpu_id;       /**< @brief vCPU ID */
    uint32_t irq;           /**< @brief 中断号 */
} vmm_ipc_inject_irq_req_t;

/**
 * @brief VMM 注入中断响应
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    int32_t result;         /**< @brief 结果（0 成功，<0 错误码） */
} vmm_ipc_inject_irq_resp_t;

/**
 * @brief VMM 清除中断请求
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    uint32_t vm_id;         /**< @brief VM ID */
    uint32_t vcpu_id;       /**< @brief vCPU ID */
    uint32_t irq;           /**< @brief 中断号 */
} vmm_ipc_clear_irq_req_t;

/**
 * @brief VMM 清除中断响应
 */
typedef struct
{
    vmm_ipc_msg_header_t header;
    int32_t result;         /**< @brief 结果（0 成功，<0 错误码） */
} vmm_ipc_clear_irq_resp_t;

#endif /* SERVICES_VMM_VMM_IPC_TYPES_H */
