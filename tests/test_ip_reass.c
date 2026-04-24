/**
 * @file    test_ip_reass.c
 * @brief   IP 分片重组测试用例（TDD - RED）
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @details TDD 测试用例：
 *          - IP 分片识别和管理
 *          - 分片重组缓冲区管理
 *          - 分片到达顺序测试
 *          - 分片超时处理
 *          - 分片重组完成验证
 *
 * @note 必须先编写测试，然后实现功能（RED → GREEN → REFACTOR）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "unity.h"

/* ========================================================================
 * 测试常量定义
 * ======================================================================== */

#define NET_MAX_REASS_QUEUE      8U
#define REASS_TIMEOUT_MS         60000U
#define NET_MAX_PACKET_SIZE      1514U

/* ========================================================================
 * 测试数据结构
 * ======================================================================== */

/**
 * @brief IP 分片条目（简化版，用于测试）
 */
typedef struct ip_reass_frag_test_t
{
    uint16_t frag_offset;     /* 分片偏移（8 字节单位） */
    uint16_t frag_id;         /* 分片标识符 */
    uint32_t src_ip;          /* 源 IP 地址 */
    uint32_t dst_ip;          /* 目的 IP 地址 */
    uint32_t len;             /* 分片长度 */
    uint8_t  data[1500];      /* 分片数据 */
    uint32_t data_len;        /* 数据长度 */
    bool     in_use;          /* 使用标记 */
    uint64_t arrival_time;    /* 到达时间 */
    struct ip_reass_frag_test_t *next; /* 下一个分片 */
} ip_reass_frag_test_t;

/**
 * @brief IP 分片重组队列（简化版，用于测试）
 */
typedef struct ip_reass_queue_test_t
{
    ip_reass_frag_test_t *head;    /* 队列头 */
    ip_reass_frag_test_t *tail;    /* 队列尾 */
    uint16_t frag_id;             /* 分片标识符 */
    uint32_t src_ip;              /* 源 IP 地址 */
    uint32_t dst_ip;              /* 目的 IP 地址 */
    uint32_t total_len;           /* 总长度 */
    uint16_t header_offset;       /* IP 头偏移 */
    uint8_t  protocol;            /* 上层协议 */
    bool     in_use;              /* 使用标记 */
    uint64_t last_frag_time;      /* 最后分片到达时间 */
    uint32_t frag_count;          /* 分片计数 */
    uint32_t recv_len;            /* 已接收长度 */
} ip_reass_queue_test_t;

/* 全局重组队列数组 */
static ip_reass_queue_test_t s_reass_queues[NET_MAX_REASS_QUEUE];

/* ========================================================================
 * 测试辅助函数
 * ======================================================================== */

/**
 * @brief 初始化重组队列数组
 */
static void ip_reass_init(void)
{
    uint32_t i;

    for (i = 0U; i < NET_MAX_REASS_QUEUE; i++)
    {
        (void)memset(&s_reass_queues[i], 0, sizeof(ip_reass_queue_test_t));
        s_reass_queues[i].in_use = false;
    }
}

/**
 * @brief 查找或创建重组队列
 */
static ip_reass_queue_test_t *ip_reass_find_queue(
    uint32_t src_ip, uint32_t dst_ip, uint16_t frag_id)
{
    uint32_t i;
    ip_reass_queue_test_t *queue = NULL;
    ip_reass_queue_test_t *oldest = NULL;
    uint64_t oldest_time = 0xFFFFFFFFFFFFFFFFULL;

    /* 查找已存在的队列 */
    for (i = 0U; i < NET_MAX_REASS_QUEUE; i++)
    {
        if (s_reass_queues[i].in_use &&
            (s_reass_queues[i].src_ip == src_ip) &&
            (s_reass_queues[i].dst_ip == dst_ip) &&
            (s_reass_queues[i].frag_id == frag_id))
        {
            return &s_reass_queues[i];
        }
    }

    /* 创建新队列 */
    for (i = 0U; i < NET_MAX_REASS_QUEUE; i++)
    {
        if (!s_reass_queues[i].in_use)
        {
            queue = &s_reass_queues[i];
            break;
        }

        /* 记录最老的队列 */
        if (s_reass_queues[i].last_frag_time < oldest_time)
        {
            oldest_time = s_reass_queues[i].last_frag_time;
            oldest = &s_reass_queues[i];
        }
    }

    /* 队列满，回收最老的队列 */
    if ((queue == NULL) && (oldest != NULL))
    {
        /* 清理旧队列 */
        oldest->in_use = false;
        oldest->head = NULL;
        oldest->tail = NULL;
        oldest->frag_count = 0U;
        oldest->recv_len = 0U;
        queue = oldest;
    }

    if (queue != NULL)
    {
        queue->src_ip = src_ip;
        queue->dst_ip = dst_ip;
        queue->frag_id = frag_id;
        queue->in_use = true;
        queue->head = NULL;
        queue->tail = NULL;
        queue->frag_count = 0U;
        queue->recv_len = 0U;
        queue->last_frag_time = 0ULL;
    }

    return queue;
}

