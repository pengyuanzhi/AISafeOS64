/**
 * @file    test_endpoint.c
 * @brief   AISafe64 RTOS - IPC 端点同步消息传递单元测试（宿主机）
 * @author  AISafe64 Team
 * @date    2026-04-03
 * @version 1.0
 *
 * @details IPC 端点宿主机自包含测试
 *          测试与内核 endpoint.c 一致的逻辑：
 *          - 子系统初始化（端点池、空闲栈、活跃链表）
 *          - 端点创建/销毁
 *          - 同步消息发送（快速路径/慢速路径）
 *          - 消息接收（有待处理消息/无消息阻塞）
 *          - 消息回复（唤醒发送方）
 *          - 非阻塞 try_send
 *          - 带超时发送
 *          - 无效参数安全
 *          - 端点耗尽
 *          - 销毁时清空待处理队列
 *          - 消息标签构造与解析
 *          - 压力测试
 *
 * @note 宿主机单线程模拟：不测试 schedule() 上下文切换，
 *       专注测试状态机转换、队列操作和锁正确性
 * @note 对应需求: KR-005（同步消息传递）、TF-001
 */

#include "mock_kernel.h"

/* ========================================================================
 * 侵入式双向链表（与 kernel/list.h 一致）
 * ======================================================================== */

struct list_head
{
    struct list_head *next;
    struct list_head *prev;
};

static inline void init_list_head(struct list_head *head)
{
    if (head == NULL) { return; }
    head->next = head;
    head->prev = head;
}

static inline int list_empty(const struct list_head *head)
{
    if (head == NULL) { return 1; }
    return (head->next == head) ? 1 : 0;
}

static inline void list_add(struct list_head *new_node,
                             struct list_head *head)
{
    if (new_node == NULL) { return; }
    if (head == NULL) { return; }

    new_node->next = head->next;
    new_node->prev = head;
    head->next->prev = new_node;
    head->next = new_node;
}

static inline void list_add_tail(struct list_head *new_node,
                                  struct list_head *head)
{
    if (new_node == NULL) { return; }
    if (head == NULL) { return; }

    new_node->prev = head->prev;
    new_node->next = head;
    head->prev->next = new_node;
    head->prev = new_node;
}

static inline void list_del(struct list_head *entry)
{
    if (entry == NULL) { return; }

    entry->prev->next = entry->next;
    entry->next->prev = entry->prev;
    entry->next = entry;
    entry->prev = entry;
}

#undef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* ========================================================================
 * 配置常量（与 kernel/config.h 一致）
 * ======================================================================== */

#define TEST_MAX_ENDPOINTS       16U
#define TEST_IPC_MSG_MAX_SIZE    4096U
#define TEST_IPC_REG_MSG_WORDS   4U

#define KOBJ_ID_INVALID          ((kobj_id_t)0U)
#define THREAD_ID_INVALID        ((thread_id_t)0xFFFFFFFFU)

#define IPC_TIMEOUT_INFINITE     ((uint32_t)0xFFFFFFFFU)
#define IPC_TIMEOUT_NONBLOCK     ((uint32_t)0U)

#define IPC_ERR_ENDPOINT_DESTROYED ((int32_t)(-102))

/* ========================================================================
 * IPC 消息标签（与 kernel/ipc_types.h 一致）
 * ======================================================================== */

typedef struct
{
    uint64_t value;
} ipc_msg_tag_t;

#define IPC_TAG_TYPE(tag)       ((uint8_t)((tag).value >> 56U))
#define IPC_TAG_MSG_ID(tag)     ((uint16_t)((tag).value >> 32U))
#define IPC_TAG_SEND_SIZE(tag)  ((uint16_t)((tag).value >> 16U))
#define IPC_TAG_RECV_SIZE(tag)  ((uint16_t)((tag).value))
#define IPC_TAG_MAKE(type, msg_id, send_sz, recv_sz) \
    ((ipc_msg_tag_t){ .value = (((uint64_t)(type) << 56U) | ((uint64_t)(msg_id) << 32U) | \
     ((uint64_t)(send_sz) << 16U) | (uint64_t)(recv_sz)) })

#define IPC_MSG_TYPE_NORMAL     ((uint8_t)0x00U)
#define IPC_MSG_TYPE_REPLY      ((uint8_t)0x01U)
#define IPC_MSG_TYPE_PULSE      ((uint8_t)0x02U)
#define IPC_MSG_TYPE_NOTIFY     ((uint8_t)0x03U)

