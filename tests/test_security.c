/**
 * @file    test_security.c
 * @brief   AISafe64 RTOS - 安全子系统单元测试（宿主机版）
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 1.0
 *
 * @details 安全子系统测试：
 *          - 安全状态管理
 *          - 安全事件审计日志
 *          - SHA-256 完整性校验框架
 *          - 页面访问权限检查
 *          - 安全状态转换
 *
 * @note 宿主机自包含测试（不依赖内核头文件）
 * @note 对应需求: SE-006~009
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
 * 安全子系统 Mock 实现
 * ======================================================================== */

typedef int32_t kernel_status_t;
#define KERNEL_OK  ((kernel_status_t)0)

typedef enum
{
    SECURITY_STATE_NORMAL   = 0U,
    SECURITY_STATE_DEGRADED = 1U,
    SECURITY_STATE_ALERT    = 2U,
    SECURITY_STATE_PANIC    = 3U
} security_state_t;

typedef enum
{
    SECURITY_EVENT_NONE                = 0U,
    SECURITY_EVENT_STACK_OVERFLOW      = 1U,
    SECURITY_EVENT_KERNEL_RO_VIOLATION = 2U,
    SECURITY_EVENT_INVALID_PAGE_ACCESS = 3U,
    SECURITY_EVENT_CAPABILITY_VIOLATION = 4U,
    SECURITY_EVENT_DOUBLE_FREE         = 5U
} security_event_t;

typedef struct
{
    uint32_t total_events;
    uint32_t stack_overflows;
    uint32_t page_faults;
    uint32_t capability_violations;
    uint32_t double_frees;
} security_stats_t;

#define SECURITY_AUDIT_LOG_MAX  64U
#define SECURITY_SHA256_SIZE    32U

typedef struct
{
    uint64_t          timestamp;
    security_event_t  event;
    uint32_t          detail;
} audit_entry_t;

/* 全局状态 */
static security_state_t s_state;
static security_stats_t s_stats;
static audit_entry_t    s_audit_log[SECURITY_AUDIT_LOG_MAX];
static uint32_t         s_audit_head;
static uint32_t         s_audit_tail;
static uint32_t         s_audit_count;
static bool             s_initialized;

/* SHA-256 简化实现 */
static const uint32_t s_sha256_init[8U] =
{
    0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
    0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U
};

static const uint32_t s_sha256_k[16U] =
{
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
    0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
    0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
    0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U
};

static kernel_status_t sha256_compute(const void *data, uint32_t len,
                                        uint8_t hash[SECURITY_SHA256_SIZE])
{
    const uint8_t *msg;
    uint32_t h[8U];
    uint32_t i;

    if ((data == NULL) || (hash == NULL))
    {
        return -22;
    }

    msg = (const uint8_t *)data;
    for (i = 0U; i < 8U; i++)
    {
        h[i] = s_sha256_init[i];
    }

    for (i = 0U; i < len; i++)
    {
        uint32_t idx = i % 8U;
        uint32_t shift = (i % 4U) * 8U;
        h[idx] = h[idx] ^ ((uint32_t)msg[i] << shift);
        h[idx] = h[idx] ^ s_sha256_k[i % 16U];
    }

    for (i = 0U; i < 8U; i++)
    {
        hash[i * 4U + 0U] = (uint8_t)(h[i] >> 24U);
        hash[i * 4U + 1U] = (uint8_t)(h[i] >> 16U);
        hash[i * 4U + 2U] = (uint8_t)(h[i] >> 8U);
        hash[i * 4U + 3U] = (uint8_t)(h[i]);
    }

    return KERNEL_OK;
}

static kernel_status_t verify_integrity(const void *data, uint32_t len,
                                          const uint8_t expected[SECURITY_SHA256_SIZE])
{
    uint8_t actual[SECURITY_SHA256_SIZE];
    uint32_t i;
    kernel_status_t ret;
    uint32_t diff;

    if ((data == NULL) || (expected == NULL))
    {
        return -22;
    }

    ret = sha256_compute(data, len, actual);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    diff = 0U;
    for (i = 0U; i < SECURITY_SHA256_SIZE; i++)
    {
        diff |= (uint32_t)(actual[i] ^ expected[i]);
    }

    if (diff != 0U)
    {
        return -1;
    }

    return KERNEL_OK;
}

static kernel_status_t security_subsys_init(void)
{
    s_state = SECURITY_STATE_NORMAL;
    (void)memset(&s_stats, 0, sizeof(security_stats_t));
    (void)memset(s_audit_log, 0, sizeof(s_audit_log));
    s_audit_head = 0U;
    s_audit_tail = 0U;
    s_audit_count = 0U;
    s_initialized = true;
    return KERNEL_OK;
}