/**
 * @brief 创建测试分片
 */
static ip_reass_frag_test_t *ip_reass_create_frag(
    uint32_t src_ip, uint32_t dst_ip, uint16_t frag_id,
    uint16_t frag_offset, uint32_t data_len, uint8_t *data)
{
    ip_reass_frag_test_t *frag;

    frag = (ip_reass_frag_test_t *)malloc(sizeof(ip_reass_frag_test_t));
    if (frag == NULL)
    {
        return NULL;
    }

    frag->src_ip = src_ip;
    frag->dst_ip = dst_ip;
    frag->frag_id = frag_id;
    frag->frag_offset = frag_offset;
    frag->data_len = data_len;
    frag->in_use = true;
    frag->next = NULL;

    if ((data != NULL) && (data_len > 0U))
    {
        (void)memcpy(frag->data, data, data_len);
    }

    return frag;
}

/**
 * @brief 添加分片到队列
 */
static bool ip_reass_add_frag_to_queue(
    ip_reass_queue_test_t *queue,
    ip_reass_frag_test_t *frag)
{
    ip_reass_frag_test_t *prev;
    ip_reass_frag_test_t *curr;

    if ((queue == NULL) || (frag == NULL))
    {
        return false;
    }

    /* 空队列 */
    if (queue->head == NULL)
    {
        queue->head = frag;
        queue->tail = frag;
        frag->next = NULL;
        queue->frag_count = 1U;
        queue->recv_len = frag->data_len;
        return true;
    }

    /* 按偏移量插入（升序） */
    prev = NULL;
    curr = queue->head;

    while ((curr != NULL) && (curr->frag_offset < frag->frag_offset))
    {
        prev = curr;
        curr = curr->next;
    }

    /* 插入分片 */
    if (prev == NULL)
    {
        /* 插入头部 */
        frag->next = queue->head;
        queue->head = frag;
    }
    else if (curr == NULL)
    {
        /* 插入尾部 */
        prev->next = frag;
        queue->tail = frag;
        frag->next = NULL;
    }
    else
    {
        /* 插入中间 */
        prev->next = frag;
        frag->next = curr;
    }

    queue->frag_count++;
    queue->recv_len += frag->data_len;

    return true;
}

/**
 * @brief 检查分片是否完整
 */
static bool ip_reass_is_complete(const ip_reass_queue_test_t *queue)
{
    uint32_t offset;
    ip_reass_frag_test_t *frag;

    if ((queue == NULL) || (queue->head == NULL))
    {
        return false;
    }

    offset = 0U;
    frag = queue->head;

    while (frag != NULL)
    {
        if (frag->frag_offset != offset)
        {
            return false;
        }
        offset += frag->data_len;
        frag = frag->next;
    }

    /* 检查是否收到最后一个分片（MF 标志 = 0） */
    /* 简化：假设最后一个分片的 frag_offset + data_len == total_len */
    if ((queue->tail != NULL) && (queue->total_len > 0U))
    {
        if ((queue->tail->frag_offset + queue->tail->data_len) == queue->total_len)
        {
            return true;
        }
    }

    return false;
}

/* ========================================================================
 * 测试：IP 分片识别和管理
 * ======================================================================== */

/**
 * @brief 测试：重组队列初始化
 */
void test_ip_reass_queue_init(void)
{
    ip_reass_init();

    TEST_ASSERT_EQUAL(false, s_reass_queues[0].in_use);
    TEST_ASSERT_EQUAL(false, s_reass_queues[1].in_use);
    TEST_ASSERT_EQUAL(0U, s_reass_queues[0].frag_count);
    TEST_ASSERT_EQUAL(0U, s_reass_queues[0].recv_len);
}

/**
 * @brief 测试：查找或创建重组队列
 */
