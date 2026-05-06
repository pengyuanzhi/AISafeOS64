/**
 * @file    vmm_cli.c
 * @brief   VMM CLI 工具
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details VMM 命令行工具，提供以下命令：
 *          - vmm-create: 创建虚拟机
 *          - vmm-start: 启动虚拟机
 *          - vmm-stop: 停止虚拟机
 *          - vmm-pause: 暂停虚拟机
 *          - vmm-resume: 恢复虚拟机
 *          - vmm-list: 列出所有虚拟机
 *          - vmm-stats: 查看虚拟机统计信息
 *          - vmm-inject: 注入中断
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

/* ========================================================================
 * CLI 命令
 * ======================================================================== */

/**
 * @brief 创建虚拟机命令
 *
 * @param argc   参数个数
 * @param argv   参数列表
 */
static void cmd_create_vm(int argc, char *argv[])
{
    vmm_ipc_create_vm_req_t req;
    vmm_ipc_create_vm_resp_t resp;
    int32_t ret;

    if (argc < 4)
    {
        printf("用法: vmm-create <name> <mem_size> <num_vcpus>\n");
        printf("示例: vmm-create guest1 256 2\n");
        return;
    }

    /* 填充请求消息 */
    (void)memset(&req, 0, sizeof(req));
    req.header.msg_type = VMM_IPC_CREATE_VM;
    req.header.msg_id = alloc_msg_id();
    (void)strncpy(req.name, argv[1], 31);
    req.name[31] = '\0';
    req.mem_size = (uint64_t)atol(argv[2]);
    req.num_vcpus = (uint32_t)atoi(argv[3]);

    /* 发送请求 */
    ret = vmm_ipc_send(&req, sizeof(req));
    if (ret < 0)
    {
        printf("发送请求失败: %d\n", ret);
        return;
    }

    /* 接收响应 */
    ret = vmm_ipc_recv(&resp, sizeof(resp));
    if (ret < 0)
    {
        printf("接收响应失败: %d\n", ret);
        return;
    }

    /* 处理响应 */
    if (resp.result >= 0)
    {
        printf("创建虚拟机成功: VM ID = %d\n", resp.result);
    }
    else
    {
        printf("创建虚拟机失败: %d (status=%u)\n", resp.result, resp.header.status);
    }
}

/**
 * @brief 启动虚拟机命令
 *
 * @param argc   参数个数
 * @param argv   参数列表
 */
static void cmd_start_vm(int argc, char *argv[])
{
    vmm_ipc_start_vm_req_t req;
    vmm_ipc_start_vm_resp_t resp;
    int32_t ret;

    if (argc < 2)
    {
        printf("用法: vmm-start <vm_id>\n");
        printf("示例: vmm-start 0\n");
        return;
    }

    /* 填充请求消息 */
    (void)memset(&req, 0, sizeof(req));
    req.header.msg_type = VMM_IPC_START_VM;
    req.header.msg_id = alloc_msg_id();
    req.vm_id = (uint32_t)atoi(argv[1]);

    /* 发送请求 */
    ret = vmm_ipc_send(&req, sizeof(req));
    if (ret < 0)
    {
        printf("发送请求失败: %d\n", ret);
        return;
    }

    /* 接收响应 */
    ret = vmm_ipc_recv(&resp, sizeof(resp));
    if (ret < 0)
    {
        printf("接收响应失败: %d\n", ret);
        return;
    }

    /* 处理响应 */
    if (resp.result == 0)
    {
        printf("启动虚拟机成功: VM ID = %u\n", req.vm_id);
    }
    else
    {
        printf("启动虚拟机失败: %d (status=%u)\n", resp.result, resp.header.status);
    }
}

/**
 * @brief 停止虚拟机命令
 *
 * @param argc   参数个数
 * @param argv   参数列表
 */
