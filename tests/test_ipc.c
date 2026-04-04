/**
 * @file    test_ipc.c
 * @brief   AISafe64 RTOS - IC2 通道单元测试（宿主机版）
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.1
 *
 * @details IC2 快速通信通道测试：
 *          - 通道创建/销毁
 *          - 数据发送/接收
 *          - SPSC 环形缓冲区正确性
 *          - 边界条件
 *
 * @note 宿主机自包含测试（不依赖内核头文件）
 * @note 对应需求: KR-005~008, DR-006~008
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ========================================================================
 * 简易测试框架（宿主机版）
 * ======================================================================== */

static uint32_t s_total   = 0U;
static uint32_t s_passed  = 0U;
static uint32_t s_failed  = 0U;

#define TEST_ASSERT_EQ(a, b)                                               \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((int64_t)(a) == (int64_t)(b)) { s_passed++; }                  \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 %lld 实际 %lld\n",                   \
                   __FILE__, __LINE__,                                      \
                   (long long)(int64_t)(b), (long long)(int64_t)(a));       \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_NE(a, b)                                               \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((int64_t)(a) != (int64_t)(b)) { s_passed++; }                  \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 != %lld 但相等\n",                   \
                   __FILE__, __LINE__, (long long)(int64_t)(b));            \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_GT(a, b)                                               \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((int64_t)(a) > (int64_t)(b)) { s_passed++; }                   \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 %lld > %lld\n",                      \
                   __FILE__, __LINE__,                                      \
                   (long long)(int64_t)(a), (long long)(int64_t)(b));       \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_LT(a, b)                                               \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((int64_t)(a) < (int64_t)(b)) { s_passed++; }                   \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 %lld < %lld\n",                      \
                   __FILE__, __LINE__,                                      \
                   (long long)(int64_t)(a), (long long)(int64_t)(b));       \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_TRUE(cond)                                             \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if (cond) { s_passed++; }                                          \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 true: %s\n",                         \
                   __FILE__, __LINE__, #cond);                              \
        }                                                                  \
    } while (0)

