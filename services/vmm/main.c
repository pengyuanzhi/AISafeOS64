/**
 * @file    main.c
 * @brief   VMM 服务入口
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 3.0
 *
 * @details 虚拟机监视器服务主程序：
 *          - VMM 子系统初始化
 *          - IPC 消息循环
 *          - VM 生命周期管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "vmm.h"
#include <kernel/errno.h>
#include <stdint.h>

/* ========================================================================
 * VMM 服务主函数
 * ======================================================================== */

int main(void)
{
    kernel_status_t ret;

    /* 初始化 VMM 子系统 */
    ret = vmm_init();
    if (ret != KERNEL_OK)
    {
        return (int)ret;
    }

    for (;;)
    {
        /*
         * 实际实现中：
         * 1. 通过 IPC 接收 VM 管理请求
         * 2. 根据 IPC 消息类型调用相应 VMM API
         * 3. 处理 VM 退出事件
         * 4. 响应虚拟设备 MMIO 请求
         */
    }

    return 0;
}
