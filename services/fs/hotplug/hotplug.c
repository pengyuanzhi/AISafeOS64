/**
 * @file    hotplug.c
 * @brief   热插拔支持实现
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details 热插拔支持实现：
 *          - 设备插入/移除事件处理
 *          - 热插拔回调管理
 *          - 设备自动挂载/卸载
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "hotplug.h"
#include "partition.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ========================================================================
 * 热插拔管理器
 * ======================================================================== */

static hotplug_manager_t s_hotplug_mgr;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 查找空闲设备槽位
 */
static hotplug_device_t *find_free_device(void)
{
    uint32_t i;

    for (i = 0U; i < HOTPLUG_MAX_DEVICES; i++)
    {
        if (!s_hotplug_mgr.devices[i].in_use)
        {
            return &s_hotplug_mgr.devices[i];
        }
    }

    return NULL;
}

/**
 * @brief 分发热插拔事件到所有回调
 */
static void dispatch_event(const hotplug_event_t *event)
{
    uint32_t i;

    for (i = 0U; i < 16U; i++)
    {
        if (s_hotplug_mgr.callbacks[i] != NULL)
        {
            (void)s_hotplug_mgr.callbacks[i](event,
                                               s_hotplug_mgr.callback_data[i]);
        }
    }
}

/* ========================================================================
 * 热插拔管理接口实现
 * ======================================================================== */

/**
 * @brief 初始化热插拔管理器
 */
int32_t hotplug_init(void)
{
    uint32_t i;

    /* 清零管理器 */
    (void)memset(&s_hotplug_mgr, 0, sizeof(hotplug_manager_t));

    /* 清空设备表 */
    for (i = 0U; i < HOTPLUG_MAX_DEVICES; i++)
    {
        (void)memset(&s_hotplug_mgr.devices[i], 0, sizeof(hotplug_device_t));
    }

    s_hotplug_mgr.event_id = 1U;
    s_hotplug_mgr.initialized = true;

    printf("[Hotplug] Hotplug manager initialized\n");

    return 0;
}

/**
 * @brief 启动热插拔监控
 */
int32_t hotplug_start(void)
{
    if (!s_hotplug_mgr.initialized)
    {
        return -1;
    }

    /* TODO: 启动设备监控线程 */
    printf("[Hotplug] Hotplug monitoring started\n");

    return 0;
}

/**
 * @brief 停止热插拔监控
 */
int32_t hotplug_stop(void)
{
    if (!s_hotplug_mgr.initialized)
    {
        return -1;
    }

    /* TODO: 停止设备监控线程 */
    printf("[Hotplug] Hotplug monitoring stopped\n");

    return 0;
}

/* ========================================================================
 * 设备管理接口实现
 * ======================================================================== */

/**
 * @brief 添加设备
 */
int32_t hotplug_add_device(const char *device_path,
                              hotplug_dev_type_t type,
                              uint64_t size_in_sectors)
{
    hotplug_device_t *dev;

    if (device_path == NULL || !s_hotplug_mgr.initialized)
    {
        return -1;
    }

    /* 检查设备路径长度 */
    if (strlen(device_path) >= HOTPLUG_MAX_PATH_LEN)
    {
        return -1;
    }

    /* 检查设备是否已存在 */
    if (hotplug_find_device(device_path) != NULL)
    {
        return -1;
    }

    /* 分配设备槽位 */
    dev = find_free_device();
    if (dev == NULL)
    {
        return -1;
    }

    /* 填充设备信息 */
    (void)strncpy(dev->device_path, device_path, HOTPLUG_MAX_PATH_LEN - 1U);
    dev->device_path[HOTPLUG_MAX_PATH_LEN - 1U] = '\0';
    dev->type = type;
    dev->state = HOTPLUG_DEV_STATE_CONNECTED;
    dev->size_in_sectors = size_in_sectors;
    dev->ref_count = 0U;
    dev->in_use = true;
    dev->mounted = false;
    dev->mount_point[0] = '\0';

    printf("[Hotplug] Device added: %s, type: %u, size: %llu sectors\n",
           device_path, (uint32_t)type, size_in_sectors);

    /* 触发设备插入事件 */
    (void)hotplug_trigger_insert(device_path, type, size_in_sectors);

    return 0;
}

/**
 * @brief 移除设备
 */
int32_t hotplug_remove_device(const char *device_path)
{
    hotplug_device_t *dev;
    uint32_t i;

    if (device_path == NULL || !s_hotplug_mgr.initialized)
    {
        return -1;
    }

    /* 查找设备 */
    dev = hotplug_find_device(device_path);
    if (dev == NULL)
    {
        return -1;
    }

    /* 检查设备是否被使用 */
    if (dev->ref_count > 0U)
    {
        printf("[Hotplug] Device %s is in use (ref_count=%u), cannot remove\n",
               device_path, dev->ref_count);
        return -1;
    }

    /* 检查设备是否已挂载 */
    if (dev->mounted)
    {
        printf("[Hotplug] Device %s is mounted at %s, unmounting...\n",
               device_path, dev->mount_point);

        /* TODO: 调用 fs_unmount 卸载文件系统 */
    }

    /* 清空设备信息 */
    dev->in_use = false;

    printf("[Hotplug] Device removed: %s\n", device_path);

    /* 触发设备移除事件 */
    (void)hotplug_trigger_remove(device_path);

    return 0;
}

