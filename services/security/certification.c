/**
 * @file    certification.c
 * @brief   安全认证框架实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 功能安全认证框架核心实现：
 *          - 安全需求注册与状态追踪
 *          - 测试用例管理
 *          - 需求→测试→代码 追溯矩阵
 *          - 模块覆盖率统计
 *          - 合规性检查
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SE-006~009
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/certification.h>
#include <kernel/security.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 认证框架全局状态
 * ======================================================================== */

/** @brief 需求表 */
static cert_requirement_t s_requirements[CERT_MAX_REQUIREMENTS];

/** @brief 需求使用标记 */
static bool s_req_used[CERT_MAX_REQUIREMENTS];

/** @brief 需求计数 */
static uint32_t s_req_count;

/** @brief 测试用例表 */
static cert_test_case_t s_tests[CERT_MAX_TEST_CASES];

/** @brief 测试使用标记 */
static bool s_test_used[CERT_MAX_TEST_CASES];

/** @brief 测试计数 */
static uint32_t s_test_count;

/** @brief 追溯矩阵 */
static cert_trace_entry_t s_traces[CERT_MAX_TRACES];

/** @brief 追溯使用标记 */
static bool s_trace_used[CERT_MAX_TRACES];

/** @brief 追溯计数 */
static uint32_t s_trace_count;

/** @brief 模块统计缓存 */
static cert_module_stats_t s_module_stats[CERT_MAX_MODULES];

/** @brief 模块统计使用标记 */
static bool s_module_used[CERT_MAX_MODULES];

/** @brief 初始化标志 */
static bool s_initialized;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 安全字符串复制
 */
static void cert_strcpy(char *dst, const char *src, uint32_t n)
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

/**
 * @brief 字符串比较
 */
static bool cert_streq(const char *a, const char *b)
{
    uint32_t i;

    for (i = 0U; i < CERT_REQ_ID_MAX; i++)
    {
        if (a[i] != b[i])
        {
            return false;
        }
        if (a[i] == '\0')
        {
            return true;
        }
    }

    return true;
}

/**
 * @brief 查找需求索引
 *
 * @return 索引，-1 表示未找到
 */
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

/**
 * @brief 查找或创建模块统计
 *
 * @return 模块索引，-1 表示失败
 */
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
        (void)memset(&s_module_stats[(uint32_t)empty_slot], 0,
                      sizeof(cert_module_stats_t));
        cert_strcpy(s_module_stats[(uint32_t)empty_slot].module, module, 16U);
        s_module_used[(uint32_t)empty_slot] = true;
        return empty_slot;
    }

    return -1;
}

/* ========================================================================
 * 初始化
 * ======================================================================== */

kernel_status_t cert_init(void)
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

/* ========================================================================
 * 需求管理
 * ======================================================================== */

kernel_status_t cert_register_requirement(const char *req_id, const char *module,
                                            cert_sil_t sil, cert_asil_t asil)
{
    uint32_t i;

    if (!s_initialized)
    {
        return -(int32_t)22;
    }

    if ((req_id == NULL) || (module == NULL))
    {
        return -(int32_t)22;
    }

    /* 检查重复 */
    if (cert_find_req(req_id) >= 0)
    {
        return -(int32_t)17; /* -EEXIST */
    }

    /* 查找空槽 */
    for (i = 0U; i < CERT_MAX_REQUIREMENTS; i++)
    {
        if (!s_req_used[i])
        {
            break;
        }
    }

    if (i >= CERT_MAX_REQUIREMENTS)
    {
        return -(int32_t)12;
    }

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

    /* 更新模块统计 */
    int32_t mod_idx = cert_find_or_create_module(module);
    if (mod_idx >= 0)
    {
        s_module_stats[(uint32_t)mod_idx].total_requirements++;
    }

    return KERNEL_OK;
}

kernel_status_t cert_update_req_status(const char *req_id, cert_req_status_t status)
{
    int32_t idx;

    if (req_id == NULL)
    {
        return -(int32_t)22;
    }

    idx = cert_find_req(req_id);
    if (idx < 0)
    {
        return -(int32_t)2;
    }

    s_requirements[(uint32_t)idx].status = status;

    return KERNEL_OK;
}

/* ========================================================================
 * 测试管理
 * ======================================================================== */

int32_t cert_register_test(const char *req_id, const char *name)
{
    uint32_t i;
    int32_t req_idx;

    if ((req_id == NULL) || (name == NULL))
    {
        return -(int32_t)22;
    }

    req_idx = cert_find_req(req_id);
    if (req_idx < 0)
    {
        return -(int32_t)2;
    }

    for (i = 0U; i < CERT_MAX_TEST_CASES; i++)
    {
        if (!s_test_used[i])
        {
            break;
        }
    }

    if (i >= CERT_MAX_TEST_CASES)
    {
        return -(int32_t)12;
    }

    s_tests[i].test_id = i;
    cert_strcpy(s_tests[i].req_id, req_id, CERT_REQ_ID_MAX);
    cert_strcpy(s_tests[i].name, name, 32U);
    s_tests[i].result = CERT_TEST_PENDING;
    s_tests[i].coverage_percent = 0U;
    s_tests[i].executed_at = 0ULL;
    s_tests[i].active = true;

    s_test_used[i] = true;
    s_test_count++;

    /* 更新需求的测试计数 */
    s_requirements[(uint32_t)req_idx].test_count++;

    /* 更新模块统计 */
    int32_t mod_idx = cert_find_or_create_module(
        s_requirements[(uint32_t)req_idx].module);
    if (mod_idx >= 0)
    {
        s_module_stats[(uint32_t)mod_idx].total_tests++;
    }

    return (int32_t)i;
}

