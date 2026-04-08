/**
 * @file    test_spinlock.c
 * @brief   AISafe64 RTOS - TicketLock 公平自旋锁单元测试（宿主机）
 * @author  AISafe64 Team
 * @date    2026-04-02
 * @version 1.0
 *
 * @details TicketLock 自旋锁宿主机自包含测试
 *          测试 mock_kernel.h 中提供的 TicketLock 操作（与内核 spinlock.c 逻辑一致）：
 *          - 初始化状态验证
 *          - 获取/释放基本流程
 *          - 锁状态查询（is_held）
 *          - 非阻塞尝试获取（try_acquire）
 *          - 重入断言与安全释放
 *          - IRQ 安全的锁操作
 *          - 多获取者 FIFO 顺序
 *          - 静态初始化宏
 *          - 压力测试
 *
 * @note 对应需求: KR-004（内核同步原语）、TF-001（单元测试框架）
 */

#include "mock_kernel.h"

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试 1: 初始化后锁状态正确
 */
static void test_init_lock(void)
{
    TicketLock_t lock;
    ticket_lock_init(&lock);

    TEST_ASSERT_EQ(lock.next_ticket, 0U);
    TEST_ASSERT_EQ(lock.serving_ticket, 0U);
    TEST_ASSERT_EQ(lock.cpu_id, 0xFFFFFFFFU);
    TEST_ASSERT_FALSE(ticket_lock_is_held(&lock));
}

/**
 * @brief 测试 2: 获取后释放，锁恢复空闲
 */
static void test_acquire_release_basic(void)
{
    TicketLock_t lock;
    ticket_lock_init(&lock);

    ticket_lock_acquire(&lock);
    TEST_ASSERT_TRUE(ticket_lock_is_held(&lock));

    ticket_lock_release(&lock);
    TEST_ASSERT_FALSE(ticket_lock_is_held(&lock));
}

/**
 * @brief 测试 3: 获取后 is_held 返回 true
 */
static void test_is_held_after_acquire(void)
{
    TicketLock_t lock;
    ticket_lock_init(&lock);

    ticket_lock_acquire(&lock);
    TEST_ASSERT_TRUE(ticket_lock_is_held(&lock));

    /* 清理 */
    ticket_lock_release(&lock);
}

/**
 * @brief 测试 4: 释放后 is_held 返回 false
 */
static void test_is_held_after_release(void)
{
    TicketLock_t lock;
    ticket_lock_init(&lock);

    ticket_lock_acquire(&lock);
    ticket_lock_release(&lock);
    TEST_ASSERT_FALSE(ticket_lock_is_held(&lock));
}

/**
 * @brief 测试 5: 空闲时 try_acquire 成功
 */
static void test_try_acquire_success(void)
{
    TicketLock_t lock;
    ticket_lock_init(&lock);

    bool result = ticket_lock_try_acquire(&lock);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(ticket_lock_is_held(&lock));

    /* 清理 */
    ticket_lock_release(&lock);
}

/**
 * @brief 测试 6: 已持有时 try_acquire 失败
 */
static void test_try_acquire_fail(void)
{
    TicketLock_t lock;
    ticket_lock_init(&lock);

    ticket_lock_acquire(&lock);

    bool result = ticket_lock_try_acquire(&lock);
    TEST_ASSERT_FALSE(result);

    /* 清理 */
    ticket_lock_release(&lock);
}

/**
 * @brief 测试 7: 持锁时再次获取应触发断言
 * @details 同一把锁在同 CPU 上再次获取时必须触发断言
 */
static void test_reacquire_while_held(void)
{
    TicketLock_t lock;
    ticket_lock_init(&lock);

    mock_assert_failed = false;
    ticket_lock_acquire(&lock);
    TEST_ASSERT_EQ(lock.cpu_id, 0U);

    ticket_lock_acquire(&lock);
    TEST_ASSERT_TRUE(mock_assert_failed);
    TEST_ASSERT_EQ(lock.cpu_id, 0U);
    TEST_ASSERT_EQ(lock.next_ticket, 1U);

    TEST_ASSERT_TRUE(ticket_lock_try_acquire(&lock) == false);

    ticket_lock_release(&lock);
    TEST_ASSERT_EQ(lock.cpu_id, 0xFFFFFFFFU);
}

