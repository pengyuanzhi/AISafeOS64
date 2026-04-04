/**
 * @file    channel.c
 * @brief   IPC 通道、连接和 Pulse 管理
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件实现了 QNX 风格的通道-连接模型和 Pulse 消息机制：
 *          - ipc_channel_subsys_init:         通道子系统初始化
 *          - ipc_channel_create:              创建通道
 *          - ipc_channel_destroy:             销毁通道
 *          - ipc_connect_attach:              客户端连接到通道
 *          - ipc_connect_detach:              断开连接
 *          - ipc_pulse_send:                  发送 Pulse
 *          - ipc_pulse_receive:               非阻塞接收 Pulse
 *          - ipc_pulse_receive_blocking:      阻塞接收 Pulse
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-007（Pulse 轻量级消息）、KR-023（通道-连接模型）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/ipc_channel.h>
#include <kernel/ipc_types.h>
#include <kernel/spinlock.h>
#include <kernel/barrier.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <kernel/compiler.h>
#include "thread.h"
#include "scheduler.h"
#include <stdint.h>
#include <string.h>
#include "hal.h"

/* ========================================================================
 * 全局通道和连接表
 * ======================================================================== */

/**
 * @brief 全局通道对象表（静态分配）
 */
static ipc_channel_t s_channels[CONFIG_IPC_MAX_CHANNELS];

/**
 * @brief 全局连接对象表（静态分配）
 */
static ipc_connection_t s_connections[CONFIG_IPC_MAX_CONNECTIONS];

/**
 * @brief 空闲通道索引栈 */
static uint32_t s_free_ch_stack[CONFIG_IPC_MAX_CHANNELS];

/** @brief 空闲通道计数 */
static uint32_t s_free_ch_count;

/** @brief 空闲连接索引栈 */
static uint32_t s_free_conn_stack[CONFIG_IPC_MAX_CONNECTIONS];

/** @brief 空闲连接计数 */
static uint32_t s_free_conn_count;

/** @brief 通道子系统全局锁 */
static TicketLock_t s_ch_subsys_lock;

/** @brief 全局 Pulse 对象池（避免动态分配） */
static ipc_pulse_t s_pulse_pool[CONFIG_IPC_MAX_PULSE_QUEUE * CONFIG_IPC_MAX_CHANNELS];

/** @brief Pulse 池全局锁 */
static TicketLock_t s_pulse_pool_lock;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 分配空闲通道索引
 */
static int32_t alloc_channel_index(void)
{
    int32_t idx;

    ticket_lock_acquire(&s_ch_subsys_lock);

    if (s_free_ch_count == 0U)
    {
        ticket_lock_release(&s_ch_subsys_lock);
        return -1;
    }

    s_free_ch_count--;
    idx = (int32_t)s_free_ch_stack[s_free_ch_count];

    ticket_lock_release(&s_ch_subsys_lock);

    return idx;
}

/**
 * @brief 释放通道索引
 */
static void free_channel_index(uint32_t idx)
{
    ticket_lock_acquire(&s_ch_subsys_lock);
    s_free_ch_stack[s_free_ch_count] = idx;
    s_free_ch_count++;
    ticket_lock_release(&s_ch_subsys_lock);
}

/**
 * @brief 分配空闲连接索引
 */
static int32_t alloc_connection_index(void)
{
    int32_t idx;

    ticket_lock_acquire(&s_ch_subsys_lock);

    if (s_free_conn_count == 0U)
    {
        ticket_lock_release(&s_ch_subsys_lock);
        return -1;
    }

    s_free_conn_count--;
    idx = (int32_t)s_free_conn_stack[s_free_conn_count];

    ticket_lock_release(&s_ch_subsys_lock);

    return idx;
}

/**
 * @brief 释放连接索引
 */
static void free_connection_index(uint32_t idx)
{
    ticket_lock_acquire(&s_ch_subsys_lock);
    s_free_conn_stack[s_free_conn_count] = idx;
    s_free_conn_count++;
    ticket_lock_release(&s_ch_subsys_lock);
}

