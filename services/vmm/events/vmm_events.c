/**
 * @file    vmm_events.c
 * @brief   VM 事件管理实现
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 本文件实现了 VM 事件管理的所有功能：
 *          - 事件队列管理
 *          - 事件创建/销毁
 *          - 事件添加/移除
 *          - 事件等待/通知
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */
#include "vmm_events.h"
#include <stdint.h>
#include <string.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <kernel/mm/mem.h>
#include "vmm.h"

/* ========================================================================
 * 内部状态
 * ======================================================================== */

/** @brief 全局事件队列 */
static vmm_event_queue_t s_event_queue;

/** @brief 全局事件表 */
static vmm_event_desc_t s_events[16];

/** @brief 事件回调函数 */
static void (*s_event_callback)(vmm_event_desc_t *event) = NULL;

/** @brief 事件管理器初始化标志 */
static bool s_events_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 检查事件队列是否为空
 *
 * @return true=空, false=不空
 */
static bool vmm_events_is_empty(void)
{
    return (s_event_queue.size == 0U);
}

/**
 * @brief 检查事件队列是否已满
 *
 * @return true=满, false=不满
 */
static bool vmm_events_is_full(void)
{
    return (s_event_queue.size >= s_event_queue.capacity);
}

/**
 * @brief 计算下一个头索引
 *
 * @return 下一个头索引
 */
static uint32_t vmm_events_next_head(void)
{
    return (s_event_queue.head + 1U) % s_event_queue.capacity;
}

/**
 * @brief 计算下一个尾索引
 *
 * @return 下一个尾索引
 */
static uint32_t vmm_events_next_tail(void)
{
    return (s_event_queue.tail + 1U) % s_event_queue.capacity;
}

/* ========================================================================
 * 公共 API - 事件管理器初始化/销毁
 * ======================================================================== */

kernel_status_t vmm_events_init(uint32_t capacity)
{
    /* 检查是否已经初始化 */
    if (s_events_initialized)
    {
        return -(int32_t)EPERM;
    }

    /* 检查容量是否有效 */
    if (capacity == 0U || capacity > 16U)
    {
        return -(int32_t)EINVAL;
    }

    /* 初始化事件队列 */
    (void)memset(&s_event_queue, 0, sizeof(vmm_event_queue_t));
    s_event_queue.capacity = capacity;
    s_event_queue.size = 0U;
    s_event_queue.head = 0U;
    s_event_queue.tail = 0U;

    /* 初始化事件表 */
    (void)memset(s_events, 0, sizeof(s_events));
    for (uint32_t i = 0U; i < capacity; i++)
    {
        s_events[i].type = VMM_EVENT_MAX;
    }

    /* 标记为已初始化 */
    s_events_initialized = true;

    return KERNEL_OK;
}