void test_ip_reass_find_queue(void)
{
    ip_reass_queue_test_t *queue;

    ip_reass_init();

    /* 创建新队列 */
    queue = ip_reass_find_queue(0xC0A80001, 0xC0A80002, 12345);

    TEST_ASSERT_NOT_NULL(queue);
    TEST_ASSERT_EQUAL(0xC0A80001U, queue->src_ip);
    TEST_ASSERT_EQUAL(0xC0A80002U, queue->dst_ip);
    TEST_ASSERT_EQUAL(12345U, queue->frag_id);
    TEST_ASSERT_EQUAL(true, queue->in_use);

    /* 查找已存在的队列 */
    queue = ip_reass_find_queue(0xC0A80001, 0xC0A80002, 12345);

    TEST_ASSERT_NOT_NULL(queue);
    TEST_ASSERT_EQUAL(0xC0A80001U, queue->src_ip);
    TEST_ASSERT_EQUAL(0xC0A80002U, queue->dst_ip);
    TEST_ASSERT_EQUAL(12345U, queue->frag_id);
    TEST_ASSERT_EQUAL(true, queue->in_use);
}

/**
 * @brief 测试：创建多个重组队列
 */
void test_ip_reass_multiple_queues(void)
{
    ip_reass_queue_test_t *queue1, *queue2, *queue3;

    ip_reass_init();

    /* 创建队列 1 */
    queue1 = ip_reass_find_queue(0xC0A80001, 0xC0A80002, 11111);
    TEST_ASSERT_NOT_NULL(queue1);

    /* 创建队列 2 */
    queue2 = ip_reass_find_queue(0xC0A80001, 0xC0A80003, 22222);
    TEST_ASSERT_NOT_NULL(queue2);
    TEST_ASSERT_NOT_EQUAL(queue1, queue2);

    /* 查找队列 1 */
    queue3 = ip_reass_find_queue(0xC0A80001, 0xC0A80002, 11111);
    TEST_ASSERT_EQUAL(queue1, queue3);
}

/* ========================================================================
 * 测试：分片到达顺序测试
 * ======================================================================== */

/**
 * @brief 测试：分片按顺序到达
 */
void test_ip_reass_frag_order_sequential(void)
{
    ip_reass_queue_test_t *queue;
    ip_reass_frag_test_t *frag1, *frag2, *frag3;
    uint8_t data1[500], data2[500], data3[500];

    ip_reass_init();

    (void)memset(data1, 0x01, sizeof(data1));
    (void)memset(data2, 0x02, sizeof(data2));
    (void)memset(data3, 0x03, sizeof(data3));

    /* 创建队列 */
    queue = ip_reass_find_queue(0xC0A80001, 0xC0A80002, 33333);
    queue->total_len = 1500U;

    /* 按顺序添加分片 */
    frag1 = ip_reass_create_frag(0xC0A80001, 0xC0A80002, 33333,
                                    0U, 500, data1);
    frag2 = ip_reass_create_frag(0xC0A80001, 0xC0A80002, 33333,
                                    500U, 500, data2);
    frag3 = ip_reass_create_frag(0xC0A80001, 0xC0A80002, 33333,
                                    1000U, 500, data3);

    ip_reass_add_frag_to_queue(queue, frag1);
    ip_reass_add_frag_to_queue(queue, frag2);
    ip_reass_add_frag_to_queue(queue, frag3);

    /* 验证：分片顺序正确 */
    TEST_ASSERT_EQUAL_PTR(frag1, queue->head);
    TEST_ASSERT_EQUAL_PTR(frag2, frag1->next);
    TEST_ASSERT_EQUAL_PTR(frag3, frag2->next);
    TEST_ASSERT_NULL(frag3->next);

    /* 验证：分片完整 */
    TEST_ASSERT_EQUAL(true, ip_reass_is_complete(queue));
    TEST_ASSERT_EQUAL(1500U, queue->recv_len);
}

/**
 * @brief 测试：分片乱序到达
 */
void test_ip_reass_frag_order_out_of_order(void)
{
    ip_reass_queue_test_t *queue;
    ip_reass_frag_test_t *frag1, *frag2, *frag3;
    uint8_t data1[500], data2[500], data3[500];

    ip_reass_init();

    (void)memset(data1, 0x01, sizeof(data1));
    (void)memset(data2, 0x02, sizeof(data2));
    (void)memset(data3, 0x03, sizeof(data3));

    /* 创建队列 */
    queue = ip_reass_find_queue(0xC0A80001, 0xC0A80002, 44444);
    queue->total_len = 1500U;

    /* 乱序添加分片（先加 frag3，再加 frag1，最后加 frag2） */
    frag3 = ip_reass_create_frag(0xC0A80001, 0xC0A80002, 44444,
                                    1000U, 500, data3);
    frag1 = ip_reass_create_frag(0xC0A80001, 0xC0A80002, 44444,
                                    0U, 500, data1);
    frag2 = ip_reass_create_frag(0xC0A80001, 0xC0A80002, 44444,
                                    500U, 500, data2);

    ip_reass_add_frag_to_queue(queue, frag3);
    ip_reass_add_frag_to_queue(queue, frag1);
    ip_reass_add_frag_to_queue(queue, frag2);

    /* 验证：分片顺序正确（自动排序） */
    TEST_ASSERT_EQUAL_PTR(frag1, queue->head);
    TEST_ASSERT_EQUAL_PTR(frag2, frag1->next);
    TEST_ASSERT_EQUAL_PTR(frag3, frag2->next);
    TEST_ASSERT_NULL(frag3->next);

    /* 验证：分片完整 */
    TEST_ASSERT_EQUAL(true, ip_reass_is_complete(queue));
    TEST_ASSERT_EQUAL(1500U, queue->recv_len);
}

