/**
 * @file    test_evidence.c
 * @brief   AISafe64 RTOS - 认证证据收集框架单元测试（宿主机）
 * @author  AISafe64 Team
 * @date    2026-04-02
 * @version 1.0
 *
 * @details 认证证据收集框架宿主机自包含测试
 *          - 模块注册与证据收集
 *          - 测试结果记录
 *          - 覆盖率跟踪
 *          - 合规性检查
 *
 * @note 对应需求: SE-009, SE-010, DC-001~005
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ========================================================================
 * 测试框架宏
 * ======================================================================== */

static uint32_t s_total  = 0U;
static uint32_t s_passed = 0U;
static uint32_t s_failed = 0U;

#define TEST_ASSERT(cond) do { \
    s_total++; \
    if (cond) { s_passed++; } \
    else { s_failed++; printf("  失败: %s (行 %d)\n", #cond, __LINE__); } \
} while (0)

#define TEST_ASSERT_EQ(a, b) TEST_ASSERT((a) == (b))
#define TEST_ASSERT_NE(a, b) TEST_ASSERT((a) != (b))
#define TEST_ASSERT_TRUE(x)  TEST_ASSERT((x) == true)
#define TEST_ASSERT_FALSE(x) TEST_ASSERT((x) == false)
#define TEST_ASSERT_GT(a, b) TEST_ASSERT((a) > (b))

/* ========================================================================
 * 模拟认证证据框架
 * ======================================================================== */

#define EVIDENCE_MAX_MODULES  32U
#define EVIDENCE_TYPE_COUNT   15U

typedef enum
{
    EVIDENCE_UNIT_TEST       = 0U,
    EVIDENCE_INTEGRATION     = 1U,
    EVIDENCE_COVERAGE        = 2U,
    EVIDENCE_TRACEABILITY    = 3U,
    EVIDENCE_CODE_REVIEW     = 4U,
    EVIDENCE_STATIC_ANALYSIS = 5U,
    EVIDENCE_FORMAL_VERIFY   = 6U,
    EVIDENCE_LOAD_TEST       = 7U,
    EVIDENCE_DEMO_TEST       = 8U,
    EVIDENCE_WCET_ANALYSIS   = 9U,
    EVIDENCE_FMEA_REPORT     = 10U,
    EVIDENCE_SIL_ASSESSMENT  = 11U,
    EVIDENCE_ASIL_ASSESSMENT = 12U,
    EVIDENCE_COMPLIANCE      = 13U,
    EVIDENCE_CERTIFICATION   = 14U
} evidence_type_t;

typedef struct
{
    char    module[16];
    bool    evidence_collected[EVIDENCE_TYPE_COUNT];
    bool    verified[EVIDENCE_TYPE_COUNT];
    uint32_t coverage_percent;
    uint32_t tests_total;
    uint32_t tests_passed;
    uint32_t tests_failed;
} module_evidence_t;

typedef struct
{
    module_evidence_t modules[32];
    uint32_t          module_count;
    uint32_t          total_tests;
    uint32_t          total_passed;
    uint32_t          total_failed;
    uint32_t          avg_coverage;
    uint32_t          requirements_met;
    uint32_t          requirements_total;
    bool              certified;
} evidence_state_t;

typedef struct { uint32_t _dummy; } TicketLock_t;
static void ticket_lock_acquire(TicketLock_t *l) { (void)l; }
static void ticket_lock_release(TicketLock_t *l) { (void)l; }
static void ticket_lock_init(TicketLock_t *l) { (void)l; }

typedef struct
{
    module_evidence_t  modules[EVIDENCE_MAX_MODULES];
    uint32_t           module_count;
    bool               initialized;
    TicketLock_t       lock;
} evidence_internal_t;

static evidence_internal_t s_ev;

static void evidence_init(void)
{
    uint32_t i;

    ticket_lock_init(&s_ev.lock);

    for (i = 0U; i < EVIDENCE_MAX_MODULES; i++)
    {
        memset(&s_ev.modules[i], 0, sizeof(module_evidence_t));
    }

    s_ev.module_count = 0U;
    s_ev.initialized = true;
}

static int32_t evidence_register_module(const char *module)
{
    uint32_t idx;
    module_evidence_t *mod;

    if ((module == NULL) || !s_ev.initialized)
    {
        return -1;
    }

    if (s_ev.module_count >= EVIDENCE_MAX_MODULES)
    {
        return -1;
    }

    ticket_lock_acquire(&s_ev.lock);

    idx = s_ev.module_count;
    mod = &s_ev.modules[idx];

    strncpy(mod->module, module, sizeof(mod->module) - 1U);
    mod->module[sizeof(mod->module) - 1U] = '\0';
    mod->coverage_percent = 0U;
    mod->tests_total = 0U;
    mod->tests_passed = 0U;
    mod->tests_failed = 0U;

    s_ev.module_count = idx + 1U;

    ticket_lock_release(&s_ev.lock);

    return 0;
}

static int32_t evidence_find_module(const char *module)
{
    uint32_t i;
    for (i = 0U; i < s_ev.module_count; i++)
    {
        if (strcmp(s_ev.modules[i].module, module) == 0)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

static void evidence_record_test(const char *module,
                                  uint32_t total, uint32_t passed,
                                  uint32_t failed, uint32_t coverage)
{
    int32_t idx;

    if ((module == NULL) || !s_ev.initialized)
    {
        return;
    }

    ticket_lock_acquire(&s_ev.lock);

    idx = evidence_find_module(module);
    if (idx < 0)
    {
        ticket_lock_release(&s_ev.lock);
        return;
    }

    s_ev.modules[idx].tests_total = total;
    s_ev.modules[idx].tests_passed = passed;
    s_ev.modules[idx].tests_failed = failed;
    s_ev.modules[idx].coverage_percent = coverage;

    if ((total > 0U) && (failed == 0U))
    {
        s_ev.modules[idx].evidence_collected[EVIDENCE_UNIT_TEST] = true;
        s_ev.modules[idx].verified[EVIDENCE_UNIT_TEST] = true;
    }

    if (coverage >= 95U)
    {
        s_ev.modules[idx].evidence_collected[EVIDENCE_COVERAGE] = true;
        s_ev.modules[idx].verified[EVIDENCE_COVERAGE] = true;
    }

    ticket_lock_release(&s_ev.lock);
}

static void evidence_set_item(const char *module, evidence_type_t type,
                               bool verified)
{
    int32_t idx;

    if ((module == NULL) || !s_ev.initialized)
    {
        return;
    }

    if ((uint32_t)type >= EVIDENCE_TYPE_COUNT)
    {
        return;
    }

    ticket_lock_acquire(&s_ev.lock);

    idx = evidence_find_module(module);
    if (idx < 0)
    {
        ticket_lock_release(&s_ev.lock);
        return;
    }

    s_ev.modules[idx].evidence_collected[(uint32_t)type] = true;
    s_ev.modules[idx].verified[(uint32_t)type] = verified;

    ticket_lock_release(&s_ev.lock);
}

static bool evidence_check(const char *module, evidence_type_t type)
{
    int32_t idx;
    bool result;

    if ((module == NULL) || !s_ev.initialized)
    {
        return false;
    }

    if ((uint32_t)type >= EVIDENCE_TYPE_COUNT)
    {
        return false;
    }

    ticket_lock_acquire(&s_ev.lock);

    idx = evidence_find_module(module);
    if (idx < 0)
    {
        ticket_lock_release(&s_ev.lock);
        return false;
    }

    result = s_ev.modules[idx].evidence_collected[(uint32_t)type] &&
             s_ev.modules[idx].verified[(uint32_t)type];

    ticket_lock_release(&s_ev.lock);

    return result;
}

static void evidence_get_global(evidence_state_t *state)
{
    uint32_t i, j;

    if ((state == NULL) || !s_ev.initialized)
    {
        return;
    }

    ticket_lock_acquire(&s_ev.lock);

    memset(state, 0, sizeof(evidence_state_t));
    state->module_count = s_ev.module_count;

    for (i = 0U; i < s_ev.module_count; i++)
    {
        const module_evidence_t *mod = &s_ev.modules[i];

        memcpy(&state->modules[i], mod, sizeof(module_evidence_t));
        state->total_tests += mod->tests_total;
        state->total_passed += mod->tests_passed;
        state->total_failed += mod->tests_failed;
        state->avg_coverage += mod->coverage_percent;

        bool all_met = true;
        for (j = 0U; j < EVIDENCE_TYPE_COUNT; j++)
        {
            if (!mod->evidence_collected[j] || !mod->verified[j])
            {
                all_met = false;
                break;
            }
        }
        if (all_met)
        {
            state->requirements_met++;
        }
        state->requirements_total++;
    }

    if (s_ev.module_count > 0U)
    {
        state->avg_coverage = state->avg_coverage / s_ev.module_count;
    }

    if ((state->total_failed == 0U) &&
        (state->avg_coverage >= 95U) &&
        (state->requirements_met == state->requirements_total) &&
        (state->requirements_total > 0U))
    {
        state->certified = true;
    }

    ticket_lock_release(&s_ev.lock);
}

static int32_t evidence_check_compliance(void)
{
    uint32_t i;

    if (!s_ev.initialized)
    {
        return -1;
    }

    ticket_lock_acquire(&s_ev.lock);

    for (i = 0U; i < s_ev.module_count; i++)
    {
        if (s_ev.modules[i].tests_failed > 0U)
        {
            ticket_lock_release(&s_ev.lock);
            return -1;
        }

        if (s_ev.modules[i].coverage_percent < 95U)
        {
            ticket_lock_release(&s_ev.lock);
            return -1;
        }

        if (!s_ev.modules[i].verified[EVIDENCE_UNIT_TEST])
        {
            ticket_lock_release(&s_ev.lock);
            return -1;
        }
    }

    ticket_lock_release(&s_ev.lock);

    return 0;
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

static void test_evidence_init_basic(void)
{
    evidence_init();
    TEST_ASSERT_TRUE(s_ev.initialized);
    TEST_ASSERT_EQ(s_ev.module_count, 0U);
}

static void test_evidence_register_module(void)
{
    evidence_init();

    int32_t ret = evidence_register_module("scheduler");
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQ(s_ev.module_count, 1U);
    TEST_ASSERT_EQ(strcmp(s_ev.modules[0].module, "scheduler"), 0);
}

static void test_evidence_register_null(void)
{
    evidence_init();

    int32_t ret = evidence_register_module(NULL);
    TEST_ASSERT_EQ(ret, -1);
}

static void test_evidence_record_test_pass(void)
{
    evidence_init();
    evidence_register_module("sched");

    evidence_record_test("sched", 100U, 100U, 0U, 97U);

    int32_t idx = evidence_find_module("sched");
    TEST_ASSERT_GT(idx, -1);
    TEST_ASSERT_EQ(s_ev.modules[idx].tests_total, 100U);
    TEST_ASSERT_EQ(s_ev.modules[idx].tests_passed, 100U);
    TEST_ASSERT_EQ(s_ev.modules[idx].tests_failed, 0U);
    TEST_ASSERT_EQ(s_ev.modules[idx].coverage_percent, 97U);

    /* 自动标记单元测试通过 */
    TEST_ASSERT_TRUE(s_ev.modules[idx].evidence_collected[EVIDENCE_UNIT_TEST]);
    TEST_ASSERT_TRUE(s_ev.modules[idx].verified[EVIDENCE_UNIT_TEST]);

    /* 自动标记覆盖率达标 */
    TEST_ASSERT_TRUE(s_ev.modules[idx].evidence_collected[EVIDENCE_COVERAGE]);
    TEST_ASSERT_TRUE(s_ev.modules[idx].verified[EVIDENCE_COVERAGE]);
}

static void test_evidence_record_test_fail(void)
{
    evidence_init();
    evidence_register_module("mm");

    evidence_record_test("mm", 50U, 48U, 2U, 88U);

    int32_t idx = evidence_find_module("mm");
    TEST_ASSERT_GT(idx, -1);
    TEST_ASSERT_EQ(s_ev.modules[idx].tests_failed, 2U);

    /* 失败时不自动标记 */
    TEST_ASSERT_FALSE(s_ev.modules[idx].evidence_collected[EVIDENCE_UNIT_TEST]);
    TEST_ASSERT_FALSE(s_ev.modules[idx].verified[EVIDENCE_UNIT_TEST]);
}

static void test_evidence_set_item(void)
{
    evidence_init();
    evidence_register_module("ipc");

    evidence_set_item("ipc", EVIDENCE_STATIC_ANALYSIS, true);

    TEST_ASSERT_TRUE(evidence_check("ipc", EVIDENCE_STATIC_ANALYSIS));

    /* 未设置的不应该通过 */
    TEST_ASSERT_FALSE(evidence_check("ipc", EVIDENCE_CODE_REVIEW));
}

static void test_evidence_set_item_not_verified(void)
{
    evidence_init();
    evidence_register_module("fs");

    evidence_set_item("fs", EVIDENCE_FORMAL_VERIFY, false);

    /* 设置了但未验证 */
    TEST_ASSERT_TRUE(s_ev.modules[0].evidence_collected[EVIDENCE_FORMAL_VERIFY]);
    TEST_ASSERT_FALSE(s_ev.modules[0].verified[EVIDENCE_FORMAL_VERIFY]);
    TEST_ASSERT_FALSE(evidence_check("fs", EVIDENCE_FORMAL_VERIFY));
}

static void test_evidence_check_nonexistent(void)
{
    evidence_init();

    TEST_ASSERT_FALSE(evidence_check("nonexist", EVIDENCE_UNIT_TEST));
}

static void test_evidence_compliance_pass(void)
{
    evidence_init();
    evidence_register_module("sched");
    evidence_record_test("sched", 100U, 100U, 0U, 98U);

    int32_t ret = evidence_check_compliance();
    TEST_ASSERT_EQ(ret, 0);
}

static void test_evidence_compliance_fail_coverage(void)
{
    evidence_init();
    evidence_register_module("mm");
    evidence_record_test("mm", 50U, 50U, 0U, 80U);

    int32_t ret = evidence_check_compliance();
    TEST_ASSERT_EQ(ret, -1);
}

static void test_evidence_compliance_fail_tests(void)
{
    evidence_init();
    evidence_register_module("ipc");
    evidence_record_test("ipc", 20U, 18U, 2U, 96U);

    int32_t ret = evidence_check_compliance();
    TEST_ASSERT_EQ(ret, -1);
}

static void test_evidence_get_global(void)
{
    evidence_init();
    evidence_register_module("sched");
    evidence_record_test("sched", 100U, 100U, 0U, 97U);

    evidence_register_module("mm");
    evidence_record_test("mm", 50U, 50U, 0U, 96U);

    evidence_state_t state;
    evidence_get_global(&state);

    TEST_ASSERT_EQ(state.module_count, 2U);
    TEST_ASSERT_EQ(state.total_tests, 150U);
    TEST_ASSERT_EQ(state.total_passed, 150U);
    TEST_ASSERT_EQ(state.total_failed, 0U);
    TEST_ASSERT_EQ(state.avg_coverage, 96U);  /* (97+96)/2 = 96 */
}

static void test_evidence_null_safety(void)
{
    evidence_init();

    /* NULL 参数安全 */
    evidence_get_global(NULL);
    evidence_record_test(NULL, 0U, 0U, 0U, 0U);
    evidence_set_item(NULL, EVIDENCE_UNIT_TEST, true);
    TEST_ASSERT_FALSE(evidence_check(NULL, EVIDENCE_UNIT_TEST));
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("=== 认证证据收集框架测试 ===\n\n");

    test_evidence_init_basic();
    test_evidence_register_module();
    test_evidence_register_null();
    test_evidence_record_test_pass();
    test_evidence_record_test_fail();
    test_evidence_set_item();
    test_evidence_set_item_not_verified();
    test_evidence_check_nonexistent();
    test_evidence_compliance_pass();
    test_evidence_compliance_fail_coverage();
    test_evidence_compliance_fail_tests();
    test_evidence_get_global();
    test_evidence_null_safety();

    printf("\n结果: %u 通过 / %u 失败 / %u 总计\n",
           s_passed, s_failed, s_total);

    return (s_failed > 0U) ? 1 : 0;
}
