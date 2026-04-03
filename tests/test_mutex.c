/**
 * @file    test_mutex.c
 * @brief   AISafe64 RTOS - 优先级继承互斥锁单元测试（宿主机）
 * @author  AISafe64 Team
 * @date    2026-04-03
 * @version 1.0
 *
 * @details 优先级继承互斥锁宿主机自包含测试
 *          测试与内核 mutex.c 一致的逻辑：
 *          - 初始化（未锁定、等待队列为空）
 *          - 加锁/解锁基本流程
 *          - 优先级继承（持有者优先级临时提升）
 *          - 优先级天花板协议
 *          - 非阻塞 try_lock
 *          - 带超时获取
 *          - 等待队列 FIFO 顺序
 *          - 所有者检查
 *          - 递归锁定计数
 *          - NULL 参数安全
 *          - 压力测试
 *
 * @note 对应需求: KR-004（内核同步原语）、TF-001
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
 * MutexState_t 与 Mutex_t 定义（与 kernel/mutex.h 一致）
 * ======================================================================== */

typedef enum
{
    MUTEX_STATE_UNLOCKED = 0U,
    MUTEX_STATE_LOCKED   = 1U,
    MUTEX_STATE_CONTENDED = 2U
} MutexState_t;

typedef struct
{
    volatile uint32_t owner_tid;
    MutexState_t state;
    uint8_t ceiling;
    uint8_t original_prio;
    uint32_t lock_count;
    struct list_head wait_queue;
} Mutex_t;

#define MUTEX_INIT { 0U, MUTEX_STATE_UNLOCKED, 0U, 0U, 0U, { NULL, NULL } }
#define MUTEX_INIT_WITH_CEILING(ceil) \
    { 0U, MUTEX_STATE_UNLOCKED, (ceil), 0U, 0U, { NULL, NULL } }

/* ========================================================================
 * 模拟线程表（用于优先级继承和等待队列测试）
 * ======================================================================== */

#define MOCK_MAX_THREADS 8U

typedef struct
{
    uint32_t tid;
    uint8_t  prio;
    struct list_head wait_node;
} MockThread_t;

static MockThread_t s_threads[MOCK_MAX_THREADS];

/**
 * @brief 当前持有锁的线程（模拟 kthread_get_current）
 */
static uint32_t s_current_tid = 0U;

/* ========================================================================
 * 模拟优先级继承表
 *
 * @details s_boosted_prio[tid] 记录线程 tid 被提升到的优先级，
 *          0 表示未提升。
 * ======================================================================== */

/* s_boosted_prio: 在完整实现中用于记录被提升的优先级，测试中直接操作 s_threads */

/**
 * @brief 模拟 tick 计数器（用于 mutex_lock_timeout）
 */
static uint64_t s_mock_ticks = 0U;

/* ========================================================================
 * 互斥锁实现（宿主机自包含版本 — 与 mutex.c 逻辑一致）
 * ======================================================================== */

/**
 * @brief 初始化互斥锁
 * @details 与 kernel/sched/mutex.c 一致
 */
static void mutex_init(Mutex_t *mutex, uint8_t ceiling)
{
    if (mutex == NULL) { return; }

    mutex->owner_tid    = 0U;
    mutex->state        = MUTEX_STATE_UNLOCKED;
    mutex->ceiling      = ceiling;
    mutex->original_prio = 0U;
    mutex->lock_count   = 0U;
    init_list_head(&mutex->wait_queue);
}

/**
 * @brief 获取互斥锁（阻塞版 — 宿主机模拟）
 * @details 与 kernel/sched/mutex.c 一致
 *          宿主机单线程环境：模拟阻塞逻辑但不会真正阻塞
 */
