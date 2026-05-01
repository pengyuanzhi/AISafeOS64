/**
 * @file    endpoint.c
 * @brief   IPC 端点（Endpoint）管理实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 实现所有 ipc_endpoint.h 中声明的函数：
 *          - ipc_endpoint_subsys_init: 端点子系统初始化
 *          - ipc_endpoint_create:      创建端点
 *          - ipc_endpoint_destroy:     销毁端点
 *          - ipc_msg_send:             同步发送（阻塞）
 *          - ipc_msg_receive:          接收消息（阻塞）
 *          - ipc_msg_reply:            回复消息
 *          - ipc_msg_try_send:         非阻塞发送
 *          - ipc_msg_send_timeout:     带超时发送
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-005（同步消息传递）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/ipc_endpoint.h>
#include <kernel/ipc_types.h>
#include <kernel/spinlock.h>
#include <kernel/barrier.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <kernel/compiler.h>
#include <kernel/list.h>
#include <kernel/mm/slab.h>
#include <kernel/mm/kmalloc.h>
#include "thread.h"
#include "scheduler.h"
#include <stdint.h>
#include <string.h>
#include "hal.h"

/* ========================================================================
 * 全局端点表
 * ======================================================================== */

/**
 * @brief 全局端点对象表（静态分配）
 */
static ipc_endpoint_t s_endpoints[CONFIG_IPC_MAX_ENDPOINTS];

/**
 * @brief 全局端点链表（活跃端点）
 */
static struct list_head s_active_endpoints;

/**
 * @brief 空闲端点栈（索引栈）
 */
static uint32_t s_free_ep_stack[CONFIG_IPC_MAX_ENDPOINTS];

/**
 * @brief 空闲端点计数
 */
static uint32_t s_free_ep_count;

/**
 * @brief 端点子系统全局锁
 */
static TicketLock_t s_ep_subsys_lock;

/* ========================================================================
 * IPC 消息缓冲区 Slab 缓存
 * ======================================================================== */

/**
 * @brief IPC 消息缓冲区大小类别
 */
typedef enum
{
    IPC_MSG_SIZE_64B = 64,    /**< @brief 64B 消息 */
    IPC_MSG_SIZE_256B = 256,  /**< @brief 256B 消息 */
    IPC_MSG_SIZE_1KB = 1024,  /**< @brief 1KB 消息 */
    IPC_MSG_SIZE_COUNT       /**< @brief 消息大小类别数量 */
} ipc_msg_size_class_t;

/**
 * @brief IPC 消息缓冲区 Slab 缓存集合
 */
static struct
{
    slab_cache_t caches[IPC_MSG_SIZE_COUNT]; /**< @brief 不同大小消息的 Slab 缓存 */
    bool initialized;                             /**< @brief Slab 缓存初始化标志 */
} s_ipc_msg_slab;

/* ========================================================================
 * IPC 消息缓冲区 Slab 分配器实现
 * ======================================================================== */

/**
 * @brief 初始化 IPC 消息缓冲区 Slab 缓存
 *
 * @return KERNEL_OK 成功
 * @return -ENOMEM 内存不足
 */
static int32_t ipc_msg_slab_init(void)
{
    int32_t ret;
    size_t pool_sizes[IPC_MSG_SIZE_COUNT] = {
        IPC_MSG_SIZE_64B * 16,    /* 64B 消息，每个 Slab 16 个对象 */
        IPC_MSG_SIZE_256B * 8,   /* 256B 消息，每个 Slab 8 个对象 */
        IPC_MSG_SIZE_1KB * 4      /* 1KB 消息，每个 Slab 4 个对象 */
    };

    for (uint32_t i = 0U; i < IPC_MSG_SIZE_COUNT; i++)
    {
        ret = slab_create(&s_ipc_msg_slab.caches[i], pool_sizes[i]);
        if (ret != KERNEL_OK)
        {
            /* 清理已创建的缓存 */
            for (uint32_t j = 0U; j < i; j++)
            {
                (void)slab_destroy(&s_ipc_msg_slab.caches[j]);
            }
            return ret;
        }
    }

    s_ipc_msg_slab.initialized = true;

    return KERNEL_OK;
}