/**
 * @brief 查找设备
 */
hotplug_device_t *hotplug_find_device(const char *device_path)
{
    uint32_t i;

    if (device_path == NULL)
    {
        return NULL;
    }

    for (i = 0U; i < HOTPLUG_MAX_DEVICES; i++)
    {
        if (s_hotplug_mgr.devices[i].in_use &&
            strcmp(s_hotplug_mgr.devices[i].device_path, device_path) == 0)
        {
            return &s_hotplug_mgr.devices[i];
        }
    }

    return NULL;
}

/**
 * @brief 设置设备挂载点
 */
int32_t hotplug_set_mount_point(const char *device_path,
                                  const char *mount_point)
{
    hotplug_device_t *dev;

    if (device_path == NULL || mount_point == NULL)
    {
        return -1;
    }

    /* 查找设备 */
    dev = hotplug_find_device(device_path);
    if (dev == NULL)
    {
        return -1;
    }

    /* 设置挂载点 */
    (void)strncpy(dev->mount_point, mount_point, HOTPLUG_MAX_PATH_LEN - 1U);
    dev->mount_point[HOTPLUG_MAX_PATH_LEN - 1U] = '\0';
    dev->mounted = true;

    printf("[Hotplug] Device %s mounted at %s\n", device_path, mount_point);

    return 0;
}

/* ========================================================================
 * 回调管理接口实现
 * ======================================================================== */

/**
 * @brief 注册热插拔回调
 */
int32_t hotplug_register_callback(hotplug_callback_t callback,
                                    void *user_data)
{
    uint32_t i;

    if (callback == NULL)
    {
        return -1;
    }

    /* 分配回调槽位 */
    for (i = 0U; i < 16U; i++)
    {
        if (s_hotplug_mgr.callbacks[i] == NULL)
        {
            s_hotplug_mgr.callbacks[i] = callback;
            s_hotplug_mgr.callback_data[i] = user_data;

            printf("[Hotplug] Callback registered, id: %u\n", i);

            return (int32_t)i;
        }
    }

    return -1;
}

/**
 * @brief 注销热插拔回调
 */
int32_t hotplug_unregister_callback(uint32_t callback_id)
{
    if (callback_id >= 16U)
    {
        return -1;
    }

    s_hotplug_mgr.callbacks[callback_id] = NULL;
    s_hotplug_mgr.callback_data[callback_id] = NULL;

    printf("[Hotplug] Callback unregistered, id: %u\n", callback_id);

    return 0;
}

/* ========================================================================
 * 事件处理接口实现
 * ======================================================================== */

/**
 * @brief 处理热插拔事件
 */
int32_t hotplug_handle_event(const hotplug_event_t *event)
{
    if (event == NULL)
    {
        return -1;
    }

    printf("[Hotplug] Event: type=%u, device=%s, timestamp=%llu\n",
           (uint32_t)event->type, event->device_path, event->timestamp);

    /* 分发事件到所有回调 */
    dispatch_event(event);

    return 0;
}

/**
 * @brief 触发设备插入事件
 */
int32_t hotplug_trigger_insert(const char *device_path,
                                  hotplug_dev_type_t type,
                                  uint64_t size_in_sectors)
{
    hotplug_event_t event;

    (void)memset(&event, 0, sizeof(hotplug_event_t));

    event.type = HOTPLUG_EVENT_INSERT;
    event.dev_type = type;
    (void)strncpy(event.device_path, device_path, HOTPLUG_MAX_PATH_LEN - 1U);
    event.device_path[HOTPLUG_MAX_PATH_LEN - 1U] = '\0';
    event.timestamp = 0ULL; /* TODO: 获取当前时间戳 */
    event.event_id = s_hotplug_mgr.event_id++;

    return hotplug_handle_event(&event);
}

/**
 * @brief 触发设备移除事件
 */
int32_t hotplug_trigger_remove(const char *device_path)
{
    hotplug_event_t event;

    (void)memset(&event, 0, sizeof(hotplug_event_t));

    event.type = HOTPLUG_EVENT_REMOVE;
    event.dev_type = HOTPLUG_DEV_TYPE_BLOCK; /* TODO: 查找设备类型 */
    (void)strncpy(event.device_path, device_path, HOTPLUG_MAX_PATH_LEN - 1U);
    event.device_path[HOTPLUG_MAX_PATH_LEN - 1U] = '\0';
    event.timestamp = 0ULL; /* TODO: 获取当前时间戳 */
    event.event_id = s_hotplug_mgr.event_id++;

    return hotplug_handle_event(&event);
}