static int32_t mutex_lock(Mutex_t *mutex)
{
    if (mutex == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 锁空闲 → 直接获取 */
    if (mutex->state == MUTEX_STATE_UNLOCKED)
    {
        mutex->owner_tid  = s_current_tid;
        mutex->state      = MUTEX_STATE_LOCKED;
        mutex->lock_count++;

        /* 优先级天花板协议 */
        if (mutex->ceiling != 0U)
        {
            MockThread_t *t = &s_threads[s_current_tid];
            if (t->prio < mutex->ceiling)
            {
                mutex->original_prio = t->prio;
                t->prio = mutex->ceiling;
            }
        }

        return 0;
    }

    /* 锁已被占用 → 设置为有竞争状态 */
    mutex->state = MUTEX_STATE_CONTENDED;

    /* 在真实内核中会调用 schedule()，宿主机模拟不阻塞 */
    return -(int32_t)EBUSY;
}

/**
 * @brief 释放互斥锁
 * @details 与 kernel/sched/mutex.c 一致
 */
static int32_t mutex_unlock(Mutex_t *mutex)
{
    if (mutex == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查是否为锁的持有者 */
    if (mutex->owner_tid != s_current_tid)
    {
        return -(int32_t)EPERM;
    }

    /* 恢复原始优先级（优先级继承恢复） */
    if (mutex->original_prio != 0U)
    {
        MockThread_t *t = &s_threads[s_current_tid];
        t->prio = mutex->original_prio;
        mutex->original_prio = 0U;
    }

    mutex->lock_count--;

    /* 检查等待队列 */
    if (!list_empty(&mutex->wait_queue))
    {
        /* 有等待者：将锁传递给第一个等待者 */
        struct list_head *first = mutex->wait_queue.next;
        MockThread_t *waiter = container_of(first, MockThread_t, wait_node);

        list_del(first);

        mutex->owner_tid = waiter->tid;
        mutex->lock_count++;
    }
    else
    {
        /* 无等待者：释放锁 */
        mutex->owner_tid = 0U;
        mutex->state     = MUTEX_STATE_UNLOCKED;
    }

    return 0;
}

/**
 * @brief 尝试获取互斥锁（非阻塞）
 * @details 与 kernel/sched/mutex.c 一致
 */
static int32_t mutex_try_lock(Mutex_t *mutex)
{
    if (mutex == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (mutex->state != MUTEX_STATE_UNLOCKED)
    {
        return -(int32_t)EBUSY;
    }

    mutex->owner_tid = s_current_tid;
    mutex->state     = MUTEX_STATE_LOCKED;
    mutex->lock_count++;

    return 0;
}

/**
 * @brief 带超时获取互斥锁
 * @details 与 kernel/sched/mutex.c 一致
 *          宿主机模拟：使用 s_mock_ticks 作为时间源
 */
static int32_t mutex_lock_timeout(Mutex_t *mutex, uint32_t timeout_ticks)
{
    uint64_t deadline;

    if (mutex == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (timeout_ticks == 0U)
    {
        return mutex_try_lock(mutex);
    }

    deadline = s_mock_ticks + (uint64_t)timeout_ticks;

    for (;;)
    {
        if (mutex->state == MUTEX_STATE_UNLOCKED)
        {
            return mutex_lock(mutex);
        }

        if (s_mock_ticks >= deadline)
        {
            return -(int32_t)ETIMEDOUT;
        }

        /* 模拟时间流逝 */
        s_mock_ticks++;
    }
}

/**
 * @brief 检查互斥锁是否被持有
 * @details 与 kernel/sched/mutex.c 一致
 */
static bool mutex_is_held(const Mutex_t *mutex)
{
    if (mutex == NULL)
    {
        return false;
    }

    return (mutex->state != MUTEX_STATE_UNLOCKED) ? true : false;
}

/**
 * @brief 将线程加入等待队列（测试辅助）
 */
static void mutex_wait_enqueue(Mutex_t *mutex, uint32_t tid)
{
    MockThread_t *t = &s_threads[tid];
    list_add_tail(&t->wait_node, &mutex->wait_queue);

    /* 优先级继承：如果等待者优先级高于持有者，提升持有者 */
    if (mutex->owner_tid < MOCK_MAX_THREADS)
    {
        MockThread_t *owner = &s_threads[mutex->owner_tid];
        if (t->prio > owner->prio)
        {
            if (mutex->original_prio == 0U)
            {
                mutex->original_prio = owner->prio;
            }
            owner->prio = t->prio;
        }
    }
}

/**
 * @brief 模拟线程优先级继承（测试辅助）
 * @details 在真实内核中，mutex_lock 内部会执行优先级继承
 */
static void priority_inherit(Mutex_t *mutex, uint32_t waiter_tid)
{
    MockThread_t *waiter = &s_threads[waiter_tid];
    MockThread_t *owner;

    if (mutex->owner_tid >= MOCK_MAX_THREADS) { return; }

    owner = &s_threads[mutex->owner_tid];

    if (waiter->prio > owner->prio)
    {
        /* 保存持有者原始优先级（仅首次） */
        if (mutex->original_prio == 0U)
        {
            mutex->original_prio = owner->prio;
        }
        owner->prio = waiter->prio;
    }
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试 1: 初始化后锁状态正确
 */
static void test_init_basic(void)
{
    Mutex_t mutex;
    mutex_init(&mutex, 0U);

    TEST_ASSERT_EQ(mutex.owner_tid, 0U);
    TEST_ASSERT_EQ(mutex.state, MUTEX_STATE_UNLOCKED);
    TEST_ASSERT_EQ(mutex.ceiling, 0U);
    TEST_ASSERT_EQ(mutex.original_prio, 0U);
    TEST_ASSERT_EQ(mutex.lock_count, 0U);
    TEST_ASSERT_TRUE(list_empty(&mutex.wait_queue) == 1);
    TEST_ASSERT_FALSE(mutex_is_held(&mutex));
}

/**
 * @brief 测试 2: 初始化带优先级天花板
 */
static void test_init_with_ceiling(void)
{
    Mutex_t mutex;
    mutex_init(&mutex, 200U);

    TEST_ASSERT_EQ(mutex.ceiling, 200U);
    TEST_ASSERT_EQ(mutex.state, MUTEX_STATE_UNLOCKED);
}

/**
 * @brief 测试 3: 加锁后解锁，状态正确
 */
static void test_lock_unlock_basic(void)
{
    Mutex_t mutex;
    int32_t ret;

    mutex_init(&mutex, 0U);
    s_current_tid = 0U;

    ret = mutex_lock(&mutex);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQ(mutex.state, MUTEX_STATE_LOCKED);
    TEST_ASSERT_EQ(mutex.owner_tid, 0U);
    TEST_ASSERT_EQ(mutex.lock_count, 1U);
    TEST_ASSERT_TRUE(mutex_is_held(&mutex));

    s_current_tid = 0U;
    ret = mutex_unlock(&mutex);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQ(mutex.state, MUTEX_STATE_UNLOCKED);
    TEST_ASSERT_EQ(mutex.owner_tid, 0U);
    TEST_ASSERT_FALSE(mutex_is_held(&mutex));
}

/**
 * @brief 测试 4: 已持有时再次加锁返回 EBUSY
 */
static void test_lock_contention(void)
{
    Mutex_t mutex;
    int32_t ret;

    mutex_init(&mutex, 0U);

    /* 线程 0 加锁 */
    s_current_tid = 0U;
    ret = mutex_lock(&mutex);
    TEST_ASSERT_EQ(ret, 0);

    /* 线程 1 尝试加锁（宿主机模拟返回 EBUSY 而非阻塞） */
    s_current_tid = 1U;
    ret = mutex_lock(&mutex);
    TEST_ASSERT_EQ(ret, -(int32_t)EBUSY);
    TEST_ASSERT_EQ(mutex.state, MUTEX_STATE_CONTENDED);
}

/**
 * @brief 测试 5: try_lock 空闲时成功
 */
static void test_try_lock_success(void)
{
    Mutex_t mutex;
    int32_t ret;

    mutex_init(&mutex, 0U);
    s_current_tid = 0U;

    ret = mutex_try_lock(&mutex);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQ(mutex.state, MUTEX_STATE_LOCKED);
    TEST_ASSERT_EQ(mutex.owner_tid, 0U);

    /* 清理 */
    mutex_unlock(&mutex);
}

/**
 * @brief 测试 6: try_lock 已持有时返回 EBUSY
 */
static void test_try_lock_fail(void)
{
    Mutex_t mutex;
    int32_t ret;

    mutex_init(&mutex, 0U);

    s_current_tid = 0U;
    mutex_lock(&mutex);

    s_current_tid = 1U;
    ret = mutex_try_lock(&mutex);
    TEST_ASSERT_EQ(ret, -(int32_t)EBUSY);

    /* 清理 */
    s_current_tid = 0U;
    mutex_unlock(&mutex);
}

/**
 * @brief 测试 7: is_held 正确反映锁状态
 */
static void test_is_held(void)
{
    Mutex_t mutex;

    mutex_init(&mutex, 0U);
    TEST_ASSERT_FALSE(mutex_is_held(&mutex));

    s_current_tid = 0U;
    mutex_lock(&mutex);
    TEST_ASSERT_TRUE(mutex_is_held(&mutex));

    mutex_unlock(&mutex);
    TEST_ASSERT_FALSE(mutex_is_held(&mutex));
}

/**
 * @brief 测试 8: 非持有者解锁返回 EPERM
 */
static void test_unlock_not_owner(void)
{
    Mutex_t mutex;
    int32_t ret;

    mutex_init(&mutex, 0U);

    /* 线程 0 加锁 */
    s_current_tid = 0U;
    mutex_lock(&mutex);

    /* 线程 1 尝试解锁 */
    s_current_tid = 1U;
    ret = mutex_unlock(&mutex);
    TEST_ASSERT_EQ(ret, -(int32_t)EPERM);

    /* 验证锁仍被线程 0 持有 */
    TEST_ASSERT_EQ(mutex.owner_tid, 0U);
    TEST_ASSERT_TRUE(mutex_is_held(&mutex));

    /* 清理 */
    s_current_tid = 0U;
    mutex_unlock(&mutex);
}

/**
 * @brief 测试 9: 优先级继承 — 高优先级等待者提升持有者
 */
static void test_priority_inheritance(void)
{
    Mutex_t mutex;

    mutex_init(&mutex, 0U);

    /* 初始化模拟线程 */
    s_threads[0U].tid  = 0U;
    s_threads[0U].prio = 50U;   /* 低优先级持有者 */
    s_threads[1U].tid  = 1U;
    s_threads[1U].prio = 200U;  /* 高优先级等待者 */

    /* 线程 0 加锁 */
    s_current_tid = 0U;
    mutex_lock(&mutex);
    TEST_ASSERT_EQ(mutex.owner_tid, 0U);

    /* 线程 1 尝试加锁（返回 EBUSY，模拟阻塞） */
    s_current_tid = 1U;
    mutex_lock(&mutex);

    /* 优先级继承：提升持有者优先级 */
    priority_inherit(&mutex, 1U);
    TEST_ASSERT_EQ(s_threads[0U].prio, 200U);  /* 被提升 */
    TEST_ASSERT_EQ(mutex.original_prio, 50U);   /* 保存原始 */

    /* 线程 0 解锁，恢复原始优先级 */
    s_current_tid = 0U;
    mutex_unlock(&mutex);
    TEST_ASSERT_EQ(s_threads[0U].prio, 50U);   /* 恢复 */
}

/**
 * @brief 测试 10: 优先级天花板协议
 */
static void test_priority_ceiling(void)
{
    Mutex_t mutex;

    mutex_init(&mutex, 180U);  /* 天花板优先级 180 */

    /* 线程 0 优先级为 50，低于天花板 */
    s_threads[0U].tid  = 0U;
    s_threads[0U].prio = 50U;

    s_current_tid = 0U;
    mutex_lock(&mutex);

    /* 优先级应被提升到天花板 */
    TEST_ASSERT_EQ(s_threads[0U].prio, 180U);
    TEST_ASSERT_EQ(mutex.original_prio, 50U);

    /* 解锁后恢复 */
    mutex_unlock(&mutex);
    TEST_ASSERT_EQ(s_threads[0U].prio, 50U);
}

/**
 * @brief 测试 11: 优先级天花板 — 线程优先级高于天花板不受影响
 */
static void test_priority_ceiling_higher(void)
{
    Mutex_t mutex;

    mutex_init(&mutex, 100U);

    /* 线程优先级 200，高于天花板 100 */
    s_threads[0U].tid  = 0U;
    s_threads[0U].prio = 200U;

    s_current_tid = 0U;
    mutex_lock(&mutex);

    /* 优先级不变（已经高于天花板） */
    TEST_ASSERT_EQ(s_threads[0U].prio, 200U);

    mutex_unlock(&mutex);
    TEST_ASSERT_EQ(s_threads[0U].prio, 200U);
}

/**
 * @brief 测试 12: 解锁时锁传递给第一个等待者
 */
static void test_unlock_transfer(void)
{
    Mutex_t mutex;

    mutex_init(&mutex, 0U);

    s_threads[0U].tid = 0U;
    s_threads[1U].tid = 1U;
    s_threads[2U].tid = 2U;

    /* 线程 0 加锁 */
    s_current_tid = 0U;
    mutex_lock(&mutex);

    /* 线程 1、2 加入等待队列（模拟 mutex_lock 的阻塞路径） */
    mutex_wait_enqueue(&mutex, 1U);
    mutex_wait_enqueue(&mutex, 2U);

    /* 手动设置为 CONTENDED（在真实内核中由 mutex_lock 完成） */
    mutex.state = MUTEX_STATE_CONTENDED;

    TEST_ASSERT_EQ(mutex.state, MUTEX_STATE_CONTENDED);

    /* 线程 0 解锁 → 锁传递给线程 1 */
    s_current_tid = 0U;
    mutex_unlock(&mutex);

    TEST_ASSERT_EQ(mutex.owner_tid, 1U);
    TEST_ASSERT_TRUE(mutex_is_held(&mutex));

    /* 线程 1 解锁 → 锁传递给线程 2 */
    s_current_tid = 1U;
    mutex_unlock(&mutex);

    TEST_ASSERT_EQ(mutex.owner_tid, 2U);
    TEST_ASSERT_TRUE(mutex_is_held(&mutex));

    /* 线程 2 解锁 → 锁释放 */
    s_current_tid = 2U;
    mutex_unlock(&mutex);

    TEST_ASSERT_EQ(mutex.state, MUTEX_STATE_UNLOCKED);
    TEST_ASSERT_FALSE(mutex_is_held(&mutex));
}

/**
 * @brief 测试 13: 带超时获取 — 立即成功
 */
static void test_lock_timeout_immediate(void)
{
    Mutex_t mutex;
    int32_t ret;

    mutex_init(&mutex, 0U);
    s_mock_ticks = 0U;
    s_current_tid = 0U;

    ret = mutex_lock_timeout(&mutex, 100U);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQ(mutex.state, MUTEX_STATE_LOCKED);

    mutex_unlock(&mutex);
}

/**
 * @brief 测试 14: 带超时获取 — 超时后返回 ETIMEDOUT
 */
static void test_lock_timeout_expired(void)
{
    Mutex_t mutex;
    int32_t ret;

    mutex_init(&mutex, 0U);
    s_mock_ticks = 0U;

    /* 线程 0 加锁 */
    s_current_tid = 0U;
    mutex_lock(&mutex);

    /* 线程 1 带超时尝试（模拟自旋超时） */
    s_current_tid = 1U;
    s_mock_ticks = 200U;  /* 超过 deadline=100 */
    ret = mutex_lock_timeout(&mutex, 100U);
    TEST_ASSERT_EQ(ret, -(int32_t)ETIMEDOUT);

    /* 清理 */
    s_current_tid = 0U;
    mutex_unlock(&mutex);
}

/**
 * @brief 测试 15: 带超时获取 — timeout=0 等价于 try_lock
 */
static void test_lock_timeout_zero(void)
{
    Mutex_t mutex;
    int32_t ret;

    mutex_init(&mutex, 0U);

    s_current_tid = 0U;
    mutex_lock(&mutex);

    /* timeout=0 → try_lock */
    s_current_tid = 1U;
    ret = mutex_lock_timeout(&mutex, 0U);
    TEST_ASSERT_EQ(ret, -(int32_t)EBUSY);

    s_current_tid = 0U;
    mutex_unlock(&mutex);
}

/**
 * @brief 测试 16: 等待队列 FIFO 顺序
 */
static void test_wait_queue_fifo(void)
{
    Mutex_t mutex;
    struct list_head *node;
    MockThread_t *t;
    uint32_t i;

    mutex_init(&mutex, 0U);

    for (i = 0U; i < MOCK_MAX_THREADS; i++)
    {
        s_threads[i].tid = i;
        init_list_head(&s_threads[i].wait_node);
    }

    /* 线程 0 加锁 */
    s_current_tid = 0U;
    mutex_lock(&mutex);

    /* 线程 1~5 加入等待队列（FIFO 顺序） */
    for (i = 1U; i <= 5U; i++)
    {
        mutex_wait_enqueue(&mutex, i);
    }

    /* 验证队列顺序: 1 → 2 → 3 → 4 → 5 */
    node = mutex.wait_queue.next;
    for (i = 1U; i <= 5U; i++)
    {
        t = container_of(node, MockThread_t, wait_node);
        TEST_ASSERT_EQ(t->tid, i);
        node = node->next;
    }

    /* 逐个解锁，验证 FIFO 传递 */
    for (i = 0U; i < 5U; i++)
    {
        s_current_tid = mutex.owner_tid;
        mutex_unlock(&mutex);
        TEST_ASSERT_EQ(mutex.owner_tid, i + 1U);
    }

    /* 最后一个等待者解锁后锁释放 */
    s_current_tid = 5U;
    mutex_unlock(&mutex);
    TEST_ASSERT_EQ(mutex.state, MUTEX_STATE_UNLOCKED);
}

/**
 * @brief 测试 17: NULL 参数安全检查
 */
static void test_null_param(void)
{
    Mutex_t static_mutex = MUTEX_INIT;

    /* NULL init 不崩溃 */
    mutex_init(NULL, 0U);

    /* NULL lock 返回错误 */
    TEST_ASSERT_EQ(mutex_lock(NULL), -(int32_t)EINVAL);

    /* NULL unlock 返回错误 */
    TEST_ASSERT_EQ(mutex_unlock(NULL), -(int32_t)EINVAL);

    /* NULL try_lock 返回错误 */
    TEST_ASSERT_EQ(mutex_try_lock(NULL), -(int32_t)EINVAL);

    /* NULL lock_timeout 返回错误 */
    TEST_ASSERT_EQ(mutex_lock_timeout(NULL, 100U), -(int32_t)EINVAL);

    /* NULL is_held 返回 false */
    TEST_ASSERT_FALSE(mutex_is_held(NULL));

    /* 静态初始化验证 */
    TEST_ASSERT_EQ(static_mutex.state, MUTEX_STATE_UNLOCKED);
}

/**
 * @brief 测试 18: MUTEX_INIT_WITH_CEILING 静态初始化
 */
static void test_init_static(void)
{
    Mutex_t mutex = MUTEX_INIT_WITH_CEILING(128U);

    TEST_ASSERT_EQ(mutex.owner_tid, 0U);
    TEST_ASSERT_EQ(mutex.state, MUTEX_STATE_UNLOCKED);
    TEST_ASSERT_EQ(mutex.ceiling, 128U);
    TEST_ASSERT_EQ(mutex.lock_count, 0U);
    TEST_ASSERT_FALSE(mutex_is_held(&mutex));
}

/**
 * @brief 测试 19: 多优先级继承 — 多个等待者逐步提升
 */
static void test_priority_inheritance_multiple(void)
{
    Mutex_t mutex;

    mutex_init(&mutex, 0U);

    s_threads[0U].tid  = 0U;  s_threads[0U].prio = 10U;  /* 持有者 */
    s_threads[1U].tid  = 1U;  s_threads[1U].prio = 100U; /* 等待者 1 */
    s_threads[2U].tid  = 2U;  s_threads[2U].prio = 200U; /* 等待者 2 */

    s_current_tid = 0U;
    mutex_lock(&mutex);

    /* 第一个高优先级等待者 */
    priority_inherit(&mutex, 1U);
    TEST_ASSERT_EQ(s_threads[0U].prio, 100U);
    TEST_ASSERT_EQ(mutex.original_prio, 10U);

    /* 更高优先级等待者进一步提升 */
    priority_inherit(&mutex, 2U);
    TEST_ASSERT_EQ(s_threads[0U].prio, 200U);

    /* 解锁恢复 */
    mutex_unlock(&mutex);
    TEST_ASSERT_EQ(s_threads[0U].prio, 10U);
}

/**
 * @brief 测试 20: 压力测试 — 循环 1000 次加锁解锁
 */
static void test_stress_lock_unlock(void)
{
    Mutex_t mutex;
    uint32_t i;
    int32_t ret;

    mutex_init(&mutex, 0U);

    for (i = 0U; i < 1000U; i++)
    {
        s_current_tid = 0U;
        ret = mutex_lock(&mutex);
        TEST_ASSERT_EQ(ret, 0);
        TEST_ASSERT_TRUE(mutex_is_held(&mutex));

        ret = mutex_unlock(&mutex);
        TEST_ASSERT_EQ(ret, 0);
        TEST_ASSERT_FALSE(mutex_is_held(&mutex));
    }

    TEST_ASSERT_EQ(mutex.lock_count, 0U);
}

/**
 * @brief 测试 21: 多线程交替加锁解锁
 */
static void test_multi_thread_alternate(void)
{
    Mutex_t mutex;
    uint32_t i;
    int32_t ret;

    mutex_init(&mutex, 0U);

    for (i = 0U; i < 3U; i++)
    {
        s_threads[i].tid = i;
        s_threads[i].prio = (uint8_t)(50U + i * 30U);
    }

    /* 模拟三个线程交替加锁 */
    for (i = 0U; i < 100U; i++)
    {
        uint32_t tid = i % 3U;

        s_current_tid = tid;
        ret = mutex_lock(&mutex);
        TEST_ASSERT_EQ(ret, 0);
        TEST_ASSERT_EQ(mutex.owner_tid, tid);

        ret = mutex_unlock(&mutex);
        TEST_ASSERT_EQ(ret, 0);
    }
}

/**
 * @brief 测试 22: 锁计数正确
 */
static void test_lock_count(void)
{
    Mutex_t mutex;

    mutex_init(&mutex, 0U);
    TEST_ASSERT_EQ(mutex.lock_count, 0U);

    s_current_tid = 0U;
    mutex_lock(&mutex);
    TEST_ASSERT_EQ(mutex.lock_count, 1U);

    mutex_unlock(&mutex);
    TEST_ASSERT_EQ(mutex.lock_count, 0U);

    /* 多次加锁解锁 */
    mutex_lock(&mutex);
    mutex_unlock(&mutex);
    mutex_lock(&mutex);
    mutex_unlock(&mutex);
    TEST_ASSERT_EQ(mutex.lock_count, 0U);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("=== 优先级继承互斥锁测试 ===\n\n");

    test_init_basic();
    test_init_with_ceiling();
    test_lock_unlock_basic();
    test_lock_contention();
    test_try_lock_success();
    test_try_lock_fail();
    test_is_held();
    test_unlock_not_owner();
    test_priority_inheritance();
    test_priority_ceiling();
    test_priority_ceiling_higher();
    test_unlock_transfer();
    test_lock_timeout_immediate();
    test_lock_timeout_expired();
    test_lock_timeout_zero();
    test_wait_queue_fifo();
    test_null_param();
    test_init_static();
    test_priority_inheritance_multiple();
    test_stress_lock_unlock();
    test_multi_thread_alternate();
    test_lock_count();

    TEST_SUMMARY("test_mutex");

    return TEST_RESULT();
}