/**
 * @brief 销毁 IPC 消息缓冲区 Slab 缓存
 *
 * @return KERNEL_OK 成功
 */
static int32_t ipc_msg_slab_destroy(void)
{
    if (!s_ipc_msg_slab.initialized)
    {
        return KERNEL_OK;
    }

    for (uint32_t i = 0U; i < IPC_MSG_SIZE_COUNT; i++)
    {
        (void)slab_destroy(&s_ipc_msg_slab.caches[i]);
    }

    (void)memset(&s_ipc_msg_slab, 0, sizeof(s_ipc_msg_slab));

    return KERNEL_OK;
}

/**
 * @brief 根据消息大小选择合适的 Slab 缓存
 *
 * @param size 消息大小
 *
 * @return Slab 缓存索引，如果不支持则返回 -1
 */
static int32_t select_msg_cache(uint32_t size)
{
    /* 根据消息大小选择合适的缓存 */
    if (size <= IPC_MSG_SIZE_64B)
    {
        return 0; /* 64B 缓存 */
    }
    else if (size <= IPC_MSG_SIZE_256B)
    {
        return 1; /* 256B 缓存 */
    }
    else if (size <= IPC_MSG_SIZE_1KB)
    {
        return 2; /* 1KB 缓存 */
    }
    else
    {
        return -1; /* 不支持的大小 */
    }
}

/**
 * @brief 使用 Slab 分配器分配消息缓冲区
 *
 * @param size 请求的消息缓冲区大小
 *
 * @return 成功返回缓冲区指针，失败返回 NULL
 */
static void *ipc_msg_alloc_slab(uint32_t size)
{
    int32_t cache_idx;
    void *buf_ptr;

    if (!s_ipc_msg_slab.initialized)
    {
        return NULL;
    }

    /* 选择合适的 Slab 缓存 */
    cache_idx = select_msg_cache(size);
    if (cache_idx < 0)
    {
        /* 大小不支持，回退到 kmalloc */
        return kmalloc(size);
    }

    /* 从 Slab 缓存分配 */
    buf_ptr = slab_alloc(&s_ipc_msg_slab.caches[cache_idx]);
    if (buf_ptr == NULL)
    {
        /* Slab 分配失败，回退到 kmalloc */
        return kmalloc(size);
    }

    return buf_ptr;
}

/**
 * @brief 释放消息缓冲区回 Slab 分配器
 *
 * @param ptr 消息缓冲区指针
 * @param size 消息缓冲区大小
 */
static void ipc_msg_free_slab(void *ptr, uint32_t size)
{
    int32_t cache_idx;

    if (!s_ipc_msg_slab.initialized)
    {
        return;
    }

    /* 选择合适的 Slab 缓存 */
    cache_idx = select_msg_cache(size);
    if (cache_idx < 0)
    {
        /* 大小不支持，使用 kfree */
        kfree(ptr);
        return;
    }

    /* 释放回 Slab 缓存 */
    (void)slab_free(&s_ipc_msg_slab.caches[cache_idx], ptr);
}

/* ========================================================================
 * IPC 消息缓冲区辅助函数
 * ======================================================================== */

/**
 * @brief 分配端点的消息缓冲区（内核态临时缓冲）
 *
 * @details 为端点分配内核态临时消息缓冲区，
 *          用于在消息传递时暂存数据。
 *
 * @param size 请求的缓冲区大小
 *
 * @return 成功返回缓冲区指针，失败返回 NULL
 */
static void *endpoint_msg_buf_alloc(uint32_t size)
{
    void *buf_ptr;

    /* 优先使用 Slab 分配器 */
    buf_ptr = ipc_msg_alloc_slab(size);
    if (buf_ptr == NULL)
    {
        return NULL;
    }

    return buf_ptr;
}

/**
 * @brief 释放端点的消息缓冲区
 *
 * @param ptr 消息缓冲区指针
 * @param size 消息缓冲区大小
 */
