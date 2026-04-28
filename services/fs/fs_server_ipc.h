/**
 * @file    fs_server_ipc.h
 * @brief   FS 服务 IPC 服务器端头文件
 * @author  AISafe64 Team
 * @date    2026-04-28
 * @version 2.0
 *
 * @details FS 服务 IPC 服务器端接口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef FS_SERVER_IPC_H
#define FS_SERVER_IPC_H

/**
 * @brief 初始化 FS 服务器
 */
void fs_server_init(void);

/**
 * @brief FS 服务器主函数（包含 IPC 消息处理循环）
 */
void fs_server_main(void);

#endif /* FS_SERVER_IPC_H */
