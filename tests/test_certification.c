/**
 * @file    test_certification.c
 * @brief   AISafe64 RTOS - 安全认证框架单元测试（宿主机版）
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 1.0
 *
 * @details 功能安全认证框架测试：
 *          - 需求注册与查询
 *          - 测试用例管理
 *          - 追溯矩阵
 *          - 模块覆盖率统计
 *          - 合规性检查
 *          - 全局报告
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
 * 认证框架 Mock 实现
 * ======================================================================== */

typedef int32_t kernel_status_t;
#define KERNEL_OK  ((kernel_status_t)0)

#define CERT_MAX_REQUIREMENTS  256U
#define CERT_MAX_TEST_CASES    512U
#define CERT_MAX_TRACES        1024U
#define CERT_MAX_MODULES       32U
#define CERT_REQ_ID_MAX        32U

typedef enum { CERT_SIL_NONE = 0U, CERT_SIL_1, CERT_SIL_2, CERT_SIL_3, CERT_SIL_4 } cert_sil_t;
typedef enum { CERT_ASIL_NONE = 0U, CERT_ASIL_A, CERT_ASIL_B, CERT_ASIL_C, CERT_ASIL_D } cert_asil_t;

typedef enum
{
    CERT_REQ_DRAFT    = 0U,
    CERT_REQ_APPROVED = 1U,
    CERT_REQ_TESTED   = 2U,
    CERT_REQ_VERIFIED = 3U,
    CERT_REQ_CERTIFIED = 4U
} cert_req_status_t;

typedef enum
{
    CERT_TEST_PENDING = 0U,
    CERT_TEST_PASSED  = 1U,
    CERT_TEST_FAILED  = 2U,
    CERT_TEST_SKIPPED = 3U
} cert_test_result_t;

typedef struct
{
    char              req_id[CERT_REQ_ID_MAX];
    char              module[16];
    cert_sil_t        target_sil;
    cert_asil_t       target_asil;
    cert_req_status_t status;
    uint32_t          test_count;
    uint32_t          test_passed;
    bool              has_trace;
} cert_requirement_t;

typedef struct
{
    uint32_t           test_id;
    char               req_id[CERT_REQ_ID_MAX];
    char               name[32];
    cert_test_result_t result;
    uint32_t           coverage_percent;
    uint64_t           executed_at;
    bool               active;
} cert_test_case_t;

typedef struct
{
    char     req_id[CERT_REQ_ID_MAX];
    uint32_t test_id;
    uint32_t source_file_hash;
    uint32_t line_start;
    uint32_t line_end;
    bool     verified;
} cert_trace_entry_t;

typedef struct
{
    char     module[16];
    uint32_t total_requirements;
    uint32_t total_tests;
    uint32_t tests_passed;
    uint32_t avg_coverage;
} cert_module_stats_t;

/* 全局状态 */
static cert_requirement_t s_requirements[CERT_MAX_REQUIREMENTS];
static bool s_req_used[CERT_MAX_REQUIREMENTS];
static uint32_t s_req_count;

static cert_test_case_t s_tests[CERT_MAX_TEST_CASES];
static bool s_test_used[CERT_MAX_TEST_CASES];
static uint32_t s_test_count;

static cert_trace_entry_t s_traces[CERT_MAX_TRACES];
static bool s_trace_used[CERT_MAX_TRACES];
static uint32_t s_trace_count;

static cert_module_stats_t s_module_stats[CERT_MAX_MODULES];
static bool s_module_used[CERT_MAX_MODULES];

static bool s_initialized;

static void cert_strcpy(char *dst, const char *src, uint32_t n)
{
    uint32_t i;
    if ((dst == NULL) || (src == NULL) || (n == 0U)) { return; }
    for (i = 0U; (i < (n - 1U)) && (src[i] != '\0'); i++) { dst[i] = src[i]; }
    dst[i] = '\0';
}

static bool cert_streq(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0U; i < CERT_REQ_ID_MAX; i++)
    {
        if (a[i] != b[i]) { return false; }
        if (a[i] == '\0') { return true; }
    }
    return true;
}

