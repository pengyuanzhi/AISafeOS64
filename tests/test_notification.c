/**
 * @file    test_notification.c
 * @brief   AISafe64 RTOS - IPC 异步通知单元测试（宿主机）
 * @author  AISafe64 Team
 * @date    2026-04-03
 * @version 1.0
 *
 * @details IPC 通知宿主机自包含测试
 *          测试与内核 notification.c 一致的逻辑：
 *          - 子系统初始化（通知池、空闲栈）
 *          - 通知创建/销毁
 *          - 信号触发（位掩码 OR）
 *          - 非阻塞 try_wait（有待处理/无信号）
 *          - 等待掩码匹配
 *          - 多位信号组合
 *          - 销毁后操作安全
 *          - 通知耗尽
 *          - NULL 参数安全
 *          - 信号清除
 *          - 压力测试
 *
 * @note 宿主机单线程模拟：不测试 schedule() 上下文切换，
 *       专注测试状态机转换、位掩码操作和锁正确性
 * @note 对应需求: KR-006（异步通知）、TF-001
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

static inline void list_del(struct list_head *entry)
{
    if (entry == NULL) { return; }

    entry->prev->next = entry->next;
    entry->next->prev = entry->prev;
    entry->next = entry;
    entry->prev = entry;
}

/* ========================================================================
 * 配置常量（与 kernel/config.h 一致）
 * ======================================================================== */

#define TEST_MAX_NOTIFICATIONS  16U

#define KOBJ_ID_INVALID         ((kobj_id_t)0U)
#define THREAD_ID_INVALID       ((thread_id_t)0xFFFFFFFFU)

/* ========================================================================
 * 通知状态与类型（与 kernel/ipc_types.h 一致）
 * ======================================================================== */

typedef enum
{
    IPC_NOTIFY_IDLE = 0U,
    IPC_NOTIFY_PENDING,
    IPC_NOTIFY_WAITING
} ipc_notify_state_t;

typedef struct
{
    kobj_id_t           id;
    ipc_notify_state_t  state;
    uint64_t            signals;
    uint64_t            waited_mask;
    thread_id_t         waiter_tid;
    struct list_head    node;
} ipc_notification_t;

/* ========================================================================
 * 通知子系统模拟实现（与 notification.c 逻辑一致）
 * ======================================================================== */

static ipc_notification_t s_notifications[TEST_MAX_NOTIFICATIONS];
static uint32_t s_free_notify_stack[TEST_MAX_NOTIFICATIONS];
static uint32_t s_free_notify_count;
static TicketLock_t s_notify_subsys_lock;

/**
 * @brief 分配空闲通知索引
 */
static int32_t alloc_notify_index(void)
{
    int32_t idx;

    ticket_lock_acquire(&s_notify_subsys_lock);

    if (s_free_notify_count == 0U)
    {
        ticket_lock_release(&s_notify_subsys_lock);
        return -1;
    }

    s_free_notify_count--;
    idx = (int32_t)s_free_notify_stack[s_free_notify_count];

    ticket_lock_release(&s_notify_subsys_lock);

    return idx;
}

/**
 * @brief 释放通知索引
 */
static void free_notify_index(uint32_t idx)
{
    ticket_lock_acquire(&s_notify_subsys_lock);
    s_free_notify_stack[s_free_notify_count] = idx;
    s_free_notify_count++;
    ticket_lock_release(&s_notify_subsys_lock);
}

/**
 * @brief 通过 ID 获取通知对象指针
 */
static ipc_notification_t *get_notification(kobj_id_t notify_id)
{
    uint32_t idx;

    if (notify_id == KOBJ_ID_INVALID)
    {
        return NULL;
    }

    idx = (uint32_t)(notify_id & 0xFFFFU);
    if (idx >= TEST_MAX_NOTIFICATIONS)
    {
        return NULL;
    }

    return &s_notifications[idx];
}

/**
 * @brief 索引 0 保留（因为 id=0 与 KOBJ_ID_INVALID 冲突）
 */
#define TEST_USABLE_NOTIFICATIONS (TEST_MAX_NOTIFICATIONS - 1U)