kernel_status_t cert_record_test_result(uint32_t test_id,
                                          cert_test_result_t result,
                                          uint32_t coverage)
{
    int32_t req_idx;
    int32_t mod_idx;

    if (test_id >= CERT_MAX_TEST_CASES)
    {
        return -(int32_t)22;
    }

    if (!s_test_used[test_id])
    {
        return -(int32_t)2;
    }

    s_tests[test_id].result = result;
    s_tests[test_id].coverage_percent = coverage;

    /* 如果通过，更新需求的通过计数 */
    if (result == CERT_TEST_PASSED)
    {
        req_idx = cert_find_req(s_tests[test_id].req_id);
        if (req_idx >= 0)
        {
            s_requirements[(uint32_t)req_idx].test_passed++;

            /* 检查是否所有测试都通过 */
            if (s_requirements[(uint32_t)req_idx].test_passed >=
                s_requirements[(uint32_t)req_idx].test_count)
            {
                s_requirements[(uint32_t)req_idx].status = CERT_REQ_TESTED;
            }

            /* 更新模块统计 */
            mod_idx = cert_find_or_create_module(
                s_requirements[(uint32_t)req_idx].module);
            if (mod_idx >= 0)
            {
                s_module_stats[(uint32_t)mod_idx].tests_passed++;
            }
        }
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 追溯矩阵
 * ======================================================================== */

kernel_status_t cert_add_trace(const char *req_id, uint32_t test_id,
                                 uint32_t file_hash, uint32_t line_start,
                                 uint32_t line_end)
{
    uint32_t i;
    int32_t req_idx;

    if (req_id == NULL)
    {
        return -(int32_t)22;
    }

    req_idx = cert_find_req(req_id);
    if (req_idx < 0)
    {
        return -(int32_t)2;
    }

    for (i = 0U; i < CERT_MAX_TRACES; i++)
    {
        if (!s_trace_used[i])
        {
            break;
        }
    }

    if (i >= CERT_MAX_TRACES)
    {
        return -(int32_t)12;
    }

    cert_strcpy(s_traces[i].req_id, req_id, CERT_REQ_ID_MAX);
    s_traces[i].test_id = test_id;
    s_traces[i].source_file_hash = file_hash;
    s_traces[i].line_start = line_start;
    s_traces[i].line_end = line_end;
    s_traces[i].verified = false;

    s_trace_used[i] = true;
    s_trace_count++;

    /* 更新需求追溯标记 */
    s_requirements[(uint32_t)req_idx].has_trace = true;

    return KERNEL_OK;
}

/* ========================================================================
 * 模块统计与合规检查
 * ======================================================================== */

kernel_status_t cert_get_module_stats(const char *module,
                                        cert_module_stats_t *stats)
{
    uint32_t i;

    if ((module == NULL) || (stats == NULL))
    {
        return -(int32_t)22;
    }

    for (i = 0U; i < CERT_MAX_MODULES; i++)
    {
        if (s_module_used[i] && cert_streq(s_module_stats[i].module, module))
        {
            (void)memcpy(stats, &s_module_stats[i], sizeof(cert_module_stats_t));
            return KERNEL_OK;
        }
    }

    return -(int32_t)2;
}

kernel_status_t cert_check_compliance(void)
{
    uint32_t i;
    uint32_t met_count = 0U;

    for (i = 0U; i < CERT_MAX_REQUIREMENTS; i++)
    {
        if (!s_req_used[i])
        {
            continue;
        }

        /* 检查需求是否达到 TESTED 或更高级别 */
        if ((s_requirements[i].status >= CERT_REQ_TESTED) &&
            s_requirements[i].has_trace)
        {
            met_count++;
        }
    }

    if (met_count < s_req_count)
    {
        return -(int32_t)1; /* 未完全合规 */
    }

    return KERNEL_OK;
}

void cert_get_global_report(uint32_t *total_reqs, uint32_t *reqs_met,
                              uint32_t *total_tests, uint32_t *tests_passed,
                              uint32_t *avg_mcdc)
{
    uint32_t i;
    uint32_t met = 0U;
    uint32_t passed = 0U;
    uint32_t coverage_sum = 0U;
    uint32_t coverage_count = 0U;

    if (total_reqs != NULL)
    {
        *total_reqs = s_req_count;
    }

    if (total_tests != NULL)
    {
        *total_tests = s_test_count;
    }

    for (i = 0U; i < CERT_MAX_REQUIREMENTS; i++)
    {
        if (s_req_used[i] && (s_requirements[i].status >= CERT_REQ_TESTED))
        {
            met++;
        }
    }

    for (i = 0U; i < CERT_MAX_TEST_CASES; i++)
    {
        if (s_test_used[i])
        {
            if (s_tests[i].result == CERT_TEST_PASSED)
            {
                passed++;
            }
            coverage_sum += s_tests[i].coverage_percent;
            coverage_count++;
        }
    }

    if (reqs_met != NULL)
    {
        *reqs_met = met;
    }

    if (tests_passed != NULL)
    {
        *tests_passed = passed;
    }

    if (avg_mcdc != NULL)
    {
        if (coverage_count > 0U)
        {
            *avg_mcdc = coverage_sum / coverage_count;
        }
        else
        {
            *avg_mcdc = 0U;
        }
    }
}