static void cmd_stop_vm(int argc, char *argv[])
{
    vmm_ipc_stop_vm_req_t req;
    vmm_ipc_stop_vm_resp_t resp;
    int32_t ret;

    if (argc < 2)
    {
        printf("用法: vmm-stop <vm_id>\n");
        printf("示例: vmm-stop 0\n");
        return;
    }

    /* 填充请求消息 */
    (void)memset(&req, 0, sizeof(req));
    req.header.msg_type = VMM_IPC_STOP_VM;
    req.header.msg_id = alloc_msg_id();
    req.vm_id = (uint32_t)atoi(argv[1]);

    /* 发送请求 */
    ret = vmm_ipc_send(&req, sizeof(req));
    if (ret < 0)
    {
        printf("发送请求失败: %d\n", ret);
        return;
    }

    /* 接收响应 */
    ret = vmm_ipc_recv(&resp, sizeof(resp));
    if (ret < 0)
    {
        printf("接收响应失败: %d\n", ret);
        return;
    }

    /* 处理响应 */
    if (resp.result == 0)
    {
        printf("停止虚拟机成功: VM ID = %u\n", req.vm_id);
    }
    else
    {
        printf("停止虚拟机失败: %d (status=%u)\n", resp.result, resp.header.status);
    }
}

/**
 * @brief 暂停虚拟机命令
 *
 * @param argc   参数个数
 * @param argv   参数列表
 */
static void cmd_pause_vm(int argc, char *argv[])
{
    vmm_ipc_pause_vm_req_t req;
    vmm_ipc_pause_vm_resp_t resp;
    int32_t ret;

    if (argc < 2)
    {
        printf("用法: vmm-pause <vm_id>\n");
        printf("示例: vmm-pause 0\n");
        return;
    }

    /* 填充请求消息 */
    (void)memset(&req, 0, sizeof(req));
    req.header.msg_type = VMM_IPC_PAUSE_VM;
    req.header.msg_id = alloc_msg_id();
    req.vm_id = (uint32_t)atoi(argv[1]);

    /* 发送请求 */
    ret = vmm_ipc_send(&req, sizeof(req));
    if (ret < 0)
    {
        printf("发送请求失败: %d\n", ret);
        return;
    }

    /* 接收响应 */
    ret = vmm_ipc_recv(&resp, sizeof(resp));
    if (ret < 0)
    {
        printf("接收响应失败: %d\n", ret);
        return;
    }

    /* 处理响应 */
    if (resp.result == 0)
    {
        printf("暂停虚拟机成功: VM ID = %u\n", req.vm_id);
    }
    else
    {
        printf("暂停虚拟机失败: %d (status=%u)\n", resp.result, resp.header.status);
    }
}

/**
 * @brief 恢复虚拟机命令
 *
 * @param argc   参数个数
 * @param argv   参数列表
 */
static void cmd_resume_vm(int argc, char *argv[])
{
    vmm_ipc_resume_vm_req_t req;
    vmm_ipc_resume_vm_resp_t resp;
    int32_t ret;

    if (argc < 2)
    {
        printf("用法: vmm-resume <vm_id>\n");
        printf("示例: vmm-resume 0\n");
        return;
    }

    /* 填充请求消息 */
    (void)memset(&req, 0, sizeof(req));
    req.header.msg_type = VMM_IPC_RESUME_VM;
    req.header.msg_id = alloc_msg_id();
    req.vm_id = (uint32_t)atoi(argv[1]);

    /* 发送请求 */
    ret = vmm_ipc_send(&req, sizeof(req));
    if (ret < 0)
    {
        printf("发送请求失败: %d\n", ret);
        return;
    }

    /* 接收响应 */
    ret = vmm_ipc_recv(&resp, sizeof(resp));
    if (ret < 0)
    {
        printf("接收响应失败: %d\n", ret);
        return;
    }

    /* 处理响应 */
    if (resp.result == 0)
    {
        printf("恢复虚拟机成功: VM ID = %u\n", req.vm_id);
    }
    else
    {
        printf("恢复虚拟机失败: %d (status=%u)\n", resp.result, resp.header.status);
    }
}

/**
 * @brief 列出所有虚拟机命令
 *
 * @param argc   参数个数
 * @param argv   参数列表
 */