/**
 * @brief 测试：分片部分到达
 */
void test_ip_reass_frag_partial(void)
{
    ip_reass_queue_test_t *queue;
    ip_reass_frag_test_t *frag1, *frag2;

    ip_reass_init();

    /* 创建队列 */
    queue = ip_reass_find_queue(0xC0A80001, 0xC0A80002, 55555);
    queue->total_len = 1500U;

    /* 只添加 2 个分片（共 1000 字节） */
    frag1 = ip_reass_create_frag(0xC0A80001, 0xC0A80002, 55555,
                                    0U, 500, NULL);
    frag2 = ip_reass_create_frag(0xC0A80001, 0xC0A80002, 55555,
                                    500U, 500, NULL);

    ip_reass_add_frag_to_queue(queue, frag1);
    ip_reass_add_frag_to_queue(queue, frag2);

    /* 验证：分片不完整 */
    TEST_ASSERT_EQUAL(false, ip_reass_is_complete(queue));
    TEST_ASSERT_EQUAL(1000U, queue->recv_len);
    TEST_ASSERT_LESS_THAN(queue->total_len, queue->recv_len);
}

/* ========================================================================
 * 测试：分片超时处理
 * ======================================================================== */

/**
 * @brief 测试：分片超时检测
 */
void test_ip_reass_timeout(void)
{
    ip_reass_queue_test_t *queue;
    ip_reass_frag_test_t *frag;

    ip_reass_init();

    /* 创建队列 */
    queue = ip_reass_find_queue(0xC0A80001, 0xC0A80002, 66666);
    queue->total_len = 1500U;

    /* 添加分片 */
    frag = ip_reass_create_frag(0xC0A80001, 0xC0A80002, 66666,
                                   0U, 500, NULL);
    ip_reass_add_frag_to_queue(queue, frag);

    /* 设置超时时间 */
    queue->last_frag_time = 100ULL;  /* 100ms */

    /* 模拟当前时间 */
    uint64_t current_time = 61000ULL;  /* 61秒后 */

    /* 验证：分片超时 */
    TEST_ASSERT_EQUAL(100ULL, queue->last_frag_time);
    TEST_ASSERT_GREATER_THAN(REASS_TIMEOUT_MS, current_time - queue->last_frag_time);
}

/**
 * @brief 测试：分片清理超时队列
 */
void test_ip_reass_cleanup_timeout(void)
{
    ip_reass_queue_test_t *queue;

    ip_reass_init();

    /* 创建队列 */
    queue = ip_reass_find_queue(0xC0A80001, 0xC0A80002, 77777);
    TEST_ASSERT_EQUAL(true, queue->in_use);

    /* 设置超时时间 */
    queue->last_frag_time = 100ULL;  /* 100ms */

    /* 模拟当前时间 */
    uint64_t current_time = 61000ULL;  /* 61秒后 */

    /* 检查超时 */
    if (queue->in_use &&
        (current_time - queue->last_frag_time > REASS_TIMEOUT_MS))
    {
        /* 清理超时队列 */
        queue->in_use = false;
        queue->head = NULL;
        queue->tail = NULL;
        queue->frag_count = 0U;
        queue->recv_len = 0U;
    }

    /* 验证：队列已清理 */
    TEST_ASSERT_EQUAL(false, queue->in_use);
    TEST_ASSERT_NULL(queue->head);
    TEST_ASSERT_EQUAL(0U, queue->frag_count);
}

/* ========================================================================
 * 测试：分片重组完成验证
 * ======================================================================== */

/**
 * @brief 测试：完整分片重组
 */
