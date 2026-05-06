/**
 * @file    net.h
 * @brief   网络服务公共头文件
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details 网络服务公共接口
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 网络服务初始化
 * ======================================================================== */

/**
 * @brief 初始化网络服务
 *
 * @return 0 成功，<0 失败
 */
int32_t net_service_init(void);

/**
 * @brief 启动网络服务
 *
 * @return 0 成功，<0 失败
 */
int32_t net_service_start(void);

/**
 * @brief 停止网络服务
 *
 * @return 0 成功，<0 失败
 */
int32_t net_service_stop(void);

/**
 * @brief 获取网络服务状态
 *
 * @return true 运行，false 停止
 */
bool net_service_is_running(void);

#endif /* NET_H */