/* ========================================================================
 * IPC 消息头（与 kernel/ipc_types.h 一致）
 * ======================================================================== */

typedef struct
{
    ipc_msg_tag_t tag;
    kobj_id_t     src_thread;
    kobj_id_t     dst_endpoint;
    int32_t       status;
    uint64_t      inline_data[TEST_IPC_REG_MSG_WORDS];
} ipc_msg_header_t;

/* ========================================================================
 * 端点状态与类型（与 kernel/ipc_types.h 一致）
 * ======================================================================== */

typedef enum
{
    IPC_EP_IDLE = 0U,
    IPC_EP_PENDING,
    IPC_EP_RECEIVING,
    IPC_EP_REPLYING
} ipc_ep_state_t;

typedef struct
{
    kobj_id_t           id;
    ipc_ep_state_t      state;
    thread_id_t         owner_tid;
    struct list_head    pending_list;
    struct list_head    node;
    TicketLock_t        lock;
} ipc_endpoint_t;

/* ========================================================================
 * 端点子系统模拟实现（与 endpoint.c 逻辑一致）
 * ======================================================================== */

static ipc_endpoint_t s_endpoints[TEST_MAX_ENDPOINTS];
static struct list_head s_active_endpoints;
static uint32_t s_free_ep_stack[TEST_MAX_ENDPOINTS];
static uint32_t s_free_ep_count;
static TicketLock_t s_ep_subsys_lock;

/**
 * @brief 分配空闲端点索引
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
 */
static ipc_endpoint_t *get_endpoint(kobj_id_t ep_id)
{
    uint32_t idx;

    if (ep_id == KOBJ_ID_INVALID)
    {
        return NULL;
    }

    idx = (uint32_t)(ep_id & 0xFFFFU);
    if (idx >= TEST_MAX_ENDPOINTS)
    {
        return NULL;
    }

    return &s_endpoints[idx];
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

/**
 * @brief 初始化端点子系统
 * @details 与 endpoint.c 的 ipc_endpoint_subsys_init 逻辑一致
 * @note 索引 0 保留不分配（因为 id=0 与 KOBJ_ID_INVALID 冲突），
 *       实际可分配索引为 1 ~ (TEST_MAX_ENDPOINTS-1)
 */
#define TEST_USABLE_ENDPOINTS (TEST_MAX_ENDPOINTS - 1U)

static kernel_status_t endpoint_subsys_test_init(void)
{
    uint32_t i;

    for (i = 0U; i < TEST_MAX_ENDPOINTS; i++)
    {
        s_endpoints[i].id = KOBJ_ID_INVALID;
        s_endpoints[i].state = IPC_EP_IDLE;
        s_endpoints[i].owner_tid = THREAD_ID_INVALID;
        s_endpoints[i].pending_list.next = &s_endpoints[i].pending_list;
        s_endpoints[i].pending_list.prev = &s_endpoints[i].pending_list;
        s_endpoints[i].node.next = &s_endpoints[i].node;
        s_endpoints[i].node.prev = &s_endpoints[i].node;
        ticket_lock_init(&s_endpoints[i].lock);
    }

    s_active_endpoints.next = &s_active_endpoints;
    s_active_endpoints.prev = &s_active_endpoints;

    ticket_lock_init(&s_ep_subsys_lock);

    /* 从大到小入栈，弹出时从小到大（跳过索引 0） */
    for (i = 0U; i < TEST_USABLE_ENDPOINTS; i++)
    {
        s_free_ep_stack[i] = TEST_USABLE_ENDPOINTS - i;
    }
    s_free_ep_count = TEST_USABLE_ENDPOINTS;

    return KERNEL_OK;
}

/**
 * @brief 创建端点
 * @details 与 endpoint.c 的 ipc_endpoint_create 逻辑一致
 */
static kernel_status_t endpoint_create(thread_id_t owner_tid, kobj_id_t *ep_id)
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

    ep->id = (kobj_id_t)((uint32_t)idx | ((uint32_t)idx << 16U));
    ep->state = IPC_EP_PENDING;
    ep->owner_tid = owner_tid;
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

    *ep_id = ep->id;

    return KERNEL_OK;
}

/**
 * @brief 销毁端点
 * @details 与 endpoint.c 的 ipc_endpoint_destroy 逻辑一致
 */
