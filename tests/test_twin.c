/**
 * @file    test_twin.c
 * @brief   AISafe64 RTOS - 双生驱动框架单元测试（宿主机版）
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 1.0
 *
 * @details 双生驱动框架测试：
 *          - 双生对创建/销毁
 *          - 控制面/数据面启动停止
 *          - 故障处理与降级模式
 *          - 自动恢复
 *
 * @note 宿主机自包含测试（不依赖内核头文件）
 * @note 对应需求: DR-006~008
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * 简易测试框架（内联版）
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
 * 双生驱动 Mock 实现
 * ======================================================================== */

typedef int32_t kernel_status_t;
#define KERNEL_OK  ((kernel_status_t)0)
#define EINVAL     22
#define ENOENT     2
#define ENOMEM     12

#define TWIN_MAX_PAIRS     8U
#define TWIN_NAME_MAX      32U

typedef enum
{
    TWIN_STATE_IDLE         = 0U,
    TWIN_STATE_INITIALIZING = 1U,
    TWIN_STATE_ACTIVE       = 2U,
    TWIN_STATE_CTRL_DOWN    = 3U,
    TWIN_STATE_DEGRADED     = 4U,
    TWIN_STATE_STOPPED      = 5U
} twin_state_t;

typedef struct
{
    uint32_t ctrl_send_count;
    uint32_t ctrl_recv_count;
    uint32_t data_io_count;
    uint32_t failover_count;
    uint32_t recovery_count;
} twin_stats_t;

typedef struct
{
    uint32_t      pair_id;
    char          name[TWIN_NAME_MAX];
    twin_state_t  state;
    uint32_t      ctrl_plane_id;
    uint32_t      data_plane_id;
    int32_t       ctrl_channel;
    int32_t       data_channel;
    bool          auto_recovery;
    uint32_t      failover_timeout_ms;
    twin_stats_t  stats;
} twin_pair_t;

/* 全局状态 */
static twin_pair_t s_pairs[TWIN_MAX_PAIRS];
static bool s_pair_used[TWIN_MAX_PAIRS];
static bool s_initialized = false;

/* 模拟 IC2 通道（简化） */
static int32_t s_next_channel = 0;

static int32_t mock_ic2_create(const char *name, uint32_t a, uint32_t b)
{
    (void)name;
    (void)a;
    (void)b;
    return s_next_channel++;
}

static void mock_ic2_destroy(int32_t ch)
{
    (void)ch;
}

static kernel_status_t twin_init(void)
{
    uint32_t i;
    (void)memset(s_pairs, 0, sizeof(s_pairs));
    (void)memset(s_pair_used, 0, sizeof(s_pair_used));
    for (i = 0U; i < TWIN_MAX_PAIRS; i++)
    {
        s_pairs[i].pair_id = i;
        s_pairs[i].state = TWIN_STATE_IDLE;
        s_pairs[i].ctrl_channel = -1;
        s_pairs[i].data_channel = -1;
    }
    s_next_channel = 0;
    s_initialized = true;
    return KERNEL_OK;
}

static void twin_strcpy(char *dst, const char *src, uint32_t n)
{
    uint32_t i;
    if ((dst == NULL) || (src == NULL) || (n == 0U)) { return; }
    for (i = 0U; (i < (n - 1U)) && (src[i] != '\0'); i++) { dst[i] = src[i]; }
    dst[i] = '\0';
}

static int32_t twin_create(const char *name, uint32_t ctrl_id,
                             uint32_t data_id, bool auto_recovery)
{
    uint32_t i;
    twin_pair_t *pair;

    if (!s_initialized) { return -(int32_t)EINVAL; }
    if (name == NULL) { return -(int32_t)EINVAL; }
    if ((ctrl_id == 0U) || (data_id == 0U)) { return -(int32_t)EINVAL; }

    for (i = 0U; i < TWIN_MAX_PAIRS; i++)
    {
        if (!s_pair_used[i]) { break; }
    }
    if (i >= TWIN_MAX_PAIRS) { return -(int32_t)ENOMEM; }

    pair = &s_pairs[i];
    twin_strcpy(pair->name, name, TWIN_NAME_MAX);
    pair->pair_id = i;
    pair->state = TWIN_STATE_IDLE;
    pair->ctrl_plane_id = ctrl_id;
    pair->data_plane_id = data_id;
    pair->ctrl_channel = -1;
    pair->data_channel = -1;
    pair->auto_recovery = auto_recovery;
    pair->failover_timeout_ms = 1000U;
    (void)memset(&pair->stats, 0, sizeof(twin_stats_t));

    s_pair_used[i] = true;
    return (int32_t)i;
}

