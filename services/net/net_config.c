/**
 * @file    net_config.c
 * @brief   网络配置实现
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details 网络配置实现：
 *          - IP 地址配置
 *          - 子网掩码配置
 *          - 网关配置
 *          - 网络接口管理
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "net_config.h"
#include <string.h>

/* ========================================================================
 * 内部变量
 * ======================================================================== */

/** @brief 网络配置 */
static net_config_t s_net_config = {0};

/** @brief 网络配置初始化标志 */
static bool s_config_initialized = false;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 格式化 IP 地址为字符串
 *
 * @param ip        IP 地址（网络字节序）
 *
 * @return IP 地址字符串（静态缓冲区）
 */
const char *ip_to_string(uint32_t ip)
{
    static char buffer[16];

    if (ip == 0U)
    {
        return "0.0.0.0";
    }

    (void)snprintf(buffer, sizeof(buffer),
                   "%u.%u.%u.%u",
                   (ip >> 24) & 0xFFU,
                   (ip >> 16) & 0xFFU,
                   (ip >> 8) & 0xFFU,
                   ip & 0xFFU);

    return buffer;
}

/**
 * @brief 格式化 MAC 地址为字符串
 *
 * @param mac       MAC 地址（6 字节）
 *
 * @return MAC 地址字符串（静态缓冲区）
 */
const char *mac_to_string(const uint8_t *mac)
{
    static char buffer[18];
    uint32_t i;

    if (mac == NULL)
    {
        return "00:00:00:00:00:00";
    }

    (void)snprintf(buffer, sizeof(buffer),
                   "%02X:%02X:%02X:%02X:%02X:%02X",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return buffer;
}

/* ========================================================================
 * 网络配置接口实现
 * ======================================================================== */

/**
 * @brief 初始化网络配置
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_init(void)
{
    /* 清空配置 */
    (void)memset(&s_net_config, 0, sizeof(net_config_t));

    /* 设置默认 MAC 地址 */
    s_net_config.mac_addr[0] = 0x02U;
    s_net_config.mac_addr[1] = 0x00U;
    s_net_config.mac_addr[2] = 0x00U;
    s_net_config.mac_addr[3] = 0x00U;
    s_net_config.mac_addr[4] = 0x00U;
    s_net_config.mac_addr[5] = 0x01U;

    /* 设置默认 IP 地址 */
    s_net_config.ip_addr = 0x0A000001U; /* 10.0.0.1 */
    s_net_config.netmask = 0xFFFFFF00U; /* 255.255.255.0 */

    s_config_initialized = true;

    return 0;
}

/**
 * @brief 设置 IP 地址
 *
 * @param ip_addr    IP 地址（网络字节序）
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_set_ip(uint32_t ip_addr)
{
    if (ip_addr == 0U)
    {
        return -22; /* EINVAL */
    }

    if (!s_config_initialized)
    {
        return -19; /* ENODEV */
    }

    s_net_config.ip_addr = ip_addr;

    return 0;
}

/**
 * @brief 获取 IP 地址
 *
 * @return IP 地址（网络字节序）
 */
uint32_t net_config_get_ip(void)
{
    if (!s_config_initialized)
    {
        return 0U;
    }

    return s_net_config.ip_addr;
}

/**
 * @brief 设置子网掩码
 *
 * @param netmask    子网掩码（网络字节序）
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_set_netmask(uint32_t netmask)
{
    if (netmask == 0U)
    {
        return -22; /* EINVAL */
    }

    if (!s_config_initialized)
    {
        return -19; /* ENODEV */
    }

    s_net_config.netmask = netmask;

    return 0;
}

/**
 * @brief 获取子网掩码
 *
 * @return 子网掩码（网络字节序）
 */
uint32_t net_config_get_netmask(void)
{
    if (!s_config_initialized)
    {
        return 0U;
    }

    return s_net_config.netmask;
}

/**
 * @brief 设置网关
 *
 * @param gateway    网关 IP 地址（网络字节序）
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_set_gateway(uint32_t gateway)
{
    if (gateway == 0U)
    {
        return -22; /* EINVAL */
    }

    if (!s_config_initialized)
    {
        return -19; /* ENODEV */
    }

    s_net_config.gateway = gateway;

    return 0;
}

/**
 * @brief 获取网关
 *
 * @return 网关 IP 地址（网络字节序）
 */
uint32_t net_config_get_gateway(void)
{
    if (!s_config_initialized)
    {
        return 0U;
    }

    return s_net_config.gateway;
}

/**
 * @brief 设置 MAC 地址
 *
 * @param mac_addr    MAC 地址（6 字节）
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_set_mac(const uint8_t *mac_addr)
{
    if (mac_addr == NULL)
    {
        return -22; /* EINVAL */
    }

    if (!s_config_initialized)
    {
        return -19; /* ENODEV */
    }

    (void)memcpy(s_net_config.mac_addr, mac_addr, 6U);

    return 0;
}

/**
 * @brief 获取 MAC 地址
 *
 * @param mac_addr    输出 MAC 地址（6 字节）
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_get_mac(uint8_t *mac_addr)
{
    if (mac_addr == NULL)
    {
        return -22; /* EINVAL */
    }

    if (!s_config_initialized)
    {
        return -19; /* ENODEV */
    }

    (void)memcpy(mac_addr, s_net_config.mac_addr, 6U);

    return 0;
}

/**
 * @brief 启动网络接口
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_up(void)
{
    if (!s_config_initialized)
    {
        return -19; /* ENODEV */
    }

    s_net_config.up = true;

    return 0;
}

/**
 * @brief 关闭网络接口
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_down(void)
{
    if (!s_config_initialized)
    {
        return -19; /* ENODEV */
    }

    s_net_config.up = false;

    return 0;
}

/**
 * @brief 检查网络接口是否启动
 *
 * @return true 启动，false 关闭
 */
bool net_config_is_up(void)
{
    if (!s_config_initialized)
    {
        return false;
    }

    return s_net_config.up;
}

/**
 * @brief 使能 DHCP
 *
 * @param enable    true 使能，false 禁用
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_dhcp_enable(bool enable)
{
    if (!s_config_initialized)
    {
        return -19; /* ENODEV */
    }

    s_net_config.dhcp_enabled = enable;

    return 0;
}

/**
 * @brief 获取网络配置
 *
 * @param config    输出网络配置
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_get(net_config_t *config)
{
    if (config == NULL)
    {
        return -22; /* EINVAL */
    }

    if (!s_config_initialized)
    {
        return -19; /* ENODEV */
    }

    (void)memcpy(config, &s_net_config, sizeof(net_config_t));

    return 0;
}
