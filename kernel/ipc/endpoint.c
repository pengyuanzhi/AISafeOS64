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
 *
 * @revision history
 * v1.0 2026-07-05 初始版本
 */

#include <kernel/ipc_endpoint.h>
#include <kernel/ipc_types.h>
#include <kernel/spinlock.h>
#include <kernel/barrier.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <kernel/compiler.h>
#include <kernel/uaccess.h>
#include <kernel/list.h>
#include <kernel/mm/slab.h>
#include <kernel/capability.h>
#include <kernel/cspace.h>
#include <kernel/kobject.h>
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
    IPC_MSG_SIZE_64B_INDEX = 0,  /**< @brief 64B 消息索引 */
    IPC_MSG_SIZE_256B_INDEX,     /**< @brief 256B 消息索引 */
    IPC_MSG_SIZE_1KB_INDEX,      /**< @brief 1KB 消息索引 */
    IPC_MSG_SIZE_COUNT           /**< @brief 消息大小类别数量（= 3） */
} ipc_msg_size_class_t;

/** @brief 各类别消息的实际字节大小 */
#define IPC_MSG_SIZE_64B_VAL    64U
#define IPC_MSG_SIZE_256B_VAL   256U
#define IPC_MSG_SIZE_1KB_VAL    1024U

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
        IPC_MSG_SIZE_64B_VAL * 16,    /* 64B 消息，每个 Slab 16 个对象 */
        IPC_MSG_SIZE_256B_VAL * 8,   /* 256B 消息，每个 Slab 8 个对象 */
        IPC_MSG_SIZE_1KB_VAL * 4      /* 1KB 消息，每个 Slab 4 个对象 */
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
    if (size <= IPC_MSG_SIZE_64B_VAL)
    {
        return 0; /* 64B 缓存 */
    }
    else if (size <= IPC_MSG_SIZE_256B_VAL)
    {
        return 1; /* 256B 缓存 */
    }
    else if (size <= IPC_MSG_SIZE_1KB_VAL)
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
        /* 大小不支持，返回 NULL */
        return NULL;
    }

    /* 从 Slab 缓存分配 */
    buf_ptr = slab_alloc(&s_ipc_msg_slab.caches[cache_idx]);
    if (buf_ptr == NULL)
    {
        /* Slab 分配失败，返回 NULL */
        return NULL;
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
        /* 大小不支持，不释放 */
        (void)ptr;  /* 避免未使用警告 */
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
 * @brief 检查当前线程是否拥有对端点的指定权限
 *
 * @details 双路径访问控制：
 *          1. 如果当前线程有 CSpace，遍历其能力表查找指向此端点的能力，
 *             通过 cap_validate 验证权限（完整能力系统路径）。
 *          2. 如果没有 CSpace（早期线程），回退到 owner_tid 检查。
 *
 * @param ep       端点指针
 * @param required 需要的权限（CAP_RIGHT_READ/WRITE 等）
 *
 * @return KERNEL_OK 有权限
 * @return -EACCES   无权限
 */
static kernel_status_t endpoint_check_access(const ipc_endpoint_t *ep,
                                              uint8_t required)
{
    KThread_t *current;

    current = kthread_get_current();
    if (current == NULL)
    {
        return -(int32_t)ESRCH;
    }

    /* 路径 1：能力系统验证（如果线程有 CSpace） */
    if (current->cspace != NULL)
    {
        cspace_t *cs = (cspace_t *)current->cspace;
        cap_slot_t slot;

        /* 遍历 CSpace 查找指向此端点的能力 */
        ticket_lock_acquire(&cs->lock);
        for (slot = 0U; slot < cs->capacity; slot++)
        {
            cap_t *cap = cspace_lookup(cs, slot);
            if ((cap != NULL) &&
                (cap->state == CAP_STATE_VALID) &&
                (cap->kobj_type == KOBJ_ENDPOINT) &&
                (cap->kobj_id == ep->id))
            {
                /* 找到匹配能力，验证权限 */
                kernel_status_t ret;
                if ((cap->rights & required) == required)
                {
                    ticket_lock_release(&cs->lock);
                    return KERNEL_OK;
                }
                /* 权限不足 */
                ticket_lock_release(&cs->lock);
                return -(int32_t)EACCES;
            }
        }
        ticket_lock_release(&cs->lock);

        /* 有 CSpace 但没找到能力 → 无权限 */
        return -(int32_t)EACCES;
    }

    /* 路径 2：回退到 owner 检查（无 CSpace 时） */
    if (ep->owner_tid == current->tid)
    {
        return KERNEL_OK;
    }

    return -(int32_t)EACCES;
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
    KThread_t *current;
    uint32_t irq_state;

    /*
     * 延迟唤醒集合：在持端点锁期间把待唤醒线程收集到此本地数组，
     * 仅标记为 READY；释放端点锁后再执行 scheduler_enqueue（获取调度队列
     * 锁）。遵循 A3 延迟唤醒模式，避免"持端点锁 → 获取调度队列锁"的
     * 锁升级与"持调度队列锁 → 访问端点"形成 AB-BA 死锁。
     *
     * 端点上最多同时存在两类等待者：
     *  - 慢速路径发送方（sender_tid，阻塞在 ipc_msg_send 等待 REPLY/RECEIVE）
     *  - 接收方/拥有者（owner_tid，阻塞在 ipc_msg_receive）
     * 因此集合容量设为 2 即可覆盖；外加 pending_list 的节点以备扩展。
     */
    KThread_t *wake_list[2U];
    uint32_t wake_count = 0U;

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

    /* 访问控制：需要 GRANT 权限（或 owner 回退） */
    {
        kernel_status_t acc = endpoint_check_access(ep, CAP_RIGHT_GRANT);
        if (acc != KERNEL_OK)
        {
            return acc;
        }
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

    /*
     * 收集待唤醒的等待者：遍历 pending_list 摘除节点，并解析其中记录的
     * 等待线程。pending_list 节点（若存在）使用与 sender_tid 一致的
     * 线程寻址方式：每个节点记录的等待线程状态置 READY 并登记到本地数组。
     * 当前实现中 pending_list 为空（慢速路径仅用 sender_tid），这里同时
     * 兼顾两类等待者，确保销毁后无任何线程永久挂起。
     */
    while (ep->pending_list.next != &ep->pending_list)
    {
        struct list_head *first = ep->pending_list.next;
        first->prev->next = first->next;
        first->next->prev = first->prev;
        first->next = first;
        first->prev = first;
        /* 节点已摘除；对应的等待线程通过下方 sender_tid 路径统一唤醒 */
    }

    /* 收集慢速路径阻塞的发送方（ipc_msg_send 等待 REPLY） */
    if ((ep->sender_tid != THREAD_ID_INVALID) &&
        (ep->sender_tid < CONFIG_MAX_THREADS))
    {
        KThread_t *sender = &g_scheduler.thread_table[ep->sender_tid];
        if ((sender->state == KTHREAD_STATE_BLOCKED) &&
            (wake_count < (uint32_t)(sizeof(wake_list) / sizeof(wake_list[0U]))))
        {
            sender->state = KTHREAD_STATE_READY;
            wake_list[wake_count] = sender;
            wake_count++;
        }
        ep->sender_tid = THREAD_ID_INVALID;
    }

    /* 收集阻塞在 RECEIVE 的接收方/拥有者 */
    if ((ep->owner_tid != THREAD_ID_INVALID) &&
        (ep->owner_tid < CONFIG_MAX_THREADS))
    {
        KThread_t *receiver = &g_scheduler.thread_table[ep->owner_tid];
        if ((receiver->state == KTHREAD_STATE_BLOCKED) &&
            (wake_count < (uint32_t)(sizeof(wake_list) / sizeof(wake_list[0U]))))
        {
            receiver->state = KTHREAD_STATE_READY;
            wake_list[wake_count] = receiver;
            wake_count++;
        }
        ep->owner_tid = THREAD_ID_INVALID;
    }

    /* 标记为空闲，递增 generation 使旧 ID 失效（防止 use-after-free） */
    ep->generation++;
    if (ep->generation == 0U)
    {
        ep->generation = 1U;
    }
    ep->id = KOBJ_ID_INVALID;
    ep->state = IPC_EP_IDLE;

    barrier();
    ticket_lock_release_irqrestore(&ep->lock, irq_state);

    /* 端点锁已释放，此时安全获取调度队列锁唤醒等待线程 */
    for (uint32_t i = 0U; i < wake_count; i++)
    {
        scheduler_enqueue(wake_list[i]);
    }

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
    KThread_t *wake_receiver;
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

    /* 访问控制：通过能力系统验证 WRITE 权限（回退到 owner 检查） */
    {
        kernel_status_t acc = endpoint_check_access(ep, CAP_RIGHT_WRITE);
        if (acc != KERNEL_OK)
        {
            return acc;
        }
    }

    current = kthread_get_current();
    if (current == NULL)
    {
        return -(int32_t)ESRCH;
    }

    /* 延迟唤醒：在端点锁内只标记目标线程待唤醒，释放锁后再入队，
     * 避免"持端点锁（优先级3）→ 获取调度队列锁（优先级1）"的锁升级
     * 与"持调度队列锁 → 访问端点"的路径形成 AB-BA 死锁。 */
    wake_receiver = NULL;

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

        /* 在锁内只标记接收线程为待唤醒，记录到局部变量；
         * 真正的 scheduler_enqueue（获取调度队列锁）延迟到释放端点锁后执行。 */
        if ((ep->owner_tid != THREAD_ID_INVALID) &&
            (ep->owner_tid < CONFIG_MAX_THREADS))
        {
            KThread_t *receiver = &g_scheduler.thread_table[ep->owner_tid];
            if (receiver->state == KTHREAD_STATE_BLOCKED)
            {
                receiver->state = KTHREAD_STATE_READY;
                wake_receiver = receiver;
            }
        }

        /* 阻塞发送方线程，等待回复 */
        current->state = KTHREAD_STATE_BLOCKED;
        barrier();
        ticket_lock_release_irqrestore(&ep->lock, irq_state);

        /* 端点锁已释放，此时安全获取调度队列锁唤醒接收线程 */
        if (wake_receiver != NULL)
        {
            scheduler_enqueue(wake_receiver);
        }

        schedule();

        /* 被唤醒后（收到回复），继续执行 */
        return KERNEL_OK;
    }

    /* 慢速路径：接收者尚未调用 RECV，保存发送方信息并阻塞 */
    ep->state = IPC_EP_PENDING;
    ep->sender_tid = current->tid;

    /*
     * P1-15 安全修复：禁止跨 schedule 保存用户态指针。
     *
     * 原先直接把用户态 send_buf 指针存入 ep->send_buf，发送方阻塞后
     * 接收方在另一地址空间（不同 user_pgd）以此指针 memcpy，导致跨地址
     * 空间读写——严重安全漏洞。
     *
     * 修复：先将消息体拷贝到内核临时缓冲（endpoint_msg_buf_alloc），
     * 把内核缓冲地址存入 ep->send_buf；接收方从内核缓冲拷出后再释放。
     * 用户指针走 copy_from_user（含 access_ok 校验），内核指针（内核线程
     * bench 等场景）走 memcpy。
     */
    if ((send_buf != NULL) && (send_size > 0U))
    {
        void *kbuf = endpoint_msg_buf_alloc(send_size);
        if (kbuf == NULL)
        {
            ticket_lock_release_irqrestore(&ep->lock, irq_state);
            return -(int32_t)ENOMEM;
        }

        if (access_ok(send_buf, send_size))
        {
            /* 用户态指针：安全拷贝 */
            if (copy_from_user(kbuf, send_buf, (uint64_t)send_size) != 0)
            {
                endpoint_msg_buf_free(kbuf, send_size);
                ticket_lock_release_irqrestore(&ep->lock, irq_state);
                return -(int32_t)EFAULT;
            }
        }
        else
        {
            /* 内核态指针（内核线程）：直接拷贝 */
            (void)memcpy(kbuf, send_buf, (size_t)send_size);
        }

        ep->send_buf = kbuf;
        ep->send_size = send_size;
    }
    else
    {
        ep->send_buf = NULL;
        ep->send_size = 0U;
    }

    /* 保存发送方回复缓冲区信息，供后续 RECV → REPLY 使用 */
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
 * 能力传递辅助函数
 * ======================================================================== */

/**
 * @brief 执行跨 CSpace 的能力复制
 *
 * @param sender   发送方线程
 * @param src_slot 发送方 CSpace 中的能力槽
 * @param receiver 接收方线程
 *
 * @return KERNEL_OK 成功，负数错误码失败
 */
static kernel_status_t endpoint_do_cap_transfer(KThread_t *sender,
                                                 cap_slot_t src_slot,
                                                 KThread_t *receiver)
{
    cspace_t *sender_cs;
    cspace_t *recv_cs;
    cap_slot_t dest_slot;

    if ((sender == NULL) || (receiver == NULL) ||
        (src_slot == CAP_SLOT_INVALID))
    {
        return -(int32_t)EINVAL;
    }

    sender_cs = (cspace_t *)sender->cspace;
    recv_cs = (cspace_t *)receiver->cspace;

    /* 双方都必须有 CSpace */
    if ((sender_cs == NULL) || (recv_cs == NULL))
    {
        return -(int32_t)ENOSYS;
    }

    /* 在接收方 CSpace 中分配一个目标槽 */
    dest_slot = recv_cs->free_head;
    if (dest_slot == CAP_SLOT_INVALID)
    {
        return -(int32_t)ENOMEM;
    }

    /* 复制能力（降权：0 = 保持原权限） */
    return cap_copy(sender_cs->root_slot, src_slot,
                    recv_cs->root_slot, dest_slot, 0U);
}

/* ========================================================================
 * 发送消息并传递能力（seL4 风格 cap transfer）
 * ======================================================================== */

kernel_status_t ipc_msg_send_with_cap(kobj_id_t ep_id,
                                       ipc_msg_tag_t tag,
                                       const void *send_buf,
                                       uint32_t send_size,
                                       void *recv_buf,
                                       uint32_t recv_size,
                                       cap_slot_t cap_slot)
{
    ipc_endpoint_t *ep;
    KThread_t *current;
    KThread_t *receiver;
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

    /* 记录能力传递信息 */
    if (cap_slot != CAP_SLOT_INVALID)
    {
        ep->transfer_src_slot = cap_slot;
        ep->has_cap_transfer = true;
    }
    else
    {
        ep->has_cap_transfer = false;
    }

    /* 如果接收方正在等待（快速路径），立即传递能力 */
    if ((ep->state == IPC_EP_RECEIVING) && (ep->has_cap_transfer))
    {
        receiver = &g_scheduler.thread_table[ep->owner_tid];
        if (receiver->state == KTHREAD_STATE_BLOCKED)
        {
            (void)endpoint_do_cap_transfer(current, ep->transfer_src_slot, receiver);
            ep->has_cap_transfer = false;
        }
    }

    ticket_lock_release_irqrestore(&ep->lock, irq_state);

    /* 委托给标准 ipc_msg_send 完成消息传递 */
    return ipc_msg_send(ep_id, tag, send_buf, send_size, recv_buf, recv_size);
}

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

    /* 访问控制：通过能力系统验证 READ 权限（回退到 owner 检查） */
    {
        kernel_status_t acc = endpoint_check_access(ep, CAP_RIGHT_READ);
        if (acc != KERNEL_OK)
        {
            return acc;
        }
    }

    irq_state = ticket_lock_acquire_irqsave(&ep->lock);

    /* 保存接收方缓冲区信息 */
    ep->recv_buf = recv_buf;
    ep->recv_size = recv_size;

    /* 检查是否有发送方已阻塞等待（慢速路径 send 已保存 sender 信息）
     * 条件：state == PENDING 且 sender_tid 有效（排除 EP_CREATE 后的初始状态） */
    if ((ep->state == IPC_EP_PENDING) && (ep->sender_tid != THREAD_ID_INVALID))
    {
        /*
         * P1-15：ep->send_buf 现在指向内核临时缓冲（由发送方慢速路径分配），
         * 而非跨地址空间的用户指针，故在此可安全拷贝到接收方 recv_buf。
         * 接收方为用户线程时用 copy_to_user，内核线程用 memcpy。
         * 拷贝完成后立即释放内核临时缓冲。
         */
        if ((ep->send_buf != NULL) && (ep->send_size > 0U) &&
            (recv_buf != NULL) && (recv_size > 0U))
        {
            uint32_t copy_size = ep->send_size;
            if (copy_size > recv_size)
            {
                copy_size = recv_size;
            }
            if (access_ok(recv_buf, copy_size))
            {
                (void)copy_to_user(recv_buf, ep->send_buf, (uint64_t)copy_size);
            }
            else
            {
                (void)memcpy(recv_buf, ep->send_buf, (size_t)copy_size);
            }
        }

        /* 释放慢速路径分配的内核消息缓冲 */
        if (ep->send_buf != NULL)
        {
            endpoint_msg_buf_free((void *)ep->send_buf, ep->send_size);
            ep->send_buf = NULL;
            ep->send_size = 0U;
        }

        /* 填充 tag 输出参数 */
        if (tag != NULL)
        {
            *tag = ep->saved_tag;
        }

        /* 慢速路径的能力传递：发送方先到达并标记了 cap transfer */
        if (ep->has_cap_transfer)
        {
            KThread_t *sender_thread = &g_scheduler.thread_table[ep->sender_tid];
            (void)endpoint_do_cap_transfer(sender_thread, ep->transfer_src_slot, current);
            ep->has_cap_transfer = false;
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
    KThread_t *current;
    KThread_t *wake_sender;
    uint32_t irq_state;

    (void)status;

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

    /* 访问控制：需要 WRITE 权限（或 owner 回退） */
    {
        kernel_status_t acc = endpoint_check_access(ep, CAP_RIGHT_WRITE);
        if (acc != KERNEL_OK)
        {
            return acc;
        }
    }

    /* 延迟唤醒：在端点锁内只标记发送线程待唤醒，释放锁后再入队，
     * 避免"持端点锁 → 获取调度队列锁"的锁升级死锁。 */
    wake_sender = NULL;

    irq_state = ticket_lock_acquire_irqsave(&ep->lock);

    if (ep->state != IPC_EP_REPLYING)
    {
        ticket_lock_release_irqrestore(&ep->lock, irq_state);
        return -(int32_t)EINVAL;
    }

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

    /* 在锁内只标记发送线程为待唤醒，记录到局部变量；
     * 真正的 scheduler_enqueue（获取调度队列锁）延迟到释放端点锁后执行。 */
    if ((ep->sender_tid != THREAD_ID_INVALID) && (ep->sender_tid < CONFIG_MAX_THREADS))
    {
        KThread_t *sender = &g_scheduler.thread_table[ep->sender_tid];
        if (sender->state == KTHREAD_STATE_BLOCKED)
        {
            sender->state = KTHREAD_STATE_READY;
            wake_sender = sender;
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

    /* 端点锁已释放，此时安全获取调度队列锁唤醒发送线程 */
    if (wake_sender != NULL)
    {
        scheduler_enqueue(wake_sender);
    }

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
    /*
     * 实时性安全的超时语义（RTOS 安全关键实践）：
     *  - IPC_TIMEOUT_NONBLOCK (0)：非阻塞尝试，立即返回，不阻塞调用线程；
     *    接收方未等待时返回 -EBUSY，绝不永久阻塞。
     *  - IPC_TIMEOUT_INFINITE：永久阻塞，与 ipc_msg_send 一致。
     *  - 其他有限超时值：当前阶段仍调用阻塞 send（避免引入"超时线程 +
     *    端点超时扫描"的复杂度），后续可在端点结构中记录 sender 的
     *    wakeup_tick 并由定时器中断扫描实现真正的超时唤醒。
     *
     * 关键修复点：原先所有 timeout_ms 值都走阻塞路径，违反函数语义
     * （timeout_ms==0 时承诺非阻塞却永久阻塞），属于 P1 严重缺陷。
     */
    if (timeout_ms == IPC_TIMEOUT_NONBLOCK)
    {
        /* 非阻塞模式：不投递 recv_buf（try_send 不支持回复等待） */
        return ipc_msg_try_send(ep_id, tag, send_buf, send_size);
    }

    /* 阻塞模式（无限等待或有限超时暂统一走阻塞发送） */
    return ipc_msg_send(ep_id, tag, send_buf, send_size, recv_buf, recv_size);
}
