/**
 * @file    vmm_server_ipc.c
 * @brief   VMM 服务 IPC 服务器端实现
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details VMM 服务 IPC 服务器端实现：
 *          - IPC 通道创建和管理
 *          - IPC 消息接收和分发
 *          - VM/vCPU 管理操作处理
 *          - 统计信息查询处理
 *
 * @note MISRA-C:2012 合规
 * @note 使用内核 IPC 系统调用
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "vmm.h"
#include "vmm_ipc_types.h"
#include "vmm_server_ipc.h"
#include <kernel/errno.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ========================================================================
 * 前向声明
 * ======================================================================== */

/**
 * @brief SVC 调用辅助函数（3 参数版本）
 */
static inline int64_t svc_call_3(uint64_t nr, int64_t a0, int64_t a1, int64_t a2);

/* ========================================================================
 * 内核 IPC 系统调用接口
 * ======================================================================== */

/** @brief IPC 通道 ID */
static uint64_t s_vmm_channel_id = 0U;

/** @brief IPC 连接 ID */
static uint64_t s_vmm_conn_id = 0U;

/** @brief VMM 服务初始化标志 */
static bool s_vmm_server_initialized = false;

/* 内核 IPC 系统调用号（与 include/kernel/syscall.h 一致） */
#define AISAFE_SYS_CHANNEL_CREATE       0x0100U
#define AISAFE_SYS_CHANNEL_DESTROY      0x0101U
#define AISAFE_SYS_CONNECT_ATTACH       0x0102U
#define AISAFE_SYS_MSG_SEND             0x0104U
#define AISAFE_SYS_MSG_RECV             0x0105U
#define AISAFE_SYS_MSG_REPLY            0x0106U

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief SVC 调用辅助宏（3 参数版本）
 *
 * @param nr   系统调用号
 * @param a0   参数 0
 * @param a1   参数 1
 * @param a2   参数 2
 *
 * @return 系统调用返回值
 */
static inline int64_t svc_call_3(uint64_t nr, int64_t a0, int64_t a1, int64_t a2)
{
    register uint64_t x8 __asm__("x8") = nr;
    register uint64_t x0 __asm__("x0") = (uint64_t)a0;
    register uint64_t x1 __asm__("x1") = (uint64_t)a1;
    register uint64_t x2 __asm__("x2") = (uint64_t)a2;

    __asm__ volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2)
        : "memory"
    );

    return (int64_t)x0;
}

/**
 * @brief 通过 SVC 调用内核 IPC 接收
 *
 * @param buf     接收缓冲区
 * @param size    缓冲区大小
 * @param msg_id_out 输出消息 ID
 *
 * @return 接收到的字节数，负数表示错误
 */
static int32_t vmm_ipc_recv(void *buf, uint64_t size, uint64_t *msg_id_out)
{
    int64_t ret;

    if (buf == NULL || msg_id_out == NULL)
    {
        return -(int32_t)EFAULT;
    }

    /* 调用内核 SYS_MSG_RECV 系统调用 */
    ret = svc_call_3(AISAFE_SYS_MSG_RECV,
                     (int64_t)s_vmm_channel_id,
                     (int64_t)buf,
                     (int64_t)size);

    if (ret < 0)
    {
        return (int32_t)ret;
    }

    /* 从消息头提取消息 ID */
    *msg_id_out = (uint64_t)ret;

    return (int32_t)ret;
}

/**
 * @brief 通过 SVC 调用内核 IPC 回复
 *
 * @param msg_id  消息 ID
 * @param buf     回复缓冲区
 * @param size    回复大小
 *
 * @return 0 成功，负数表示错误
 */
static int32_t vmm_ipc_reply(uint64_t msg_id, const void *buf, uint64_t size)
{
    int64_t ret;

    if (buf == NULL)
    {
        return -(int32_t)EFAULT;
    }

    /* 调用内核 SYS_MSG_REPLY 系统调用 */
    ret = svc_call_3(AISAFE_SYS_MSG_REPLY,
                     (int64_t)msg_id,
                     (int64_t)buf,
                     (int64_t)size);

    return (int32_t)ret;
}

/* ========================================================================
 * 消息处理函数
 * ======================================================================== */

/**
 * @brief 处理创建虚拟机请求
 *
 * @param req   请求消息
 * @param resp  响应消息
 */
