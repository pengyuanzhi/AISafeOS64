/**
 * @file    net_config.h
 * @brief   网络配置头文件
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details 网络配置接口：
 *          - IP 地址配置
 *          - 子网掩码配置
 *          - 网关配置
 *          - 网络接口管理
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef NET_CONFIG_H
#define NET_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 网络配置接口
 * ======================================================================== */

/**
 * @brief 初始化网络配置
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_init(void);

/**
 * @brief 设置 IP 地址
 *
 * @param ip_addr    IP 地址（网络字节序）
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_set_ip(uint32_t ip_addr);

/**
 * @brief 获取 IP 地址
 *
 * @return IP 地址（网络字节序）
 */
uint32_t net_config_get_ip(void);

/**
 * @brief 设置子网掩码
 *
 * @param netmask    子网掩码（网络字节序）
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_set_netmask(uint32_t netmask);

/**
 * @brief 获取子网掩码
 *
 * @return 子网掩码（网络字节序）
 */
uint32_t net_config_get_netmask(void);

/**
 * @brief 设置网关
 *
 * @param gateway    网关 IP 地址（网络字节序）
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_set_gateway(uint32_t gateway);

/**
 * @brief 获取网关
 *
 * @return 网关 IP 地址（网络字节序）
 */
uint32_t net_config_get_gateway(void);

/**
 * @brief 设置 MAC 地址
 *
 * @param mac_addr    MAC 地址（6 字节）
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_set_mac(const uint8_t *mac_addr);

/**
 * @brief 获取 MAC 地址
 *
 * @param mac_addr    输出 MAC 地址（6 字节）
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_get_mac(uint8_t *mac_addr);

/**
 * @brief 启动网络接口
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_up(void);

/**
 * @brief 关闭网络接口
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_down(void);

/**
 * @brief 检查网络接口是否启动
 *
 * @return true 启动，false 关闭
 */
bool net_config_is_up(void);

/**
 * @brief 使能 DHCP
 *
 * @param enable    true 使能，false 禁用
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_dhcp_enable(bool enable);

/**
 * @brief 获取网络配置
 *
 * @param config    输出网络配置
 *
 * @return 0 成功，<0 失败
 */
int32_t net_config_get(net_config_t *config);

#endif /* NET_CONFIG_H */