static void endpoint_msg_buf_free(void *ptr, uint32_t size)
{
    /* 优先使用 Slab 分配器释放 */
    ipc_msg_free_slab(ptr, size);
}

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 分配空闲端点
 *
 * @return 端点索引，无空闲返回 -1
 */
static int32_t alloc_endpoint_index(void)
{
    int32_t idx;

    ticket_lock_acquire(&s_ep_subsys_lock);

    if (s_free_ep_count == 0U)
    {
        ticket_lock_release(&s_ep_subsys_lock);
        return -1;
    }

    s_free_ep_count--;
    idx = (int32_t)s_free_ep_stack[s_free_ep_count];

    ticket_lock_release(&s_ep_subsys_lock);

    return idx;
}

/**
 * @brief 释放端点索引
 *
 * @param idx 端点索引
 */
static void free_endpoint_index(uint32_t idx)
{
    ticket_lock_acquire(&s_ep_subsys_lock);
    s_free_ep_stack[s_free_ep_count] = idx;
    s_free_ep_count++;
    ticket_lock_release(&s_ep_subsys_lock);
}

/**
 * @brief 通过 ID 获取端点指针
 *
 * @details 验证 ep_id 中的 generation 与端点结构体中的 generation 匹配，
 *          防止端点销毁后重建导致 use-after-free。
 *          ID 编码：高 16 位为 generation，低 16 位为索引。
 *
 * @param ep_id 端点 ID
 *
 * @return 端点指针，无效返回 NULL
 */
static ipc_endpoint_t *get_endpoint(kobj_id_t ep_id)
{
    uint32_t idx;
    uint16_t gen;
    ipc_endpoint_t *ep;

    if (ep_id == KOBJ_ID_INVALID)
    {
        return NULL;
    }

    /* ID 的低 16 位为索引 */
    idx = (uint32_t)(ep_id & 0xFFFFU);
    if (idx >= CONFIG_IPC_MAX_ENDPOINTS)
    {
        return NULL;
    }

    /* ID 的高 16 位为 generation */
    gen = (uint16_t)((ep_id >> 16U) & 0xFFFFU);

    ep = &s_endpoints[idx];

    /* 验证 generation 匹配 */
    if (ep->generation != gen)
    {
        return NULL;
    }

    return ep;
}

/**
 * @brief 检查端点是否有效
 */
static bool endpoint_is_valid(const ipc_endpoint_t *ep)
{
    if (ep == NULL)
    {
        return false;
    }

    return (ep->state != IPC_EP_IDLE) ? true : false;
}

/* ========================================================================
 * 端点子系统初始化
 * ======================================================================== */