kernel_status_t vmm_events_destroy(void)
{
    if (!s_events_initialized)
    {
        return -(int32_t)EPERM;
    }

    /* 清空事件队列 */
    s_event_queue.size = 0U;
    s_event_queue.head = 0U;
    s_event_queue.tail = 0U;

    /* 清空事件表 */
    (void)memset(s_events, 0, sizeof(s_events));

    /* 清空回调 */
    s_event_callback = NULL;

    /* 标记为未初始化 */
    s_events_initialized = false;

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 事件创建/销毁
 * ======================================================================== */

kernel_status_t vmm_events_create(vmm_event_type_t type,
                                    uint32_t vm_id,
                                    uint32_t vcpu_id,
                                    vmm_event_desc_t **event)
{
    uint32_t i;

    if (!s_events_initialized)
    {
        return -(int32_t)EPERM;
    }

    if (event == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (type >= VMM_EVENT_MAX)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找空闲事件槽 */
    for (i = 0U; i < s_event_queue.capacity; i++)
    {
        if (s_events[i].type == VMM_EVENT_MAX)
        {
            break;
        }
    }

    if (i >= s_event_queue.capacity)
    {
        return -(int32_t)ENOMEM;
    }

    /* 初始化事件描述符 */
    (void)memset(&s_events[i], 0, sizeof(vmm_event_desc_t));
    s_events[i].type = type;
    s_events[i].vm_id = vm_id;
    s_events[i].vcpu_id = vcpu_id;
    s_events[i].is_pending = false;
    s_events[i].user_data = NULL;
    s_events[i].callback = NULL;

    /* 返回事件指针 */
    *event = &s_events[i];

    return KERNEL_OK;
}

kernel_status_t vmm_events_destroy_event(vmm_event_desc_t *event)
{
    if (!s_events_initialized)
    {
        return -(int32_t)EPERM;
    }

    if (event == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 清空事件描述符 */
    (void)memset(event, 0, sizeof(vmm_event_desc_t));
    event->type = VMM_EVENT_MAX;

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 事件队列操作
 * ======================================================================== */

kernel_status_t vmm_events_add(vmm_event_desc_t *event)
{
    if (!s_events_initialized)
    {
        return -(int32_t)EPERM;
    }

    if (event == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (vmm_events_is_full())
    {
        return -(int32_t)EBUSY;
    }

    /* 标记为待处理 */
    event->is_pending = true;

    /* 添加到队列 */
    s_event_queue.events[s_event_queue.tail] = *event;
    s_event_queue.tail = vmm_events_next_tail();
    s_event_queue.size++;

    return KERNEL_OK;
}

kernel_status_t vmm_events_remove(vmm_event_desc_t **event)
{
    if (!s_events_initialized)
    {
        return -(int32_t)EPERM;
    }

    if (event == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (vmm_events_is_empty())
    {
        return -(int32_t)ENODATA;
    }

    /* 从队列中取出事件 */
    *event = &s_event_queue.events[s_event_queue.head];
    s_event_queue.head = vmm_events_next_head();
    s_event_queue.size--;

    /* 清空事件描述符 */
    (void)memset(*event, 0, sizeof(vmm_event_desc_t));
    (*event)->type = VMM_EVENT_MAX;

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 事件等待和通知
 * ======================================================================== */

kernel_status_t vmm_events_wait(uint32_t timeout)
{
    /* 简化实现：立即返回（不等待） */
    /* 完整实现需要使用内核睡眠机制 */
    
    return KERNEL_OK;
}

kernel_status_t vmm_events_notify(vmm_event_desc_t *event)
{
    if (!s_events_initialized)
    {
        return -(int32_t)EPERM;
    }

    if (event == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 调用回调函数 */
    if (s_event_callback != NULL && event->callback != NULL)
    {
        s_event_callback(event);
    }
    else if (s_event_callback != NULL)
    {
        s_event_callback(event);
    }

    /* 清空事件描述符 */
    (void)memset(event, 0, sizeof(vmm_event_desc_t));
    event->type = VMM_EVENT_MAX;

    return KERNEL_OK;
}

kernel_status_t vmm_events_wait_and_notify(uint32_t timeout)
{
    vmm_event_desc_t *event;

    /* 等待事件 */
    kernel_status_t ret = vmm_events_remove(&event);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    /* 通知事件 */
    return vmm_events_notify(event);
}

kernel_status_t vmm_events_clear(void)
{
    if (!s_events_initialized)
    {
        return -(int32_t)EPERM;
    }

    /* 清空队列 */
    s_event_queue.size = 0U;
    s_event_queue.head = 0U;
    s_event_queue.tail = 0U;

    /* 清空事件表 */
    (void)memset(s_events, 0, sizeof(s_events));

    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 回调注册
 * ======================================================================== */

kernel_status_t vmm_events_register_callback(void (*callback)(vmm_event_desc_t *event))
{
    if (!s_events_initialized)
    {
        return -(int32_t)EPERM;
    }

    s_event_callback = callback;

    return KERNEL_OK;
}

kernel_status_t vmm_events_unregister_callback(void (*callback)(vmm_event_desc_t *event))
{
    if (!s_events_initialized)
    {
        return -(int32_t)EPERM;
    }

    if (s_event_callback == callback)
    {
        s_event_callback = NULL;
    }

    return KERNEL_OK;
}