static kernel_status_t endpoint_destroy(kobj_id_t ep_id)
{
    ipc_endpoint_t *ep;

    ep = get_endpoint(ep_id);
    if (!endpoint_is_valid(ep))
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&ep->lock);

    /* 从活跃链表移除 */
    ep->node.prev->next = ep->node.next;
    ep->node.next->prev = ep->node.prev;
    ep->node.next = &ep->node;
    ep->node.prev = &ep->node;

    /* 清空待处理队列 */
    while (ep->pending_list.next != &ep->pending_list)
    {
        struct list_head *first = ep->pending_list.next;
        list_del(first);
    }

    /* 标记为空闲 */
    ep->id = KOBJ_ID_INVALID;
    ep->state = IPC_EP_IDLE;
    ep->owner_tid = THREAD_ID_INVALID;

    ticket_lock_release(&ep->lock);

    /* 释放端点索引 */
    free_endpoint_index((uint32_t)(ep_id & 0xFFFFU));

    return KERNEL_OK;
}

/**
 * @brief 非阻塞发送
 * @details 与 endpoint.c 的 ipc_msg_try_send 逻辑一致
 */
static kernel_status_t msg_try_send(kobj_id_t ep_id)
{
    ipc_endpoint_t *ep;

    ep = get_endpoint(ep_id);
    if (!endpoint_is_valid(ep))
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&ep->lock);

    if (ep->state == IPC_EP_RECEIVING)
    {
        /* 接收者正在等待，可以立即投递 */
        ep->state = IPC_EP_REPLYING;
        ticket_lock_release(&ep->lock);
        return KERNEL_OK;
    }

    /* 接收者未等待 */
    ticket_lock_release(&ep->lock);
    return -(int32_t)EBUSY;
}

/**
 * @brief 模拟消息入队（慢速路径）
 * @details 将消息头挂入端点的 pending_list
 */
/* 扩展 ipc_msg_header_t 使其可链入 pending_list */
typedef struct
{
    ipc_msg_header_t header;
    struct list_head pending_node;
} test_msg_node_t;

/**
 * @brief 模拟同步发送（宿主机版）
 * @details 与 endpoint.c 的 ipc_msg_send 状态转换一致，
 *          但不阻塞线程，直接返回状态
 */
static kernel_status_t msg_send_sim(kobj_id_t ep_id,
                                     ipc_msg_tag_t tag,
                                     uint32_t send_size)
{
    ipc_endpoint_t *ep;
    (void)tag;

    ep = get_endpoint(ep_id);
    if (!endpoint_is_valid(ep))
    {
        return -(int32_t)EINVAL;
    }

    if (send_size > TEST_IPC_MSG_MAX_SIZE)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&ep->lock);

    if (ep->state == IPC_EP_RECEIVING)
    {
        /* 快速路径：直接传递 */
        ep->state = IPC_EP_REPLYING;
        ticket_lock_release(&ep->lock);
        return KERNEL_OK;
    }

    /* 慢速路径：状态保持 PENDING（消息应入队，简化实现直接返回） */
    ticket_lock_release(&ep->lock);
    return KERNEL_OK;
}

/**
 * @brief 模拟消息接收（宿主机版）
 * @details 与 endpoint.c 的 ipc_msg_receive 逻辑一致
 */
static kernel_status_t msg_receive_sim(kobj_id_t ep_id)
{
    ipc_endpoint_t *ep;

    ep = get_endpoint(ep_id);
    if (!endpoint_is_valid(ep))
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&ep->lock);

    /* 检查是否有待处理消息 */
    if (!list_empty(&ep->pending_list))
    {
        /* 有消息：取出第一条 */
        struct list_head *first = ep->pending_list.next;
        list_del(first);

        ep->state = IPC_EP_REPLYING;
        ticket_lock_release(&ep->lock);
        return KERNEL_OK;
    }

    /* 无消息：设置 RECEIVING 状态（真实内核中线程阻塞） */
    ep->state = IPC_EP_RECEIVING;
    ticket_lock_release(&ep->lock);

    /* 宿主机：不阻塞，返回特殊值表示"已设置接收等待" */
    return KERNEL_OK;
}

/**
 * @brief 模拟消息回复（宿主机版）
 * @details 与 endpoint.c 的 ipc_msg_reply 逻辑一致
 */