/**
 * @brief 初始化通知子系统
 * @details 与 notification.c 的 ipc_notification_subsys_init 逻辑一致
 */
static kernel_status_t notification_subsys_test_init(void)
{
    uint32_t i;

    for (i = 0U; i < TEST_MAX_NOTIFICATIONS; i++)
    {
        s_notifications[i].id = KOBJ_ID_INVALID;
        s_notifications[i].state = IPC_NOTIFY_IDLE;
        s_notifications[i].signals = 0ULL;
        s_notifications[i].waited_mask = 0ULL;
        s_notifications[i].waiter_tid = THREAD_ID_INVALID;
        s_notifications[i].node.next = &s_notifications[i].node;
        s_notifications[i].node.prev = &s_notifications[i].node;
    }

    ticket_lock_init(&s_notify_subsys_lock);

    /* 从大到小入栈，弹出时从小到大（跳过索引 0） */
    for (i = 0U; i < TEST_USABLE_NOTIFICATIONS; i++)
    {
        s_free_notify_stack[i] = TEST_USABLE_NOTIFICATIONS - i;
    }
    s_free_notify_count = TEST_USABLE_NOTIFICATIONS;

    return KERNEL_OK;
}

/**
 * @brief 创建通知对象
 * @details 与 notification.c 的 ipc_notification_create 逻辑一致
 */
static kernel_status_t notification_create(thread_id_t owner_tid,
                                           kobj_id_t *notify_id)
{
    int32_t idx;
    ipc_notification_t *ntf;

    if (notify_id == NULL)
    {
        return -(int32_t)EINVAL;
    }

    (void)owner_tid;

    idx = alloc_notify_index();
    if (idx < 0)
    {
        return -(int32_t)ENOMEM;
    }

    ntf = &s_notifications[(uint32_t)idx];

    ntf->id = (kobj_id_t)((uint32_t)idx | ((uint32_t)idx << 16U));
    ntf->state = IPC_NOTIFY_IDLE;
    ntf->signals = 0ULL;
    ntf->waited_mask = 0ULL;
    ntf->waiter_tid = THREAD_ID_INVALID;

    *notify_id = ntf->id;

    return KERNEL_OK;
}

/**
 * @brief 销毁通知对象
 * @details 与 notification.c 的 ipc_notification_destroy 逻辑一致
 */
