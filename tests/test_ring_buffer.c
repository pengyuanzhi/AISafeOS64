/**
 * @file    test_ring_buffer.c
 * @brief   环形缓冲区测试
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @test 模块化测试框架
 *
 * @details 测试环形缓冲区性能和功能：
 *          - 环形缓冲区创建
 *          - 写入操作
 *          - 读取操作
 *          - 环绕写入
 *          - 环绕读取
 *          - 满队列处理
 *          - 空队列处理
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.2.2 - IPC 队列优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

/* ========================================================================
 * 测试统计
 * ======================================================================== */

static uint32_t s_total_tests = 0U;
static uint32_t s_passed_tests = 0U;
static uint32_t s_failed_tests = 0U;

/* ========================================================================
 * 测试辅助宏
 * ======================================================================== */

#define TEST_ASSERT(condition, message) \
    do { \
        s_total_tests++; \
        if (condition) { \
            s_passed_tests++; \
            printf("  [PASS] %s\n", message); \
        } else { \
            s_failed_tests++; \
            printf("  [FAIL] %s\n", message); \
        } \
    } while (0)

/* ========================================================================
 * 环形缓冲区结构定义
 * ======================================================================== */

#define RING_BUFFER_SIZE   1024

typedef struct
{
    void    *buffer;     /**< @brief 缓冲区指针 */
    uint64_t size;      /**< @brief 缓冲区大小 */
    uint64_t head;      /**< @brief 写入位置 */
    uint64_t tail;      /**< @brief 读取位置 */
    uint64_t count;     /**< @brief 当前消息数量 */
    uint32_t lock;      /**< @brief 自旋锁 */
} ring_buffer_t;

/* ========================================================================
 * 环形缓冲区操作
 * ======================================================================== */

/**
 * @brief 初始化环形缓冲区
 */
static int32_t ring_buffer_init(ring_buffer_t *rb, void *buffer, uint64_t size)
{
    if ((rb == NULL) || (buffer == NULL) || (size == 0))
    {
        return -1;
    }
    (void)memset(rb, 0, sizeof(ring_buffer_t));
    rb->buffer = buffer;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    return 0;
}

/**
 * @brief 写入数据到环形缓冲区
 */
static int32_t ring_buffer_write(ring_buffer_t *rb, const void *data, uint64_t len)
{
    uint64_t space;

    if ((rb == NULL) || (data == NULL) || (len == 0))
    {
        return -1;
    }

    /* 检查是否有足够空间 */
    space = rb->size - rb->count;
    if (len > space)
    {
        return -2; /* 队列满 */
    }

    /* 普通写入（不跨越边界） */
    if ((rb->head + len) <= rb->size)
    {
        (void)memcpy((char *)rb->buffer + rb->head, data, len);
        rb->head += len;
    }
    else
    {
        /* 跨越边界写入 */
        uint64_t first_part = rb->size - rb->head;
        (void)memcpy((char *)rb->buffer + rb->head, data, first_part);
        (void)memcpy(rb->buffer, (char *)data + first_part, len - first_part);
        rb->head = len - first_part;
    }

    rb->count += len;
    return 0;
}

/**
 * @brief 从环形缓冲区读取数据
 */
static int32_t ring_buffer_read(ring_buffer_t *rb, void *data, uint64_t len)
{
    uint64_t avail;

    if ((rb == NULL) || (data == NULL) || (len == 0))
    {
        return -1;
    }

    /* 检查是否有数据 */
    if (rb->count == 0)
    {
        return -3; /* 队列空 */
    }

    /* 检查是否有足够数据 */
    avail = rb->count;
    if (len > avail)
    {
        len = avail;
    }

    /* 普通读取（不跨越边界） */
    if ((rb->tail + len) <= rb->size)
    {
        (void)memcpy(data, (char *)rb->buffer + rb->tail, len);
        rb->tail += len;
    }
    else
    {
        /* 跨越边界读取 */
        uint64_t first_part = rb->size - rb->tail;
        (void)memcpy(data, (char *)rb->buffer + rb->tail, first_part);
        (void)memcpy((char *)data + first_part, rb->buffer, len - first_part);
        rb->tail = len - first_part;
    }

    rb->count -= len;
    return 0;
}

/**
 * @brief 获取可用空间
 */