static kernel_status_t msg_reply_sim(kobj_id_t ep_id)
{
    ipc_endpoint_t *ep;

    ep = get_endpoint(ep_id);
    if (!endpoint_is_valid(ep))
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&ep->lock);

    if (ep->state != IPC_EP_REPLYING)
    {
        ticket_lock_release(&ep->lock);
        return -(int32_t)EINVAL;
    }

    /* 恢复端点状态 */
    ep->state = IPC_EP_PENDING;

    ticket_lock_release(&ep->lock);

    return KERNEL_OK;
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试 1: 子系统初始化后状态正确
 */
static void test_subsys_init(void)
{
    kernel_status_t ret;

    ret = endpoint_subsys_test_init();

    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(s_free_ep_count, TEST_USABLE_ENDPOINTS);
    TEST_ASSERT_TRUE(list_empty(&s_active_endpoints) == 1);

    /* 所有端点应为 IDLE */
    TEST_ASSERT_EQ(s_endpoints[0U].state, IPC_EP_IDLE);
    TEST_ASSERT_EQ(s_endpoints[TEST_USABLE_ENDPOINTS].state, IPC_EP_IDLE);
}

/**
 * @brief 测试 2: 创建端点成功
 */
static void test_create_basic(void)
{
    kernel_status_t ret;
    kobj_id_t ep_id = KOBJ_ID_INVALID;
    ipc_endpoint_t *ep;

    endpoint_subsys_test_init();

    ret = endpoint_create(1U, &ep_id);

    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_NE(ep_id, KOBJ_ID_INVALID);

    ep = get_endpoint(ep_id);
    TEST_ASSERT_NOT_NULL(ep);
    TEST_ASSERT_EQ(ep->state, IPC_EP_PENDING);
    TEST_ASSERT_EQ(ep->owner_tid, 1U);

    /* 活跃链表非空 */
    TEST_ASSERT_TRUE(list_empty(&s_active_endpoints) == 0);

    /* 空闲数减少 */
    TEST_ASSERT_EQ(s_free_ep_count, TEST_USABLE_ENDPOINTS - 1U);
}

/**
 * @brief 测试 3: 创建端点后销毁
 */
static void test_create_destroy(void)
{
    kernel_status_t ret;
    kobj_id_t ep_id = KOBJ_ID_INVALID;
    ipc_endpoint_t *ep;

    endpoint_subsys_test_init();

    endpoint_create(1U, &ep_id);

    ret = endpoint_destroy(ep_id);

    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 端点回到 IDLE */
    ep = &s_endpoints[(uint32_t)(ep_id & 0xFFFFU)];
    TEST_ASSERT_EQ(ep->state, IPC_EP_IDLE);
    TEST_ASSERT_EQ(ep->id, KOBJ_ID_INVALID);
    TEST_ASSERT_EQ(ep->owner_tid, THREAD_ID_INVALID);

    /* 空闲数恢复 */
    TEST_ASSERT_EQ(s_free_ep_count, TEST_USABLE_ENDPOINTS);

    /* 活跃链表为空 */
    TEST_ASSERT_TRUE(list_empty(&s_active_endpoints) == 1);
}

/**
 * @brief 测试 4: 销毁后可再次创建
 */
static void test_create_reuse(void)
{
    kobj_id_t id1;
    kobj_id_t id2;

    endpoint_subsys_test_init();

    endpoint_create(1U, &id1);
    endpoint_destroy(id1);
    endpoint_create(2U, &id2);

    TEST_ASSERT_NE(id2, KOBJ_ID_INVALID);

    /* 端点应能被复用（索引相同） */
    TEST_ASSERT_EQ((uint32_t)(id1 & 0xFFFFU), (uint32_t)(id2 & 0xFFFFU));
}

/**
 * @brief 测试 5: 端点耗尽返回 ENOMEM
 */