static kernel_status_t notification_destroy(kobj_id_t notify_id)
{
    ipc_notification_t *ntf;

    ntf = get_notification(notify_id);
    if (ntf == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 标记为空闲 */
    ntf->id = KOBJ_ID_INVALID;
    ntf->state = IPC_NOTIFY_IDLE;
    ntf->signals = 0ULL;
    ntf->waited_mask = 0ULL;
    ntf->waiter_tid = THREAD_ID_INVALID;

    free_notify_index((uint32_t)(notify_id & 0xFFFFU));

    return KERNEL_OK;
}

/**
 * @brief 触发通知信号
 * @details 与 notification.c 的 ipc_notification_signal 逻辑一致
 */
static kernel_status_t notification_signal(kobj_id_t notify_id,
                                            uint64_t signal)
{
    ipc_notification_t *ntf;

    if (signal == 0ULL)
    {
        return -(int32_t)EINVAL;
    }

    ntf = get_notification(notify_id);
    if (ntf == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 原子地设置信号位 */
    ntf->signals |= signal;

    /* 状态转为 PENDING */
    ntf->state = IPC_NOTIFY_PENDING;

    return KERNEL_OK;
}

/**
 * @brief 非阻塞等待
 * @details 与 notification.c 的 ipc_notification_try_wait 逻辑一致
 */
static kernel_status_t notification_try_wait(kobj_id_t notify_id,
                                              uint64_t waited_mask,
                                              uint64_t *triggered)
{
    ipc_notification_t *ntf;
    uint64_t active;

    if ((waited_mask == 0ULL) || (triggered == NULL))
    {
        return -(int32_t)EINVAL;
    }

    ntf = get_notification(notify_id);
    if (ntf == NULL)
    {
        return -(int32_t)EINVAL;
    }

    active = ntf->signals & waited_mask;
    if (active == 0ULL)
    {
        return -(int32_t)EAGAIN;
    }

    /* 有待处理信号 */
    *triggered = active;
    ntf->signals &= ~active;

    if (ntf->signals == 0ULL)
    {
        ntf->state = IPC_NOTIFY_IDLE;
    }

    return KERNEL_OK;
}

/**
 * @brief 模拟等待（宿主机版）
 * @details 与 notification.c 的 ipc_notification_wait 逻辑一致，
 *          但不阻塞线程，直接返回状态
 */
static kernel_status_t notification_wait_sim(kobj_id_t notify_id,
                                              uint64_t waited_mask,
                                              uint64_t *triggered)
{
    ipc_notification_t *ntf;
    uint64_t active;

    if ((waited_mask == 0ULL) || (triggered == NULL))
    {
        return -(int32_t)EINVAL;
    }

    ntf = get_notification(notify_id);
    if (ntf == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查是否已有待处理的信号 */
    active = ntf->signals & waited_mask;
    if (active != 0ULL)
    {
        /* 有待处理信号：直接返回 */
        *triggered = active;
        ntf->signals &= ~active;
        ntf->state = IPC_NOTIFY_IDLE;
        return KERNEL_OK;
    }

    /* 无待处理信号：在真实内核中会阻塞，
     * 宿主机模拟：设置 WAITING 状态并返回特殊值 */
    ntf->state = IPC_NOTIFY_WAITING;
    ntf->waited_mask = waited_mask;
    ntf->waiter_tid = 1U; /* 模拟线程 ID */

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

    ret = notification_subsys_test_init();

    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(s_free_notify_count, TEST_USABLE_NOTIFICATIONS);

    /* 所有通知对象应为 IDLE */
    TEST_ASSERT_EQ(s_notifications[0U].state, IPC_NOTIFY_IDLE);
    TEST_ASSERT_EQ(s_notifications[TEST_USABLE_NOTIFICATIONS].state, IPC_NOTIFY_IDLE);
}

/**
 * @brief 测试 2: 创建通知成功
 */
static void test_create_basic(void)
{
    kernel_status_t ret;
    kobj_id_t ntf_id = KOBJ_ID_INVALID;
    ipc_notification_t *ntf;

    notification_subsys_test_init();

    ret = notification_create(1U, &ntf_id);

    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_NE(ntf_id, KOBJ_ID_INVALID);

    ntf = get_notification(ntf_id);
    TEST_ASSERT_NOT_NULL(ntf);
    TEST_ASSERT_EQ(ntf->state, IPC_NOTIFY_IDLE);
    TEST_ASSERT_EQ(ntf->signals, 0ULL);

    /* 空闲数减少 */
    TEST_ASSERT_EQ(s_free_notify_count, TEST_USABLE_NOTIFICATIONS - 1U);
}

/**
 * @brief 测试 3: 创建后销毁
 */
static void test_create_destroy(void)
{
    kernel_status_t ret;
    kobj_id_t ntf_id = KOBJ_ID_INVALID;
    ipc_notification_t *ntf;

    notification_subsys_test_init();

    notification_create(1U, &ntf_id);

    ret = notification_destroy(ntf_id);

    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ntf = &s_notifications[(uint32_t)(ntf_id & 0xFFFFU)];
    TEST_ASSERT_EQ(ntf->state, IPC_NOTIFY_IDLE);
    TEST_ASSERT_EQ(ntf->id, KOBJ_ID_INVALID);
    TEST_ASSERT_EQ(ntf->signals, 0ULL);

    /* 空闲数恢复 */
    TEST_ASSERT_EQ(s_free_notify_count, TEST_USABLE_NOTIFICATIONS);
}

/**
 * @brief 测试 4: 销毁后可再次创建
 */
static void test_create_reuse(void)
{
    kobj_id_t id1;
    kobj_id_t id2;

    notification_subsys_test_init();

    notification_create(1U, &id1);
    notification_destroy(id1);
    notification_create(2U, &id2);

    TEST_ASSERT_NE(id2, KOBJ_ID_INVALID);
}

/**
 * @brief 测试 5: 通知耗尽返回 ENOMEM
 */
static void test_create_exhaust(void)
{
    kobj_id_t ids[TEST_USABLE_NOTIFICATIONS];
    kobj_id_t extra;
    kernel_status_t ret;
    uint32_t i;

    notification_subsys_test_init();

    /* 分配全部可用通知 */
    for (i = 0U; i < TEST_USABLE_NOTIFICATIONS; i++)
    {
        ret = notification_create(i + 1U, &ids[i]);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    /* 再次分配应失败 */
    ret = notification_create(99U, &extra);
    TEST_ASSERT_EQ(ret, -(int32_t)ENOMEM);

    /* 清理 */
    for (i = 0U; i < TEST_USABLE_NOTIFICATIONS; i++)
    {
        notification_destroy(ids[i]);
    }
}

/**
 * @brief 测试 6: NULL 参数安全检查
 */
static void test_null_param(void)
{
    kernel_status_t ret;

    notification_subsys_test_init();

    /* NULL notify_id */
    ret = notification_create(1U, NULL);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 无效 ID 销毁 */
    ret = notification_destroy(KOBJ_ID_INVALID);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 越界 ID */
    ret = notification_destroy((kobj_id_t)(TEST_MAX_NOTIFICATIONS + 1U));
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* signal=0 */
    kobj_id_t ntf_id;
    notification_create(1U, &ntf_id);
    ret = notification_signal(ntf_id, 0ULL);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
    notification_destroy(ntf_id);
}

/**
 * @brief 测试 7: 信号触发 — 单位信号
 */
static void test_signal_single_bit(void)
{
    kernel_status_t ret;
    kobj_id_t ntf_id;
    ipc_notification_t *ntf;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);
    ntf = get_notification(ntf_id);

    /* 触发 bit 0 */
    ret = notification_signal(ntf_id, 0x01ULL);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(ntf->signals, 0x01ULL);
    TEST_ASSERT_EQ(ntf->state, IPC_NOTIFY_PENDING);

    notification_destroy(ntf_id);
}

/**
 * @brief 测试 8: 信号触发 — 多位组合
 */
static void test_signal_multi_bit(void)
{
    kobj_id_t ntf_id;
    ipc_notification_t *ntf;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);
    ntf = get_notification(ntf_id);

    /* 先触发 bit 0 */
    notification_signal(ntf_id, 0x01ULL);
    TEST_ASSERT_EQ(ntf->signals, 0x01ULL);

    /* 再触发 bit 2 */
    notification_signal(ntf_id, 0x04ULL);
    TEST_ASSERT_EQ(ntf->signals, 0x05ULL);

    /* 再触发 bit 7 */
    notification_signal(ntf_id, 0x80ULL);
    TEST_ASSERT_EQ(ntf->signals, 0x85ULL);

    notification_destroy(ntf_id);
}

/**
 * @brief 测试 9: 信号触发 — 高位信号（bit 63）
 */
static void test_signal_high_bit(void)
{
    kobj_id_t ntf_id;
    ipc_notification_t *ntf;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);
    ntf = get_notification(ntf_id);

    notification_signal(ntf_id, 0x8000000000000000ULL);
    TEST_ASSERT_EQ(ntf->signals, 0x8000000000000000ULL);

    notification_destroy(ntf_id);
}

/**
 * @brief 测试 10: try_wait — 有待处理信号
 */
static void test_try_wait_success(void)
{
    kobj_id_t ntf_id;
    kernel_status_t ret;
    uint64_t triggered = 0ULL;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);

    /* 触发多个信号 */
    notification_signal(ntf_id, 0x03ULL);  /* bit 0 和 bit 1 */

    /* try_wait 等待 bit 0 */
    ret = notification_try_wait(ntf_id, 0x01ULL, &triggered);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(triggered, 0x01ULL);

    notification_destroy(ntf_id);
}