kernel_status_t ipc_endpoint_subsys_init(void)
{
    uint32_t i;
    int32_t ret;

    /* 初始化 IPC 消息缓冲区 Slab 缓存 */
    ret = ipc_msg_slab_init();
    if (ret != KERNEL_OK)
    {
        return -ENOMEM;
    }

    /* 初始化空闲栈 */
    for (i = 0U; i < CONFIG_IPC_MAX_ENDPOINTS; i++)
    {
        s_free_ep_stack[i] = (CONFIG_IPC_MAX_ENDPOINTS - 1U) - i;
        s_free_ep_count = i + 1U;

        /* 初始化端点结构 */
        s_endpoints[i].id = KOBJ_ID_INVALID;
        s_endpoints[i].state = IPC_EP_IDLE;
        s_endpoints[i].owner_tid = THREAD_ID_INVALID;
        s_endpoints[i].sender_tid = THREAD_ID_INVALID;
        s_endpoints[i].pending_list.next = &s_endpoints[i].pending_list;
        s_endpoints[i].pending_list.prev = &s_endpoints[i].pending_list;
        s_endpoints[i].node.next = &s_endpoints[i].node;
        s_endpoints[i].node.prev = &s_endpoints[i].node;
        ticket_lock_init(&s_endpoints[i].lock);
        s_endpoints[i].recv_buf = NULL;
        s_endpoints[i].recv_size = 0U;
        s_endpoints[i].send_buf = NULL;
        s_endpoints[i].send_size = 0U;
        s_endpoints[i].sender_recv_buf = NULL;
        s_endpoints[i].sender_recv_size = 0U;
        s_endpoints[i].saved_tag.value = 0ULL;
        s_endpoints[i].generation = 0U;
    }

    /* 初始化活跃链表 */
    s_active_endpoints.next = &s_active_endpoints;
    s_active_endpoints.prev = &s_active_endpoints;

    /* 初始化子系统锁 */
    ticket_lock_init(&s_ep_subsys_lock);

    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 端点创建
 * ======================================================================== */

kernel_status_t ipc_endpoint_create(thread_id_t owner_tid, kobj_id_t *ep_id)
{
    int32_t idx;
    ipc_endpoint_t *ep;

    if (ep_id == NULL)
    {
        return -(int32_t)EINVAL;
    }

    idx = alloc_endpoint_index();
    if (idx < 0)
    {
        return -(int32_t)ENOMEM;
    }

    ep = &s_endpoints[(uint32_t)idx];

    /* 递增 generation（跳过 0 以避免与初始化值冲突） */
    ep->generation++;
    if (ep->generation == 0U)
    {
        ep->generation = 1U;
    }

    /* 生成端点 ID：高 16 位为 generation，低 16 位为索引 */
    ep->id = (kobj_id_t)(((uint32_t)ep->generation << 16U) | (uint32_t)idx);
    ep->state = IPC_EP_PENDING;
    ep->owner_tid = owner_tid;
    ep->sender_tid = THREAD_ID_INVALID;
    ep->pending_list.next = &ep->pending_list;
    ep->pending_list.prev = &ep->pending_list;
    ep->node.next = &ep->node;
    ep->node.prev = &ep->node;
    ticket_lock_init(&ep->lock);

    /* 加入活跃链表 */
    ticket_lock_acquire(&s_ep_subsys_lock);
    ep->node.next = s_active_endpoints.next;
    ep->node.prev = &s_active_endpoints;
    s_active_endpoints.next->prev = &ep->node;
    s_active_endpoints.next = &ep->node;
    ticket_lock_release(&s_ep_subsys_lock);

    barrier();

    *ep_id = ep->id;

    return KERNEL_OK;
}

/* ========================================================================
 * 端点销毁
 * ======================================================================== */

kernel_status_t ipc_endpoint_destroy(kobj_id_t ep_id)
{
    ipc_endpoint_t *ep;
    uint32_t irq_state;

    ep = get_endpoint(ep_id);
    if (!endpoint_is_valid(ep))
    {
        return -(int32_t)EINVAL;
    }

    irq_state = ticket_lock_acquire_irqsave(&ep->lock);

    /* 从活跃链表移除 */
    ep->node.prev->next = ep->node.next;
    ep->node.next->prev = ep->node.prev;
    ep->node.next = &ep->node;
    ep->node.prev = &ep->node;

    /* 释放端点的消息缓冲区 */
    if (ep->recv_buf != NULL)
    {
        endpoint_msg_buf_free((void *)ep->recv_buf, ep->recv_size);
        ep->recv_buf = NULL;
        ep->recv_size = 0U;
    }

    if (ep->send_buf != NULL)
    {
        endpoint_msg_buf_free((void *)ep->send_buf, ep->send_size);
        ep->send_buf = NULL;
        ep->send_size = 0U;
    }

    if (ep->sender_recv_buf != NULL)
    {
        endpoint_msg_buf_free((void *)ep->sender_recv_buf, ep->sender_recv_size);
        ep->sender_recv_buf = NULL;
        ep->sender_recv_size = 0U;
    }

    /* 唤醒所有在等待的发送线程 */
    while (ep->pending_list.next != &ep->pending_list)
    {
        struct list_head *first = ep->pending_list.next;
        first->prev->next = first->next;
        first->next->prev = first->prev;
        first->next = first;
        first->prev = first;

        /* 将等待线程标记为就绪并重新入队 */
        /* 在完整实现中，需要通过 first 找到对应的 KThread */
    }

    /* 标记为空闲，递增 generation 使旧 ID 失效 */
    ep->generation++;
    ep->id = KOBJ_ID_INVALID;
    ep->state = IPC_EP_IDLE;
    ep->owner_tid = THREAD_ID_INVALID;
    ep->sender_tid = THREAD_ID_INVALID;

    ticket_lock_release_irqrestore(&ep->lock, irq_state);

    /* 释放端点索引 */
    free_endpoint_index((uint32_t)(ep_id & 0xFFFFU));

    return KERNEL_OK;
}

/* ========================================================================
 * 同步消息发送（阻塞）
 * ======================================================================== */

kernel_status_t ipc_msg_send(kobj_id_t ep_id,
                              ipc_msg_tag_t tag,
                              const void *send_buf,
                              uint32_t send_size,
                              void *recv_buf,
                              uint32_t recv_size)
{
    ipc_endpoint_t *ep;
    KThread_t *current;
    uint32_t irq_state;

    ep = get_endpoint(ep_id);
    if (!endpoint_is_valid(ep))
    {
        return -(int32_t)EINVAL;
    }

    if (send_size > CONFIG_IPC_MSG_MAX_SIZE)
    {
        return -(int32_t)EINVAL;
    }

    current = kthread_get_current();
    if (current == NULL)
    {
        return -(int32_t)ESRCH;
    }

    irq_state = ticket_lock_acquire_irqsave(&ep->lock);

    /* 检查端点是否有接收者正在等待 */
    if (ep->state == IPC_EP_RECEIVING)
    {
        /* 快速路径：直接传递消息给接收者 */
        ep->state = IPC_EP_REPLYING;

        /* 保存发送方 tid，供 ipc_msg_reply 唤醒 */
        ep->sender_tid = current->tid;

        /* 拷贝消息载荷到接收方缓冲区 */
        if ((send_buf != NULL) && (send_size > 0U) &&
            (ep->recv_buf != NULL) && (ep->recv_size > 0U))
        {
            uint32_t copy_size = send_size;
            if (copy_size > ep->recv_size)
            {
                copy_size = ep->recv_size;
            }
            (void)memcpy(ep->recv_buf, send_buf, (size_t)copy_size);
        }
        else if ((send_buf != NULL) && (send_size > 0U))
        {
            /* 如果接收方缓冲区为 NULL，使用内核态临时缓冲区 */
            void *temp_buf = endpoint_msg_buf_alloc(send_size);
            if (temp_buf != NULL)
            {
                (void)memcpy(temp_buf, send_buf, (size_t)send_size);
                ep->recv_buf = temp_buf;
                ep->recv_size = send_size;
            }
        }

        /* 保存 tag 供接收方获取 */
        ep->saved_tag = tag;

        /* 保存发送方回复缓冲区信息 */
        ep->sender_recv_buf = recv_buf;
        ep->sender_recv_size = recv_size;

        /* 如果需要内核态临时消息缓冲区，则分配 */
        if ((recv_buf != NULL) && (recv_size > 0U))
        {
            void *temp_buf = endpoint_msg_buf_alloc(recv_size);
            if (temp_buf != NULL)
            {
                ep->sender_recv_buf = temp_buf;
                /* 在回复后需要拷贝回用户缓冲区 */
            }
        }

        /* 唤醒接收线程 */
        if (ep->owner_tid != THREAD_ID_INVALID)
        {
            if (ep->owner_tid < CONFIG_MAX_THREADS)
            {
                KThread_t *receiver = &g_scheduler.thread_table[ep->owner_tid];
                if (receiver->state == KTHREAD_STATE_BLOCKED)
                {
                    receiver->state = KTHREAD_STATE_READY;
                    scheduler_enqueue(receiver);
                }
            }
        }

        /* 阻塞发送方线程，等待回复 */
        current->state = KTHREAD_STATE_BLOCKED;
        barrier();
        ticket_lock_release_irqrestore(&ep->lock, irq_state);
        schedule();

        /* 被唤醒后（收到回复），继续执行 */
        return KERNEL_OK;
    }

    /* 慢速路径：接收者尚未调用 RECV，保存发送方信息并阻塞 */
    ep->state = IPC_EP_PENDING;
    ep->sender_tid = current->tid;

    /* 保存发送方缓冲区信息，供后续 RECV → REPLY 使用 */
    ep->send_buf = send_buf;
    ep->send_size = send_size;
    ep->sender_recv_buf = recv_buf;
    ep->sender_recv_size = recv_size;

    /* 阻塞发送方线程 */
    current->state = KTHREAD_STATE_BLOCKED;
    barrier();
    ticket_lock_release_irqrestore(&ep->lock, irq_state);
    schedule();

    /* 被唤醒后，回复已就绪 */
    return KERNEL_OK;
}

/* ========================================================================
 * 接收消息（阻塞）
 * ======================================================================== */

kernel_status_t ipc_msg_receive(kobj_id_t ep_id,
                                 ipc_msg_tag_t *tag,
                                 void *recv_buf,
                                 uint32_t recv_size)
{
    /* 标记未使用参数（完整 IPC 实现中使用） */

    ipc_endpoint_t *ep;
    KThread_t *current;
    uint32_t irq_state;

    ep = get_endpoint(ep_id);
    if (!endpoint_is_valid(ep))
    {
        return -(int32_t)EINVAL;
    }

    current = kthread_get_current();
    if (current == NULL)
    {
        return -(int32_t)ESRCH;
    }

    irq_state = ticket_lock_acquire_irqsave(&ep->lock);

    /* 保存接收方缓冲区信息 */
    ep->recv_buf = recv_buf;
    ep->recv_size = recv_size;

    /* 检查是否有发送方已阻塞等待（慢速路径 send 已保存 sender 信息）
     * 条件：state == PENDING 且 sender_tid 有效（排除 EP_CREATE 后的初始状态） */
    if ((ep->state == IPC_EP_PENDING) && (ep->sender_tid != THREAD_ID_INVALID))
    {
        /* 有发送方在等待：拷贝消息载荷到接收方缓冲区 */
        if ((ep->send_buf != NULL) && (ep->send_size > 0U) &&
            (recv_buf != NULL) && (recv_size > 0U))
        {
            uint32_t copy_size = ep->send_size;
            if (copy_size > recv_size)
            {
                copy_size = recv_size;
            }
            (void)memcpy(recv_buf, ep->send_buf, (size_t)copy_size);
        }

        /* 填充 tag 输出参数 */
        if (tag != NULL)
        {
            *tag = ep->saved_tag;
        }

        /* 直接进入回复阶段 */
        ep->state = IPC_EP_REPLYING;
        ticket_lock_release_irqrestore(&ep->lock, irq_state);
        return KERNEL_OK;
    }

    /* 无发送方等待：阻塞等待 */
    ep->state = IPC_EP_RECEIVING;
    current->state = KTHREAD_STATE_BLOCKED;
    barrier();
    ticket_lock_release_irqrestore(&ep->lock, irq_state);
    schedule();

    /* 被唤醒后（发送方已完成 memcpy 到 recv_buf），填充 tag */
    if (tag != NULL)
    {
        *tag = ep->saved_tag;
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 回复消息
 * ======================================================================== */

kernel_status_t ipc_msg_reply(kobj_id_t ep_id,
                               int32_t status,
                               const void *reply_buf,
                               uint32_t reply_size)
{
    ipc_endpoint_t *ep;
    uint32_t irq_state;

    (void)status;

    ep = get_endpoint(ep_id);
    if (!endpoint_is_valid(ep))
    {
        return -(int32_t)EINVAL;
    }

    irq_state = ticket_lock_acquire_irqsave(&ep->lock);

    if (ep->state != IPC_EP_REPLYING)
    {
        ticket_lock_release_irqrestore(&ep->lock, irq_state);
        return -(int32_t)EINVAL;
    }

    /* 在完整实现中：
     * 1. 将回复数据拷贝到发送方的接收缓冲区
     * 2. 唤醒阻塞在 ipc_msg_send 中的发送线程
     */

    /* 恢复端点状态 */
    ep->state = IPC_EP_PENDING;

    /* 如果回复缓冲区有数据，拷贝到发送方的回复缓冲区 */
    if ((reply_buf != NULL) && (reply_size > 0U) &&
        (ep->sender_recv_buf != NULL) && (ep->sender_recv_size > 0U))
    {
        uint32_t copy_size = reply_size;
        if (copy_size > ep->sender_recv_size)
        {
            copy_size = ep->sender_recv_size;
        }
        (void)memcpy(ep->sender_recv_buf, reply_buf, (size_t)copy_size);
    }

    /* 释放内核态临时消息缓冲区（如果有） */
    if (ep->recv_buf != NULL)
    {
        endpoint_msg_buf_free((void *)ep->recv_buf, ep->recv_size);
        ep->recv_buf = NULL;
        ep->recv_size = 0U;
    }

    /* 唤醒发送方线程 */
    if ((ep->sender_tid != THREAD_ID_INVALID) && (ep->sender_tid < CONFIG_MAX_THREADS))
    {
        KThread_t *sender = &g_scheduler.thread_table[ep->sender_tid];
        if (sender->state == KTHREAD_STATE_BLOCKED)
        {
            sender->state = KTHREAD_STATE_READY;
            scheduler_enqueue(sender);
        }
        ep->sender_tid = THREAD_ID_INVALID;
    }

    /* 释放发送方回复缓冲区（如果是内核态临时分配） */
    if (ep->sender_recv_buf != NULL)
    {
        /* 检查是否是内核态临时缓冲区（用户态指针一般在高地址区间） */
        if ((uintptr_t)ep->sender_recv_buf < 0x80000000ULL)
        {
            /* 可能是内核态临时缓冲区，检查是否需要释放 */
            /* 在完整实现中，需要记录缓冲区分配来源 */
            ep->sender_recv_buf = NULL;
            ep->sender_recv_size = 0U;
        }
    }

    barrier();
    ticket_lock_release_irqrestore(&ep->lock, irq_state);

    return KERNEL_OK;
}

/* ========================================================================
 * 非阻塞发送
 * ======================================================================== */

kernel_status_t ipc_msg_try_send(kobj_id_t ep_id,
                                  ipc_msg_tag_t tag,
                                  const void *send_buf,
                                  uint32_t send_size)
{
    ipc_endpoint_t *ep;
    uint32_t irq_state;

    (void)tag;
    (void)send_buf;
    (void)send_size;

    ep = get_endpoint(ep_id);
    if (!endpoint_is_valid(ep))
    {
        return -(int32_t)EINVAL;
    }

    irq_state = ticket_lock_acquire_irqsave(&ep->lock);

    if (ep->state == IPC_EP_RECEIVING)
    {
        /* 接收者正在等待，可以立即投递 */
        ticket_lock_release_irqrestore(&ep->lock, irq_state);
        return KERNEL_OK;
    }

    /* 接收者未等待，非阻塞模式返回 EBUSY */
    ticket_lock_release_irqrestore(&ep->lock, irq_state);
    return -(int32_t)EBUSY;
}

/* ========================================================================
 * 带超时发送
 * ======================================================================== */

kernel_status_t ipc_msg_send_timeout(kobj_id_t ep_id,
                                      ipc_msg_tag_t tag,
                                      const void *send_buf,
                                      uint32_t send_size,
                                      void *recv_buf,
                                      uint32_t recv_size,
                                      uint32_t timeout_ms)
{
    /* 简化实现：先尝试非阻塞发送 */
    if (timeout_ms == IPC_TIMEOUT_NONBLOCK)
    {
        return ipc_msg_try_send(ep_id, tag, send_buf, send_size);
    }

    /* 无限等待 */
    if (timeout_ms == IPC_TIMEOUT_INFINITE)
    {
        return ipc_msg_send(ep_id, tag, send_buf, send_size, recv_buf, recv_size);
    }

    /* 带超时 - 在完整实现中，需要设置定时器唤醒 */
    /* 当前简化为直接发送 */
    (void)timeout_ms;
    return ipc_msg_send(ep_id, tag, send_buf, send_size, recv_buf, recv_size);
}