static void security_report_event(security_event_t event, uint32_t detail)
{
    uint32_t idx;
    audit_entry_t *entry;

    if (!s_initialized)
    {
        return;
    }

    s_stats.total_events++;

    switch (event)
    {
        case SECURITY_EVENT_STACK_OVERFLOW:
            s_stats.stack_overflows++;
            break;
        case SECURITY_EVENT_KERNEL_RO_VIOLATION:
        case SECURITY_EVENT_INVALID_PAGE_ACCESS:
            s_stats.page_faults++;
            break;
        case SECURITY_EVENT_CAPABILITY_VIOLATION:
            s_stats.capability_violations++;
            break;
        case SECURITY_EVENT_DOUBLE_FREE:
            s_stats.double_frees++;
            break;
        default:
            break;
    }

    idx = s_audit_head % SECURITY_AUDIT_LOG_MAX;
    entry = &s_audit_log[idx];
    entry->timestamp = 0ULL;
    entry->event = event;
    entry->detail = detail;
    s_audit_head++;

    if (s_audit_count < SECURITY_AUDIT_LOG_MAX)
    {
        s_audit_count++;
    }
    else
    {
        s_audit_tail++;
    }

    if ((event == SECURITY_EVENT_KERNEL_RO_VIOLATION) ||
        (event == SECURITY_EVENT_DOUBLE_FREE))
    {
        if (s_state == SECURITY_STATE_NORMAL)
        {
            s_state = SECURITY_STATE_DEGRADED;
        }
    }
}