static void handle_create_vm(const vmm_ipc_create_vm_req_t *req,
                              vmm_ipc_create_vm_resp_t *resp)
{
    kernel_status_t ret;
    uint64_t mem_base;
    int32_t vm_id;

    /* 参数检查 */
    if (req == NULL || resp == NULL)
    {
        return;
    }

    /* 分配 Guest 物理内存（简化：使用固定地址） */
    mem_base = 0x40000000ULL + ((uint64_t)req->mem_size * 4ULL);

    /* 创建 VM */
    vm_id = vmm_create_vm(req->name, mem_base, req->mem_size, req->num_vcpus);
    if (vm_id < 0)
    {
        resp->result = vm_id;
        resp->header.status = (uint32_t)(-vm_id);
        return;
    }

    /* 初始化 VGIC */
    ret = vgic_init((uint32_t)vm_id);
    if (ret != KERNEL_OK)
    {
        vmm_destroy_vm((uint32_t)vm_id);
        resp->result = (int32_t)ret;
        resp->header.status = (uint32_t)(-ret);
        return;
    }

    resp->result = vm_id;
    resp->header.status = 0U;
}

/**
 * @brief 处理销毁虚拟机请求
 *
 * @param req   请求消息
 * @param resp  响应消息
 */
static void handle_destroy_vm(const vmm_ipc_destroy_vm_req_t *req,
                               vmm_ipc_destroy_vm_resp_t *resp)
{
    kernel_status_t ret;

    /* 参数检查 */
    if (req == NULL || resp == NULL)
    {
        return;
    }

    /* 销毁 VGIC */
    vgic_destroy(req->vm_id);

    /* 销毁 VM */
    ret = vmm_destroy_vm(req->vm_id);
    if (ret != KERNEL_OK)
    {
        resp->result = (int32_t)ret;
        resp->header.status = (uint32_t)(-ret);
        return;
    }

    resp->result = 0;
    resp->header.status = 0U;
}

/**
 * @brief 处理启动虚拟机请求
 *
 * @param req   请求消息
 * @param resp  响应消息
 */
static void handle_start_vm(const vmm_ipc_start_vm_req_t *req,
                             vmm_ipc_start_vm_resp_t *resp)
{
    kernel_status_t ret;

    /* 参数检查 */
    if (req == NULL || resp == NULL)
    {
        return;
    }

    /* 启动 VM */
    ret = vmm_start_vm(req->vm_id);
    if (ret != KERNEL_OK)
    {
        resp->result = (int32_t)ret;
        resp->header.status = (uint32_t)(-ret);
        return;
    }

    resp->result = 0;
    resp->header.status = 0U;
}

/**
 * @brief 处理停止虚拟机请求
 *
 * @param req   请求消息
 * @param resp  响应消息
 */
static void handle_stop_vm(const vmm_ipc_stop_vm_req_t *req,
                            vmm_ipc_stop_vm_resp_t *resp)
{
    kernel_status_t ret;

    /* 参数检查 */
    if (req == NULL || resp == NULL)
    {
        return;
    }

    /* 停止 VM */
    ret = vmm_stop_vm(req->vm_id);
    if (ret != KERNEL_OK)
    {
        resp->result = (int32_t)ret;
        resp->header.status = (uint32_t)(-ret);
        return;
    }

    resp->result = 0;
    resp->header.status = 0U;
}

/**
 * @brief 处理暂停虚拟机请求
 *
 * @param req   请求消息
 * @param resp  响应消息
 */
static void handle_pause_vm(const vmm_ipc_pause_vm_req_t *req,
                             vmm_ipc_pause_vm_resp_t *resp)
{
    kernel_status_t ret;

    /* 参数检查 */
    if (req == NULL || resp == NULL)
    {
        return;
    }

    /* 暂停 VM */
    ret = vmm_pause_vm(req->vm_id);
    if (ret != KERNEL_OK)
    {
        resp->result = (int32_t)ret;
        resp->header.status = (uint32_t)(-ret);
        return;
    }

    resp->result = 0;
    resp->header.status = 0U;
}

/**
 * @brief 处理恢复虚拟机请求
 *
 * @param req   请求消息
 * @param resp  响应消息
 */
static void handle_resume_vm(const vmm_ipc_resume_vm_req_t *req,
                              vmm_ipc_resume_vm_resp_t *resp)
{
    kernel_status_t ret;

    /* 参数检查 */
    if (req == NULL || resp == NULL)
    {
        return;
    }

    /* 恢复 VM */
    ret = vmm_resume_vm(req->vm_id);
    if (ret != KERNEL_OK)
    {
        resp->result = (int32_t)ret;
        resp->header.status = (uint32_t)(-ret);
        return;
    }

    resp->result = 0;
    resp->header.status = 0U;
}