static int32_t cert_find_req(const char *req_id)
{
    uint32_t i;
    for (i = 0U; i < CERT_MAX_REQUIREMENTS; i++)
    {
        if (s_req_used[i] && cert_streq(s_requirements[i].req_id, req_id))
        {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t cert_find_or_create_module(const char *module)
{
    uint32_t i;
    int32_t empty_slot = -1;
    for (i = 0U; i < CERT_MAX_MODULES; i++)
    {
        if (s_module_used[i])
        {
            if (cert_streq(s_module_stats[i].module, module))
            {
                return (int32_t)i;
            }
        }
        else if (empty_slot < 0)
        {
            empty_slot = (int32_t)i;
        }
    }
    if (empty_slot >= 0)
    {
        (void)memset(&s_module_stats[(uint32_t)empty_slot], 0, sizeof(cert_module_stats_t));
        cert_strcpy(s_module_stats[(uint32_t)empty_slot].module, module, 16U);
        s_module_used[(uint32_t)empty_slot] = true;
        return empty_slot;
    }
    return -1;
}

static kernel_status_t cert_init(void)
{
    (void)memset(s_requirements, 0, sizeof(s_requirements));
    (void)memset(s_req_used, 0, sizeof(s_req_used));
    (void)memset(s_tests, 0, sizeof(s_tests));
    (void)memset(s_test_used, 0, sizeof(s_test_used));
    (void)memset(s_traces, 0, sizeof(s_traces));
    (void)memset(s_trace_used, 0, sizeof(s_trace_used));
    (void)memset(s_module_stats, 0, sizeof(s_module_stats));
    (void)memset(s_module_used, 0, sizeof(s_module_used));
    s_req_count = 0U;
    s_test_count = 0U;
    s_trace_count = 0U;
    s_initialized = true;
    return KERNEL_OK;
}

static kernel_status_t cert_register_requirement(const char *req_id, const char *module,
                                                   cert_sil_t sil, cert_asil_t asil)
{
    uint32_t i;
    int32_t mod_idx;

    if (!s_initialized) { return -22; }
    if ((req_id == NULL) || (module == NULL)) { return -22; }
    if (cert_find_req(req_id) >= 0) { return -17; /* EEXIST */ }

    for (i = 0U; i < CERT_MAX_REQUIREMENTS; i++)
    {
        if (!s_req_used[i]) { break; }
    }
    if (i >= CERT_MAX_REQUIREMENTS) { return -12; }

    cert_strcpy(s_requirements[i].req_id, req_id, CERT_REQ_ID_MAX);
    cert_strcpy(s_requirements[i].module, module, 16U);
    s_requirements[i].target_sil = sil;
    s_requirements[i].target_asil = asil;
    s_requirements[i].status = CERT_REQ_APPROVED;
    s_requirements[i].test_count = 0U;
    s_requirements[i].test_passed = 0U;
    s_requirements[i].has_trace = false;
    s_req_used[i] = true;
    s_req_count++;

    mod_idx = cert_find_or_create_module(module);
    if (mod_idx >= 0) { s_module_stats[(uint32_t)mod_idx].total_requirements++; }

    return KERNEL_OK;
}

static int32_t cert_register_test(const char *req_id, const char *name)
{
    uint32_t i;
    int32_t req_idx;
    int32_t mod_idx;

    if ((req_id == NULL) || (name == NULL)) { return -22; }
    req_idx = cert_find_req(req_id);
    if (req_idx < 0) { return -2; }

    for (i = 0U; i < CERT_MAX_TEST_CASES; i++)
    {
        if (!s_test_used[i]) { break; }
    }
    if (i >= CERT_MAX_TEST_CASES) { return -12; }

    s_tests[i].test_id = i;
    cert_strcpy(s_tests[i].req_id, req_id, CERT_REQ_ID_MAX);
    cert_strcpy(s_tests[i].name, name, 32U);
    s_tests[i].result = CERT_TEST_PENDING;
    s_tests[i].coverage_percent = 0U;
    s_tests[i].executed_at = 0ULL;
    s_tests[i].active = true;
    s_test_used[i] = true;
    s_test_count++;

    s_requirements[(uint32_t)req_idx].test_count++;

    mod_idx = cert_find_or_create_module(s_requirements[(uint32_t)req_idx].module);
    if (mod_idx >= 0) { s_module_stats[(uint32_t)mod_idx].total_tests++; }

    return (int32_t)i;
}

static kernel_status_t cert_record_test_result(uint32_t test_id,
                                                 cert_test_result_t result,
                                                 uint32_t coverage)
{
    int32_t req_idx;
    int32_t mod_idx;

    if (test_id >= CERT_MAX_TEST_CASES) { return -22; }
    if (!s_test_used[test_id]) { return -2; }

    s_tests[test_id].result = result;
    s_tests[test_id].coverage_percent = coverage;

    if (result == CERT_TEST_PASSED)
    {
        req_idx = cert_find_req(s_tests[test_id].req_id);
        if (req_idx >= 0)
        {
            s_requirements[(uint32_t)req_idx].test_passed++;
            if (s_requirements[(uint32_t)req_idx].test_passed >=
                s_requirements[(uint32_t)req_idx].test_count)
            {
                s_requirements[(uint32_t)req_idx].status = CERT_REQ_TESTED;
            }
            mod_idx = cert_find_or_create_module(
                s_requirements[(uint32_t)req_idx].module);
            if (mod_idx >= 0) { s_module_stats[(uint32_t)mod_idx].tests_passed++; }
        }
    }
    return KERNEL_OK;
}

static kernel_status_t cert_add_trace(const char *req_id, uint32_t test_id,
                                        uint32_t file_hash, uint32_t line_start,
                                        uint32_t line_end)
{
    uint32_t i;
    int32_t req_idx;

    if (req_id == NULL) { return -22; }
    req_idx = cert_find_req(req_id);
    if (req_idx < 0) { return -2; }

    for (i = 0U; i < CERT_MAX_TRACES; i++)
    {
        if (!s_trace_used[i]) { break; }
    }
    if (i >= CERT_MAX_TRACES) { return -12; }

    cert_strcpy(s_traces[i].req_id, req_id, CERT_REQ_ID_MAX);
    s_traces[i].test_id = test_id;
    s_traces[i].source_file_hash = file_hash;
    s_traces[i].line_start = line_start;
    s_traces[i].line_end = line_end;
    s_traces[i].verified = false;
    s_trace_used[i] = true;
    s_trace_count++;
    s_requirements[(uint32_t)req_idx].has_trace = true;

    return KERNEL_OK;
}

static kernel_status_t cert_check_compliance(void)
{
    uint32_t i;
    uint32_t met_count = 0U;

    for (i = 0U; i < CERT_MAX_REQUIREMENTS; i++)
    {
        if (!s_req_used[i]) { continue; }
        if ((s_requirements[i].status >= CERT_REQ_TESTED) &&
            s_requirements[i].has_trace)
        {
            met_count++;
        }
    }

    return (met_count >= s_req_count) ? KERNEL_OK : -1;
}

static kernel_status_t cert_get_module_stats(const char *module,
                                               cert_module_stats_t *stats)
{
    uint32_t i;
    if ((module == NULL) || (stats == NULL)) { return -22; }
    for (i = 0U; i < CERT_MAX_MODULES; i++)
    {
        if (s_module_used[i] && cert_streq(s_module_stats[i].module, module))
        {
            (void)memcpy(stats, &s_module_stats[i], sizeof(cert_module_stats_t));
            return KERNEL_OK;
        }
    }
    return -2;
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

void test_cert_init_succeeds(void)
{
    kernel_status_t ret = cert_init();
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_TRUE(s_initialized);
}

void test_cert_register_requirement_basic(void)
{
    cert_init();
    kernel_status_t ret = cert_register_requirement("KR-001", "kernel",
                                                      CERT_SIL_4, CERT_ASIL_D);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ((int64_t)s_req_count, 1);
}

void test_cert_register_requirement_duplicate(void)
{
    cert_init();
    (void)cert_register_requirement("KR-002", "mm", CERT_SIL_3, CERT_ASIL_C);
    kernel_status_t ret = cert_register_requirement("KR-002", "mm",
                                                      CERT_SIL_3, CERT_ASIL_C);
    TEST_ASSERT_LT(ret, 0); /* EEXIST */
}

void test_cert_register_requirement_null(void)
{
    cert_init();
    TEST_ASSERT_LT(cert_register_requirement(NULL, "mm", CERT_SIL_1, CERT_ASIL_A), 0);
    TEST_ASSERT_LT(cert_register_requirement("KR-003", NULL, CERT_SIL_1, CERT_ASIL_A), 0);
}

void test_cert_register_test_basic(void)
{
    cert_init();
    (void)cert_register_requirement("KR-010", "ipc", CERT_SIL_2, CERT_ASIL_B);

    int32_t tid = cert_register_test("KR-010", "test_ipc_basic");
    TEST_ASSERT_GT(tid, -1);
    TEST_ASSERT_EQ((int64_t)s_test_count, 1);
}

void test_cert_register_test_nonexistent_req(void)
{
    cert_init();
    int32_t tid = cert_register_test("KR-999", "test_nonexist");
    TEST_ASSERT_LT(tid, 0);
}

void test_cert_record_test_passed(void)
{
    cert_init();
    (void)cert_register_requirement("KR-020", "sched", CERT_SIL_4, CERT_ASIL_D);
    int32_t tid = cert_register_test("KR-020", "test_sched_basic");

    kernel_status_t ret = cert_record_test_result((uint32_t)tid,
                                                    CERT_TEST_PASSED, 98U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 需求应自动变为 TESTED */
    int32_t idx = cert_find_req("KR-020");
    TEST_ASSERT_GT(idx, -1);
    TEST_ASSERT_EQ((int64_t)s_requirements[(uint32_t)idx].status,
                   (int64_t)CERT_REQ_TESTED);
    TEST_ASSERT_EQ((int64_t)s_requirements[(uint32_t)idx].test_passed, 1);
}

void test_cert_record_test_failed(void)
{
    cert_init();
    (void)cert_register_requirement("KR-021", "mm", CERT_SIL_3, CERT_ASIL_C);
    int32_t tid = cert_register_test("KR-021", "test_mm_fail");

    (void)cert_record_test_result((uint32_t)tid, CERT_TEST_FAILED, 45U);

    /* 需求不应变为 TESTED */
    int32_t idx = cert_find_req("KR-021");
    TEST_ASSERT_EQ((int64_t)s_requirements[(uint32_t)idx].status,
                   (int64_t)CERT_REQ_APPROVED);
}

void test_cert_multiple_tests_per_req(void)
{
    int32_t tid1;
    int32_t tid2;
    int32_t idx;

    cert_init();
    (void)cert_register_requirement("KR-030", "driver", CERT_SIL_2, CERT_ASIL_B);
    tid1 = cert_register_test("KR-030", "test_driver_init");
    tid2 = cert_register_test("KR-030", "test_driver_io");

    /* 第一个测试通过 */
    (void)cert_record_test_result((uint32_t)tid1, CERT_TEST_PASSED, 95U);
    idx = cert_find_req("KR-030");
    TEST_ASSERT_EQ((int64_t)s_requirements[(uint32_t)idx].status,
                   (int64_t)CERT_REQ_APPROVED); /* 还没全部通过 */

    /* 第二个测试通过 */
    (void)cert_record_test_result((uint32_t)tid2, CERT_TEST_PASSED, 97U);
    TEST_ASSERT_EQ((int64_t)s_requirements[(uint32_t)idx].status,
                   (int64_t)CERT_REQ_TESTED); /* 全部通过 */
}

void test_cert_add_trace_basic(void)
{
    cert_init();
    (void)cert_register_requirement("KR-040", "fs", CERT_SIL_2, CERT_ASIL_B);
    int32_t tid = cert_register_test("KR-040", "test_fs_rw");
    (void)cert_record_test_result((uint32_t)tid, CERT_TEST_PASSED, 100U);

    kernel_status_t ret = cert_add_trace("KR-040", (uint32_t)tid,
                                           0xDEADBEEFU, 100U, 200U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    int32_t idx = cert_find_req("KR-040");
    TEST_ASSERT_TRUE(s_requirements[(uint32_t)idx].has_trace);
    TEST_ASSERT_EQ((int64_t)s_trace_count, 1);
}

void test_cert_add_trace_nonexistent_req(void)
{
    cert_init();
    TEST_ASSERT_LT(cert_add_trace("KR-999", 0U, 0U, 0U, 0U), 0);
}

void test_cert_module_stats(void)
{
    cert_init();
    (void)cert_register_requirement("KR-050", "net", CERT_SIL_2, CERT_ASIL_B);
    (void)cert_register_requirement("KR-051", "net", CERT_SIL_2, CERT_ASIL_B);
    int32_t tid = cert_register_test("KR-050", "test_net_init");
    (void)cert_record_test_result((uint32_t)tid, CERT_TEST_PASSED, 90U);

    cert_module_stats_t stats;
    kernel_status_t ret = cert_get_module_stats("net", &stats);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_EQ((int64_t)stats.total_requirements, 2);
    TEST_ASSERT_EQ((int64_t)stats.total_tests, 1);
    TEST_ASSERT_EQ((int64_t)stats.tests_passed, 1);
}

void test_cert_module_stats_nonexistent(void)
{
    cert_init();
    cert_module_stats_t stats;
    TEST_ASSERT_LT(cert_get_module_stats("nonexistent", &stats), 0);
}

void test_cert_compliance_not_met(void)
{
    cert_init();
    (void)cert_register_requirement("KR-060", "arch", CERT_SIL_4, CERT_ASIL_D);
    /* 没有测试和追溯 -> 不合规 */
    TEST_ASSERT_LT(cert_check_compliance(), 0);
}

void test_cert_compliance_met(void)
{
    cert_init();
    (void)cert_register_requirement("KR-070", "lib", CERT_SIL_1, CERT_ASIL_A);
    int32_t tid = cert_register_test("KR-070", "test_lib_basic");
    (void)cert_record_test_result((uint32_t)tid, CERT_TEST_PASSED, 100U);
    (void)cert_add_trace("KR-070", (uint32_t)tid, 0x12345678U, 1U, 50U);

    TEST_ASSERT_EQ(cert_check_compliance(), KERNEL_OK);
}

void test_cert_compliance_partial(void)
{
    cert_init();
    (void)cert_register_requirement("KR-080", "crypto", CERT_SIL_3, CERT_ASIL_C);
    (void)cert_register_requirement("KR-081", "crypto", CERT_SIL_3, CERT_ASIL_C);

    int32_t tid = cert_register_test("KR-080", "test_crypto_basic");
    (void)cert_record_test_result((uint32_t)tid, CERT_TEST_PASSED, 95U);
    (void)cert_add_trace("KR-080", (uint32_t)tid, 0U, 1U, 100U);
    /* KR-081 未测试 -> 不合规 */

    TEST_ASSERT_LT(cert_check_compliance(), 0);
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("\n=== 安全认证框架 单元测试 ===\n\n");

    printf("--- 初始化 ---\n");
    TEST_RUN(cert_init_succeeds);

    printf("--- 需求注册 ---\n");
    TEST_RUN(cert_register_requirement_basic);
    TEST_RUN(cert_register_requirement_duplicate);
    TEST_RUN(cert_register_requirement_null);

    printf("--- 测试用例管理 ---\n");
    TEST_RUN(cert_register_test_basic);
    TEST_RUN(cert_register_test_nonexistent_req);
    TEST_RUN(cert_record_test_passed);
    TEST_RUN(cert_record_test_failed);
    TEST_RUN(cert_multiple_tests_per_req);

    printf("--- 追溯矩阵 ---\n");
    TEST_RUN(cert_add_trace_basic);
    TEST_RUN(cert_add_trace_nonexistent_req);

    printf("--- 模块统计 ---\n");
    TEST_RUN(cert_module_stats);
    TEST_RUN(cert_module_stats_nonexistent);

    printf("--- 合规性检查 ---\n");
    TEST_RUN(cert_compliance_not_met);
    TEST_RUN(cert_compliance_met);
    TEST_RUN(cert_compliance_partial);

    printf("\n=== 测试结果 ===\n");
    printf("总计: %u  通过: %u  失败: %u\n",
           s_total, s_passed, s_failed);

    return (s_failed == 0U) ? 0 : 1;
}