static void cmd_list_vms(int argc, char *argv[])
{
    vmm_ipc_list_vms_req_t req;
    vmm_ipc_list_vms_resp_t resp;
    int32_t ret;
    uint32_t i;

    (void)argc;
    (void)argv;

    /* 填充请求消息 */
    (void)memset(&req, 0, sizeof(req));
    req.header.msg_type = VMM_IPC_LIST_VMS;
    req.header.msg_id = alloc_msg_id();

    /* 发送请求 */
    ret = vmm_ipc_send(&req, sizeof(req));
    if (ret < 0)
    {
        printf("发送请求失败: %d\n", ret);
        return;
    }

    /* 接收响应 */
    ret = vmm_ipc_recv(&resp, sizeof(resp));
    if (ret < 0)
    {
        printf("接收响应失败: %d\n", ret);
        return;
    }

    /* 处理响应 */
    if (resp.result >= 0)
    {
        printf("虚拟机列表 (%u 个):\n", resp.num_vms);
        printf("================================================================================\n");
        printf("VM ID | 状态     | 名称     | 内存大小 | vCPU 数量\n");
        printf("--------------------------------------------------------------------------------\n");
        for (i = 0U; i < resp.num_vms; i++)
        {
            const char *state_str;
            switch (resp.vms[i].state)
            {
                case 0U: state_str = "NONE";     break;
                case 1U: state_str = "CREATED"; break;
                case 2U: state_str = "RUNNING"; break;
                case 3U: state_str = "PAUSED";  break;
                case 4U: state_str = "STOPPED"; break;
                default: state_str = "UNKNOWN"; break;
            }
            printf("%5u | %-8s | %-8s | %8lu | %10u\n",
                   resp.vms[i].vm_id,
                   state_str,
                   resp.vms[i].name,
                   resp.vms[i].mem_size,
                   resp.vms[i].vcpu_count);
        }
        printf("================================================================================\n");
    }
    else
    {
        printf("获取虚拟机列表失败: %d (status=%u)\n", resp.result, resp.header.status);
    }
}

/**
 * @brief 查看虚拟机统计信息命令
 *
 * @param argc   参数个数
 * @param argv   参数列表
 */
static void cmd_get_vm_stats(int argc, char *argv[])
{
    vmm_ipc_get_vm_stats_req_t req;
    vmm_ipc_get_vm_stats_resp_t resp;
    int32_t ret;

    if (argc < 2)
    {
        printf("用法: vmm-stats <vm_id>\n");
        printf("示例: vmm-stats 0\n");
        return;
    }

    /* 填充请求消息 */
    (void)memset(&req, 0, sizeof(req));
    req.header.msg_type = VMM_IPC_GET_VM_STATS;
    req.header.msg_id = alloc_msg_id();
    req.vm_id = (uint32_t)atoi(argv[1]);

    /* 发送请求 */
    ret = vmm_ipc_send(&req, sizeof(req));
    if (ret < 0)
    {
        printf("发送请求失败: %d\n", ret);
        return;
    }

    /* 接收响应 */
    ret = vmm_ipc_recv(&resp, sizeof(resp));
    if (ret < 0)
    {
        printf("接收响应失败: %d\n", ret);
        return;
    }

    /* 处理响应 */
    if (resp.result == 0)
    {
        printf("虚拟机统计信息 (VM ID = %u):\n", req.vm_id);
        printf("================================================================================\n");
        printf("VM 退出次数      : %lu\n", resp.vm_stats.exit_count);
        printf("中断注入次数     : %lu\n", resp.vm_stats.irq_count);
        printf("MMIO 访问次数    : %lu\n", resp.vm_stats.mmio_count);
        printf("Hypercall 次数   : %lu\n", resp.vm_stats.hypercall_count);
        printf("================================================================================\n");
    }
    else
    {
        printf("获取虚拟机统计信息失败: %d (status=%u)\n", resp.result, resp.header.status);
    }
}

/**
 * @brief 注入中断命令
 *
 * @param argc   参数个数
 * @param argv   参数列表
 */