/**
 * @brief 处理创建 vCPU 请求
 *
 * @param req   请求消息
 * @param resp  响应消息
 */
static void handle_create_vcpu(const vmm_ipc_create_vcpu_req_t *req,
                                vmm_ipc_create_vcpu_resp_t *resp)
{
    kernel_status_t ret;
    int32_t vcpu_id;

    /* 参数检查 */
    if (req == NULL || resp == NULL)
    {
        return;
    }

    /* 创建 vCPU */
    vcpu_id = vmm_create_vcpu(req->vm_id, req->entry_point);
    if (vcpu_id < 0)
    {
        resp->result = vcpu_id;
        resp->header.status = (uint32_t)(-vcpu_id);
        return;
    }

    resp->result = vcpu_id;
    resp->header.status = 0U;
}

/**
 * @brief 处理销毁 vCPU 请求
 *
 * @param req   请求消息
 * @param resp  响应消息
 */
static void handle_destroy_vcpu(const vmm_ipc_destroy_vcpu_req_t *req,
                                 vmm_ipc_destroy_vcpu_resp_t *resp)
{
    kernel_status_t ret;

    /* 参数检查 */
    if (req == NULL || resp == NULL)
    {
        return;
    }

    /* 销毁 vCPU */
    ret = vmm_destroy_vcpu(req->vm_id, req->vcpu_id);
    if (ret != KERNEL_OK)
    {
        resp->result = (int32_t)ret;
        resp->header.status = (uint32_t)(-ret);
        return;
    }

    resp->result = 0;
    resp->header.status = 0U;
}

/**
 * @brief 处理暂停 vCPU 请求
 *
 * @param req   请求消息
 * @param resp  响应消息
 */
static void handle_pause_vcpu(const vmm_ipc_pause_vcpu_req_t *req,
                               vmm_ipc_pause_vcpu_resp_t *resp)
{
    kernel_status_t ret;

    /* 参数检查 */
    if (req == NULL || resp == NULL)
    {
        return;
    }

    /* 暂停 vCPU */
    ret = vmm_pause_vcpu(req->vm_id, req->vcpu_id);
    if (ret != KERNEL_OK)
    {
        resp->result = (int32_t)ret;
        resp->header.status = (uint32_t)(-ret);
        return;
    }

    resp->result = 0;
    resp->header.status = 0U;
}

/**
 * @brief 处理运行 vCPU 请求
 *
 * @param req   请求消息
 * @param resp  响应消息
 */
static void handle_run_vcpu(const vmm_ipc_run_vcpu_req_t *req,
                             vmm_ipc_run_vcpu_resp_t *resp)
{
    kernel_status_t ret;

    /* 参数检查 */
    if (req == NULL || resp == NULL)
    {
        return;
    }

    /* 运行 vCPU */
    ret = vmm_run_vcpu(req->vm_id, req->vcpu_id);
    if (ret != KERNEL_OK)
    {
        resp->result = (int32_t)ret;
        resp->header.status = (uint32_t)(-ret);
        return;
    }

    resp->result = 0;
    resp->header.status = 0U;
}

/**
 * @brief 处理列出所有虚拟机请求
 *
 * @param req   请求消息
 * @param resp  响应消息
 */
static void handle_list_vms(const vmm_ipc_list_vms_req_t *req,
                             vmm_ipc_list_vms_resp_t *resp)
{
    vm_desc_t *vm;
    uint32_t i;
    uint32_t count;

    (void)req;  /* 未使用 */

    /* 参数检查 */
    if (resp == NULL)
    {
        return;
    }

    /* 遍历所有 VM */
    count = 0U;
    for (i = 0U; i < 4U; i++)
    {
        vm = vmm_get_vm(i);
        if (vm != NULL && vm->active)
        {
            resp->vms[count].vm_id = vm->vm_id;
            resp->vms[count].state = (uint32_t)vm->state;
            resp->vms[count].active = vm->active;
            (void)memcpy(resp->vms[count].name, vm->name, 32);
            resp->vms[count].mem_size = vm->mem_size;
            resp->vms[count].vcpu_count = vm->vcpu_count;
            count++;
        }
    }

    resp->num_vms = count;
    resp->result = (int32_t)count;
    resp->header.status = 0U;
}

/**
 * @brief 处理获取虚拟机信息请求
 *
 * @param req   请求消息
 * @param resp  响应消息
 */
