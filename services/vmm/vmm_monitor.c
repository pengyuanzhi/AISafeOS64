/**
 * @file    vmm_monitor.c
 * @brief   VMM Monitor 工具
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details VMM Monitor 工具，实时监控虚拟机的状态和统计信息
 *
 * @note MISRA-C:2012 合规
 * @note 使用内核 IPC 系统调用
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include "../vmm_ipc_types.h"

/* ========================================================================
 * 内核 IPC 系统调用接口
 * ======================================================================== */

/** @brief IPC 通道 ID */
static uint64_t s_vmm_channel_id = 0U;

/** @brief 消息 ID 计数器 */
static uint32_t s_msg_id_counter = 0U;

/* 内核 IPC 系统调用号 */
#define AISAFE_SYS_CONNECT_ATTACH       0x0102U
#define AISAFE_SYS_MSG_SEND             0x0104U
#define AISAFE_SYS_MSG_RECV             0x0105U

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
 * @brief 通过 SVC 调用内核 IPC 发送
 *
 * @param buf     发送缓冲区
 * @param size    消息大小
 *
 * @return 消息 ID，负数表示错误
 */
static int32_t vmm_ipc_send(const void *buf, uint64_t size)
{
    int64_t ret;

    if (buf == NULL)
    {
        return -(int32_t)EFAULT;
    }

    /* 调用内核 SYS_MSG_SEND 系统调用 */
    ret = svc_call_3(AISAFE_SYS_MSG_SEND,
                     (int64_t)s_vmm_channel_id,
                     (int64_t)buf,
                     (int64_t)size);

    return (int32_t)ret;
}

/**
 * @brief 通过 SVC 调用内核 IPC 接收
 *
 * @param buf     接收缓冲区
 * @param size    缓冲区大小
 *
 * @return 接收到的字节数，负数表示错误
 */
static int32_t vmm_ipc_recv(void *buf, uint64_t size)
{
    int64_t ret;

    if (buf == NULL)
    {
        return -(int32_t)EFAULT;
    }

    /* 调用内核 SYS_MSG_RECV 系统调用 */
    ret = svc_call_3(AISAFE_SYS_MSG_RECV,
                     (int64_t)s_vmm_channel_id,
                     (int64_t)buf,
                     (int64_t)size);

    return (int32_t)ret;
}

/**
 * @brief 分配新的消息 ID
 *
 * @return 新的消息 ID
 */
static uint32_t alloc_msg_id(void)
{
    uint32_t msg_id = s_msg_id_counter++;
    return msg_id;
}

/**
 * @brief 清屏
 */
static void clear_screen(void)
{
    printf("\033[2J\033[H");
}

/**
 * @brief 获取虚拟机列表
 *
 * @param resp   响应消息缓冲区
 *
 * @return 0 成功，负数表示错误
 */
static int32_t get_vm_list(vmm_ipc_list_vms_resp_t *resp)
{
    vmm_ipc_list_vms_req_t req;
    int32_t ret;

    if (resp == NULL)
    {
        return -(int32_t)EFAULT;
    }

    /* 填充请求消息 */
    (void)memset(&req, 0, sizeof(req));
    req.header.msg_type = VMM_IPC_LIST_VMS;
    req.header.msg_id = alloc_msg_id();

    /* 发送请求 */
    ret = vmm_ipc_send(&req, sizeof(req));
    if (ret < 0)
    {
        return ret;
    }

    /* 接收响应 */
    ret = vmm_ipc_recv(resp, sizeof(*resp));
    if (ret < 0)
    {
        return ret;
    }

    return 0;
}

/**
 * @brief 获取虚拟机统计信息
 *
 * @param vm_id  VM ID
 * @param resp   响应消息缓冲区
 *
 * @return 0 成功，负数表示错误
 */
static int32_t get_vm_stats(uint32_t vm_id, vmm_ipc_get_vm_stats_resp_t *resp)
{
    vmm_ipc_get_vm_stats_req_t req;
    int32_t ret;

    if (resp == NULL)
    {
        return -(int32_t)EFAULT;
    }

    /* 填充请求消息 */
    (void)memset(&req, 0, sizeof(req));
    req.header.msg_type = VMM_IPC_GET_VM_STATS;
    req.header.msg_id = alloc_msg_id();
    req.vm_id = vm_id;

    /* 发送请求 */
    ret = vmm_ipc_send(&req, sizeof(req));
    if (ret < 0)
    {
        return ret;
    }

    /* 接收响应 */
    ret = vmm_ipc_recv(resp, sizeof(*resp));
    if (ret < 0)
    {
        return ret;
    }

    return 0;
}