/**
 * @brief 测试 11: try_wait — 无信号返回 EAGAIN
 */
static void test_try_wait_empty(void)
{
    kobj_id_t ntf_id;
    kernel_status_t ret;
    uint64_t triggered = 0ULL;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);

    /* 未触发任何信号，try_wait 应返回 EAGAIN */
    ret = notification_try_wait(ntf_id, 0xFFULL, &triggered);
    TEST_ASSERT_EQ(ret, -(int32_t)EAGAIN);

    notification_destroy(ntf_id);
}

/**
 * @brief 测试 12: try_wait 掩码过滤
 */
static void test_try_wait_mask_filter(void)
{
    kobj_id_t ntf_id;
    kernel_status_t ret;
    uint64_t triggered = 0ULL;
    ipc_notification_t *ntf;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);
    ntf = get_notification(ntf_id);

    /* 触发 bit 0, 2, 4 */
    notification_signal(ntf_id, 0x15ULL);
    TEST_ASSERT_EQ(ntf->signals, 0x15ULL);

    /* try_wait 只等 bit 0 和 bit 4 */
    ret = notification_try_wait(ntf_id, 0x11ULL, &triggered);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(triggered, 0x11ULL);  /* bit 0 + bit 4 */

    /* bit 2 仍保留 */
    TEST_ASSERT_EQ(ntf->signals, 0x04ULL);

    notification_destroy(ntf_id);
}