static void handle_get_vm_info(const vmm_ipc_get_vm_info_req_t *req,
                                 vmm_ipc_get_vm_info_resp_t *resp)
{
    vm_desc_t *vm;

    /* 参数检查 */
    if (req == NULL || resp == NULL)
    {
        return;
    }

    /* 获取 VM 信息 */
    vm = vmm_get_vm(req->vm_id);
    if (vm == NULL || !vm->active)
    {
        resp->result = -(int32_t)ENOENT;
        resp->header.status = ENOENT;
        return;
    }

    resp->vm_info.vm_id = vm->vm_id;
    resp->vm_info.state = (uint32_t)vm->state;
    resp->vm_info.active = vm->active;
    (void)memcpy(resp->vm_info.name, vm->name, 32);
    resp->vm_info.mem_size = vm->mem_size;
    resp->vm_info.vcpu_count = vm->vcpu_count;

    resp->result = 0;
    resp->header.status = 0U;
}

/**
 * @brief 处理获取虚拟机统计信息请求
 *
 * @param req   请求消息
 * @param resp  响应消息
 */
static void handle_get_vm_stats(const vmm_ipc_get_vm_stats_req_t *req,
                                  vmm_ipc_get_vm_stats_resp_t *resp)
{
    vm_desc_t *vm;

    /* 参数检查 */
    if (req == NULL || resp == NULL)
    {
        return;
    }

    /* 获取 VM 信息 */
    vm = vmm_get_vm(req->vm_id);
    if (vm == NULL || !vm->active)
    {
        resp->result = -(int32_t)ENOENT;
        resp->header.status = ENOENT;
        return;
    }

    resp->vm_stats.exit_count = vm->stats.exit_count;
    resp->vm_stats.irq_count = vm->stats.irq_inject_count;
    resp->vm_stats.mmio_count = vm->stats.mmio_access_count;
    resp->vm_stats.hypercall_count = vm->stats.hypercall_count;

    resp->result = 0;
    resp->header.status = 0U;
}

/**
 * @brief 处理注入中断请求
 *
 * @param req   请求消息
 * @param resp  响应消息
 */
static void handle_inject_irq(const vmm_ipc_inject_irq_req_t *req,
                               vmm_ipc_inject_irq_resp_t *resp)
{
    kernel_status_t ret;

    /* 参数检查 */
    if (req == NULL || resp == NULL)
    {
        return;
    }

    /* 注入中断 */
    ret = vmm_inject_irq(req->vm_id, req->vcpu_id, req->irq);
    if (ret != KERNEL_OK)
    {
        resp->result = (int32_t)ret;
        resp->header.status = (uint32_t)(-ret);
        return;
    }

    resp->result = 0;
    resp->header.status = 0U;
}

/**
 * @brief 处理清除中断请求
 *
 * @param req   请求消息
 * @param resp  响应消息
 */
static void handle_clear_irq(const vmm_ipc_clear_irq_req_t *req,
                              vmm_ipc_clear_irq_resp_t *resp)
{
    kernel_status_t ret;

    /* 参数检查 */
    if (req == NULL || resp == NULL)
    {
        return;
    }

    /* 清除中断 */
    ret = vmm_clear_irq(req->vm_id, req->vcpu_id, req->irq);
    if (ret != KERNEL_OK)
    {
        resp->result = (int32_t)ret;
        resp->header.status = (uint32_t)(-ret);
        return;
    }

    resp->result = 0;
    resp->header.status = 0U;
}

/**
 * @brief 消息分发器
 *
 * @param buf   消息缓冲区
 * @param size  消息大小
 */