/**
 * @brief 显示虚拟机列表
 *
 * @param resp   响应消息
 */
static void display_vm_list(const vmm_ipc_list_vms_resp_t *resp)
{
    uint32_t i;

    if (resp == NULL)
    {
        return;
    }

    printf("虚拟机列表 (%u 个):\n", resp->num_vms);
    printf("================================================================================\n");
    printf("VM ID | 状态     | 名称     | 内存大小 | vCPU 数量\n");
    printf("--------------------------------------------------------------------------------\n");
    for (i = 0U; i < resp->num_vms; i++)
    {
        const char *state_str;
        switch (resp->vms[i].state)
        {
            case 0U: state_str = "NONE";     break;
            case 1U: state_str = "CREATED"; break;
            case 2U: state_str = "RUNNING"; break;
            case 3U: state_str = "PAUSED";  break;
            case 4U: state_str = "STOPPED"; break;
            default: state_str = "UNKNOWN"; break;
        }
        printf("%5u | %-8s | %-8s | %8lu | %10u\n",
               resp->vms[i].vm_id,
               state_str,
               resp->vms[i].name,
               resp->vms[i].mem_size,
               resp->vms[i].vcpu_count);
    }
    printf("================================================================================\n");
}

/**
 * @brief 显示虚拟机统计信息
 *
 * @param vm_id  VM ID
 * @param resp   响应消息
 */
static void display_vm_stats(uint32_t vm_id, const vmm_ipc_get_vm_stats_resp_t *resp)
{
    if (resp == NULL)
    {
        return;
    }

    printf("虚拟机统计信息 (VM ID = %u):\n", vm_id);
    printf("================================================================================\n");
    printf("VM 退出次数      : %lu\n", resp->vm_stats.exit_count);
    printf("中断注入次数     : %lu\n", resp->vm_stats.irq_count);
    printf("MMIO 访问次数    : %lu\n", resp->vm_stats.mmio_count);
    printf("Hypercall 次数   : %lu\n", resp->vm_stats.hypercall_count);
    printf("================================================================================\n");
}

/* ========================================================================
 * Monitor 主循环
 * ======================================================================== */

int main(int argc, char *argv[])
{
    int64_t svc_ret;
    int32_t ret;
    vmm_ipc_list_vms_resp_t vm_list_resp;
    vmm_ipc_get_vm_stats_resp_t vm_stats_resp;
    uint32_t i;
    int update_interval = 2;  /* 更新间隔（秒） */

    (void)argc;
    (void)argv;

    printf("VMM Monitor 工具 v1.0\n");
    printf("====================\n\n");

    /* 连接到 VMM IPC 通道 */
    svc_ret = svc_call_3(AISAFE_SYS_CONNECT_ATTACH,
                          0x1000ULL,  /* Channel ID */
                          (int64_t)"vmm",
                          0ULL);
    if (svc_ret < 0)
    {
        printf("连接 VMM 服务失败: %ld\n", svc_ret);
        return 1;
    }

    s_vmm_channel_id = (uint64_t)svc_ret;
    printf("连接 VMM 服务成功 (channel_id: %lu)\n\n", s_vmm_channel_id);

    /* Monitor 主循环 */
    while (true)
    {
        /* 清屏 */
        clear_screen();

        /* 显示标题 */
        printf("VMM Monitor - 实时监控\n");
        printf("按 Ctrl+C 退出\n\n");

        /* 获取虚拟机列表 */
        ret = get_vm_list(&vm_list_resp);
        if (ret < 0)
        {
            printf("获取虚拟机列表失败: %d\n", ret);
            sleep(update_interval);
            continue;
        }

        /* 显示虚拟机列表 */
        display_vm_list(&vm_list_resp);

        /* 获取每个虚拟机的统计信息 */
        printf("\n虚拟机统计信息:\n\n");
        for (i = 0U; i < vm_list_resp.num_vms; i++)
        {
            ret = get_vm_stats(vm_list_resp.vms[i].vm_id, &vm_stats_resp);
            if (ret < 0)
            {
                printf("获取虚拟机 %u 统计信息失败: %d\n",
                       vm_list_resp.vms[i].vm_id, ret);
                continue;
            }

            display_vm_stats(vm_list_resp.vms[i].vm_id, &vm_stats_resp);
        }

        /* 等待下一次更新 */
        sleep(update_interval);
    }

    return 0;
}
