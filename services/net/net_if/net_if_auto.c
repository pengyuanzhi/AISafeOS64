/**
 * @file    net_if_auto.c
 * @brief   网络接口自动发现和注册接口实现
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @details 网络接口自动发现和注册接口实现
 *
 * @note 这是一个简化的实现，在完整版本中应该使用 IPC
 * @note 当前假设网络接口层是全局可访问的
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "net_if_auto.h"
#include <string.h>
#include <stdlib.h>

/* ========================================================================
 * 内部常量
 * ======================================================================== */

/** @brief 最大网络接口数 */
#define NET_IF_AUTO_MAX_INTERFACES   4U

/** @brief 接口名称长度 */
#define NET_IF_AUTO_NAME_LEN         16U

/* ========================================================================
 * 驱动表
 * ======================================================================== */

/**
 * @brief 网络接口描述符
 */
typedef struct
{
    char                name[NET_IF_AUTO_NAME_LEN];     /**< @brief 接口名称 */
    char                driver[NET_IF_AUTO_NAME_LEN];    /**< @brief 驱动名称 */
    net_if_ops_auto_t    ops_copy;                         /**< @brief 操作接口副本 */
    uint8_t             mac_addr[6];                      /**< @brief MAC 地址 */
    bool                in_use;                           /**< @brief 使用标记 */
} net_if_auto_entry_t;

/** @brief 网络接口表 */
static net_if_auto_entry_t s_net_if_auto_table[NET_IF_AUTO_MAX_INTERFACES];

/** @brief 接口数量 */
static uint32_t s_net_if_auto_count = 0U;

/* ========================================================================
 * 内部函数
 * ======================================================================== */

/**
 * @brief 查找空闲槽位
 */
static int32_t find_free_slot(void)
{
    int32_t i;

    for (i = 0; i < (int32_t)NET_IF_AUTO_MAX_INTERFACES; i++)
    {
        if (!s_net_if_auto_table[i].in_use)
        {
            return i;
        }
    }

    return -1; /* 无空闲槽位 */
}

/**
 * @brief 查找接口
 */
static int32_t find_interface(const char *name)
{
    int32_t i;

    if (name == NULL)
    {
        return -1;
    }

    for (i = 0; i < (int32_t)NET_IF_AUTO_MAX_INTERFACES; i++)
    {
        if (s_net_if_auto_table[i].in_use)
        {
            if (strcmp(s_net_if_auto_table[i].name, name) == 0)
            {
                return i;
            }
        }
    }

    return -1; /* 未找到 */
}

/* ========================================================================
 * 公共接口实现
 * ======================================================================== */

int32_t net_if_auto_register(const char *name, const char *driver,
                                const net_if_ops_auto_t *ops,
                                const uint8_t mac_addr[6])
{
    int32_t slot;
    net_if_auto_entry_t *iface;

    if ((name == NULL) || (driver == NULL) || (ops == NULL))
    {
        return -22; /* EINVAL */
    }

    /* 查找空闲槽位 */
    slot = find_free_slot();
    if (slot < 0)
    {
        return -28; /* ENOSPC */
    }

    /* 填充接口信息 */
    iface = &s_net_if_auto_table[slot];
    (void)strncpy(iface->name, name, NET_IF_AUTO_NAME_LEN - 1U);
    iface->name[NET_IF_AUTO_NAME_LEN - 1U] = '\0';
    (void)strncpy(iface->driver, driver, NET_IF_AUTO_NAME_LEN - 1U);
    iface->driver[NET_IF_AUTO_NAME_LEN - 1U] = '\0';
    iface->ops_copy = *ops;  /* 复制操作接口 */
    
    /* 复制 MAC 地址 */
    if (mac_addr != NULL)
    {
        (void)memcpy(iface->mac_addr, mac_addr, 6);
    }
    else
    {
        (void)memset(iface->mac_addr, 0, 6);
    }
    
    iface->in_use = true;

    s_net_if_auto_count++;

    return 0;
}

int32_t net_if_auto_unregister(const char *name)
{
    int32_t idx;

    if (name == NULL)
    {
        return -22; /* EINVAL */
    }

    idx = find_interface(name);
    if (idx < 0)
    {
        return -2; /* ENODEV */
    }

    (void)memset(&s_net_if_auto_table[idx], 0, sizeof(net_if_auto_entry_t));
    s_net_if_auto_count--;

    return 0;
}

uint32_t net_if_auto_get_count(void)
{
    return s_net_if_auto_count;
}

int32_t net_if_auto_get_name(uint32_t index, char *name, uint32_t len)
{
    if (name == NULL)
    {
        return -22; /* EINVAL */
    }

    if (index >= s_net_if_auto_count)
    {
        return -2; /* ENODEV */
    }

    if (len < NET_IF_AUTO_NAME_LEN)
    {
        return -22; /* EINVAL */
    }

    if (!s_net_if_auto_table[index].in_use)
    {
        return -2; /* ENODEV */
    }

    (void)strncpy(name, s_net_if_auto_table[index].name, NET_IF_AUTO_NAME_LEN - 1);
    name[NET_IF_AUTO_NAME_LEN - 1] = '\0';

    return 0;
}

const net_if_ops_auto_t *net_if_auto_get_ops(const char *name)
{
    int32_t idx;

    if (name == NULL)
    {
        return NULL;
    }

    idx = find_interface(name);
    if (idx >= 0)
    {
        return &s_net_if_auto_table[idx].ops_copy;
    }

    return NULL;
}

int32_t net_if_auto_get_mac_addr(uint32_t index, uint8_t mac_addr[6])
{
    if (mac_addr == NULL)
    {
        return -22; /* EINVAL */
    }

    if (index >= s_net_if_auto_count)
    {
        return -2; /* ENODEV */
    }

    if (!s_net_if_auto_table[index].in_use)
    {
        return -2; /* ENODEV */
    }

    (void)memcpy(mac_addr, s_net_if_auto_table[index].mac_addr, 6);

    return 0;
}