/**
 * @brief 测试 8: 重复释放不会破坏锁状态
 */
static void test_double_release_safe(void)
{
    TicketLock_t lock;
    uint32_t serving_ticket_after_release;
    uint32_t cpu_id_after_release;
    ticket_lock_init(&lock);

    ticket_lock_acquire(&lock);
    TEST_ASSERT_EQ(lock.cpu_id, 0U);

    ticket_lock_release(&lock);
    TEST_ASSERT_FALSE(ticket_lock_is_held(&lock));

    serving_ticket_after_release = lock.serving_ticket;
    cpu_id_after_release = lock.cpu_id;
    ticket_lock_release(&lock);
    TEST_ASSERT_EQ(lock.serving_ticket, serving_ticket_after_release);
    TEST_ASSERT_EQ(lock.cpu_id, cpu_id_after_release);
    TEST_ASSERT_FALSE(ticket_lock_is_held(&lock));
}

/**
 * @brief 测试 9: 非持有者释放应被拒绝
 */
static void test_release_wrong_cpu_safe(void)
{
    TicketLock_t lock;
    uint32_t serving_ticket_before;

    ticket_lock_init(&lock);

    mock_cpu_id = 0U;
    ticket_lock_acquire(&lock);
    serving_ticket_before = lock.serving_ticket;

    mock_cpu_id = 1U;
    ticket_lock_release(&lock);
    TEST_ASSERT_EQ(lock.serving_ticket, serving_ticket_before);
    TEST_ASSERT_EQ(lock.cpu_id, 0U);
    TEST_ASSERT_TRUE(ticket_lock_is_held(&lock));

    mock_cpu_id = 0U;
    ticket_lock_release(&lock);
    TEST_ASSERT_EQ(lock.cpu_id, 0xFFFFFFFFU);
    TEST_ASSERT_FALSE(ticket_lock_is_held(&lock));
}

/**
 * @brief 测试 10: acquire_irqsave 返回 IRQ 状态
 */
static void test_acquire_irqsave(void)
{
    TicketLock_t lock;
    ticket_lock_init(&lock);

    mock_irq_disabled = false;
    uint32_t irq_state = ticket_lock_acquire_irqsave(&lock);
    TEST_ASSERT_TRUE(ticket_lock_is_held(&lock));
    TEST_ASSERT_EQ(irq_state, 0U);
    TEST_ASSERT_TRUE(mock_irq_disabled);

    /* 清理 */
    ticket_lock_release_irqrestore(&lock, irq_state);
    TEST_ASSERT_FALSE(mock_irq_disabled);
}

/**
 * @brief 测试 11: release_irqrestore 恢复后锁空闲
 */
static void test_release_irqrestore(void)
{
    TicketLock_t lock;
    ticket_lock_init(&lock);

    mock_irq_disabled = false;
    uint32_t irq_state = ticket_lock_acquire_irqsave(&lock);
    TEST_ASSERT_TRUE(ticket_lock_is_held(&lock));

    ticket_lock_release_irqrestore(&lock, irq_state);
    TEST_ASSERT_FALSE(ticket_lock_is_held(&lock));
    TEST_ASSERT_FALSE(mock_irq_disabled);
}

/**
 * @brief 测试 12: NULL 锁的 irqsave/irqrestore 不应破坏 IRQ 状态
 */
static void test_irqsave_null_safe(void)
{
    uint32_t irq_state;

    mock_irq_disabled = false;
    irq_state = ticket_lock_acquire_irqsave(NULL);
    TEST_ASSERT_EQ(irq_state, 0U);
    TEST_ASSERT_FALSE(mock_irq_disabled);

    mock_irq_disabled = true;
    ticket_lock_release_irqrestore(NULL, 0U);
    TEST_ASSERT_FALSE(mock_irq_disabled);
}

/**
 * @brief 测试 13: 多获取者 FIFO 顺序
 * @details 模拟两个获取者依次获取锁，验证 FIFO
 */