/**
 * @brief 通过 ID 获取通道指针
 */
static ipc_channel_t *get_channel(kobj_id_t ch_id)
{
    uint32_t idx;

    if (ch_id == KOBJ_ID_INVALID)
    {
        return NULL;
    }

    idx = (uint32_t)(ch_id & 0xFFFFU);
    if (idx >= CONFIG_IPC_MAX_CHANNELS)
    {
        return NULL;
    }

    return &s_channels[idx];
}

/**
 * @brief 通过 ID 获取连接指针
 */
static ipc_connection_t *get_connection(kobj_id_t conn_id)
{
    uint32_t idx;

    if (conn_id == KOBJ_ID_INVALID)
    {
        return NULL;
    }

    idx = (uint32_t)(conn_id & 0xFFFFU);
    if (idx >= CONFIG_IPC_MAX_CONNECTIONS)
    {
        return NULL;
    }

    return &s_connections[idx];
}

/* ========================================================================
 * 通道子系统初始化
 * ======================================================================== */

kernel_status_t ipc_channel_subsys_init(void)
{
    uint32_t i;

    /* 初始化通道表 */
    for (i = 0U; i < CONFIG_IPC_MAX_CHANNELS; i++)
    {
        s_free_ch_stack[i] = (CONFIG_IPC_MAX_CHANNELS - 1U) - i;
        s_free_ch_count = i + 1U;

        s_channels[i].id = KOBJ_ID_INVALID;
        s_channels[i].state = IPC_CH_CLOSED;
        s_channels[i].owner_tid = THREAD_ID_INVALID;
        s_channels[i].conn_list.next = &s_channels[i].conn_list;
        s_channels[i].conn_list.prev = &s_channels[i].conn_list;
        s_channels[i].pulse_queue.next = &s_channels[i].pulse_queue;
        s_channels[i].pulse_queue.prev = &s_channels[i].pulse_queue;
        s_channels[i].pulse_count = 0U;
        s_channels[i].node.next = &s_channels[i].node;
        s_channels[i].node.prev = &s_channels[i].node;
        ticket_lock_init(&s_channels[i].lock);
    }

    /* 初始化连接表 */
    for (i = 0U; i < CONFIG_IPC_MAX_CONNECTIONS; i++)
    {
        s_free_conn_stack[i] = (CONFIG_IPC_MAX_CONNECTIONS - 1U) - i;
        s_free_conn_count = i + 1U;

        s_connections[i].id = KOBJ_ID_INVALID;
        s_connections[i].state = IPC_CONN_DISCONNECTED;
        s_connections[i].channel_id = KOBJ_ID_INVALID;
        s_connections[i].client_tid = THREAD_ID_INVALID;
        s_connections[i].pending_msg = NULL;
        s_connections[i].ch_node.next = &s_connections[i].ch_node;
        s_connections[i].ch_node.prev = &s_connections[i].ch_node;
        s_connections[i].node.next = &s_connections[i].node;
        s_connections[i].node.prev = &s_connections[i].node;
    }

    ticket_lock_init(&s_ch_subsys_lock);
    ticket_lock_init(&s_pulse_pool_lock);

    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 创建通道
 * ======================================================================== */

kernel_status_t ipc_channel_create(thread_id_t owner_tid, kobj_id_t *ch_id)
{
    int32_t idx;
    ipc_channel_t *ch;

    if (ch_id == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (owner_tid >= CONFIG_MAX_THREADS)
    {
        return -(int32_t)EINVAL;
    }

    idx = alloc_channel_index();
    if (idx < 0)
    {
        return -(int32_t)ENOMEM;
    }

    ch = &s_channels[(uint32_t)idx];

    ch->id = (kobj_id_t)((uint32_t)idx | ((uint32_t)idx << 16U));
    ch->state = IPC_CH_OPEN;
    ch->owner_tid = owner_tid;
    ch->conn_list.next = &ch->conn_list;
    ch->conn_list.prev = &ch->conn_list;
    ch->pulse_queue.next = &ch->pulse_queue;
    ch->pulse_queue.prev = &ch->pulse_queue;
    ch->pulse_count = 0U;
    ch->node.next = &ch->node;
    ch->node.prev = &ch->node;
    ticket_lock_init(&ch->lock);

    barrier();

    *ch_id = ch->id;

    return KERNEL_OK;
}

/* ========================================================================
 * 销毁通道
 * ======================================================================== */

kernel_status_t ipc_channel_destroy(kobj_id_t ch_id)
{
    ipc_channel_t *ch;
    uint32_t irq_state;

    ch = get_channel(ch_id);
    if (ch == NULL)
    {
        return -(int32_t)EINVAL;
    }

    irq_state = ticket_lock_acquire_irqsave(&ch->lock);

    if (ch->state != IPC_CH_OPEN)
    {
        ticket_lock_release_irqrestore(&ch->lock, irq_state);
        return -(int32_t)EINVAL;
    }

    /* 断开所有连接 */
    while (ch->conn_list.next != &ch->conn_list)
    {
        struct list_head *conn_node = ch->conn_list.next;
        ipc_connection_t *conn = container_of(conn_node, ipc_connection_t, ch_node);

        /* 从通道连接列表移除 */
        conn->ch_node.prev->next = conn->ch_node.next;
        conn->ch_node.next->prev = conn->ch_node.prev;
        conn->ch_node.next = &conn->ch_node;
        conn->ch_node.prev = &conn->ch_node;

        /* 标记连接为断开 */
        conn->state = IPC_CONN_DISCONNECTED;
        conn->channel_id = KOBJ_ID_INVALID;

        /* 如果有等待消息的客户端线程，唤醒它 */
        if (conn->client_tid < CONFIG_MAX_THREADS)
        {
            KThread_t *client = &g_scheduler.thread_table[conn->client_tid];
            if (client->state == KTHREAD_STATE_BLOCKED)
            {
                client->state = KTHREAD_STATE_READY;
                scheduler_enqueue(client);
            }
        }

        free_connection_index((uint32_t)(conn->id & 0xFFFFU));
    }

    /* 清空 Pulse 队列 */
    ch->pulse_queue.next = &ch->pulse_queue;
    ch->pulse_queue.prev = &ch->pulse_queue;
    ch->pulse_count = 0U;

    /* 标记通道为关闭 */
    ch->state = IPC_CH_CLOSED;
    ch->owner_tid = THREAD_ID_INVALID;

    ticket_lock_release_irqrestore(&ch->lock, irq_state);

    /* 释放通道索引 */
    free_channel_index((uint32_t)(ch_id & 0xFFFFU));

    return KERNEL_OK;
}

/* ========================================================================
 * 连接到通道
 * ======================================================================== */

kernel_status_t ipc_connect_attach(thread_id_t client_tid,
                                    kobj_id_t ch_id,
                                    kobj_id_t *conn_id)
{
    int32_t idx;
    ipc_channel_t *ch;
    ipc_connection_t *conn;
    uint32_t irq_state;

    if (conn_id == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (client_tid >= CONFIG_MAX_THREADS)
    {
        return -(int32_t)EINVAL;
    }

    ch = get_channel(ch_id);
    if (ch == NULL)
    {
        return -(int32_t)EINVAL;
    }

    idx = alloc_connection_index();
    if (idx < 0)
    {
        return -(int32_t)ENOMEM;
    }

    conn = &s_connections[(uint32_t)idx];

    irq_state = ticket_lock_acquire_irqsave(&ch->lock);

    if (ch->state != IPC_CH_OPEN)
    {
        ticket_lock_release_irqrestore(&ch->lock, irq_state);
        free_connection_index((uint32_t)idx);
        return IPC_ERR_CHANNEL_CLOSED;
    }

    /* 初始化连接 */
    conn->id = (kobj_id_t)((uint32_t)idx | ((uint32_t)idx << 16U));
    conn->state = IPC_CONN_CONNECTED;
    conn->channel_id = ch_id;
    conn->client_tid = client_tid;
    conn->pending_msg = NULL;

    /* 加入通道的连接列表 */
    conn->ch_node.next = ch->conn_list.next;
    conn->ch_node.prev = &ch->conn_list;
    ch->conn_list.next->prev = &conn->ch_node;
    ch->conn_list.next = &conn->ch_node;

    barrier();

    ticket_lock_release_irqrestore(&ch->lock, irq_state);

    *conn_id = conn->id;

    return KERNEL_OK;
}

/* ========================================================================
 * 断开连接
 * ======================================================================== */

kernel_status_t ipc_connect_detach(kobj_id_t conn_id)
{
    ipc_connection_t *conn;
    ipc_channel_t *ch;
    uint32_t irq_state;

    conn = get_connection(conn_id);
    if (conn == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (conn->state != IPC_CONN_CONNECTED)
    {
        return -(int32_t)EINVAL;
    }

    ch = get_channel(conn->channel_id);
    if (ch == NULL)
    {
        return -(int32_t)EINVAL;
    }

    irq_state = ticket_lock_acquire_irqsave(&ch->lock);

    /* 从通道连接列表移除 */
    conn->ch_node.prev->next = conn->ch_node.next;
    conn->ch_node.next->prev = conn->ch_node.prev;
    conn->ch_node.next = &conn->ch_node;
    conn->ch_node.prev = &conn->ch_node;

    /* 如果有待处理的消息，回复错误 */
    if (conn->pending_msg != NULL)
    {
        conn->pending_msg = NULL;
    }

    /* 标记为断开 */
    conn->state = IPC_CONN_DISCONNECTED;
    conn->channel_id = KOBJ_ID_INVALID;
    conn->client_tid = THREAD_ID_INVALID;

    ticket_lock_release_irqrestore(&ch->lock, irq_state);

    free_connection_index((uint32_t)(conn_id & 0xFFFFU));

    return KERNEL_OK;
}

/* ========================================================================
 * 发送 Pulse
 * ======================================================================== */

kernel_status_t ipc_pulse_send(kobj_id_t conn_id,
                                priority_t prio,
                                int32_t code,
                                int32_t value)
{
    ipc_connection_t *conn;
    ipc_channel_t *ch;
    ipc_pulse_t *pulse;
    struct list_head *pos;
    uint32_t irq_state;

    conn = get_connection(conn_id);
    if (conn == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (conn->state != IPC_CONN_CONNECTED)
    {
        return IPC_ERR_CONN_DISCONNECTED;
    }

    ch = get_channel(conn->channel_id);
    if (ch == NULL)
    {
        return -(int32_t)EINVAL;
    }

    irq_state = ticket_lock_acquire_irqsave(&ch->lock);

    if (ch->state != IPC_CH_OPEN)
    {
        ticket_lock_release_irqrestore(&ch->lock, irq_state);
        return IPC_ERR_CHANNEL_CLOSED;
    }

    /* 检查 Pulse 队列是否已满 */
    if (ch->pulse_count >= CONFIG_IPC_MAX_PULSE_QUEUE)
    {
        ticket_lock_release_irqrestore(&ch->lock, irq_state);
        return IPC_ERR_PULSE_QUEUE_FULL;
    }

    /* 从 Pulse 池分配一个 Pulse 对象 */
    pulse = NULL;
    ticket_lock_acquire(&s_pulse_pool_lock);
    {
        /* 简化实现：使用静态数组中的空闲 Pulse */
        uint32_t pool_idx = ch->pulse_count;
        uint32_t base = (uint32_t)(ch->id & 0xFFFFU) * CONFIG_IPC_MAX_PULSE_QUEUE;
        if ((base + pool_idx) < (CONFIG_IPC_MAX_PULSE_QUEUE * CONFIG_IPC_MAX_CHANNELS))
        {
            pulse = &s_pulse_pool[base + pool_idx];
        }
    }
    ticket_lock_release(&s_pulse_pool_lock);

    if (pulse == NULL)
    {
        ticket_lock_release_irqrestore(&ch->lock, irq_state);
        return IPC_ERR_PULSE_QUEUE_FULL;
    }

    /* 填充 Pulse 内容 */
    pulse->prio = prio;
    pulse->code = code;
    pulse->value = value;
    pulse->conn_id = conn_id;
    pulse->node.next = &pulse->node;
    pulse->node.prev = &pulse->node;

    /* 按优先级插入 Pulse 队列（优先级高的在前） */
    pos = ch->pulse_queue.next;
    while (pos != &ch->pulse_queue)
    {
        ipc_pulse_t *existing = container_of(pos, ipc_pulse_t, node);
        if ((uint32_t)prio > (uint32_t)existing->prio)
        {
            break;
        }
        pos = pos->next;
    }

    /* 在 pos 前插入 */
    pulse->node.next = pos;
    pulse->node.prev = pos->prev;
    pos->prev->next = &pulse->node;
    pos->prev = &pulse->node;

    ch->pulse_count++;
    barrier();

    /* 如果通道拥有者在等待 Pulse，唤醒它 */
    if (ch->owner_tid < CONFIG_MAX_THREADS)
    {
        KThread_t *owner = &g_scheduler.thread_table[ch->owner_tid];
        if (owner->state == KTHREAD_STATE_BLOCKED)
        {
            owner->state = KTHREAD_STATE_READY;
            scheduler_enqueue(owner);
        }
    }

    ticket_lock_release_irqrestore(&ch->lock, irq_state);

    return KERNEL_OK;
}

/* ========================================================================
 * 接收 Pulse（非阻塞）
 * ======================================================================== */

kernel_status_t ipc_pulse_receive(kobj_id_t ch_id, ipc_pulse_t *pulse)
{
    ipc_channel_t *ch;
    ipc_pulse_t *first_pulse;
    uint32_t irq_state;

    if (pulse == NULL)
    {
        return -(int32_t)EINVAL;
    }

    ch = get_channel(ch_id);
    if (ch == NULL)
    {
        return -(int32_t)EINVAL;
    }

    irq_state = ticket_lock_acquire_irqsave(&ch->lock);

    if (ch->pulse_queue.next == &ch->pulse_queue)
    {
        ticket_lock_release_irqrestore(&ch->lock, irq_state);
        return -(int32_t)EAGAIN;
    }

    /* 取出最高优先级的 Pulse（队列头部） */
    first_pulse = container_of(ch->pulse_queue.next, ipc_pulse_t, node);

    /* 从队列移除 */
    first_pulse->node.prev->next = first_pulse->node.next;
    first_pulse->node.next->prev = first_pulse->node.prev;
    first_pulse->node.next = &first_pulse->node;
    first_pulse->node.prev = &first_pulse->node;

    ch->pulse_count--;
    barrier();

    /* 拷贝 Pulse 数据到用户缓冲区 */
    pulse->prio = first_pulse->prio;
    pulse->code = first_pulse->code;
    pulse->value = first_pulse->value;
    pulse->conn_id = first_pulse->conn_id;

    ticket_lock_release_irqrestore(&ch->lock, irq_state);

    return KERNEL_OK;
}

/* ========================================================================
 * 接收 Pulse（阻塞）
 * ======================================================================== */

kernel_status_t ipc_pulse_receive_blocking(kobj_id_t ch_id, ipc_pulse_t *pulse)
{
    ipc_channel_t *ch;
    KThread_t *current;
    uint32_t irq_state;

    if (pulse == NULL)
    {
        return -(int32_t)EINVAL;
    }

    ch = get_channel(ch_id);
    if (ch == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 先尝试非阻塞接收 */
    if (ipc_pulse_receive(ch_id, pulse) == KERNEL_OK)
    {
        return KERNEL_OK;
    }

    current = kthread_get_current();
    if (current == NULL)
    {
        return -(int32_t)ESRCH;
    }

    /* 无 Pulse：阻塞等待 */
    irq_state = ticket_lock_acquire_irqsave(&ch->lock);
    current->state = KTHREAD_STATE_BLOCKED;
    barrier();
    ticket_lock_release_irqrestore(&ch->lock, irq_state);

    schedule();

    /* 被唤醒后，再次尝试接收 */
    return ipc_pulse_receive(ch_id, pulse);
}