static kernel_status_t twin_destroy(uint32_t pair_id)
{
    twin_pair_t *pair;
    if (pair_id >= TWIN_MAX_PAIRS) { return -(int32_t)EINVAL; }
    if (!s_pair_used[pair_id]) { return -(int32_t)ENOENT; }

    pair = &s_pairs[pair_id];
    if (pair->ctrl_channel >= 0) { mock_ic2_destroy(pair->ctrl_channel); }
    if (pair->data_channel >= 0) { mock_ic2_destroy(pair->data_channel); }

    pair->state = TWIN_STATE_STOPPED;
    pair->ctrl_channel = -1;
    pair->data_channel = -1;
    s_pair_used[pair_id] = false;
    return KERNEL_OK;
}

static kernel_status_t twin_start(uint32_t pair_id)
{
    twin_pair_t *pair;
    if (pair_id >= TWIN_MAX_PAIRS) { return -(int32_t)EINVAL; }
    if (!s_pair_used[pair_id]) { return -(int32_t)ENOENT; }

    pair = &s_pairs[pair_id];
    if (pair->state != TWIN_STATE_IDLE) { return -(int32_t)EINVAL; }

    pair->state = TWIN_STATE_INITIALIZING;

    pair->ctrl_channel = mock_ic2_create("twin-ctrl",
        pair->data_plane_id, pair->ctrl_plane_id);
    pair->data_channel = mock_ic2_create("twin-data",
        pair->ctrl_plane_id, pair->data_plane_id);

    if ((pair->ctrl_channel < 0) || (pair->data_channel < 0))
    {
        pair->state = TWIN_STATE_CTRL_DOWN;
        return -(int32_t)ENOMEM;
    }

    pair->state = TWIN_STATE_ACTIVE;
    return KERNEL_OK;
}

static kernel_status_t twin_stop(uint32_t pair_id)
{
    twin_pair_t *pair;
    if (pair_id >= TWIN_MAX_PAIRS) { return -(int32_t)EINVAL; }
    if (!s_pair_used[pair_id]) { return -(int32_t)ENOENT; }

    pair = &s_pairs[pair_id];
    if (pair->ctrl_channel >= 0) { mock_ic2_destroy(pair->ctrl_channel); }
    if (pair->data_channel >= 0) { mock_ic2_destroy(pair->data_channel); }

    pair->ctrl_channel = -1;
    pair->data_channel = -1;
    pair->state = TWIN_STATE_STOPPED;
    return KERNEL_OK;
}

static kernel_status_t twin_handle_ctrl_failure(uint32_t pair_id)
{
    twin_pair_t *pair;
    if (pair_id >= TWIN_MAX_PAIRS) { return -(int32_t)EINVAL; }
    pair = &s_pairs[pair_id];

    if (pair->ctrl_channel >= 0) { mock_ic2_destroy(pair->ctrl_channel); }
    pair->ctrl_channel = -1;

    if (pair->data_channel >= 0) { mock_ic2_destroy(pair->data_channel); }
    pair->data_channel = -1;

    pair->state = TWIN_STATE_DEGRADED;
    pair->stats.failover_count++;
    return KERNEL_OK;
}