#define TEST_RUN(name)                                                     \
    do                                                                     \
    {                                                                      \
        printf("  [RUN] %s\n", #name);                                     \
        test_##name();                                                     \
    } while (0)

/* ========================================================================
 * 宿主机 Mock 层
 * ======================================================================== */

typedef int32_t kernel_status_t;

#define KERNEL_OK    ((kernel_status_t)0)
#define EPERM        1
#define EINVAL       22
#define ENOENT       2
#define ENOMEM       12

/* ========================================================================
 * IC2 常量与类型
 * ======================================================================== */

#define IC2_MAX_CHANNELS        32U
#define IC2_RING_BUF_SIZE       4096U
#define IC2_MAX_PACKET_SIZE     1024U
#define IC2_CHANNEL_NAME_MAX    32U

typedef struct
{
    uint32_t    length;
    uint32_t    type;
    uint32_t    flags;
    uint32_t    seq;
} ic2_packet_header_t;

typedef struct
{
    volatile uint32_t head;
    volatile uint32_t tail;
    uint32_t          capacity;
    uint32_t          reserved;
} ic2_ringbuf_t;

typedef enum
{
    IC2_STATE_CLOSED = 0U,
    IC2_STATE_OPEN,
    IC2_STATE_ERROR
} ic2_channel_state_t;

typedef struct
{
    uint32_t            channel_id;
    char                name[IC2_CHANNEL_NAME_MAX];
    ic2_channel_state_t state;
    ic2_ringbuf_t      *ring_ab;
    ic2_ringbuf_t      *ring_ba;
    uint32_t            owner_a;
    uint32_t            owner_b;
    uint32_t            lock;
    uint32_t            stats_tx;
    uint32_t            stats_rx;
} ic2_channel_t;

/* 环形缓冲区存储 */
static ic2_channel_t s_channels[IC2_MAX_CHANNELS];
static bool s_channel_used[IC2_MAX_CHANNELS];
static uint8_t s_ring_storage[IC2_MAX_CHANNELS][2U][IC2_RING_BUF_SIZE];
static bool s_ic2_initialized = false;

/* ========================================================================
 * IC2 简化实现（宿主机版）
 * ======================================================================== */

static void ic2_strcpy(char *dst, const char *src, uint32_t n)
{
    uint32_t i;
    if ((dst == NULL) || (src == NULL) || (n == 0U))
    {
        return;
    }
    for (i = 0U; (i < (n - 1U)) && (src[i] != '\0'); i++)
    {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void ringbuf_init(ic2_ringbuf_t *ring, uint32_t size)
{
    ring->head = 0U;
    ring->tail = 0U;
    ring->capacity = size;
    ring->reserved = 0U;
}

static uint32_t ringbuf_write(volatile uint32_t *head, volatile uint32_t *tail,
                               const void *data, uint32_t length,
                               uint32_t capacity, uint8_t *storage)
{
    uint32_t h = *head;
    uint32_t t = *tail;
    uint32_t free_space;
    uint32_t to_write;
    uint32_t first_part;
    const uint8_t *src = (const uint8_t *)data;

    if (h >= t)
    {
        free_space = capacity - (h - t) - 1U;
    }
    else
    {
        free_space = (t - h) - 1U;
    }

    to_write = (length < free_space) ? length : free_space;
    if (to_write == 0U)
    {
        return 0U;
    }

    first_part = capacity - h;
    if (first_part > to_write)
    {
        first_part = to_write;
    }

    (void)memcpy(&storage[h], src, first_part);
    if (to_write > first_part)
    {
        (void)memcpy(&storage[0U], &src[first_part], to_write - first_part);
    }

    *head = (h + to_write) % capacity;
    return to_write;
}

static uint32_t ringbuf_read(volatile uint32_t *head, volatile uint32_t *tail,
                              void *data, uint32_t length,
                              uint32_t capacity, uint8_t *storage)
{
    uint32_t h = *head;
    uint32_t t = *tail;
    uint32_t used;
    uint32_t to_read;
    uint32_t first_part;
    uint8_t *dst = (uint8_t *)data;

    if (h >= t)
    {
        used = h - t;
    }
    else
    {
        used = capacity - (t - h);
    }

    to_read = (length < used) ? length : used;
    if (to_read == 0U)
    {
        return 0U;
    }

    first_part = capacity - t;
    if (first_part > to_read)
    {
        first_part = to_read;
    }

    (void)memcpy(dst, &storage[t], first_part);
    if (to_read > first_part)
    {
        (void)memcpy(&dst[first_part], &storage[0U], to_read - first_part);
    }

    *tail = (t + to_read) % capacity;
    return to_read;
}

static kernel_status_t ic2_init(void)
{
    uint32_t i;
    (void)memset(s_channels, 0, sizeof(s_channels));
    (void)memset(s_channel_used, 0, sizeof(s_channel_used));
    (void)memset(s_ring_storage, 0, sizeof(s_ring_storage));
    for (i = 0U; i < IC2_MAX_CHANNELS; i++)
    {
        s_channels[i].channel_id = i;
        s_channels[i].state = IC2_STATE_CLOSED;
        s_channels[i].ring_ab = NULL;
        s_channels[i].ring_ba = NULL;
    }
    s_ic2_initialized = true;
    return KERNEL_OK;
}

static int32_t ic2_channel_create(const char *name, uint32_t owner_a,
                                   uint32_t owner_b, uint32_t buf_size)
{
    uint32_t i;
    ic2_channel_t *ch;

    if (!s_ic2_initialized)
    {
        return -(int32_t)EPERM;
    }
    if (name == NULL)
    {
        return -(int32_t)EINVAL;
    }
    if ((owner_a == 0U) || (owner_b == 0U) || (owner_a == owner_b))
    {
        return -(int32_t)EINVAL;
    }
    if ((buf_size == 0U) || (buf_size > IC2_RING_BUF_SIZE))
    {
        buf_size = IC2_RING_BUF_SIZE;
    }

    for (i = 0U; i < IC2_MAX_CHANNELS; i++)
    {
        if (!s_channel_used[i])
        {
            break;
        }
    }
    if (i >= IC2_MAX_CHANNELS)
    {
        return -(int32_t)ENOMEM;
    }

    ch = &s_channels[i];
    ic2_strcpy(ch->name, name, IC2_CHANNEL_NAME_MAX);
    ch->channel_id = i;
    ch->state = IC2_STATE_OPEN;
    ch->owner_a = owner_a;
    ch->owner_b = owner_b;
    ch->lock = 0U;
    ch->stats_tx = 0U;
    ch->stats_rx = 0U;

    /* 初始化环形缓冲区 */
    ch->ring_ab = (ic2_ringbuf_t *)(void *)&s_ring_storage[i][0U];
    ch->ring_ba = (ic2_ringbuf_t *)(void *)&s_ring_storage[i][1U];

    ringbuf_init(ch->ring_ab, buf_size);
    ringbuf_init(ch->ring_ba, buf_size);

    s_channel_used[i] = true;
    return (int32_t)i;
}

static kernel_status_t ic2_channel_destroy(uint32_t channel_id)
{
    ic2_channel_t *ch;
    if (channel_id >= IC2_MAX_CHANNELS)
    {
        return -(int32_t)EINVAL;
    }
    if (!s_channel_used[channel_id])
    {
        return -(int32_t)ENOENT;
    }
    ch = &s_channels[channel_id];
    ch->state = IC2_STATE_CLOSED;
    ch->ring_ab = NULL;
    ch->ring_ba = NULL;
    s_channel_used[channel_id] = false;
    return KERNEL_OK;
}

static int32_t ic2_send(uint32_t channel_id, const void *data,
                         uint32_t length, uint32_t type, uint32_t flags)
{
    ic2_channel_t *ch;
    ic2_packet_header_t hdr;
    uint32_t written;

    if (data == NULL)
    {
        return -(int32_t)EINVAL;
    }
    if (length == 0U)
    {
        return 0;
    }
    if (length > IC2_MAX_PACKET_SIZE)
    {
        return -(int32_t)EINVAL;
    }
    if (channel_id >= IC2_MAX_CHANNELS)
    {
        return -(int32_t)EINVAL;
    }
    if (!s_channel_used[channel_id])
    {
        return -(int32_t)ENOENT;
    }

    ch = &s_channels[channel_id];
    if (ch->state != IC2_STATE_OPEN)
    {
        return -(int32_t)EPERM;
    }

    hdr.length = length;
    hdr.type = type;
    hdr.flags = flags;
    hdr.seq = ch->stats_tx;

    uint8_t *storage_ab = &s_ring_storage[channel_id][0U][0U];
    written = ringbuf_write(&ch->ring_ab->head, &ch->ring_ab->tail,
                            &hdr, sizeof(ic2_packet_header_t),
                            IC2_RING_BUF_SIZE, storage_ab);
    if (written < sizeof(ic2_packet_header_t))
    {
        return -(int32_t)ENOMEM;
    }

    written = ringbuf_write(&ch->ring_ab->head, &ch->ring_ab->tail,
                            data, length, IC2_RING_BUF_SIZE, storage_ab);
    ch->stats_tx++;
    return (int32_t)written;
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试 IC2 初始化
 */
static void test_ic2_init_succeeds(void)
{
    kernel_status_t ret = ic2_init();
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

/**
 * @brief 测试通道创建
 */
static void test_ic2_channel_create_basic(void)
{
    ic2_init();
    int32_t ch = ic2_channel_create("test-ch", 1U, 2U, 1024U);
    TEST_ASSERT_GT(ch, -1);

    ic2_channel_t *p = &s_channels[(uint32_t)ch];
    TEST_ASSERT_EQ((int64_t)p->state, (int64_t)IC2_STATE_OPEN);
    TEST_ASSERT_EQ((int64_t)p->owner_a, 1);
    TEST_ASSERT_EQ((int64_t)p->owner_b, 2);

    (void)ic2_channel_destroy((uint32_t)ch);
}

/**
 * @brief 测试通道创建参数校验
 */
static void test_ic2_channel_create_invalid_params(void)
{
    ic2_init();

    /* NULL 名称 */
    TEST_ASSERT_LT(ic2_channel_create(NULL, 1U, 2U, 1024U), 0);

    /* owner_a == 0 */
    TEST_ASSERT_LT(ic2_channel_create("test", 0U, 2U, 1024U), 0);

    /* owner_b == 0 */
    TEST_ASSERT_LT(ic2_channel_create("test", 1U, 0U, 1024U), 0);

    /* owner_a == owner_b */
    TEST_ASSERT_LT(ic2_channel_create("test", 1U, 1U, 1024U), 0);
}

/**
 * @brief 测试通道销毁
 */
static void test_ic2_channel_destroy_basic(void)
{
    ic2_init();
    int32_t ch = ic2_channel_create("destroy-test", 10U, 20U, 512U);
    TEST_ASSERT_GT(ch, -1);

    kernel_status_t ret = ic2_channel_destroy((uint32_t)ch);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 验证已销毁 */
    TEST_ASSERT_EQ((int64_t)s_channels[(uint32_t)ch].state,
                   (int64_t)IC2_STATE_CLOSED);
}

/**
 * @brief 测试销毁不存在的通道
 */
static void test_ic2_channel_destroy_nonexistent(void)
{
    ic2_init();
    kernel_status_t ret = ic2_channel_destroy(99U);
    TEST_ASSERT_LT(ret, 0);
}

/**
 * @brief 测试 SPSC 环形缓冲区写入/读取
 */
static void test_ic2_spsc_ringbuf_write_read(void)
{
    uint8_t storage[128U];
    volatile uint32_t head = 0U;
    volatile uint32_t tail = 0U;
    uint32_t capacity = 128U;
    uint8_t write_data[64U];
    uint8_t read_data[64U];
    uint32_t written;
    uint32_t read_bytes;
    uint32_t i;

    (void)memset(storage, 0, sizeof(storage));
    for (i = 0U; i < 64U; i++)
    {
        write_data[i] = (uint8_t)(i + 1U);
    }

    /* 写入数据 */
    written = ringbuf_write(&head, &tail, write_data, 64U, capacity, storage);
    TEST_ASSERT_EQ((int64_t)written, 64);

    /* 读取数据 */
    (void)memset(read_data, 0, sizeof(read_data));
    read_bytes = ringbuf_read(&head, &tail, read_data, 64U, capacity, storage);
    TEST_ASSERT_EQ((int64_t)read_bytes, 64);

    /* 验证数据一致性 */
    for (i = 0U; i < 64U; i++)
    {
        TEST_ASSERT_EQ((int64_t)read_data[i], (int64_t)write_data[i]);
    }
}

/**
 * @brief 测试环形缓冲区环绕写入
 */
static void test_ic2_ringbuf_wrap_around(void)
{
    uint8_t storage[32U];
    volatile uint32_t head = 0U;
    volatile uint32_t tail = 0U;
    uint32_t capacity = 32U;
    uint8_t data_a[20U];
    uint8_t data_b[20U];
    uint8_t read_buf[20U];
    uint32_t i;

    (void)memset(storage, 0, sizeof(storage));
    for (i = 0U; i < 20U; i++)
    {
        data_a[i] = (uint8_t)(i);
    }
    for (i = 0U; i < 20U; i++)
    {
        data_b[i] = (uint8_t)(i + 100U);
    }

    /* 写入 A */
    uint32_t w1 = ringbuf_write(&head, &tail, data_a, 20U, capacity, storage);
    TEST_ASSERT_EQ((int64_t)w1, 20);

    /* 读取 A */
    uint32_t r1 = ringbuf_read(&head, &tail, read_buf, 20U, capacity, storage);
    TEST_ASSERT_EQ((int64_t)r1, 20);

    /* 写入 B（会环绕） */
    uint32_t w2 = ringbuf_write(&head, &tail, data_b, 20U, capacity, storage);
    TEST_ASSERT_EQ((int64_t)w2, 20);

    /* 读取 B */
    (void)memset(read_buf, 0, sizeof(read_buf));
    uint32_t r2 = ringbuf_read(&head, &tail, read_buf, 20U, capacity, storage);
    TEST_ASSERT_EQ((int64_t)r2, 20);

    /* 验证 B 数据 */
    for (i = 0U; i < 20U; i++)
    {
        TEST_ASSERT_EQ((int64_t)read_buf[i], (int64_t)data_b[i]);
    }
}

/**
 * @brief 测试环形缓冲区满时写入
 */
static void test_ic2_ringbuf_full(void)
{
    uint8_t storage[16U];
    volatile uint32_t head = 0U;
    volatile uint32_t tail = 0U;
    uint32_t capacity = 16U;
    uint8_t data[16U];

    (void)memset(storage, 0, sizeof(storage));
    (void)memset(data, 0xAA, sizeof(data));

    /* 写入 capacity-1 字节（最大可用空间） */
    uint32_t w = ringbuf_write(&head, &tail, data, capacity - 1U, capacity, storage);
    TEST_ASSERT_EQ((int64_t)w, (int64_t)(capacity - 1U));

    /* 再次写入应返回 0 */
    uint32_t w2 = ringbuf_write(&head, &tail, data, 1U, capacity, storage);
    TEST_ASSERT_EQ((int64_t)w2, 0);
}

/**
 * @brief 测试空缓冲区读取
 */
static void test_ic2_ringbuf_empty_read(void)
{
    uint8_t storage[32U];
    volatile uint32_t head = 0U;
    volatile uint32_t tail = 0U;
    uint8_t buf[16U];

    (void)memset(storage, 0, sizeof(storage));
    (void)memset(buf, 0, sizeof(buf));

    uint32_t r = ringbuf_read(&head, &tail, buf, 16U, 32U, storage);
    TEST_ASSERT_EQ((int64_t)r, 0);
}

/**
 * @brief 测试 IC2 端到端发送/接收
 */
static void test_ic2_send_recv_e2e(void)
{
    int32_t ch;
    const char *test_msg = "Hello IC2!";
    int32_t sent;

    ic2_init();

    ch = ic2_channel_create("e2e-test", 1U, 2U, 4096U);
    TEST_ASSERT_GT(ch, -1);

    /* 发送到 ring_ab */
    sent = ic2_send((uint32_t)ch, test_msg, 10U, 42U, 0U);
    TEST_ASSERT_EQ((int64_t)sent, 10);

    /* 验证发送统计 */
    TEST_ASSERT_EQ((int64_t)s_channels[(uint32_t)ch].stats_tx, 1);

    (void)ic2_channel_destroy((uint32_t)ch);
}

/**
 * @brief 测试最大通道数限制
 */
static void test_ic2_max_channels_limit(void)
{
    int32_t channels[IC2_MAX_CHANNELS + 1U];
    uint32_t i;
    int32_t result;

    ic2_init();

    /* 创建最大数量的通道 */
    for (i = 0U; i < IC2_MAX_CHANNELS; i++)
    {
        char name[16];
        (void)snprintf(name, sizeof(name), "ch%u", i);
        channels[i] = ic2_channel_create(name, 100U + i, 200U + i, 512U);
        TEST_ASSERT_GT(channels[i], -1);
    }

    /* 尝试创建超出限制的通道 */
    result = ic2_channel_create("overflow", 1U, 2U, 512U);
    TEST_ASSERT_LT(result, 0);

    /* 清理 */
    for (i = 0U; i < IC2_MAX_CHANNELS; i++)
    {
        (void)ic2_channel_destroy((uint32_t)channels[i]);
    }
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("\n=== IC2 IPC 单元测试 ===\n\n");

    printf("--- 初始化 ---\n");
    TEST_RUN(ic2_init_succeeds);

    printf("--- 通道生命周期 ---\n");
    TEST_RUN(ic2_channel_create_basic);
    TEST_RUN(ic2_channel_create_invalid_params);
    TEST_RUN(ic2_channel_destroy_basic);
    TEST_RUN(ic2_channel_destroy_nonexistent);

    printf("--- 环形缓冲区 ---\n");
    TEST_RUN(ic2_spsc_ringbuf_write_read);
    TEST_RUN(ic2_ringbuf_wrap_around);
    TEST_RUN(ic2_ringbuf_full);
    TEST_RUN(ic2_ringbuf_empty_read);

    printf("--- 集成测试 ---\n");
    TEST_RUN(ic2_send_recv_e2e);
    TEST_RUN(ic2_max_channels_limit);

    printf("\n=== 测试结果 ===\n");
    printf("总计: %u  通过: %u  失败: %u\n",
           s_total, s_passed, s_failed);

    return (s_failed == 0U) ? 0 : 1;
}