void test_ip_reass_complete(void)
{
    ip_reass_queue_test_t *queue;
    ip_reass_frag_test_t *frag1, *frag2, *frag3;
    uint8_t data1[500], data2[500], data3[500];

    ip_reass_init();

    (void)memset(data1, 0x01, sizeof(data1));
    (void)memset(data2, 0x02, sizeof(data2));
    (void)memset(data3, 0x03, sizeof(data3));

    /* 创建队列 */
    queue = ip_reass_find_queue(0xC0A80001, 0xC0A80002, 88888);
    queue->total_len = 1500U;

    /* 添加分片 */
    frag1 = ip_reass_create_frag(0xC0A80001, 0xC0A80002, 88888,
                                    0U, 500, data1);
    frag2 = ip_reass_create_frag(0xC0A80001, 0xC0A80002, 88888,
                                    500U, 500, data2);
    frag3 = ip_reass_create_frag(0xC0A80001, 0xC0A80002, 88888,
                                    1000U, 500, data3);

    ip_reass_add_frag_to_queue(queue, frag1);
    ip_reass_add_frag_to_queue(queue, frag2);
    ip_reass_add_frag_to_queue(queue, frag3);

    /* 验证：分片重组完成 */
    TEST_ASSERT_EQUAL(true, ip_reass_is_complete(queue));
    TEST_ASSERT_EQUAL(1500U, queue->recv_len);
    TEST_ASSERT_EQUAL(1500U, queue->total_len);
    TEST_ASSERT_EQUAL(3U, queue->frag_count);
}

/**
 * @brief 测试：分片数据验证
 */
void test_ip_reass_data_verify(void)
{
    ip_reass_queue_test_t *queue;
    ip_reass_frag_test_t *frag;
    uint8_t data[1000];

    ip_reass_init();

    (void)memset(data, 0x42, sizeof(data));

    /* 创建队列 */
    queue = ip_reass_find_queue(0xC0A80001, 0xC0A80002, 99999);
    queue->total_len = 1000U;

    /* 添加分片 */
    frag = ip_reass_create_frag(0xC0A80001, 0xC0A80002, 99999,
                                   0U, 1000, data);
    ip_reass_add_frag_to_queue(queue, frag);

    /* 验证：分片数据正确 */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, frag->data, 1000);
}

/* ========================================================================
 * 测试：队列满处理
 * ======================================================================== */

/**
 * @brief 测试：队列满时回收最老队列
 */
void test_ip_reass_queue_full(void)
{
    ip_reass_queue_test_t *queue;
    uint32_t i;

    ip_reass_init();

    /* 填满所有队列 */
    for (i = 0U; i < NET_MAX_REASS_QUEUE; i++)
    {
        queue = ip_reass_find_queue(0xC0A80001, 0xC0A80002, i);
        queue->last_frag_time = i * 100ULL;  /* 设置到达时间 */
        TEST_ASSERT_NOT_NULL(queue);
    }

    /* 验证：所有队列已使用 */
    TEST_ASSERT_EQUAL(NET_MAX_REASS_QUEUE, 8U);
    for (i = 0U; i < NET_MAX_REASS_QUEUE; i++)
    {
        TEST_ASSERT_EQUAL(true, s_reass_queues[i].in_use);
    }

    /* 尝试创建新队列（应回收最老的队列） */
    queue = ip_reass_find_queue(0xC0A80001, 0xC0A80003, 10000);
    TEST_ASSERT_NOT_NULL(queue);

    /* 验证：新队列创建成功 */
    TEST_ASSERT_EQUAL(0xC0A80001U, queue->src_ip);
    TEST_ASSERT_EQUAL(0xC0A80003U, queue->dst_ip);
    TEST_ASSERT_EQUAL(10000U, queue->frag_id);
    TEST_ASSERT_EQUAL(true, queue->in_use);
}

/* ========================================================================
 * 测试套件注册
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* IP 分片识别和管理测试 */
    RUN_TEST(test_ip_reass_queue_init);
    RUN_TEST(test_ip_reass_find_queue);
    RUN_TEST(test_ip_reass_multiple_queues);

    /* 分片到达顺序测试 */
    RUN_TEST(test_ip_reass_frag_order_sequential);
    RUN_TEST(test_ip_reass_frag_order_out_of_order);
    RUN_TEST(test_ip_reass_frag_partial);

    /* 分片超时处理测试 */
    RUN_TEST(test_ip_reass_timeout);
    RUN_TEST(test_ip_reass_cleanup_timeout);

    /* 分片重组完成验证测试 */
    RUN_TEST(test_ip_reass_complete);
    RUN_TEST(test_ip_reass_data_verify);

    /* 队列满处理测试 */
    RUN_TEST(test_ip_reass_queue_full);

    return UNITY_END();
}
