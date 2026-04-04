/**
 * @file    test_channel.c
 * @brief   AISafe64 RTOS - IPC 通道/连接/Pulse 单元测试（宿主机）
 * @author  AISafe64 Team
 * @date    2026-04-03
 * @version 1.0
 *
 * @details IPC 通道-连接-Pulse 宿主机自包含测试
 *          测试与内核 channel.c 一致的逻辑：
 *          - 子系统初始化（通道池、连接池、Pulse 池、空闲栈）
 *          - 通道创建/销毁
 *          - 连接附加/分离
 *          - 通道销毁时断开所有连接
 *          - Pulse 发送（优先级排序插入）
 *          - Pulse 非阻塞接收（最高优先级先出）
 *          - Pulse 队列满返回错误
 *          - 空队列接收返回 EAGAIN
 *          - 连接断开后发送返回错误
 *          - 通道关闭后连接/发送返回错误
 *          - 多通道多连接并行
 *          - NULL / 无效参数安全
 *          - 压力测试
 *
 * @note 宿主机单线程模拟：不测试 schedule() 上下文切换，
 *       专注测试状态机转换、优先级排序和锁正确性
 * @note 对应需求: KR-007（Pulse）、KR-023（通道-连接模型）、TF-001
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

#undef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* ========================================================================
 * 配置常量（与 kernel/config.h 一致）
 * ======================================================================== */

#define TEST_MAX_CHANNELS       16U
#define TEST_MAX_CONNECTIONS    32U
#define TEST_MAX_PULSE_QUEUE    8U
#define TEST_MAX_THREADS        256U
#define TEST_IPC_REG_MSG_WORDS  4U

#define KOBJ_ID_INVALID         ((kobj_id_t)0U)
#define THREAD_ID_INVALID       ((thread_id_t)0xFFFFFFFFU)

/* ========================================================================
 * IPC 错误码（与 kernel/ipc_types.h 一致）
 * ======================================================================== */

#define IPC_ERR_CHANNEL_CLOSED    ((int32_t)(-100))
#define IPC_ERR_CONN_DISCONNECTED ((int32_t)(-101))
#define IPC_ERR_PULSE_QUEUE_FULL  ((int32_t)(-104))

/* ========================================================================
 * 通道状态与类型（与 kernel/ipc_types.h 一致）
 * ======================================================================== */

typedef enum
{
    IPC_CH_CLOSED = 0U,
    IPC_CH_OPEN
} ipc_ch_state_t;

typedef enum
{
    IPC_CONN_DISCONNECTED = 0U,
    IPC_CONN_CONNECTED
} ipc_conn_state_t;

typedef struct
{
    uint64_t value;
} ipc_msg_tag_t;

typedef struct
{
    ipc_msg_tag_t tag;
    kobj_id_t     src_thread;
    kobj_id_t     dst_endpoint;
    int32_t       status;
    uint64_t      inline_data[TEST_IPC_REG_MSG_WORDS];
} ipc_msg_header_t;

typedef struct
{
    kobj_id_t           id;
    ipc_ch_state_t      state;
    thread_id_t         owner_tid;
    struct list_head    conn_list;
    struct list_head    pulse_queue;
    uint32_t            pulse_count;
    struct list_head    node;
    TicketLock_t        lock;
} ipc_channel_t;

typedef struct
{
    kobj_id_t           id;
    ipc_conn_state_t    state;
    kobj_id_t           channel_id;
    thread_id_t         client_tid;
    ipc_msg_header_t    *pending_msg;
    struct list_head    ch_node;
    struct list_head    node;
} ipc_connection_t;

typedef struct
{
    priority_t  prio;
    uint8_t     reserved[3];
    int32_t     code;
    int32_t     value;
    kobj_id_t   conn_id;
    struct list_head node;
} ipc_pulse_t;

/* ========================================================================
 * 通道子系统模拟实现（与 channel.c 逻辑一致）
 * ======================================================================== */

static ipc_channel_t s_channels[TEST_MAX_CHANNELS];
static ipc_connection_t s_connections[TEST_MAX_CONNECTIONS];
static ipc_pulse_t s_pulse_pool[TEST_MAX_PULSE_QUEUE * TEST_MAX_CHANNELS];
static uint32_t s_free_ch_stack[TEST_MAX_CHANNELS];
static uint32_t s_free_ch_count;
static uint32_t s_free_conn_stack[TEST_MAX_CONNECTIONS];
static uint32_t s_free_conn_count;
static uint32_t s_pulse_free_stack[TEST_MAX_PULSE_QUEUE * TEST_MAX_CHANNELS];
static uint32_t s_pulse_free_count;
static TicketLock_t s_ch_subsys_lock;
static TicketLock_t s_pulse_pool_lock;

/**
 * @brief 索引 0 保留（因为 id=0 与 KOBJ_ID_INVALID 冲突）
 */
#define TEST_USABLE_CHANNELS    (TEST_MAX_CHANNELS - 1U)
#define TEST_USABLE_CONNECTIONS (TEST_MAX_CONNECTIONS - 1U)
#define TEST_PULSE_POOL_SIZE    (TEST_MAX_PULSE_QUEUE * TEST_MAX_CHANNELS)

/* ========================================================================
 * Pulse 对象池管理（与 channel.c 逻辑一致）
 * ======================================================================== */

/**
 * @brief 分配空闲 Pulse 对象
 */
static ipc_pulse_t *alloc_pulse(void)
{
    ipc_pulse_t *pulse = NULL;

    ticket_lock_acquire(&s_pulse_pool_lock);

    if (s_pulse_free_count > 0U)
    {
        s_pulse_free_count--;
        uint32_t idx = s_pulse_free_stack[s_pulse_free_count];
        pulse = &s_pulse_pool[idx];
    }

    ticket_lock_release(&s_pulse_pool_lock);

    return pulse;
}