/**
 * @brief 测试 13: try_wait 清除后状态恢复 IDLE
 */
static void test_try_wait_clear_state(void)
{
    kobj_id_t ntf_id;
    ipc_notification_t *ntf;
    uint64_t triggered = 0ULL;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);
    ntf = get_notification(ntf_id);

    /* 触发单一信号 */
    notification_signal(ntf_id, 0x01ULL);
    TEST_ASSERT_EQ(ntf->state, IPC_NOTIFY_PENDING);

    /* try_wait 消费后，signals=0 → IDLE */
    notification_try_wait(ntf_id, 0x01ULL, &triggered);
    TEST_ASSERT_EQ(ntf->signals, 0ULL);
    TEST_ASSERT_EQ(ntf->state, IPC_NOTIFY_IDLE);

    notification_destroy(ntf_id);
}

/**
 * @brief 测试 14: try_wait NULL 参数
 */
static void test_try_wait_null(void)
{
    kobj_id_t ntf_id;
    kernel_status_t ret;
    uint64_t triggered;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);

    /* mask=0 */
    ret = notification_try_wait(ntf_id, 0ULL, &triggered);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* triggered=NULL */
    ret = notification_try_wait(ntf_id, 0xFFULL, NULL);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    notification_destroy(ntf_id);
}

/**
 * @brief 测试 15: wait_sim — 有待处理信号直接返回
 */
static void test_wait_has_pending(void)
{
    kobj_id_t ntf_id;
    kernel_status_t ret;
    uint64_t triggered = 0ULL;
    ipc_notification_t *ntf;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);
    ntf = get_notification(ntf_id);

    /* 先触发信号 */
    notification_signal(ntf_id, 0xAAULL);

    /* wait 应直接返回（快速路径） */
    ret = notification_wait_sim(ntf_id, 0xFFULL, &triggered);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(triggered, 0xAAULL);

    /* 信号应被清除 */
    TEST_ASSERT_EQ(ntf->signals, 0ULL);
    TEST_ASSERT_EQ(ntf->state, IPC_NOTIFY_IDLE);

    notification_destroy(ntf_id);
}

/**
 * @brief 测试 16: wait_sim — 无信号进入 WAITING
 */
static void test_wait_no_pending(void)
{
    kobj_id_t ntf_id;
    kernel_status_t ret;
    uint64_t triggered = 0ULL;
    ipc_notification_t *ntf;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);
    ntf = get_notification(ntf_id);

    /* 无信号，wait 应设置 WAITING 状态 */
    ret = notification_wait_sim(ntf_id, 0x01ULL, &triggered);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(ntf->state, IPC_NOTIFY_WAITING);
    TEST_ASSERT_EQ(ntf->waited_mask, 0x01ULL);
    TEST_ASSERT_EQ(ntf->waiter_tid, 1U);

    notification_destroy(ntf_id);
}

/**
 * @brief 测试 17: wait_sim NULL 参数
 */
static void test_wait_null(void)
{
    kobj_id_t ntf_id;
    kernel_status_t ret;
    uint64_t triggered;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);

    /* mask=0 */
    ret = notification_wait_sim(ntf_id, 0ULL, &triggered);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* triggered=NULL */
    ret = notification_wait_sim(ntf_id, 0xFFULL, NULL);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    notification_destroy(ntf_id);
}