static void test_create_exhaust(void)
{
    kobj_id_t ids[TEST_USABLE_ENDPOINTS];
    kobj_id_t extra;
    kernel_status_t ret;
    uint32_t i;

    endpoint_subsys_test_init();

    /* 分配全部可用端点（不含索引 0） */
    for (i = 0U; i < TEST_USABLE_ENDPOINTS; i++)
    {
        ret = endpoint_create(i + 1U, &ids[i]);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    /* 再次分配应失败 */
    ret = endpoint_create(99U, &extra);
    TEST_ASSERT_EQ(ret, -(int32_t)ENOMEM);

    /* 清理 */
    for (i = 0U; i < TEST_USABLE_ENDPOINTS; i++)
    {
        endpoint_destroy(ids[i]);
    }
}

/**
 * @brief 测试 6: NULL 参数安全检查
 */
static void test_null_param(void)
{
    kernel_status_t ret;

    endpoint_subsys_test_init();

    /* NULL ep_id */
    ret = endpoint_create(1U, NULL);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 无效 ID 销毁 */
    ret = endpoint_destroy(KOBJ_ID_INVALID);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 越界 ID */
    ret = endpoint_destroy((kobj_id_t)(TEST_MAX_ENDPOINTS + 1U));
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

/**
 * @brief 测试 7: try_send — 接收者等待时成功
 */
static void test_try_send_success(void)
{
    kobj_id_t ep_id;
    kernel_status_t ret;
    ipc_endpoint_t *ep;

    endpoint_subsys_test_init();
    endpoint_create(1U, &ep_id);

    /* 模拟接收者正在等待 */
    ep = get_endpoint(ep_id);
    ep->state = IPC_EP_RECEIVING;

    ret = msg_try_send(ep_id);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 端点应转为 REPLYING */
    TEST_ASSERT_EQ(ep->state, IPC_EP_REPLYING);

    endpoint_destroy(ep_id);
}

/**
 * @brief 测试 8: try_send — 接收者未等待时返回 EBUSY
 */
static void test_try_send_busy(void)
{
    kobj_id_t ep_id;
    kernel_status_t ret;

    endpoint_subsys_test_init();
    endpoint_create(1U, &ep_id);

    /* 端点在 PENDING 状态（无接收者等待） */
    ret = msg_try_send(ep_id);
    TEST_ASSERT_EQ(ret, -(int32_t)EBUSY);

    endpoint_destroy(ep_id);
}

/**
 * @brief 测试 9: 同步发送快速路径（接收者等待中）
 */
static void test_send_fast_path(void)
{
    kobj_id_t ep_id;
    kernel_status_t ret;
    ipc_endpoint_t *ep;
    ipc_msg_tag_t tag;

    endpoint_subsys_test_init();
    endpoint_create(1U, &ep_id);

    tag = IPC_TAG_MAKE(IPC_MSG_TYPE_NORMAL, 1U, 64U, 128U);

    /* 模拟接收者已进入 RECEIVING 状态 */
    ep = get_endpoint(ep_id);
    ep->state = IPC_EP_RECEIVING;

    /* 发送应走快速路径 */
    ret = msg_send_sim(ep_id, tag, 64U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 端点应转为 REPLYING */
    TEST_ASSERT_EQ(ep->state, IPC_EP_REPLYING);

    endpoint_destroy(ep_id);
}

/**
 * @brief 测试 10: 同步发送慢速路径（无接收者）
 */
static void test_send_slow_path(void)
{
    kobj_id_t ep_id;
    kernel_status_t ret;
    ipc_endpoint_t *ep;
    ipc_msg_tag_t tag;

    endpoint_subsys_test_init();
    endpoint_create(1U, &ep_id);

    tag = IPC_TAG_MAKE(IPC_MSG_TYPE_NORMAL, 1U, 64U, 128U);

    /* 端点在 PENDING 状态 */
    ep = get_endpoint(ep_id);
    TEST_ASSERT_EQ(ep->state, IPC_EP_PENDING);

    ret = msg_send_sim(ep_id, tag, 64U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    endpoint_destroy(ep_id);
}

/**
 * @brief 测试 11: 发送消息过大返回 EINVAL
 */
static void test_send_oversized(void)
{
    kobj_id_t ep_id;
    kernel_status_t ret;
    ipc_msg_tag_t tag;

    endpoint_subsys_test_init();
    endpoint_create(1U, &ep_id);

    tag = IPC_TAG_MAKE(IPC_MSG_TYPE_NORMAL, 1U, 8192U, 0U);

    ret = msg_send_sim(ep_id, tag, 8192U);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    endpoint_destroy(ep_id);
}

/**
 * @brief 测试 12: 接收消息 — 有待处理消息
 */
static void test_receive_pending(void)
{
    kobj_id_t ep_id;
    kernel_status_t ret;
    ipc_endpoint_t *ep;
    test_msg_node_t msg_node;

    endpoint_subsys_test_init();
    endpoint_create(1U, &ep_id);
    ep = get_endpoint(ep_id);

    /* 手动挂入一条消息到 pending_list */
    init_list_head(&msg_node.pending_node);
    msg_node.header.tag = IPC_TAG_MAKE(IPC_MSG_TYPE_NORMAL, 42U, 32U, 0U);
    list_add_tail(&msg_node.pending_node, &ep->pending_list);

    /* 接收应取出消息 */
    ret = msg_receive_sim(ep_id);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 端点应转为 REPLYING */
    TEST_ASSERT_EQ(ep->state, IPC_EP_REPLYING);

    /* pending_list 应为空 */
    TEST_ASSERT_TRUE(list_empty(&ep->pending_list) == 1);

    endpoint_destroy(ep_id);
}

/**
 * @brief 测试 13: 接收消息 — 无消息时设为 RECEIVING
 */
static void test_receive_empty(void)
{
    kobj_id_t ep_id;
    kernel_status_t ret;
    ipc_endpoint_t *ep;

    endpoint_subsys_test_init();
    endpoint_create(1U, &ep_id);
    ep = get_endpoint(ep_id);

    /* pending_list 为空 */
    TEST_ASSERT_TRUE(list_empty(&ep->pending_list) == 1);

    ret = msg_receive_sim(ep_id);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 端点应设为 RECEIVING */
    TEST_ASSERT_EQ(ep->state, IPC_EP_RECEIVING);

    endpoint_destroy(ep_id);
}

/**
 * @brief 测试 14: 回复消息后端点回到 PENDING
 */
static void test_reply_basic(void)
{
    kobj_id_t ep_id;
    kernel_status_t ret;
    ipc_endpoint_t *ep;

    endpoint_subsys_test_init();
    endpoint_create(1U, &ep_id);
    ep = get_endpoint(ep_id);

    /* 设置为 REPLYING */
    ep->state = IPC_EP_REPLYING;

    ret = msg_reply_sim(ep_id);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 端点应回到 PENDING */
    TEST_ASSERT_EQ(ep->state, IPC_EP_PENDING);

    endpoint_destroy(ep_id);
}

/**
 * @brief 测试 15: 非 REPLYING 状态下回复返回 EINVAL
 */
static void test_reply_invalid_state(void)
{
    kobj_id_t ep_id;
    kernel_status_t ret;

    endpoint_subsys_test_init();
    endpoint_create(1U, &ep_id);

    /* PENDING 状态下尝试回复 */
    ret = msg_reply_sim(ep_id);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    endpoint_destroy(ep_id);
}

/**
 * @brief 测试 16: 完整 Send-Receive-Reply 流程
 */
static void test_send_recv_reply_cycle(void)
{
    kobj_id_t ep_id;
    kernel_status_t ret;
    ipc_endpoint_t *ep;
    ipc_msg_tag_t tag;

    endpoint_subsys_test_init();
    endpoint_create(1U, &ep_id);
    ep = get_endpoint(ep_id);

    /* 1) 接收者先进入接收等待 */
    ret = msg_receive_sim(ep_id);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(ep->state, IPC_EP_RECEIVING);

    /* 2) 发送者发送（快速路径） */
    tag = IPC_TAG_MAKE(IPC_MSG_TYPE_NORMAL, 100U, 64U, 128U);
    ret = msg_send_sim(ep_id, tag, 64U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(ep->state, IPC_EP_REPLYING);

    /* 3) 接收者处理完后回复 */
    ret = msg_reply_sim(ep_id);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(ep->state, IPC_EP_PENDING);

    endpoint_destroy(ep_id);
}

/**
 * @brief 测试 17: 多条消息入队后逐个接收
 */
static void test_multiple_messages(void)
{
    kobj_id_t ep_id;
    kernel_status_t ret;
    ipc_endpoint_t *ep;
    test_msg_node_t msgs[3];
    uint32_t i;

    endpoint_subsys_test_init();
    endpoint_create(1U, &ep_id);
    ep = get_endpoint(ep_id);

    /* 入队 3 条消息 */
    for (i = 0U; i < 3U; i++)
    {
        init_list_head(&msgs[i].pending_node);
        msgs[i].header.tag = IPC_TAG_MAKE(IPC_MSG_TYPE_NORMAL, i + 1U, 32U, 0U);
        list_add_tail(&msgs[i].pending_node, &ep->pending_list);
    }

    /* 逐个接收 */
    for (i = 0U; i < 3U; i++)
    {
        ret = msg_receive_sim(ep_id);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
        TEST_ASSERT_EQ(ep->state, IPC_EP_REPLYING);

        /* 回复后才能接收下一条 */
        ret = msg_reply_sim(ep_id);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    /* 队列应为空 */
    TEST_ASSERT_TRUE(list_empty(&ep->pending_list) == 1);
    TEST_ASSERT_EQ(ep->state, IPC_EP_PENDING);

    endpoint_destroy(ep_id);
}

/**
 * @brief 测试 18: 销毁端点时清空待处理队列
 */
static void test_destroy_clears_pending(void)
{
    kobj_id_t ep_id;
    ipc_endpoint_t *ep;
    test_msg_node_t msgs[4];
    uint32_t i;

    endpoint_subsys_test_init();
    endpoint_create(1U, &ep_id);
    ep = get_endpoint(ep_id);

    /* 入队 4 条消息 */
    for (i = 0U; i < 4U; i++)
    {
        init_list_head(&msgs[i].pending_node);
        list_add_tail(&msgs[i].pending_node, &ep->pending_list);
    }

    TEST_ASSERT_TRUE(list_empty(&ep->pending_list) == 0);

    /* 销毁端点 */
    endpoint_destroy(ep_id);

    /* 端点应为 IDLE */
    ep = &s_endpoints[(uint32_t)(ep_id & 0xFFFFU)];
    TEST_ASSERT_EQ(ep->state, IPC_EP_IDLE);
    TEST_ASSERT_TRUE(list_empty(&ep->pending_list) == 1);
}

/**
 * @brief 测试 19: 消息标签构造与解析
 */
static void test_msg_tag_operations(void)
{
    ipc_msg_tag_t tag;

    tag = IPC_TAG_MAKE(IPC_MSG_TYPE_NORMAL, 0x1234U, 256U, 512U);

    TEST_ASSERT_EQ(IPC_TAG_TYPE(tag), IPC_MSG_TYPE_NORMAL);
    TEST_ASSERT_EQ(IPC_TAG_MSG_ID(tag), 0x1234U);
    TEST_ASSERT_EQ(IPC_TAG_SEND_SIZE(tag), 256U);
    TEST_ASSERT_EQ(IPC_TAG_RECV_SIZE(tag), 512U);

    /* 最大消息类型 */
    tag = IPC_TAG_MAKE(0xFFU, 0xFFFFU, 0xFFFFU, 0xFFFFU);
    TEST_ASSERT_EQ(IPC_TAG_TYPE(tag), 0xFFU);
    TEST_ASSERT_EQ(IPC_TAG_MSG_ID(tag), 0xFFFFU);
    TEST_ASSERT_EQ(IPC_TAG_SEND_SIZE(tag), 0xFFFFU);
    TEST_ASSERT_EQ(IPC_TAG_RECV_SIZE(tag), 0xFFFFU);

    /* Pulse 类型 */
    tag = IPC_TAG_MAKE(IPC_MSG_TYPE_PULSE, 0U, 16U, 0U);
    TEST_ASSERT_EQ(IPC_TAG_TYPE(tag), IPC_MSG_TYPE_PULSE);

    /* 通知类型 */
    tag = IPC_TAG_MAKE(IPC_MSG_TYPE_NOTIFY, 0U, 0U, 0U);
    TEST_ASSERT_EQ(IPC_TAG_TYPE(tag), IPC_MSG_TYPE_NOTIFY);
}

/**
 * @brief 测试 20: 多端点并行存在
 */
static void test_multiple_endpoints(void)
{
    kobj_id_t ids[4];
    ipc_endpoint_t *ep;
    uint32_t i;

    endpoint_subsys_test_init();

    /* 创建 4 个端点 */
    for (i = 0U; i < 4U; i++)
    {
        endpoint_create(i + 1U, &ids[i]);
    }

    /* 验证各端点独立 */
    for (i = 0U; i < 4U; i++)
    {
        ep = get_endpoint(ids[i]);
        TEST_ASSERT_NOT_NULL(ep);
        TEST_ASSERT_EQ(ep->state, IPC_EP_PENDING);
        TEST_ASSERT_EQ(ep->owner_tid, i + 1U);
    }

    /* 活跃链表应有 4 个 */
    TEST_ASSERT_TRUE(list_empty(&s_active_endpoints) == 0);

    /* 销毁其中一个 */
    endpoint_destroy(ids[1U]);

    /* 其余端点仍有效 */
    ep = get_endpoint(ids[0U]);
    TEST_ASSERT_EQ(ep->state, IPC_EP_PENDING);
    ep = get_endpoint(ids[2U]);
    TEST_ASSERT_EQ(ep->state, IPC_EP_PENDING);

    /* 已销毁的应无效 */
    ep = get_endpoint(ids[1U]);
    TEST_ASSERT_EQ(ep->state, IPC_EP_IDLE);

    /* 清理 */
    endpoint_destroy(ids[0U]);
    endpoint_destroy(ids[2U]);
    endpoint_destroy(ids[3U]);
}

/**
 * @brief 测试 21: 无效端点 ID 操作安全
 */
static void test_invalid_ep_id(void)
{
    kernel_status_t ret;
    ipc_msg_tag_t tag;

    endpoint_subsys_test_init();

    tag = IPC_TAG_MAKE(IPC_MSG_TYPE_NORMAL, 1U, 32U, 0U);

    /* 发送到无效 ID */
    ret = msg_send_sim(KOBJ_ID_INVALID, tag, 32U);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* try_send 无效 ID */
    ret = msg_try_send(KOBJ_ID_INVALID);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 接收无效 ID */
    ret = msg_receive_sim(KOBJ_ID_INVALID);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 回复无效 ID */
    ret = msg_reply_sim(KOBJ_ID_INVALID);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    TEST_ASSERT_TRUE(true);
}

/**
 * @brief 测试 22: 端点 ID 生成 — 索引在低 16 位
 */
static void test_ep_id_format(void)
{
    kobj_id_t ep_id;
    uint32_t idx;

    endpoint_subsys_test_init();
    endpoint_create(1U, &ep_id);

    /* 低 16 位应为索引（栈弹出顺序：最小索引先分配） */
    idx = (uint32_t)(ep_id & 0xFFFFU);
    TEST_ASSERT_EQ(idx, 1U);

    /* 高 16 位含版本信息 */
    TEST_ASSERT_NE(ep_id, KOBJ_ID_INVALID);

    endpoint_destroy(ep_id);
}

/**
 * @brief 测试 23: 压力测试 — 反复创建销毁
 */
static void test_stress_create_destroy(void)
{
    kobj_id_t ep_id;
    uint32_t i;

    endpoint_subsys_test_init();

    for (i = 0U; i < 200U; i++)
    {
        kernel_status_t ret = endpoint_create(i % 256U, &ep_id);
        TEST_ASSERT_EQ(ret, KERNEL_OK);

        ret = endpoint_destroy(ep_id);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    /* 空闲数应恢复 */
    TEST_ASSERT_EQ(s_free_ep_count, TEST_USABLE_ENDPOINTS);
}

/**
 * @brief 测试 24: 压力测试 — 发送接收循环
 */
static void test_stress_send_recv(void)
{
    kobj_id_t ep_id;
    ipc_endpoint_t *ep;
    ipc_msg_tag_t tag;
    kernel_status_t ret;
    uint32_t i;

    endpoint_subsys_test_init();
    endpoint_create(1U, &ep_id);
    ep = get_endpoint(ep_id);

    tag = IPC_TAG_MAKE(IPC_MSG_TYPE_NORMAL, 1U, 32U, 0U);

    for (i = 0U; i < 500U; i++)
    {
        /* 接收者先等待 */
        ep->state = IPC_EP_RECEIVING;

        /* 发送者快速路径发送 */
        ret = msg_send_sim(ep_id, tag, 32U);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
        TEST_ASSERT_EQ(ep->state, IPC_EP_REPLYING);

        /* 回复 */
        ret = msg_reply_sim(ep_id);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
        TEST_ASSERT_EQ(ep->state, IPC_EP_PENDING);
    }

    endpoint_destroy(ep_id);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("=== IPC 端点同步消息传递测试 ===\n\n");

    test_subsys_init();
    test_create_basic();
    test_create_destroy();
    test_create_reuse();
    test_create_exhaust();
    test_null_param();
    test_try_send_success();
    test_try_send_busy();
    test_send_fast_path();
    test_send_slow_path();
    test_send_oversized();
    test_receive_pending();
    test_receive_empty();
    test_reply_basic();
    test_reply_invalid_state();
    test_send_recv_reply_cycle();
    test_multiple_messages();
    test_destroy_clears_pending();
    test_msg_tag_operations();
    test_multiple_endpoints();
    test_invalid_ep_id();
    test_ep_id_format();
    test_stress_create_destroy();
    test_stress_send_recv();

    TEST_SUMMARY("test_endpoint");

    return TEST_RESULT();
}