/**
 * @brief 释放 Pulse 对象回空闲池
 */
static void free_pulse(ipc_pulse_t *pulse)
{
    uint32_t idx;

    if (pulse == NULL)
    {
        return;
    }

    idx = (uint32_t)(pulse - s_pulse_pool);

    ticket_lock_acquire(&s_pulse_pool_lock);

    if (idx < TEST_PULSE_POOL_SIZE)
    {
        s_pulse_free_stack[s_pulse_free_count] = idx;
        s_pulse_free_count++;
    }

    ticket_lock_release(&s_pulse_pool_lock);
}

/* ========================================================================
 * 内部辅助函数（与 channel.c 逻辑一致）
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
    if (idx >= TEST_MAX_CHANNELS)
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
    if (idx >= TEST_MAX_CONNECTIONS)
    {
        return NULL;
    }

    return &s_connections[idx];
}

/**
 * @brief 检查通道是否有效
 * @note 保留以匹配内核 channel.c 逻辑
 */
static bool UNUSED channel_is_valid(const ipc_channel_t *ch)
{
    if (ch == NULL)
    {
        return false;
    }

    return (ch->state == IPC_CH_OPEN) ? true : false;
}

/**
 * @brief 检查连接是否有效
 * @note 保留以匹配内核 channel.c 逻辑
 */
static bool UNUSED connection_is_valid(const ipc_connection_t *conn)
{
    if (conn == NULL)
    {
        return false;
    }

    return (conn->state == IPC_CONN_CONNECTED) ? true : false;
}

/* ========================================================================
 * 通道子系统初始化（与 channel.c 的 ipc_channel_subsys_init 一致）
 * ======================================================================== */