static void cmd_inject_irq(int argc, char *argv[])
{
    vmm_ipc_inject_irq_req_t req;
    vmm_ipc_inject_irq_resp_t resp;
    int32_t ret;

    if (argc < 4)
    {
        printf("用法: vmm-inject <vm_id> <vcpu_id> <irq>\n");
        printf("示例: vmm-inject 0 0 32\n");
        return;
    }

    /* 填充请求消息 */
    (void)memset(&req, 0, sizeof(req));
    req.header.msg_type = VMM_IPC_INJECT_IRQ;
    req.header.msg_id = alloc_msg_id();
    req.vm_id = (uint32_t)atoi(argv[1]);
    req.vcpu_id = (uint32_t)atoi(argv[2]);
    req.irq = (uint32_t)atoi(argv[3]);

    /* 发送请求 */
    ret = vmm_ipc_send(&req, sizeof(req));
    if (ret < 0)
    {
        printf("发送请求失败: %d\n", ret);
        return;
    }

    /* 接收响应 */
    ret = vmm_ipc_recv(&resp, sizeof(resp));
    if (ret < 0)
    {
        printf("接收响应失败: %d\n", ret);
        return;
    }

    /* 处理响应 */
    if (resp.result == 0)
    {
        printf("注入中断成功: VM ID = %u, vCPU ID = %u, IRQ = %u\n",
               req.vm_id, req.vcpu_id, req.irq);
    }
    else
    {
        printf("注入中断失败: %d (status=%u)\n", resp.result, resp.header.status);
    }
}

/**
 * @brief 显示帮助信息
 */
static void print_help(void)
{
    printf("VMM 命令行工具\n");
    printf("================\n\n");
    printf("可用命令:\n");
    printf("  vmm-create  <name> <mem_size> <num_vcpus>  创建虚拟机\n");
    printf("  vmm-start   <vm_id>                      启动虚拟机\n");
    printf("  vmm-stop    <vm_id>                      停止虚拟机\n");
    printf("  vmm-pause   <vm_id>                      暂停虚拟机\n");
    printf("  vmm-resume  <vm_id>                      恢复虚拟机\n");
    printf("  vmm-list                                   列出所有虚拟机\n");
    printf("  vmm-stats   <vm_id>                      查看虚拟机统计信息\n");
    printf("  vmm-inject  <vm_id> <vcpu_id> <irq>      注入中断\n");
    printf("  help                                       显示帮助信息\n");
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(int argc, char *argv[])
{
    int64_t svc_ret;

    printf("VMM CLI 工具 v1.0\n");
    printf("================\n\n");

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

    /* 处理命令 */
    if (argc < 2)
    {
        print_help();
        return 0;
    }

    if (strcmp(argv[1], "vmm-create") == 0)
    {
        cmd_create_vm(argc - 1, argv + 1);
    }
    else if (strcmp(argv[1], "vmm-start") == 0)
    {
        cmd_start_vm(argc - 1, argv + 1);
    }
    else if (strcmp(argv[1], "vmm-stop") == 0)
    {
        cmd_stop_vm(argc - 1, argv + 1);
    }
    else if (strcmp(argv[1], "vmm-pause") == 0)
    {
        cmd_pause_vm(argc - 1, argv + 1);
    }
    else if (strcmp(argv[1], "vmm-resume") == 0)
    {
        cmd_resume_vm(argc - 1, argv + 1);
    }
    else if (strcmp(argv[1], "vmm-list") == 0)
    {
        cmd_list_vms(argc - 1, argv + 1);
    }
    else if (strcmp(argv[1], "vmm-stats") == 0)
    {
        cmd_get_vm_stats(argc - 1, argv + 1);
    }
    else if (strcmp(argv[1], "vmm-inject") == 0)
    {
        cmd_inject_irq(argc - 1, argv + 1);
    }
    else if (strcmp(argv[1], "help") == 0)
    {
        print_help();
    }
    else
    {
        printf("未知命令: %s\n", argv[1]);
        printf("使用 'help' 查看可用命令\n");
    }

    return 0;
}