static kernel_status_t twin_recover_ctrl(uint32_t pair_id)
{
    twin_pair_t *pair;
    if (pair_id >= TWIN_MAX_PAIRS) { return -(int32_t)EINVAL; }
    pair = &s_pairs[pair_id];

    if (pair->state != TWIN_STATE_DEGRADED) { return -(int32_t)EINVAL; }

    pair->ctrl_channel = mock_ic2_create("twin-ctrl-recov",
        pair->data_plane_id, pair->ctrl_plane_id);
    pair->data_channel = mock_ic2_create("twin-data-recov",
        pair->ctrl_plane_id, pair->data_plane_id);

    if ((pair->ctrl_channel < 0) || (pair->data_channel < 0))
    {
        return -(int32_t)ENOMEM;
    }

    pair->state = TWIN_STATE_ACTIVE;
    pair->stats.recovery_count++;
    return KERNEL_OK;
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

void test_twin_init_succeeds(void)
{
    kernel_status_t ret = twin_init();
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_TRUE(s_initialized);
}

void test_twin_create_basic(void)
{
    twin_init();
    int32_t pid = twin_create("eth0-twin", 1U, 2U, true);
    TEST_ASSERT_GT(pid, -1);

    twin_pair_t *p = &s_pairs[(uint32_t)pid];
    TEST_ASSERT_EQ((int64_t)p->state, (int64_t)TWIN_STATE_IDLE);
    TEST_ASSERT_EQ((int64_t)p->ctrl_plane_id, 1);
    TEST_ASSERT_EQ((int64_t)p->data_plane_id, 2);
    TEST_ASSERT_TRUE(p->auto_recovery);
}

void test_twin_create_null_name(void)
{
    twin_init();
    TEST_ASSERT_LT(twin_create(NULL, 1U, 2U, true), 0);
}

void test_twin_create_zero_plane(void)
{
    twin_init();
    TEST_ASSERT_LT(twin_create("test", 0U, 2U, true), 0);
    TEST_ASSERT_LT(twin_create("test", 1U, 0U, true), 0);
}

void test_twin_destroy_basic(void)
{
    twin_init();
    int32_t pid = twin_create("destroy-test", 10U, 20U, false);
    TEST_ASSERT_GT(pid, -1);

    kernel_status_t ret = twin_destroy((uint32_t)pid);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_TRUE(!s_pair_used[(uint32_t)pid]);
}

void test_twin_destroy_nonexistent(void)
{
    twin_init();
    TEST_ASSERT_LT(twin_destroy(99U), 0);
}

void test_twin_start_basic(void)
{
    twin_init();
    int32_t pid = twin_create("start-test", 1U, 2U, true);
    TEST_ASSERT_GT(pid, -1);

    kernel_status_t ret = twin_start((uint32_t)pid);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    twin_pair_t *p = &s_pairs[(uint32_t)pid];
    TEST_ASSERT_EQ((int64_t)p->state, (int64_t)TWIN_STATE_ACTIVE);
    TEST_ASSERT_GT(p->ctrl_channel, -1);
    TEST_ASSERT_GT(p->data_channel, -1);

    (void)twin_stop((uint32_t)pid);
}

void test_twin_start_creates_channels(void)
{
    twin_init();
    int32_t pid = twin_create("ch-test", 1U, 2U, true);
    (void)twin_start((uint32_t)pid);

    twin_pair_t *p = &s_pairs[(uint32_t)pid];
    TEST_ASSERT_GT(p->ctrl_channel, -1);
    TEST_ASSERT_GT(p->data_channel, -1);

    (void)twin_stop((uint32_t)pid);
}

void test_twin_stop_basic(void)
{
    twin_init();
    int32_t pid = twin_create("stop-test", 1U, 2U, true);
    (void)twin_start((uint32_t)pid);

    kernel_status_t ret = twin_stop((uint32_t)pid);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    twin_pair_t *p = &s_pairs[(uint32_t)pid];
    TEST_ASSERT_EQ((int64_t)p->state, (int64_t)TWIN_STATE_STOPPED);
    TEST_ASSERT_EQ((int64_t)p->ctrl_channel, -1);
    TEST_ASSERT_EQ((int64_t)p->data_channel, -1);
}

void test_twin_ctrl_failure(void)
{
    twin_init();
    int32_t pid = twin_create("fail-test", 1U, 2U, true);
    (void)twin_start((uint32_t)pid);

    kernel_status_t ret = twin_handle_ctrl_failure((uint32_t)pid);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    twin_pair_t *p = &s_pairs[(uint32_t)pid];
    TEST_ASSERT_EQ((int64_t)p->state, (int64_t)TWIN_STATE_DEGRADED);
    TEST_ASSERT_EQ((int64_t)p->stats.failover_count, 1);

    (void)twin_stop((uint32_t)pid);
}

void test_twin_recovery(void)
{
    twin_init();
    int32_t pid = twin_create("recov-test", 1U, 2U, true);
    (void)twin_start((uint32_t)pid);
    (void)twin_handle_ctrl_failure((uint32_t)pid);

    TEST_ASSERT_EQ((int64_t)s_pairs[(uint32_t)pid].state,
                   (int64_t)TWIN_STATE_DEGRADED);

    kernel_status_t ret = twin_recover_ctrl((uint32_t)pid);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    twin_pair_t *p = &s_pairs[(uint32_t)pid];
    TEST_ASSERT_EQ((int64_t)p->state, (int64_t)TWIN_STATE_ACTIVE);
    TEST_ASSERT_GT(p->ctrl_channel, -1);
    TEST_ASSERT_EQ((int64_t)p->stats.recovery_count, 1);

    (void)twin_stop((uint32_t)pid);
}

void test_twin_recovery_not_degraded(void)
{
    twin_init();
    int32_t pid = twin_create("recov-idle", 1U, 2U, true);
    /* 不在 DEGRADED 状态，恢复应失败 */
    TEST_ASSERT_LT(twin_recover_ctrl((uint32_t)pid), 0);
}

void test_twin_max_pairs(void)
{
    uint32_t i;
    int32_t result;
    twin_init();

    for (i = 0U; i < TWIN_MAX_PAIRS; i++)
    {
        char name[16];
        (void)snprintf(name, sizeof(name), "pair%u", i);
        int32_t pid = twin_create(name, 100U + i, 200U + i, true);
        TEST_ASSERT_GT(pid, -1);
    }

    /* 超出限制 */
    result = twin_create("overflow", 1U, 2U, true);
    TEST_ASSERT_LT(result, 0);

    /* 清理 */
    for (i = 0U; i < TWIN_MAX_PAIRS; i++)
    {
        (void)twin_destroy(i);
    }
}

void test_twin_lifecycle_full(void)
{
    twin_init();
    int32_t pid = twin_create("lifecycle", 1U, 2U, true);
    TEST_ASSERT_GT(pid, -1);

    /* IDLE -> ACTIVE */
    TEST_ASSERT_EQ(twin_start((uint32_t)pid), KERNEL_OK);
    TEST_ASSERT_EQ((int64_t)s_pairs[(uint32_t)pid].state,
                   (int64_t)TWIN_STATE_ACTIVE);

    /* ACTIVE -> DEGRADED */
    TEST_ASSERT_EQ(twin_handle_ctrl_failure((uint32_t)pid), KERNEL_OK);
    TEST_ASSERT_EQ((int64_t)s_pairs[(uint32_t)pid].state,
                   (int64_t)TWIN_STATE_DEGRADED);

    /* DEGRADED -> ACTIVE (恢复) */
    TEST_ASSERT_EQ(twin_recover_ctrl((uint32_t)pid), KERNEL_OK);
    TEST_ASSERT_EQ((int64_t)s_pairs[(uint32_t)pid].state,
                   (int64_t)TWIN_STATE_ACTIVE);

    /* ACTIVE -> STOPPED */
    TEST_ASSERT_EQ(twin_stop((uint32_t)pid), KERNEL_OK);
    TEST_ASSERT_EQ((int64_t)s_pairs[(uint32_t)pid].state,
                   (int64_t)TWIN_STATE_STOPPED);

    /* 统计检查 */
    TEST_ASSERT_EQ((int64_t)s_pairs[(uint32_t)pid].stats.failover_count, 1);
    TEST_ASSERT_EQ((int64_t)s_pairs[(uint32_t)pid].stats.recovery_count, 1);
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("\n=== 双生驱动框架 单元测试 ===\n\n");

    printf("--- 初始化 ---\n");
    TEST_RUN(twin_init_succeeds);

    printf("--- 创建/销毁 ---\n");
    TEST_RUN(twin_create_basic);
    TEST_RUN(twin_create_null_name);
    TEST_RUN(twin_create_zero_plane);
    TEST_RUN(twin_destroy_basic);
    TEST_RUN(twin_destroy_nonexistent);

    printf("--- 启动/停止 ---\n");
    TEST_RUN(twin_start_basic);
    TEST_RUN(twin_start_creates_channels);
    TEST_RUN(twin_stop_basic);

    printf("--- 故障/恢复 ---\n");
    TEST_RUN(twin_ctrl_failure);
    TEST_RUN(twin_recovery);
    TEST_RUN(twin_recovery_not_degraded);

    printf("--- 边界条件 ---\n");
    TEST_RUN(twin_max_pairs);
    TEST_RUN(twin_lifecycle_full);

    printf("\n=== 测试结果 ===\n");
    printf("总计: %u  通过: %u  失败: %u\n",
           s_total, s_passed, s_failed);

    return (s_failed == 0U) ? 0 : 1;
}