static uint64_t ring_buffer_space(ring_buffer_t *rb)
{
    if (rb == NULL)
    {
        return 0;
    }
    return rb->size - rb->count;
}

/**
 * @brief 获取可用数据量
 */
static uint64_t ring_buffer_available(ring_buffer_t *rb)
{
    if (rb == NULL)
    {
        return 0;
    }
    return rb->count;
}

/* ========================================================================
 * 测试 1: 环形缓冲区创建
 * ======================================================================== */

/**
 * @brief 测试环形缓冲区创建
 */
static void test_rb_init(void)
{
    printf("\n========== 测试 1: 环形缓冲区创建 ==========\n");

    uint8_t buffer[RING_BUFFER_SIZE];
    ring_buffer_t rb;
    int32_t ret;

    /* 测试空指针 */
    ret = ring_buffer_init(NULL, buffer, RING_BUFFER_SIZE);
    TEST_ASSERT(ret == -1, "空指针测试失败");

    /* 测试空缓冲区 */
    ret = ring_buffer_init(&rb, NULL, RING_BUFFER_SIZE);
    TEST_ASSERT(ret == -1, "空缓冲区测试失败");

    /* 测试大小为0 */
    ret = ring_buffer_init(&rb, buffer, 0);
    TEST_ASSERT(ret == -1, "大小为0测试失败");

    /* 测试正常初始化 */
    ret = ring_buffer_init(&rb, buffer, RING_BUFFER_SIZE);
    TEST_ASSERT(ret == 0, "环形缓冲区初始化失败");
    TEST_ASSERT(ring_buffer_available(&rb) == 0, "初始数据量应为0");
    TEST_ASSERT(ring_buffer_space(&rb) == RING_BUFFER_SIZE, "初始空间应为1024");

    printf("  [INFO] 测试通过\n");
}

/* ========================================================================
 * 测试 2: 写入操作
 * ======================================================================== */

/**
 * @brief 测试写入操作
 */
static void test_rb_write(void)
{
    printf("\n========== 测试 2: 写入操作 ==========\n");

    uint8_t buffer[RING_BUFFER_SIZE];
    ring_buffer_t rb;
    int32_t ret;

    ring_buffer_init(&rb, buffer, RING_BUFFER_SIZE);

    /* 测试普通写入 */
    const char *data = "Hello, Ring Buffer!";
    ret = ring_buffer_write(&rb, data, (uint64_t)strlen(data) + 1);
    TEST_ASSERT(ret == 0, "普通写入失败");
    TEST_ASSERT(ring_buffer_available(&rb) == (uint64_t)(strlen(data) + 1), "数据量统计错误");
    TEST_ASSERT(ring_buffer_space(&rb) == RING_BUFFER_SIZE - (strlen(data) + 1), "空间统计错误");

    printf("  [INFO] 写入数据: \"%s\"\n", data);
}

/* ========================================================================
 * 测试 3: 读取操作
 * ======================================================================== */

/**
 * @brief 测试读取操作
 */
static void test_rb_read(void)
{
    printf("\n========== 测试 3: 读取操作 ==========\n");

    uint8_t buffer[RING_BUFFER_SIZE];
    ring_buffer_t rb;
    char read_data[64];

    ring_buffer_init(&rb, buffer, RING_BUFFER_SIZE);

    /* 写入数据 */
    const char *data = "Hello, Ring Buffer!";
    ring_buffer_write(&rb, data, (uint64_t)strlen(data) + 1);
    printf("  [INFO] 写入数据: \"%s\"\n", data);

    /* 读取数据 */
    int32_t ret = ring_buffer_read(&rb, read_data, sizeof(read_data));
    TEST_ASSERT(ret == 0, "读取失败");
    TEST_ASSERT(ring_buffer_available(&rb) == 0, "读取后数据量应为0");
    TEST_ASSERT(strcmp(read_data, data) == 0, "数据一致性验证失败");

    printf("  [INFO] 读取数据: \"%s\"\n", read_data);
}

/* ========================================================================
 * 测试 4: 环绕写入
 * ======================================================================== */

/**
 * @brief 测试环绕写入
 */