/**
 * @brief 测试 18: signal 后 wait 快速路径
 */
static void test_signal_then_wait(void)
{
    kobj_id_t ntf_id;
    kernel_status_t ret;
    uint64_t triggered = 0ULL;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);

    /* 先触发 */
    notification_signal(ntf_id, 0x100ULL);

    /* wait 快速路径 */
    ret = notification_wait_sim(ntf_id, 0x100ULL, &triggered);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(triggered, 0x100ULL);

    notification_destroy(ntf_id);
}

/**
 * @brief 测试 19: 多次 signal 累积
 */
static void test_signal_accumulate(void)
{
    kobj_id_t ntf_id;
    ipc_notification_t *ntf;
    uint64_t triggered = 0ULL;
    kernel_status_t ret;
    uint32_t i;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);
    ntf = get_notification(ntf_id);

    /* 逐个触发 bit 0~7 */
    for (i = 0U; i < 8U; i++)
    {
        notification_signal(ntf_id, 1ULL << i);
    }

    /* 所有 8 位都应置位 */
    TEST_ASSERT_EQ(ntf->signals, 0xFFULL);

    /* try_wait 消费全部 */
    ret = notification_try_wait(ntf_id, 0xFFULL, &triggered);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(triggered, 0xFFULL);
    TEST_ASSERT_EQ(ntf->signals, 0ULL);
    TEST_ASSERT_EQ(ntf->state, IPC_NOTIFY_IDLE);

    notification_destroy(ntf_id);
}

/**
 * @brief 测试 20: 销毁后操作安全
 */
static void test_destroy_then_ops(void)
{
    kobj_id_t ntf_id;
    kernel_status_t ret;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);
    notification_destroy(ntf_id);

    /* 销毁后 signal 应失败 */
    ret = notification_signal(ntf_id, 0x01ULL);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 销毁后 try_wait 应失败 */
    uint64_t triggered;
    ret = notification_try_wait(ntf_id, 0x01ULL, &triggered);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 销毁后再次销毁应失败 */
    ret = notification_destroy(ntf_id);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

/**
 * @brief 测试 21: 多通知对象独立
 */
static void test_multiple_notifications(void)
{
    kobj_id_t ids[4];
    ipc_notification_t *ntf;
    uint32_t i;

    notification_subsys_test_init();

    /* 创建 4 个通知 */
    for (i = 0U; i < 4U; i++)
    {
        notification_create(i + 1U, &ids[i]);
    }

    /* 对每个通知触发不同信号 */
    notification_signal(ids[0U], 0x01ULL);
    notification_signal(ids[1U], 0x02ULL);
    notification_signal(ids[2U], 0x04ULL);
    notification_signal(ids[3U], 0x08ULL);

    /* 验证各自独立 */
    ntf = get_notification(ids[0U]);
    TEST_ASSERT_EQ(ntf->signals, 0x01ULL);

    ntf = get_notification(ids[1U]);
    TEST_ASSERT_EQ(ntf->signals, 0x02ULL);

    ntf = get_notification(ids[2U]);
    TEST_ASSERT_EQ(ntf->signals, 0x04ULL);

    ntf = get_notification(ids[3U]);
    TEST_ASSERT_EQ(ntf->signals, 0x08ULL);

    /* 销毁其中一个 */
    notification_destroy(ids[1U]);

    /* 其余仍有效 */
    ntf = get_notification(ids[0U]);
    TEST_ASSERT_NOT_NULL(ntf);
    TEST_ASSERT_NE(ntf->id, KOBJ_ID_INVALID);

    /* 清理 */
    notification_destroy(ids[0U]);
    notification_destroy(ids[2U]);
    notification_destroy(ids[3U]);
}

/**
 * @brief 测试 22: 无效 ID 操作安全
 */