static void vmm_ipc_dispatch(const uint8_t *buf, uint64_t size)
{
    const vmm_ipc_msg_header_t *header;

    /* 参数检查 */
    if (buf == NULL || size < sizeof(vmm_ipc_msg_header_t))
    {
        return;
    }

    header = (const vmm_ipc_msg_header_t *)buf;

    /* 根据消息类型分发 */
    switch (header->msg_type)
    {
        case VMM_IPC_CREATE_VM:
            handle_create_vm((const vmm_ipc_create_vm_req_t *)buf,
                               (vmm_ipc_create_vm_resp_t *)buf);
            break;

        case VMM_IPC_DESTROY_VM:
            handle_destroy_vm((const vmm_ipc_destroy_vm_req_t *)buf,
                               (vmm_ipc_destroy_vm_resp_t *)buf);
            break;

        case VMM_IPC_START_VM:
            handle_start_vm((const vmm_ipc_start_vm_req_t *)buf,
                              (vmm_ipc_start_vm_resp_t *)buf);
            break;

        case VMM_IPC_STOP_VM:
            handle_stop_vm((const vmm_ipc_stop_vm_req_t *)buf,
                             (vmm_ipc_stop_vm_resp_t *)buf);
            break;

        case VMM_IPC_PAUSE_VM:
            handle_pause_vm((const vmm_ipc_pause_vm_req_t *)buf,
                              (vmm_ipc_pause_vm_resp_t *)buf);
            break;

        case VMM_IPC_RESUME_VM:
            handle_resume_vm((const vmm_ipc_resume_vm_req_t *)buf,
                               (vmm_ipc_resume_vm_resp_t *)buf);
            break;

        case VMM_IPC_CREATE_VCPU:
            handle_create_vcpu((const vmm_ipc_create_vcpu_req_t *)buf,
                                 (vmm_ipc_create_vcpu_resp_t *)buf);
            break;

        case VMM_IPC_DESTROY_VCPU:
            handle_destroy_vcpu((const vmm_ipc_destroy_vcpu_req_t *)buf,
                                 (vmm_ipc_destroy_vcpu_resp_t *)buf);
            break;

        case VMM_IPC_PAUSE_VCPU:
            handle_pause_vcpu((const vmm_ipc_pause_vcpu_req_t *)buf,
                               (vmm_ipc_pause_vcpu_resp_t *)buf);
            break;

        case VMM_IPC_RUN_VCPU:
            handle_run_vcpu((const vmm_ipc_run_vcpu_req_t *)buf,
                             (vmm_ipc_run_vcpu_resp_t *)buf);
            break;

        case VMM_IPC_LIST_VMS:
            handle_list_vms((const vmm_ipc_list_vms_req_t *)buf,
                             (vmm_ipc_list_vms_resp_t *)buf);
            break;

        case VMM_IPC_GET_VM_INFO:
            handle_get_vm_info((const vmm_ipc_get_vm_info_req_t *)buf,
                                (vmm_ipc_get_vm_info_resp_t *)buf);
            break;

        case VMM_IPC_GET_VM_STATS:
            handle_get_vm_stats((const vmm_ipc_get_vm_stats_req_t *)buf,
                                 (vmm_ipc_get_vm_stats_resp_t *)buf);
            break;

        case VMM_IPC_INJECT_IRQ:
            handle_inject_irq((const vmm_ipc_inject_irq_req_t *)buf,
                               (vmm_ipc_inject_irq_resp_t *)buf);
            break;

        case VMM_IPC_CLEAR_IRQ:
            handle_clear_irq((const vmm_ipc_clear_irq_req_t *)buf,
                              (vmm_ipc_clear_irq_resp_t *)buf);
            break;

        default:
            break;
    }
}

/* ========================================================================
 * 服务器端主循环
 * ======================================================================== */

/**
 * @brief VMM 服务器主函数
 */
void vmm_server_main(void)
{
    uint8_t buf[4096];
    uint64_t msg_id;
    int32_t ret;

    printf("VMM 服务器启动\n");

    /* 消息接收和分发循环 */
    while (true)
    {
        /* 接收消息 */
        ret = vmm_ipc_recv(buf, sizeof(buf), &msg_id);
        if (ret < 0)
        {
            continue;
        }

        /* 分发消息 */
        vmm_ipc_dispatch(buf, (uint64_t)ret);

        /* 回复消息 */
        ret = vmm_ipc_reply(msg_id, buf, (uint64_t)ret);
        if (ret < 0)
        {
            continue;
        }
    }
}

/**
 * @brief 初始化 VMM 服务器
 */
void vmm_server_init(void)
{
    kernel_status_t ret;
    int64_t svc_ret;

    printf("初始化 VMM 服务器\n");

    /* 初始化 VMM */
    ret = vmm_init();
    if (ret != KERNEL_OK)
    {
        printf("VMM 初始化失败: %d\n", ret);
        return;
    }

    /* 创建 IPC 通道 */
    svc_ret = svc_call_3(AISAFE_SYS_CHANNEL_CREATE,
                          0x1000ULL,  /* Channel ID */
                          (int64_t)"vmm",
                          0ULL);
    if (svc_ret < 0)
    {
        printf("IPC 通道创建失败: %ld\n", svc_ret);
        return;
    }

    s_vmm_channel_id = (uint64_t)svc_ret;

    /* 标记为已初始化 */
    s_vmm_server_initialized = true;

    printf("VMM 服务器初始化完成 (channel_id: %lu)\n", s_vmm_channel_id);
}
