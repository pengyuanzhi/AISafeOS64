/**
 * @file    net_if.c
 * @brief   网络接口抽象层实现
 * @author  AISafe64 Team
 * @date    2026-04-16
 * @version 1.0
 *
 * @details 网络接口抽象层：
 *          - 驱动注册和管理
 *          - 以太网帧发送/接收
 *          - 多驱动支持
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: NW-001, NW-002
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "net_if.h"
#include <string.h>
#include <stdlib.h>

/* ========================================================================
 * 内部常量
 * ======================================================================== */

/** @brief 最大网络接口数 */
#define NET_IF_MAX_INTERFACES   4U

/** @brief 接口名称长度 */
#define NET_IF_NAME_LEN         16U

/* ========================================================================
 * 驱动表
 * ======================================================================== */

/**
 * @brief 网络接口描述符
 */
typedef struct
{
    char                name[NET_IF_NAME_LEN];     /**< @brief 接口名称 */
    char                driver[NET_IF_NAME_LEN];    /**< @brief 驱动名称 */
    net_if_ops_t        ops;                        /**< @brief 操作接口 */
    bool                in_use;                     /**< @brief 使用标记 */
} net_if_entry_t;

/** @brief 网络接口表 */
static net_if_entry_t s_net_if_table[NET_IF_MAX_INTERFACES];

/** @brief 接口数量 */
static uint32_t s_net_if_count = 0U;

/* ========================================================================
 * 内部函数
 * ======================================================================== */

/**
 * @brief 查找空闲槽位
 */
static int32_t find_free_slot(void)
{
    int32_t i;

    for (i = 0; i < (int32_t)NET_IF_MAX_INTERFACES; i++)
    {
        if (!s_net_if_table[i].in_use)
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

    for (i = 0; i < (int32_t)NET_IF_MAX_INTERFACES; i++)
    {
        if (s_net_if_table[i].in_use)
        {
            if (strcmp(s_net_if_table[i].name, name) == 0)
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

int32_t net_if_register(const char *name, const char *driver,
                        const net_if_ops_t *ops, const uint8_t mac_addr[6])
{
    int32_t slot;
    net_if_entry_t *iface;

    (void)mac_addr;  /* 避免编译警告 */

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
    iface = &s_net_if_table[slot];
    (void)strncpy(iface->name, name, NET_IF_NAME_LEN - 1U);
    iface->name[NET_IF_NAME_LEN - 1U] = '\0';
    (void)strncpy(iface->driver, driver, NET_IF_NAME_LEN - 1U);
    iface->driver[NET_IF_NAME_LEN - 1U] = '\0';
    iface->ops = *ops;  /* 复制操作接口 */
    iface->in_use = true;

    s_net_if_count++;

    return 0;
}

const net_if_ops_t *net_if_get_ops(const char *name)
{
    int32_t i;

    if (name == NULL)
    {
        return NULL;
    }

    i = find_interface(name);
    if (i >= 0)
    {
        return &s_net_if_table[i].ops;
    }

    return NULL;
}

int64_t net_if_send_frame(const char *name, const void *buf, uint64_t size)
{
    const net_if_ops_t *ops;

    if (name == NULL)
    {
        return -22; /* EINVAL */
    }

    /* 获取接口操作接口 */
    ops = net_if_get_ops(name);
    if (ops == NULL)
    {
        return -2; /* ENODEV */
    }

    if ((buf == NULL) || (size == 0U))
    {
        return -22; /* EINVAL */
    }

    /* 调用驱动接口 */
    return ops->send_frame(buf, size);
}

int64_t net_if_recv_frame(const char *name, void *buf, uint64_t size)
{
    const net_if_ops_t *ops;

    if (name == NULL)
    {
        return -22; /* EINVAL */
    }

    /* 获取接口操作接口 */
    ops = net_if_get_ops(name);
    if (ops == NULL)
    {
        return -2; /* ENODEV */
    }

    if ((buf == NULL) || (size == 0U))
    {
        return -22; /* EINVAL */
    }

    /* 调用驱动接口 */
    return ops->recv_frame(buf, size);
}

uint32_t net_if_get_count(void)
{
    return s_net_if_count;
}
