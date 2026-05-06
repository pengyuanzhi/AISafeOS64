/**
 * @file    net.c
 * @brief   网络服务实现
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details 网络服务实现
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "net.h"
#include "net_config.h"
#include "net_socket.h"
#include <string.h>

/* ========================================================================
 * 内部变量
 * ======================================================================== */

/** @brief 网络服务运行标志 */
static bool s_net_running = false;

/* ========================================================================
 * 网络服务实现
 * ======================================================================== */

/**
 * @brief 初始化网络服务
 *
 * @return 0 成功，<0 失败
 */
int32_t net_service_init(void)
{
    int32_t ret;

    /* 初始化网络配置 */
    ret = net_config_init();

    if (ret != 0)
    {
        return ret;
    }

    /* 初始化套接字模块 */
    ret = net_socket_init();

    if (ret != 0)
    {
        return ret;
    }

    s_net_running = false;

    return 0;
}

/**
 * @brief 启动网络服务
 *
 * @return 0 成功，<0 失败
 */
int32_t net_service_start(void)
{
    int32_t ret;

    /* 启动网络接口 */
    ret = net_config_up();

    if (ret != 0)
    {
        return ret;
    }

    s_net_running = true;

    return 0;
}

/**
 * @brief 停止网络服务
 *
 * @return 0 成功，<0 失败
 */
int32_t net_service_stop(void)
{
    int32_t ret;

    /* 关闭网络接口 */
    ret = net_config_down();

    if (ret != 0)
    {
        return ret;
    }

    s_net_running = false;

    return 0;
}

/**
 * @brief 获取网络服务状态
 *
 * @return true 运行，false 停止
 */
bool net_service_is_running(void)
{
    return s_net_running;
}
