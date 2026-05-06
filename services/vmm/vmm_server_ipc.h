/**
 * @file    vmm_server_ipc.h
 * @brief   VMM 服务 IPC 服务器端头文件
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details VMM 服务 IPC 服务器端接口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_VMM_VMM_SERVER_IPC_H
#define SERVICES_VMM_VMM_SERVER_IPC_H

/**
 * @brief 初始化 VMM 服务器
 */
void vmm_server_init(void);

/**
 * @brief VMM 服务器主函数（包含 IPC 消息处理循环）
 */
void vmm_server_main(void);

#endif /* SERVICES_VMM_VMM_SERVER_IPC_H */