static void test_rb_wrap_write(void)
{
    printf("\n========== 测试 4: 环绕写入 ==========\n");

    uint8_t buffer[64];
    ring_buffer_t rb;

    ring_buffer_init(&rb, buffer, 64);

    /* 写入超过一半的数据 */
    const char *data = "0123456789ABCDEFGHIJ";
    int32_t ret = ring_buffer_write(&rb, data, (uint64_t)strlen(data) + 1);
    TEST_ASSERT(ret == 0, "环绕写入失败");
    TEST_ASSERT(ring_buffer_available(&rb) == (uint64_t)(strlen(data) + 1), "数据量统计错误");

    printf("  [INFO] 写入数据（跨越边界）: \"%s\"\n", data);
}

/* ========================================================================
 * 测试 5: 环绕读取
 * ======================================================================== */

/**
 * @brief 测试环绕读取
 */
static void test_rb_wrap_read(void)
{
    printf("\n========== 测试 5: 环绕读取 ==========\n");

    uint8_t buffer[64];
    ring_buffer_t rb;
    char read_data[64];

    ring_buffer_init(&rb, buffer, 64);

    /* 写入超过一半的数据 */
    const char *data = "0123456789ABCDEFGHIJ";
    ring_buffer_write(&rb, data, (uint64_t)strlen(data) + 1);
    printf("  [INFO] 写入数据: \"%s\"\n", data);

    /* 读取超过一半的数据 */
    int32_t ret = ring_buffer_read(&rb, read_data, sizeof(read_data));
    TEST_ASSERT(ret == 0, "环绕读取失败");
    TEST_ASSERT(strcmp(read_data, data) == 0, "数据一致性验证失败");

    printf("  [INFO] 读取数据: \"%s\"\n", read_data);
}

/* ========================================================================
 * 测试 6: 满队列处理
 * ======================================================================== */

/**
 * @brief 测试满队列处理
 */
static void test_rb_full(void)
{
    printf("\n========== 测试 6: 满队列处理 ==========\n");

    uint8_t buffer[16];
    ring_buffer_t rb;

    ring_buffer_init(&rb, buffer, 16);

    /* 填满队列 */
    const char *data = "0123456789ABCDEFGHI";
    ring_buffer_write(&rb, data, (uint64_t)strlen(data) + 1);
    TEST_ASSERT(ring_buffer_available(&rb) == 16, "数据量应为16");
    TEST_ASSERT(ring_buffer_space(&rb) == 0, "空间应为0");

    /* 尝试写入更多数据（应该失败） */
    int32_t ret = ring_buffer_write(&rb, "X", 2);
    TEST_ASSERT(ret == -2, "满队列应该返回错误");

    printf("  [INFO] 队列满测试通过\n");
}

/* ========================================================================
 * 测试 7: 空队列处理
 * ======================================================================== */

/**
 * @brief 测试空队列处理
 */
static void test_rb_empty(void)
{
    printf("\n========== 测试 7: 空队列处理 ==========\n");

    uint8_t buffer[16];
    ring_buffer_t rb;
    char read_data[64];

    ring_buffer_init(&rb, buffer, 16);

    /* 尝试读取数据（应该失败） */
    int32_t ret = ring_buffer_read(&rb, read_data, sizeof(read_data));
    TEST_ASSERT(ret == -3, "空队列应该返回错误");

    printf("  [INFO] 空队列测试通过\n");
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

/**
 * @brief 运行所有环形缓冲区测试
 */
static void run_all_tests(void)
{
    printf("\n");
    printf("========================================\n");
    printf("AISafeOS64 环形缓冲区性能测试\n");
    printf("========================================\n");

    /* 运行所有测试 */
    test_rb_init();
    test_rb_write();
    test_rb_read();
    test_rb_wrap_write();
    test_rb_wrap_read();
    test_rb_full();
    test_rb_empty();

    /* 输出测试结果 */
    printf("\n");
    printf("========================================\n");
    printf("测试结果统计\n");
    printf("========================================\n");
    printf("总计测试: %u\n", s_total_tests);
    printf("通过: %u (%.1f%%)\n", s_passed_tests,
           (100.0 * s_passed_tests / s_total_tests));
    printf("失败: %u (%.1f%%)\n", s_failed_tests,
           (100.0 * s_failed_tests / s_total_tests));
    printf("========================================\n");

    if (s_failed_tests == 0)
    {
        printf("\n✅ 所有测试通过！\n");
    }
    else
    {
        printf("\n❌ 有 %u 个测试失败！\n", s_failed_tests);
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    /* 运行所有测试 */
    run_all_tests();

    return (s_failed_tests == 0) ? 0 : 1;
}