static kernel_status_t channel_subsys_test_init(void)
{
    uint32_t i;

    /* 初始化通道表（栈：索引 0 在底部，最后弹出） */
    for (i = 0U; i < TEST_MAX_CHANNELS; i++)
    {
        s_free_ch_stack[i] = i;
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
    s_free_ch_count = TEST_MAX_CHANNELS;

    /* 初始化连接表（栈：索引 0 在底部，最后弹出） */
    for (i = 0U; i < TEST_MAX_CONNECTIONS; i++)
    {
        s_free_conn_stack[i] = i;
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
    s_free_conn_count = TEST_MAX_CONNECTIONS;

    /* 初始化 Pulse 空闲池 */
    for (i = 0U; i < TEST_PULSE_POOL_SIZE; i++)
    {
        s_pulse_free_stack[i] = i;
    }
    s_pulse_free_count = TEST_PULSE_POOL_SIZE;

    ticket_lock_init(&s_ch_subsys_lock);
    ticket_lock_init(&s_pulse_pool_lock);

    return KERNEL_OK;
}

/* ========================================================================
 * 通道管理 API（与 channel.c 逻辑一致）
 * ======================================================================== */

/**
 * @brief 创建通道（与 channel.c 的 ipc_channel_create 一致）
 * @note 宿主机版：跳过 owner_tid 上限检查以简化测试
 */
static kernel_status_t channel_create(thread_id_t owner_tid, kobj_id_t *ch_id)
{
    int32_t idx;
    ipc_channel_t *ch;

    if (ch_id == NULL)
    {
        return -(int32_t)EINVAL;
    }

    (void)owner_tid;

    /* 跳过索引 0（id=0 与 KOBJ_ID_INVALID 冲突） */
    idx = alloc_channel_index();
    if (idx < 0)
    {
        return -(int32_t)ENOMEM;
    }

    if (idx == 0)
    {
        /* 索引 0 不可用，放回并重新分配 */
        free_channel_index((uint32_t)idx);
        idx = alloc_channel_index();
        if (idx <= 0)
        {
            /* 再次获得 0 或分配失败 */
            return -(int32_t)ENOMEM;
        }
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

    *ch_id = ch->id;

    return KERNEL_OK;
}

/**
 * @brief 销毁通道（与 channel.c 的 ipc_channel_destroy 一致）
 * @note 宿主机版：跳过线程唤醒（scheduler_enqueue）逻辑
 */
static kernel_status_t channel_destroy(kobj_id_t ch_id)
{
    ipc_channel_t *ch;

    ch = get_channel(ch_id);
    if (ch == NULL)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&ch->lock);

    if (ch->state != IPC_CH_OPEN)
    {
        ticket_lock_release(&ch->lock);
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

        free_connection_index((uint32_t)(conn->id & 0xFFFFU));
    }

    /* 清空 Pulse 队列并释放所有 Pulse 对象 */
    while (ch->pulse_queue.next != &ch->pulse_queue)
    {
        struct list_head *pulse_node = ch->pulse_queue.next;
        ipc_pulse_t *pulse = container_of(pulse_node, ipc_pulse_t, node);

        pulse_node->prev->next = pulse_node->next;
        pulse_node->next->prev = pulse_node->prev;

        free_pulse(pulse);
    }
    ch->pulse_count = 0U;

    /* 标记通道为关闭 */
    ch->state = IPC_CH_CLOSED;
    ch->owner_tid = THREAD_ID_INVALID;

    ticket_lock_release(&ch->lock);

    /* 释放通道索引 */
    free_channel_index((uint32_t)(ch_id & 0xFFFFU));

    return KERNEL_OK;
}

/**
 * @brief 附加连接到通道（与 channel.c 的 ipc_connect_attach 一致）
 * @note 宿主机版：跳过 client_tid 上限检查
 */
static kernel_status_t connect_attach(thread_id_t client_tid,
                                      kobj_id_t ch_id,
                                      kobj_id_t *conn_id)
{
    int32_t idx;
    ipc_channel_t *ch;
    ipc_connection_t *conn;

    if (conn_id == NULL)
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

    ticket_lock_acquire(&ch->lock);

    if (ch->state != IPC_CH_OPEN)
    {
        ticket_lock_release(&ch->lock);
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

    ticket_lock_release(&ch->lock);

    *conn_id = conn->id;

    return KERNEL_OK;
}

/**
 * @brief 分离连接（与 channel.c 的 ipc_connect_detach 一致）
 */
static kernel_status_t connect_detach(kobj_id_t conn_id)
{
    ipc_connection_t *conn;
    ipc_channel_t *ch;

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

    ticket_lock_acquire(&ch->lock);

    /* 从通道连接列表移除 */
    conn->ch_node.prev->next = conn->ch_node.next;
    conn->ch_node.next->prev = conn->ch_node.prev;
    conn->ch_node.next = &conn->ch_node;
    conn->ch_node.prev = &conn->ch_node;

    /* 标记为断开 */
    conn->state = IPC_CONN_DISCONNECTED;
    conn->channel_id = KOBJ_ID_INVALID;
    conn->client_tid = THREAD_ID_INVALID;

    ticket_lock_release(&ch->lock);

    free_connection_index((uint32_t)(conn_id & 0xFFFFU));

    return KERNEL_OK;
}

/**
 * @brief 发送 Pulse（与 channel.c 的 ipc_pulse_send 一致）
 * @note 宿主机版：跳过线程唤醒逻辑
 */
static kernel_status_t pulse_send(kobj_id_t conn_id,
                                   priority_t prio,
                                   int32_t code,
                                   int32_t value)
{
    ipc_connection_t *conn;
    ipc_channel_t *ch;
    ipc_pulse_t *pulse;
    struct list_head *pos;

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

    ticket_lock_acquire(&ch->lock);

    if (ch->state != IPC_CH_OPEN)
    {
        ticket_lock_release(&ch->lock);
        return IPC_ERR_CHANNEL_CLOSED;
    }

    /* 检查 Pulse 队列是否已满 */
    if (ch->pulse_count >= TEST_MAX_PULSE_QUEUE)
    {
        ticket_lock_release(&ch->lock);
        return IPC_ERR_PULSE_QUEUE_FULL;
    }

    /* 从 Pulse 池分配一个 Pulse 对象 */
    pulse = alloc_pulse();

    if (pulse == NULL)
    {
        ticket_lock_release(&ch->lock);
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

    ticket_lock_release(&ch->lock);

    return KERNEL_OK;
}

/**
 * @brief 非阻塞接收 Pulse（与 channel.c 的 ipc_pulse_receive 一致）
 */
static kernel_status_t pulse_receive(kobj_id_t ch_id, ipc_pulse_t *pulse)
{
    ipc_channel_t *ch;
    ipc_pulse_t *first_pulse;

    if (pulse == NULL)
    {
        return -(int32_t)EINVAL;
    }

    ch = get_channel(ch_id);
    if (ch == NULL)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&ch->lock);

    if (ch->pulse_queue.next == &ch->pulse_queue)
    {
        ticket_lock_release(&ch->lock);
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

    /* 拷贝 Pulse 数据到用户缓冲区 */
    pulse->prio = first_pulse->prio;
    pulse->code = first_pulse->code;
    pulse->value = first_pulse->value;
    pulse->conn_id = first_pulse->conn_id;

    /* 释放 Pulse 对象回空闲池 */
    free_pulse(first_pulse);

    ticket_lock_release(&ch->lock);

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

    ret = channel_subsys_test_init();

    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(s_free_ch_count, TEST_MAX_CHANNELS);
    TEST_ASSERT_EQ(s_free_conn_count, TEST_MAX_CONNECTIONS);
    TEST_ASSERT_EQ(s_pulse_free_count, TEST_PULSE_POOL_SIZE);

    /* 所有通道应为 CLOSED */
    TEST_ASSERT_EQ(s_channels[0U].state, IPC_CH_CLOSED);
    TEST_ASSERT_EQ(s_channels[TEST_MAX_CHANNELS - 1U].state, IPC_CH_CLOSED);

    /* 所有连接应为 DISCONNECTED */
    TEST_ASSERT_EQ(s_connections[0U].state, IPC_CONN_DISCONNECTED);

    /* 空闲栈：索引 0 在底部（最后弹出），最大索引在栈顶（最先弹出）
     * channel_create 会跳过索引 0（因为 id=0 与 KOBJ_ID_INVALID 冲突）
     * 初始化时所有索引按升序入栈 */
    TEST_ASSERT_EQ(s_free_ch_stack[s_free_ch_count - 1U], TEST_MAX_CHANNELS - 1U);
}

/**
 * @brief 测试 2: 创建通道成功
 */
static void test_channel_create_basic(void)
{
    kernel_status_t ret;
    kobj_id_t ch_id = KOBJ_ID_INVALID;
    ipc_channel_t *ch;

    channel_subsys_test_init();

    ret = channel_create(1U, &ch_id);

    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_NE(ch_id, KOBJ_ID_INVALID);

    ch = get_channel(ch_id);
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_EQ(ch->state, IPC_CH_OPEN);
    TEST_ASSERT_EQ(ch->owner_tid, 1U);
    TEST_ASSERT_EQ(ch->pulse_count, 0U);
    TEST_ASSERT_TRUE(list_empty(&ch->conn_list) == 1);
    TEST_ASSERT_TRUE(list_empty(&ch->pulse_queue) == 1);

    /* 空闲通道数减少 */
    TEST_ASSERT_EQ(s_free_ch_count, TEST_MAX_CHANNELS - 1U);
}

/**
 * @brief 测试 3: 创建后销毁通道
 */
static void test_channel_create_destroy(void)
{
    kernel_status_t ret;
    kobj_id_t ch_id = KOBJ_ID_INVALID;
    ipc_channel_t *ch;

    channel_subsys_test_init();

    channel_create(1U, &ch_id);

    ret = channel_destroy(ch_id);

    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ch = &s_channels[(uint32_t)(ch_id & 0xFFFFU)];
    TEST_ASSERT_EQ(ch->state, IPC_CH_CLOSED);
    TEST_ASSERT_EQ(ch->owner_tid, THREAD_ID_INVALID);

    /* 空闲数恢复 */
    TEST_ASSERT_EQ(s_free_ch_count, TEST_MAX_CHANNELS);
}

/**
 * @brief 测试 4: 销毁后可再次创建
 */
static void test_channel_create_reuse(void)
{
    kobj_id_t id1;
    kobj_id_t id2;

    channel_subsys_test_init();

    channel_create(1U, &id1);
    channel_destroy(id1);
    channel_create(2U, &id2);

    TEST_ASSERT_NE(id2, KOBJ_ID_INVALID);

    /* 索引应被复用 */
    TEST_ASSERT_EQ((uint32_t)(id1 & 0xFFFFU), (uint32_t)(id2 & 0xFFFFU));
}

/**
 * @brief 测试 5: 通道耗尽返回 ENOMEM
 */
static void test_channel_exhaust(void)
{
    /* 索引 0 被保留，因此最多可分配 TEST_MAX_CHANNELS - 1 个通道 */
    kobj_id_t ids[TEST_MAX_CHANNELS];
    kobj_id_t extra;
    kernel_status_t ret;
    uint32_t i;
    uint32_t usable = TEST_MAX_CHANNELS - 1U;

    channel_subsys_test_init();

    for (i = 0U; i < usable; i++)
    {
        ret = channel_create(i + 1U, &ids[i]);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    /* 再次分配应失败（索引 0 被跳过，所有可用索引已用完） */
    ret = channel_create(99U, &extra);
    TEST_ASSERT_EQ(ret, -(int32_t)ENOMEM);

    /* 清理 */
    for (i = 0U; i < usable; i++)
    {
        channel_destroy(ids[i]);
    }
}

/**
 * @brief 测试 6: NULL 参数安全检查
 */
static void test_null_param(void)
{
    kernel_status_t ret;

    channel_subsys_test_init();

    /* NULL ch_id */
    ret = channel_create(1U, NULL);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 无效 ID 销毁 */
    ret = channel_destroy(KOBJ_ID_INVALID);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 越界 ID */
    ret = channel_destroy((kobj_id_t)(TEST_MAX_CHANNELS + 1U));
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* NULL conn_id */
    kobj_id_t ch_id;
    channel_create(1U, &ch_id);
    ret = connect_attach(2U, ch_id, NULL);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
    channel_destroy(ch_id);
}

/**
 * @brief 测试 7: 连接到通道成功
 */
static void test_connect_attach_basic(void)
{
    kernel_status_t ret;
    kobj_id_t ch_id;
    kobj_id_t conn_id = KOBJ_ID_INVALID;
    ipc_connection_t *conn;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);

    ret = connect_attach(2U, ch_id, &conn_id);

    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_NE(conn_id, KOBJ_ID_INVALID);

    conn = get_connection(conn_id);
    TEST_ASSERT_NOT_NULL(conn);
    TEST_ASSERT_EQ(conn->state, IPC_CONN_CONNECTED);
    TEST_ASSERT_EQ(conn->channel_id, ch_id);
    TEST_ASSERT_EQ(conn->client_tid, 2U);

    /* 通道的连接列表非空 */
    ipc_channel_t *ch = get_channel(ch_id);
    TEST_ASSERT_TRUE(list_empty(&ch->conn_list) == 0);

    /* 空闲连接数减少 */
    TEST_ASSERT_EQ(s_free_conn_count, TEST_MAX_CONNECTIONS - 1U);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 8: 连接到无效通道失败
 */
static void test_connect_invalid_channel(void)
{
    kernel_status_t ret;
    kobj_id_t conn_id;

    channel_subsys_test_init();

    /* 连接到无效 ID */
    ret = connect_attach(2U, KOBJ_ID_INVALID, &conn_id);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 连接到越界 ID */
    ret = connect_attach(2U, (kobj_id_t)(TEST_MAX_CHANNELS + 1U), &conn_id);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

/**
 * @brief 测试 9: 连接到已关闭通道返回 IPC_ERR_CHANNEL_CLOSED
 */
static void test_connect_closed_channel(void)
{
    kernel_status_t ret;
    kobj_id_t ch_id;
    kobj_id_t conn_id;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);
    channel_destroy(ch_id);

    /* 连接到已关闭的通道 */
    ret = connect_attach(2U, ch_id, &conn_id);
    TEST_ASSERT_EQ(ret, IPC_ERR_CHANNEL_CLOSED);

    /* 空闲连接数不应减少 */
    TEST_ASSERT_EQ(s_free_conn_count, TEST_MAX_CONNECTIONS);
}

/**
 * @brief 测试 10: 分离连接
 */
static void test_connect_detach_basic(void)
{
    kernel_status_t ret;
    kobj_id_t ch_id;
    kobj_id_t conn_id;
    ipc_connection_t *conn;
    ipc_channel_t *ch;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);
    connect_attach(2U, ch_id, &conn_id);

    ret = connect_detach(conn_id);

    TEST_ASSERT_EQ(ret, KERNEL_OK);

    conn = &s_connections[(uint32_t)(conn_id & 0xFFFFU)];
    TEST_ASSERT_EQ(conn->state, IPC_CONN_DISCONNECTED);
    TEST_ASSERT_EQ(conn->channel_id, KOBJ_ID_INVALID);

    /* 通道的连接列表应为空 */
    ch = get_channel(ch_id);
    TEST_ASSERT_TRUE(list_empty(&ch->conn_list) == 1);

    /* 空闲连接数恢复 */
    TEST_ASSERT_EQ(s_free_conn_count, TEST_MAX_CONNECTIONS);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 11: 分离已断开的连接返回 EINVAL
 */
static void test_detach_disconnected(void)
{
    kernel_status_t ret;
    kobj_id_t ch_id;
    kobj_id_t conn_id;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);
    connect_attach(2U, ch_id, &conn_id);

    /* 先分离一次 */
    connect_detach(conn_id);

    /* 再次分离应失败 */
    ret = connect_detach(conn_id);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 12: 通道销毁时自动断开所有连接
 */
static void test_destroy_disconnects_all(void)
{
    kobj_id_t ch_id;
    kobj_id_t conn_ids[4];
    ipc_connection_t *conn;
    uint32_t i;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);

    /* 创建 4 个连接 */
    for (i = 0U; i < 4U; i++)
    {
        connect_attach(i + 10U, ch_id, &conn_ids[i]);
    }

    /* 销毁通道 */
    channel_destroy(ch_id);

    /* 所有连接应被断开 */
    for (i = 0U; i < 4U; i++)
    {
        conn = &s_connections[(uint32_t)(conn_ids[i] & 0xFFFFU)];
        TEST_ASSERT_EQ(conn->state, IPC_CONN_DISCONNECTED);
        TEST_ASSERT_EQ(conn->channel_id, KOBJ_ID_INVALID);
    }

    /* 连接索引应被释放 */
    TEST_ASSERT_EQ(s_free_conn_count, TEST_MAX_CONNECTIONS);
    TEST_ASSERT_EQ(s_free_ch_count, TEST_MAX_CHANNELS);
}

/**
 * @brief 测试 13: 发送 Pulse 成功
 */
static void test_pulse_send_basic(void)
{
    kernel_status_t ret;
    kobj_id_t ch_id;
    kobj_id_t conn_id;
    ipc_channel_t *ch;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);
    connect_attach(2U, ch_id, &conn_id);

    ret = pulse_send(conn_id, 10U, 100, 200);

    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ch = get_channel(ch_id);
    TEST_ASSERT_EQ(ch->pulse_count, 1U);
    TEST_ASSERT_TRUE(list_empty(&ch->pulse_queue) == 0);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 14: 发送 Pulse — 无效连接
 */
static void test_pulse_send_invalid_conn(void)
{
    kernel_status_t ret;

    channel_subsys_test_init();

    /* 无效连接 ID */
    ret = pulse_send(KOBJ_ID_INVALID, 10U, 100, 200);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 越界连接 ID */
    ret = pulse_send((kobj_id_t)(TEST_MAX_CONNECTIONS + 1U), 10U, 100, 200);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

/**
 * @brief 测试 15: 发送 Pulse — 断开的连接
 */
static void test_pulse_send_disconnected(void)
{
    kernel_status_t ret;
    kobj_id_t ch_id;
    kobj_id_t conn_id;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);
    connect_attach(2U, ch_id, &conn_id);

    /* 断开连接后发送 */
    connect_detach(conn_id);

    ret = pulse_send(conn_id, 10U, 100, 200);
    TEST_ASSERT_EQ(ret, IPC_ERR_CONN_DISCONNECTED);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 16: 发送 Pulse — 已关闭通道
 */
static void test_pulse_send_closed_channel(void)
{
    kernel_status_t ret;
    kobj_id_t ch_id;
    kobj_id_t conn_id;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);
    connect_attach(2U, ch_id, &conn_id);

    /* 销毁通道后发送（连接的 channel_id 仍指向旧通道，
     * 但通道已 CLOSED — 需通过 conn->channel_id 获取通道） */
    channel_destroy(ch_id);

    /* 由于 channel_destroy 断开了连接，发送应返回 CONN_DISCONNECTED */
    ret = pulse_send(conn_id, 10U, 100, 200);
    TEST_ASSERT_EQ(ret, IPC_ERR_CONN_DISCONNECTED);
}

/**
 * @brief 测试 17: Pulse 队列已满返回错误
 */
static void test_pulse_queue_full(void)
{
    kernel_status_t ret;
    kobj_id_t ch_id;
    kobj_id_t conn_id;
    uint32_t i;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);
    connect_attach(2U, ch_id, &conn_id);

    /* 填满 Pulse 队列 */
    for (i = 0U; i < TEST_MAX_PULSE_QUEUE; i++)
    {
        ret = pulse_send(conn_id, (priority_t)(i + 1U), (int32_t)i, 0);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    /* 再发送应返回队列已满 */
    ret = pulse_send(conn_id, 1U, 999, 0);
    TEST_ASSERT_EQ(ret, IPC_ERR_PULSE_QUEUE_FULL);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 18: 非阻塞接收 Pulse 成功
 */
static void test_pulse_receive_basic(void)
{
    kernel_status_t ret;
    kobj_id_t ch_id;
    kobj_id_t conn_id;
    ipc_pulse_t pulse;
    ipc_channel_t *ch;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);
    connect_attach(2U, ch_id, &conn_id);

    /* 发送一个 Pulse */
    pulse_send(conn_id, 10U, 42, 100);

    /* 接收 */
    ret = pulse_receive(ch_id, &pulse);

    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(pulse.prio, 10U);
    TEST_ASSERT_EQ(pulse.code, 42);
    TEST_ASSERT_EQ(pulse.value, 100);
    TEST_ASSERT_EQ(pulse.conn_id, conn_id);

    /* 队列应变空 */
    ch = get_channel(ch_id);
    TEST_ASSERT_EQ(ch->pulse_count, 0U);
    TEST_ASSERT_TRUE(list_empty(&ch->pulse_queue) == 1);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 19: 空队列接收返回 EAGAIN
 */
static void test_pulse_receive_empty(void)
{
    kernel_status_t ret;
    kobj_id_t ch_id;
    ipc_pulse_t pulse;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);

    /* 无 Pulse 时接收应返回 EAGAIN */
    ret = pulse_receive(ch_id, &pulse);
    TEST_ASSERT_EQ(ret, -(int32_t)EAGAIN);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 20: 接收 NULL 参数
 */
static void test_pulse_receive_null(void)
{
    kernel_status_t ret;
    kobj_id_t ch_id;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);

    /* pulse=NULL */
    ret = pulse_receive(ch_id, NULL);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 无效通道 */
    ipc_pulse_t pulse;
    ret = pulse_receive(KOBJ_ID_INVALID, &pulse);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 21: Pulse 按优先级排序（高优先级先出）
 */
static void test_pulse_priority_order(void)
{
    kobj_id_t ch_id;
    kobj_id_t conn_id;
    ipc_pulse_t pulse;
    kernel_status_t ret;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);
    connect_attach(2U, ch_id, &conn_id);

    /* 按乱序优先级发送 3 个 Pulse */
    pulse_send(conn_id, 10U, 1, 0);   /* 低优先级 */
    pulse_send(conn_id, 200U, 2, 0);  /* 高优先级 */
    pulse_send(conn_id, 50U, 3, 0);   /* 中优先级 */

    /* 应按优先级降序接收 */
    ret = pulse_receive(ch_id, &pulse);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(pulse.prio, 200U);
    TEST_ASSERT_EQ(pulse.code, 2);

    ret = pulse_receive(ch_id, &pulse);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(pulse.prio, 50U);
    TEST_ASSERT_EQ(pulse.code, 3);

    ret = pulse_receive(ch_id, &pulse);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(pulse.prio, 10U);
    TEST_ASSERT_EQ(pulse.code, 1);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 22: 相同优先级 Pulse 保持 FIFO 顺序
 */
static void test_pulse_same_priority_fifo(void)
{
    kobj_id_t ch_id;
    kobj_id_t conn_id;
    ipc_pulse_t pulse;
    kernel_status_t ret;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);
    connect_attach(2U, ch_id, &conn_id);

    /* 发送 3 个相同优先级的 Pulse */
    pulse_send(conn_id, 10U, 1, 0);
    pulse_send(conn_id, 10U, 2, 0);
    pulse_send(conn_id, 10U, 3, 0);

    /* 应按发送顺序接收（FIFO） */
    ret = pulse_receive(ch_id, &pulse);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(pulse.code, 1);

    ret = pulse_receive(ch_id, &pulse);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(pulse.code, 2);

    ret = pulse_receive(ch_id, &pulse);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(pulse.code, 3);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 23: 多个连接发送 Pulse 到同一通道
 */
static void test_pulse_from_multiple_connections(void)
{
    kobj_id_t ch_id;
    kobj_id_t conn_ids[3];
    ipc_pulse_t pulse;
    kernel_status_t ret;
    uint32_t i;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);

    /* 创建 3 个连接 */
    for (i = 0U; i < 3U; i++)
    {
        connect_attach(i + 10U, ch_id, &conn_ids[i]);
    }

    /* 每个连接发送一个 Pulse（相同优先级） */
    for (i = 0U; i < 3U; i++)
    {
        pulse_send(conn_ids[i], 10U, (int32_t)(i + 1), 0);
    }

    /* 接收 3 个 Pulse，验证 conn_id 不同 */
    for (i = 0U; i < 3U; i++)
    {
        ret = pulse_receive(ch_id, &pulse);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
        TEST_ASSERT_EQ(pulse.code, (int32_t)(i + 1));
        TEST_ASSERT_EQ(pulse.conn_id, conn_ids[i]);
    }

    channel_destroy(ch_id);
}

/**
 * @brief 测试 24: 通道销毁时清空 Pulse 队列
 */
static void test_destroy_clears_pulses(void)
{
    kobj_id_t ch_id;
    kobj_id_t conn_id;
    ipc_channel_t *ch;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);
    connect_attach(2U, ch_id, &conn_id);

    /* 发送 3 个 Pulse */
    pulse_send(conn_id, 10U, 1, 0);
    pulse_send(conn_id, 20U, 2, 0);
    pulse_send(conn_id, 30U, 3, 0);

    ch = get_channel(ch_id);
    TEST_ASSERT_EQ(ch->pulse_count, 3U);

    /* 销毁通道 */
    channel_destroy(ch_id);

    /* Pulse 队列应被清空 */
    ch = &s_channels[(uint32_t)(ch_id & 0xFFFFU)];
    TEST_ASSERT_EQ(ch->pulse_count, 0U);
    TEST_ASSERT_TRUE(list_empty(&ch->pulse_queue) == 1);

    /* Pulse 池应恢复 */
    TEST_ASSERT_EQ(s_pulse_free_count, TEST_PULSE_POOL_SIZE);
}

/**
 * @brief 测试 25: 销毁后操作安全
 */
static void test_destroy_then_ops(void)
{
    kobj_id_t ch_id;
    kobj_id_t conn_id;
    kernel_status_t ret;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);
    connect_attach(2U, ch_id, &conn_id);

    /* 先发一个 Pulse */
    pulse_send(conn_id, 10U, 1, 0);

    /* 销毁通道 */
    channel_destroy(ch_id);

    /* 销毁后接收应失败（通道已 CLOSED → get_channel 返回有效指针但 state!=OPEN） */
    ipc_pulse_t pulse;
    ret = pulse_receive(ch_id, &pulse);
    /* pulse_receive 检查 get_channel 返回非 NULL 后检查 pulse_queue 是否为空
     * 销毁时 pulse_queue 被清空，所以返回 EAGAIN */
    TEST_ASSERT_EQ(ret, -(int32_t)EAGAIN);

    /* 再次销毁应失败 */
    ret = channel_destroy(ch_id);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

/**
 * @brief 测试 26: 多通道多连接并行
 */
static void test_multiple_channels(void)
{
    kobj_id_t ch_ids[4];
    kobj_id_t conn_ids[4];
    ipc_channel_t *ch;
    ipc_connection_t *conn;
    uint32_t i;

    channel_subsys_test_init();

    /* 创建 4 个通道，每个通道一个连接 */
    for (i = 0U; i < 4U; i++)
    {
        channel_create(i + 1U, &ch_ids[i]);
        connect_attach(i + 10U, ch_ids[i], &conn_ids[i]);
    }

    /* 验证各通道和连接独立 */
    for (i = 0U; i < 4U; i++)
    {
        ch = get_channel(ch_ids[i]);
        TEST_ASSERT_NOT_NULL(ch);
        TEST_ASSERT_EQ(ch->state, IPC_CH_OPEN);
        TEST_ASSERT_EQ(ch->owner_tid, i + 1U);

        conn = get_connection(conn_ids[i]);
        TEST_ASSERT_NOT_NULL(conn);
        TEST_ASSERT_EQ(conn->state, IPC_CONN_CONNECTED);
        TEST_ASSERT_EQ(conn->channel_id, ch_ids[i]);
    }

    /* 销毁其中一个通道 */
    channel_destroy(ch_ids[1U]);

    /* 其余通道仍有效 */
    ch = get_channel(ch_ids[0U]);
    TEST_ASSERT_EQ(ch->state, IPC_CH_OPEN);

    ch = get_channel(ch_ids[1U]);
    TEST_ASSERT_EQ(ch->state, IPC_CH_CLOSED);

    /* 清理 */
    channel_destroy(ch_ids[0U]);
    channel_destroy(ch_ids[2U]);
    channel_destroy(ch_ids[3U]);
}

/**
 * @brief 测试 27: 无效 ID 操作安全
 */
static void test_invalid_id_ops(void)
{
    kernel_status_t ret;
    ipc_pulse_t pulse;

    channel_subsys_test_init();

    /* 销毁无效 ID */
    ret = channel_destroy(KOBJ_ID_INVALID);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 接收 Pulse 无效 ID */
    ret = pulse_receive(KOBJ_ID_INVALID, &pulse);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 分离无效 ID */
    ret = connect_detach(KOBJ_ID_INVALID);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 发送 Pulse 无效连接 */
    ret = pulse_send(KOBJ_ID_INVALID, 10U, 1, 0);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

/**
 * @brief 测试 28: 连接耗尽返回 ENOMEM
 */
static void test_connection_exhaust(void)
{
    kobj_id_t ch_id;
    kobj_id_t conn_ids[TEST_MAX_CONNECTIONS];
    kobj_id_t extra;
    kernel_status_t ret;
    uint32_t i;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);

    /* 分配全部连接 */
    for (i = 0U; i < TEST_MAX_CONNECTIONS; i++)
    {
        ret = connect_attach(i + 1U, ch_id, &conn_ids[i]);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    /* 再分配应失败 */
    ret = connect_attach(99U, ch_id, &extra);
    TEST_ASSERT_EQ(ret, -(int32_t)ENOMEM);

    /* 清理 */
    channel_destroy(ch_id);
}

/**
 * @brief 测试 29: 通道 ID 格式验证
 */
static void test_id_format(void)
{
    kobj_id_t ch_id;
    kobj_id_t conn_id;
    uint32_t idx;

    channel_subsys_test_init();

    channel_create(1U, &ch_id);
    idx = (uint32_t)(ch_id & 0xFFFFU);
    TEST_ASSERT_NE(idx, 0U);  /* 索引 0 不应被分配 */
    TEST_ASSERT_EQ((uint32_t)(ch_id >> 16U) & 0xFFFFU, idx);

    connect_attach(2U, ch_id, &conn_id);
    TEST_ASSERT_NE((uint32_t)(conn_id & 0xFFFFU), 0U);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 30: Pulse 完整收发循环
 */
static void test_pulse_send_receive_cycle(void)
{
    kobj_id_t ch_id;
    kobj_id_t conn_id;
    ipc_pulse_t pulse;
    kernel_status_t ret;
    uint32_t i;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);
    connect_attach(2U, ch_id, &conn_id);

    for (i = 0U; i < 100U; i++)
    {
        ret = pulse_send(conn_id, (priority_t)(i % 256U), (int32_t)i, (int32_t)(i * 2));
        TEST_ASSERT_EQ(ret, KERNEL_OK);

        ret = pulse_receive(ch_id, &pulse);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
        TEST_ASSERT_EQ(pulse.code, (int32_t)i);
        TEST_ASSERT_EQ(pulse.value, (int32_t)(i * 2));
    }

    /* Pulse 池应完全恢复 */
    TEST_ASSERT_EQ(s_pulse_free_count, TEST_PULSE_POOL_SIZE);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 31: 压力测试 — 反复创建销毁通道
 */
static void test_stress_channel_create_destroy(void)
{
    kobj_id_t ch_id;
    uint32_t i;

    channel_subsys_test_init();

    for (i = 0U; i < 200U; i++)
    {
        kernel_status_t ret = channel_create(i % 256U, &ch_id);
        TEST_ASSERT_EQ(ret, KERNEL_OK);

        ret = channel_destroy(ch_id);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    TEST_ASSERT_EQ(s_free_ch_count, TEST_MAX_CHANNELS);
}

/**
 * @brief 测试 32: 压力测试 — 反复连接/分离
 */
static void test_stress_connect_detach(void)
{
    kobj_id_t ch_id;
    kobj_id_t conn_id;
    uint32_t i;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);

    for (i = 0U; i < 200U; i++)
    {
        kernel_status_t ret = connect_attach(i % 256U, ch_id, &conn_id);
        TEST_ASSERT_EQ(ret, KERNEL_OK);

        ret = connect_detach(conn_id);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    TEST_ASSERT_EQ(s_free_conn_count, TEST_MAX_CONNECTIONS);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 33: 压力测试 — Pulse 发送/接收交替
 */
static void test_stress_pulse_send_recv(void)
{
    kobj_id_t ch_id;
    kobj_id_t conn_id;
    ipc_pulse_t pulse;
    kernel_status_t ret;
    uint32_t i;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);
    connect_attach(2U, ch_id, &conn_id);

    for (i = 0U; i < 500U; i++)
    {
        ret = pulse_send(conn_id, (priority_t)(i % 256U), (int32_t)i, 0);
        TEST_ASSERT_EQ(ret, KERNEL_OK);

        ret = pulse_receive(ch_id, &pulse);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    TEST_ASSERT_EQ(s_pulse_free_count, TEST_PULSE_POOL_SIZE);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 34: Pulse 队列满后消费再发送
 */
static void test_pulse_queue_full_then_consume(void)
{
    kobj_id_t ch_id;
    kobj_id_t conn_id;
    ipc_pulse_t pulse;
    kernel_status_t ret;
    uint32_t i;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);
    connect_attach(2U, ch_id, &conn_id);

    /* 填满 */
    for (i = 0U; i < TEST_MAX_PULSE_QUEUE; i++)
    {
        pulse_send(conn_id, (priority_t)(i + 1U), (int32_t)i, 0);
    }

    /* 队列满 */
    ret = pulse_send(conn_id, 1U, 999, 0);
    TEST_ASSERT_EQ(ret, IPC_ERR_PULSE_QUEUE_FULL);

    /* 消费一个 */
    ret = pulse_receive(ch_id, &pulse);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    /* 最高优先级应该是最高的 */
    TEST_ASSERT_EQ(pulse.prio, TEST_MAX_PULSE_QUEUE);

    /* 再发送应成功 */
    ret = pulse_send(conn_id, 1U, 999, 0);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    channel_destroy(ch_id);
}

/**
 * @brief 测试 35: 多连接多优先级混合 Pulse
 */
static void test_pulse_multi_conn_mixed_prio(void)
{
    kobj_id_t ch_id;
    kobj_id_t conn_ids[3];
    ipc_pulse_t pulse;
    kernel_status_t ret;
    uint32_t i;

    channel_subsys_test_init();
    channel_create(1U, &ch_id);

    for (i = 0U; i < 3U; i++)
    {
        connect_attach(i + 10U, ch_id, &conn_ids[i]);
    }

    /* 从不同连接发送不同优先级的 Pulse */
    pulse_send(conn_ids[0U], 100U, 1, 0);  /* 高 */
    pulse_send(conn_ids[1U], 10U,  2, 0);  /* 低 */
    pulse_send(conn_ids[2U], 50U,  3, 0);  /* 中 */

    /* 应按优先级顺序接收 */
    ret = pulse_receive(ch_id, &pulse);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(pulse.code, 1);
    TEST_ASSERT_EQ(pulse.conn_id, conn_ids[0U]);

    ret = pulse_receive(ch_id, &pulse);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(pulse.code, 3);
    TEST_ASSERT_EQ(pulse.conn_id, conn_ids[2U]);

    ret = pulse_receive(ch_id, &pulse);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(pulse.code, 2);
    TEST_ASSERT_EQ(pulse.conn_id, conn_ids[1U]);

    channel_destroy(ch_id);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("=== IPC 通道/连接/Pulse 测试 ===\n\n");

    test_subsys_init();
    test_channel_create_basic();
    test_channel_create_destroy();
    test_channel_create_reuse();
    test_channel_exhaust();
    test_null_param();
    test_connect_attach_basic();
    test_connect_invalid_channel();
    test_connect_closed_channel();
    test_connect_detach_basic();
    test_detach_disconnected();
    test_destroy_disconnects_all();
    test_pulse_send_basic();
    test_pulse_send_invalid_conn();
    test_pulse_send_disconnected();
    test_pulse_send_closed_channel();
    test_pulse_queue_full();
    test_pulse_receive_basic();
    test_pulse_receive_empty();
    test_pulse_receive_null();
    test_pulse_priority_order();
    test_pulse_same_priority_fifo();
    test_pulse_from_multiple_connections();
    test_destroy_clears_pulses();
    test_destroy_then_ops();
    test_multiple_channels();
    test_invalid_id_ops();
    test_connection_exhaust();
    test_id_format();
    test_pulse_send_receive_cycle();
    test_stress_channel_create_destroy();
    test_stress_connect_detach();
    test_stress_pulse_send_recv();
    test_pulse_queue_full_then_consume();
    test_pulse_multi_conn_mixed_prio();

    TEST_SUMMARY("test_channel");

    return TEST_RESULT();
}