static security_state_t security_get_state(void)
{
    return s_state;
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

void test_security_init_succeeds(void)
{
    kernel_status_t ret = security_subsys_init();
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ((int64_t)security_get_state(), (int64_t)SECURITY_STATE_NORMAL);
}

void test_security_event_counting(void)
{
    security_subsys_init();

    security_report_event(SECURITY_EVENT_STACK_OVERFLOW, 0U);
    security_report_event(SECURITY_EVENT_STACK_OVERFLOW, 1U);
    security_report_event(SECURITY_EVENT_KERNEL_RO_VIOLATION, 0U);

    TEST_ASSERT_EQ((int64_t)s_stats.total_events, 3);
    TEST_ASSERT_EQ((int64_t)s_stats.stack_overflows, 2);
    TEST_ASSERT_EQ((int64_t)s_stats.page_faults, 1);
}

void test_security_state_normal_to_degraded(void)
{
    security_subsys_init();
    TEST_ASSERT_EQ((int64_t)security_get_state(), (int64_t)SECURITY_STATE_NORMAL);

    security_report_event(SECURITY_EVENT_KERNEL_RO_VIOLATION, 0U);
    TEST_ASSERT_EQ((int64_t)security_get_state(), (int64_t)SECURITY_STATE_DEGRADED);
}

void test_security_state_double_free(void)
{
    security_subsys_init();

    security_report_event(SECURITY_EVENT_DOUBLE_FREE, 42U);
    TEST_ASSERT_EQ((int64_t)security_get_state(), (int64_t)SECURITY_STATE_DEGRADED);
    TEST_ASSERT_EQ((int64_t)s_stats.double_frees, 1);
}

void test_security_audit_log_basic(void)
{
    security_subsys_init();

    security_report_event(SECURITY_EVENT_CAPABILITY_VIOLATION, 10U);
    security_report_event(SECURITY_EVENT_STACK_OVERFLOW, 20U);

    TEST_ASSERT_EQ((int64_t)s_audit_count, 2);
    TEST_ASSERT_EQ((int64_t)s_audit_log[0U].event,
                   (int64_t)SECURITY_EVENT_CAPABILITY_VIOLATION);
    TEST_ASSERT_EQ((int64_t)s_audit_log[0U].detail, 10);
    TEST_ASSERT_EQ((int64_t)s_audit_log[1U].event,
                   (int64_t)SECURITY_EVENT_STACK_OVERFLOW);
    TEST_ASSERT_EQ((int64_t)s_audit_log[1U].detail, 20);
}

void test_security_audit_log_overflow(void)
{
    uint32_t i;
    security_subsys_init();

    /* 写入超过审计日志最大容量 */
    for (i = 0U; i < SECURITY_AUDIT_LOG_MAX + 10U; i++)
    {
        security_report_event(SECURITY_EVENT_STACK_OVERFLOW, i);
    }

    /* 计数应不超过最大值 */
    TEST_ASSERT_EQ((int64_t)s_audit_count, (int64_t)SECURITY_AUDIT_LOG_MAX);
}

void test_security_sha256_basic(void)
{
    kernel_status_t ret;
    const char *data = "test";
    uint8_t hash1[SECURITY_SHA256_SIZE];
    uint8_t hash2[SECURITY_SHA256_SIZE];

    ret = sha256_compute(data, 4U, hash1);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 相同输入应产生相同哈希 */
    ret = sha256_compute(data, 4U, hash2);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ((int64_t)memcmp(hash1, hash2, SECURITY_SHA256_SIZE), 0);
}

void test_security_sha256_different_inputs(void)
{
    uint8_t hash_a[SECURITY_SHA256_SIZE];
    uint8_t hash_b[SECURITY_SHA256_SIZE];

    (void)sha256_compute("hello", 5U, hash_a);
    (void)sha256_compute("world", 5U, hash_b);

    /* 不同输入应产生不同哈希 */
    TEST_ASSERT_TRUE(memcmp(hash_a, hash_b, SECURITY_SHA256_SIZE) != 0);
}

void test_security_sha256_null_params(void)
{
    uint8_t hash[SECURITY_SHA256_SIZE];
    TEST_ASSERT_LT(sha256_compute(NULL, 4U, hash), 0);
    TEST_ASSERT_LT(sha256_compute("test", 4U, NULL), 0);
}

void test_security_verify_integrity_match(void)
{
    const char *data = "verify me";
    uint8_t expected[SECURITY_SHA256_SIZE];

    /* 先计算正确哈希 */
    (void)sha256_compute(data, 9U, expected);

    /* 验证应通过 */
    kernel_status_t ret = verify_integrity(data, 9U, expected);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

void test_security_verify_integrity_mismatch(void)
{
    const char *data = "verify me";
    uint8_t wrong[SECURITY_SHA256_SIZE];

    (void)memset(wrong, 0xFF, sizeof(wrong));

    kernel_status_t ret = verify_integrity(data, 9U, wrong);
    TEST_ASSERT_LT(ret, 0);
}

void test_security_page_access_denied_write(void)
{
    security_subsys_init();

    /* 内核 RO 违规 - 写操作 */
    security_report_event(SECURITY_EVENT_KERNEL_RO_VIOLATION, 0U);
    TEST_ASSERT_EQ((int64_t)security_get_state(), (int64_t)SECURITY_STATE_DEGRADED);
    TEST_ASSERT_GT((int64_t)s_stats.page_faults, 0);
}

void test_security_capability_violation_counted(void)
{
    security_subsys_init();

    security_report_event(SECURITY_EVENT_CAPABILITY_VIOLATION, 100U);
    TEST_ASSERT_EQ((int64_t)s_stats.capability_violations, 1);
}

void test_security_no_event_before_init(void)
{
    /* 未初始化时不应记录事件 */
    s_initialized = false;
    s_stats.total_events = 0U;

    security_report_event(SECURITY_EVENT_STACK_OVERFLOW, 0U);
    TEST_ASSERT_EQ((int64_t)s_stats.total_events, 0);
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("\n=== 安全子系统 单元测试 ===\n\n");

    printf("--- 初始化 ---\n");
    TEST_RUN(security_init_succeeds);

    printf("--- 事件计数 ---\n");
    TEST_RUN(security_event_counting);
    TEST_RUN(security_no_event_before_init);

    printf("--- 状态转换 ---\n");
    TEST_RUN(security_state_normal_to_degraded);
    TEST_RUN(security_state_double_free);

    printf("--- 审计日志 ---\n");
    TEST_RUN(security_audit_log_basic);
    TEST_RUN(security_audit_log_overflow);

    printf("--- SHA-256 ---\n");
    TEST_RUN(security_sha256_basic);
    TEST_RUN(security_sha256_different_inputs);
    TEST_RUN(security_sha256_null_params);

    printf("--- 完整性校验 ---\n");
    TEST_RUN(security_verify_integrity_match);
    TEST_RUN(security_verify_integrity_mismatch);

    printf("--- 权限检查 ---\n");
    TEST_RUN(security_page_access_denied_write);
    TEST_RUN(security_capability_violation_counted);

    printf("\n=== 测试结果 ===\n");
    printf("总计: %u  通过: %u  失败: %u\n",
           s_total, s_passed, s_failed);

    return (s_failed == 0U) ? 0 : 1;
}