static void test_invalid_id_ops(void)
{
    kernel_status_t ret;
    uint64_t triggered;

    notification_subsys_test_init();

    /* signal 无效 ID */
    ret = notification_signal(KOBJ_ID_INVALID, 0x01ULL);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* try_wait 无效 ID */
    ret = notification_try_wait(KOBJ_ID_INVALID, 0x01ULL, &triggered);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* wait 无效 ID */
    ret = notification_wait_sim(KOBJ_ID_INVALID, 0x01ULL, &triggered);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

/**
 * @brief 测试 23: try_wait 部分消费
 */
static void test_try_wait_partial(void)
{
    kobj_id_t ntf_id;
    ipc_notification_t *ntf;
    uint64_t triggered = 0ULL;
    kernel_status_t ret;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);
    ntf = get_notification(ntf_id);

    /* 触发 bit 0~3 */
    notification_signal(ntf_id, 0x0FULL);

    /* 消费 bit 0 和 bit 1 */
    ret = notification_try_wait(ntf_id, 0x03ULL, &triggered);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(triggered, 0x03ULL);

    /* bit 2~3 保留 */
    TEST_ASSERT_EQ(ntf->signals, 0x0CULL);
    TEST_ASSERT_EQ(ntf->state, IPC_NOTIFY_PENDING);

    /* 再消费 bit 2 */
    ret = notification_try_wait(ntf_id, 0x04ULL, &triggered);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(triggered, 0x04ULL);

    /* bit 3 保留 */
    TEST_ASSERT_EQ(ntf->signals, 0x08ULL);
    TEST_ASSERT_EQ(ntf->state, IPC_NOTIFY_PENDING);

    /* 消费最后一位 */
    ret = notification_try_wait(ntf_id, 0x08ULL, &triggered);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ(ntf->signals, 0ULL);
    TEST_ASSERT_EQ(ntf->state, IPC_NOTIFY_IDLE);

    notification_destroy(ntf_id);
}

/**
 * @brief 测试 24: 压力测试 — 反复创建销毁
 */
static void test_stress_create_destroy(void)
{
    kobj_id_t ntf_id;
    uint32_t i;

    notification_subsys_test_init();

    for (i = 0U; i < 200U; i++)
    {
        kernel_status_t ret = notification_create(i % 256U, &ntf_id);
        TEST_ASSERT_EQ(ret, KERNEL_OK);

        ret = notification_destroy(ntf_id);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    /* 空闲数应恢复 */
    TEST_ASSERT_EQ(s_free_notify_count, TEST_USABLE_NOTIFICATIONS);
}

/**
 * @brief 测试 25: 压力测试 — 信号触发消费循环
 */
static void test_stress_signal_consume(void)
{
    kobj_id_t ntf_id;
    kernel_status_t ret;
    uint64_t triggered;
    uint32_t i;

    notification_subsys_test_init();
    notification_create(1U, &ntf_id);

    for (i = 0U; i < 500U; i++)
    {
        /* 触发 */
        ret = notification_signal(ntf_id, 0x01ULL);
        TEST_ASSERT_EQ(ret, KERNEL_OK);

        /* 消费 */
        ret = notification_try_wait(ntf_id, 0x01ULL, &triggered);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
        TEST_ASSERT_EQ(triggered, 0x01ULL);
    }

    TEST_ASSERT_EQ(s_notifications[(uint32_t)(ntf_id & 0xFFFFU)].state,
                   IPC_NOTIFY_IDLE);

    notification_destroy(ntf_id);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("=== IPC 异步通知测试 ===\n\n");

    test_subsys_init();
    test_create_basic();
    test_create_destroy();
    test_create_reuse();
    test_create_exhaust();
    test_null_param();
    test_signal_single_bit();
    test_signal_multi_bit();
    test_signal_high_bit();
    test_try_wait_success();
    test_try_wait_empty();
    test_try_wait_mask_filter();
    test_try_wait_clear_state();
    test_try_wait_null();
    test_wait_has_pending();
    test_wait_no_pending();
    test_wait_null();
    test_signal_then_wait();
    test_signal_accumulate();
    test_destroy_then_ops();
    test_multiple_notifications();
    test_invalid_id_ops();
    test_try_wait_partial();
    test_stress_create_destroy();
    test_stress_signal_consume();

    TEST_SUMMARY("test_notification");

    return TEST_RESULT();
}