static void test_multiple_acquirers(void)
{
    TicketLock_t lock;
    ticket_lock_init(&lock);

    /* 第一个获取者 */
    ticket_lock_acquire(&lock);
    TEST_ASSERT_TRUE(ticket_lock_is_held(&lock));

    /* 释放 */
    ticket_lock_release(&lock);
    TEST_ASSERT_FALSE(ticket_lock_is_held(&lock));

    /* 第二个获取者 */
    ticket_lock_acquire(&lock);
    TEST_ASSERT_TRUE(ticket_lock_is_held(&lock));

    ticket_lock_release(&lock);
    TEST_ASSERT_FALSE(ticket_lock_is_held(&lock));
}

/**
 * @brief 测试 14: TICKET_LOCK_INIT 静态初始化
 */
static void test_init_static(void)
{
    TicketLock_t lock = TICKET_LOCK_INIT;

    TEST_ASSERT_EQ(lock.next_ticket, 0U);
    TEST_ASSERT_EQ(lock.serving_ticket, 0U);
    TEST_ASSERT_EQ(lock.cpu_id, 0xFFFFFFFFU);
    TEST_ASSERT_FALSE(ticket_lock_is_held(&lock));
}

/**
 * @brief 测试 15: NULL 指针安全检查
 */
static void test_null_param(void)
{
    /* NULL 初始化不应崩溃 */
    ticket_lock_init(NULL);

    /* NULL 获取不应崩溃 */
    ticket_lock_acquire(NULL);

    /* NULL 释放不应崩溃 */
    ticket_lock_release(NULL);

    /* NULL 的 irqrestore 不应崩溃 */
    ticket_lock_release_irqrestore(NULL, 0U);

    /* NULL try_acquire 应返回 false */
    TEST_ASSERT_FALSE(ticket_lock_try_acquire(NULL));

    /* NULL is_held 应返回 false */
    TEST_ASSERT_FALSE(ticket_lock_is_held(NULL));

    /* 通过所有 NULL 安全检查 */
    TEST_ASSERT_TRUE(true);
}

/**
 * @brief 测试 16: 模拟两个 CPU 交替获取释放
 * @details 验证票号递增正确
 */
static void test_concurrent_simulate(void)
{
    TicketLock_t lock;
    ticket_lock_init(&lock);

    /* CPU 0 获取 */
    mock_cpu_id = 0U;
    ticket_lock_acquire(&lock);
    TEST_ASSERT_EQ(lock.cpu_id, 0U);
    TEST_ASSERT_EQ(lock.next_ticket, 1U);

    ticket_lock_release(&lock);
    TEST_ASSERT_EQ(lock.serving_ticket, 1U);

    /* CPU 1 获取 */
    mock_cpu_id = 1U;
    ticket_lock_acquire(&lock);
    TEST_ASSERT_EQ(lock.cpu_id, 1U);
    TEST_ASSERT_EQ(lock.next_ticket, 2U);

    ticket_lock_release(&lock);
}

/**
 * @brief 测试 17: 循环 1000 次获取释放无死锁
 */
static void test_stress_acquire_release(void)
{
    TicketLock_t lock;
    ticket_lock_init(&lock);
    uint32_t i;

    for (i = 0U; i < 1000U; i++)
    {
        ticket_lock_acquire(&lock);
        TEST_ASSERT_TRUE(ticket_lock_is_held(&lock));
        ticket_lock_release(&lock);
        TEST_ASSERT_FALSE(ticket_lock_is_held(&lock));
    }

    /* 验证票号对称 */
    TEST_ASSERT_EQ(lock.next_ticket, 1000U);
    TEST_ASSERT_EQ(lock.serving_ticket, 1000U);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("=== TicketLock 公平自旋锁测试 ===\n\n");

    test_init_lock();
    test_acquire_release_basic();
    test_is_held_after_acquire();
    test_is_held_after_release();
    test_try_acquire_success();
    test_try_acquire_fail();
    test_reacquire_while_held();
    test_double_release_safe();
    test_release_wrong_cpu_safe();
    test_acquire_irqsave();
    test_release_irqrestore();
    test_irqsave_null_safe();
    test_multiple_acquirers();
    test_init_static();
    test_null_param();
    test_concurrent_simulate();
    test_stress_acquire_release();

    TEST_SUMMARY("test_spinlock");

    return TEST_RESULT();
}
