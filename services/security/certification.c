/**
 * @file    certification.c
 * @brief   安全认证框架实现
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 3.0
 *
 * @details 功能安全认证框架核心实现：
 *          - 安全需求注册与状态追踪
 *          - 测试用例管理
 *          - 需求→测试→代码 追溯矩阵
 *          - 模块覆盖率统计
 *          - ISO 26262 ASIL-D 证据项收集
 *          - IEC 61508 SIL-4 证据项收集
 *          - 代码覆盖率统计框架
 *          - 合规性检查
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SE-006~015
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/certification.h>
#include <kernel/security.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 认证常量扩展
 * ======================================================================== */

/** @brief 最大证据项数 */
#define CERT_MAX_EVIDENCE         256U

/** @brief 最大覆盖率记录数 */
#define CERT_MAX_COVERAGE_ENTRIES 128U

/** @brief 证据描述最大长度 */
#define CERT_EVIDENCE_DESC_MAX    64U

/** @brief 源文件路径最大长度 */
#define CERT_FILE_PATH_MAX        48U

/* ========================================================================
 * ISO 26262 ASIL-D 证据类型
 * ======================================================================== */

/**
 * @brief ASIL-D 证据类型
 */
typedef enum
{
    EVID_ASIL_D_SRS = 0U,          /**< @brief 软件需求规范 */
    EVID_ASIL_D_SAD,               /**< @brief 软件架构设计 */
    EVID_ASIL_D_SDD,               /**< @brief 软件详细设计 */
    EVID_ASIL_D_UNIT_TEST,         /**< @brief 单元测试报告 */
    EVID_ASIL_D_INT_TEST,          /**< @brief 集成测试报告 */
    EVID_ASIL_D_SYS_TEST,          /**< @brief 系统测试报告 */
    EVID_ASIL_D_SAST_REPORT,       /**< @brief 静态分析报告 */
    EVID_ASIL_D_MCDC_REPORT,       /**< @brief MC/DC 覆盖率报告 */
    EVID_ASIL_D_SAFE_ANALYSIS,     /**< @brief 安全分析报告（FMEA/FTA） */
    EVID_ASIL_D_CODE_REVIEW,       /**< @brief 代码审查记录 */
    EVID_ASIL_D_TRACE_MATRIX       /**< @brief 追溯矩阵 */
} evidence_asil_type_t;

/* ========================================================================
 * IEC 61508 SIL-4 证据类型
 * ======================================================================== */

/**
 * @brief SIL-4 证据类型
 */
typedef enum
{
    EVID_SIL4_FS_CONCEPT = 0U,     /**< @brief 功能安全概念 */
    EVID_SIL4_TECH_SAFETY,         /**< @brief 技术安全需求 */
    EVID_SIL4_SW_SAFETY_REQ,       /**< @brief 软件安全需求 */
    EVID_SIL4_SW_ARCH_DESIGN,      /**< @brief 软件架构设计 */
    EVID_SIL4_SW_MODULE_DESIGN,    /**< @brief 软件模块设计 */
    EVID_SIL4_SW_CODING_STD,       /**< @brief 编码标准合规 */
    EVID_SIL4_SW_UNIT_TEST,        /**< @brief 软件单元测试 */
    EVID_SIL4_SW_INT_TEST,         /**< @brief 软件集成测试 */
    EVID_SIL4_SW_PRE_COMMISSION,   /**< @brief 软件预验证 */
    EVID_SIL4_SW_VAL_TEST,         /**< @brief 软件验证测试 */
    EVID_SIL4_SW_MOD_ANALYSIS,     /**< @brief 模块化分析 */
    EVID_SIL4_SW_COVERAGE,         /**< @brief 软件覆盖率分析 */
    EVID_SIL4_STATIC_ANALYSIS,     /**< @brief 静态分析报告 */
    EVID_SIL4_FMEA_REPORT,         /**< @brief FMEA 报告 */
    EVID_SIL4_FTA_REPORT           /**< @brief FTA 报告 */
} evidence_sil4_type_t;

/* ========================================================================
 * 覆盖率类型
 * ======================================================================== */

/**
 * @brief 代码覆盖率类型
 */
typedef enum
{
    COV_TYPE_STATEMENT = 0U,  /**< @brief 语句覆盖率 */
    COV_TYPE_BRANCH,          /**< @brief 分支覆盖率 */
    COV_TYPE_MCDC,            /**< @brief MC/DC 覆盖率 */
    COV_TYPE_FUNCTION         /**< @brief 函数覆盖率 */
} coverage_type_t;

/* ========================================================================
 * 证据项数据结构
 * ======================================================================== */

/**
 * @brief 认证证据条目
 */
typedef struct
{
    char     evidence_id[CERT_REQ_ID_MAX];   /**< @brief 证据 ID */
    char     description[CERT_EVIDENCE_DESC_MAX]; /**< @brief 描述 */
    char     linked_req[CERT_REQ_ID_MAX];    /**< @brief 关联需求 ID */
    char     file_path[CERT_FILE_PATH_MAX];  /**< @brief 关联文件路径 */
    uint32_t evidence_type;                  /**< @brief 证据类型 */
    cert_sil_t sil_level;                    /**< @brief SIL 等级 */
    cert_asil_t asil_level;                  /**< @brief ASIL 等级 */
    bool     collected;                      /**< @brief 已收集标记 */
    bool     verified;                       /**< @brief 已验证标记 */
    bool     active;                         /**< @brief 活跃标记 */
} cert_evidence_t;

/**
 * @brief 代码覆盖率记录
 */
typedef struct
{
    char     file_path[CERT_FILE_PATH_MAX]; /**< @brief 源文件路径 */
    char     module[16];                    /**< @brief 所属模块 */
    coverage_type_t type;                   /**< @brief 覆盖率类型 */
    uint32_t total_items;                   /**< @brief 总项目数 */
    uint32_t covered_items;                 /**< @brief 已覆盖项目数 */
    uint32_t percentage;                    /**< @brief 覆盖率百分比 */
    bool     active;                        /**< @brief 活跃标记 */
} cert_coverage_entry_t;

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

/** @brief 证据项表 */
static cert_evidence_t s_evidences[CERT_MAX_EVIDENCE];

/** @brief 证据计数 */
static uint32_t s_evidence_count;

/** @brief 覆盖率记录表 */
static cert_coverage_entry_t s_coverage[CERT_MAX_COVERAGE_ENTRIES];

/** @brief 覆盖率计数 */
static uint32_t s_coverage_count;

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
    (void)memset(s_evidences, 0, sizeof(s_evidences));
    (void)memset(s_coverage, 0, sizeof(s_coverage));

    s_req_count = 0U;
    s_test_count = 0U;
    s_trace_count = 0U;
    s_evidence_count = 0U;
    s_coverage_count = 0U;
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

    if (cert_find_req(req_id) >= 0)
    {
        return -(int32_t)17;
    }

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

    {
        int32_t mod_idx = cert_find_or_create_module(module);
        if (mod_idx >= 0)
        {
            s_module_stats[(uint32_t)mod_idx].total_requirements++;
        }
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

    s_requirements[(uint32_t)req_idx].test_count++;

    {
        int32_t mod_idx = cert_find_or_create_module(
            s_requirements[(uint32_t)req_idx].module);
        if (mod_idx >= 0)
        {
            s_module_stats[(uint32_t)mod_idx].total_tests++;
        }
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

    s_requirements[(uint32_t)req_idx].has_trace = true;

    return KERNEL_OK;
}

/* ========================================================================
 * ISO 26262 ASIL-D 证据收集
 * ======================================================================== */

/**
 * @brief 注册 ASIL-D 证据项
 *
 * @param evidence_id  证据 ID
 * @param type         证据类型
 * @param linked_req   关联需求 ID
 * @param description  描述
 * @param file_path    关联文件路径
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t cert_register_asil_evidence(const char *evidence_id,
                                              evidence_asil_type_t type,
                                              const char *linked_req,
                                              const char *description,
                                              const char *file_path)
{
    uint32_t i;

    if (!s_initialized)
    {
        return -(int32_t)22;
    }

    if ((evidence_id == NULL) || (description == NULL))
    {
        return -(int32_t)22;
    }

    if (s_evidence_count >= CERT_MAX_EVIDENCE)
    {
        return -(int32_t)12;
    }

    for (i = 0U; i < CERT_MAX_EVIDENCE; i++)
    {
        if (!s_evidences[i].active)
        {
            break;
        }
    }

    if (i >= CERT_MAX_EVIDENCE)
    {
        return -(int32_t)12;
    }

    cert_strcpy(s_evidences[i].evidence_id, evidence_id, CERT_REQ_ID_MAX);
    cert_strcpy(s_evidences[i].description, description, CERT_EVIDENCE_DESC_MAX);
    s_evidences[i].evidence_type = (uint32_t)type;
    s_evidences[i].asil_level = CERT_ASIL_D;
    s_evidences[i].sil_level = CERT_SIL_NONE;
    s_evidences[i].collected = false;
    s_evidences[i].verified = false;
    s_evidences[i].active = true;

    if (linked_req != NULL)
    {
        cert_strcpy(s_evidences[i].linked_req, linked_req, CERT_REQ_ID_MAX);
    }

    if (file_path != NULL)
    {
        cert_strcpy(s_evidences[i].file_path, file_path, CERT_FILE_PATH_MAX);
    }

    s_evidence_count++;

    return KERNEL_OK;
}

/* ========================================================================
 * IEC 61508 SIL-4 证据收集
 * ======================================================================== */

/**
 * @brief 注册 SIL-4 证据项
 *
 * @param evidence_id  证据 ID
 * @param type         证据类型
 * @param linked_req   关联需求 ID
 * @param description  描述
 * @param file_path    关联文件路径
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t cert_register_sil4_evidence(const char *evidence_id,
                                              evidence_sil4_type_t type,
                                              const char *linked_req,
                                              const char *description,
                                              const char *file_path)
{
    uint32_t i;

    if (!s_initialized)
    {
        return -(int32_t)22;
    }

    if ((evidence_id == NULL) || (description == NULL))
    {
        return -(int32_t)22;
    }

    if (s_evidence_count >= CERT_MAX_EVIDENCE)
    {
        return -(int32_t)12;
    }

    for (i = 0U; i < CERT_MAX_EVIDENCE; i++)
    {
        if (!s_evidences[i].active)
        {
            break;
        }
    }

    if (i >= CERT_MAX_EVIDENCE)
    {
        return -(int32_t)12;
    }

    cert_strcpy(s_evidences[i].evidence_id, evidence_id, CERT_REQ_ID_MAX);
    cert_strcpy(s_evidences[i].description, description, CERT_EVIDENCE_DESC_MAX);
    s_evidences[i].evidence_type = (uint32_t)type;
    s_evidences[i].sil_level = CERT_SIL_4;
    s_evidences[i].asil_level = CERT_ASIL_NONE;
    s_evidences[i].collected = false;
    s_evidences[i].verified = false;
    s_evidences[i].active = true;

    if (linked_req != NULL)
    {
        cert_strcpy(s_evidences[i].linked_req, linked_req, CERT_REQ_ID_MAX);
    }

    if (file_path != NULL)
    {
        cert_strcpy(s_evidences[i].file_path, file_path, CERT_FILE_PATH_MAX);
    }

    s_evidence_count++;

    return KERNEL_OK;
}

/**
 * @brief 标记证据已收集
 *
 * @param evidence_id 证据 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t cert_mark_evidence_collected(const char *evidence_id)
{
    uint32_t i;

    if (evidence_id == NULL)
    {
        return -(int32_t)22;
    }

    for (i = 0U; i < CERT_MAX_EVIDENCE; i++)
    {
        if (s_evidences[i].active &&
            cert_streq(s_evidences[i].evidence_id, evidence_id))
        {
            s_evidences[i].collected = true;
            return KERNEL_OK;
        }
    }

    return -(int32_t)2;
}

/**
 * @brief 验证证据
 *
 * @param evidence_id 证据 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t cert_verify_evidence(const char *evidence_id)
{
    uint32_t i;

    if (evidence_id == NULL)
    {
        return -(int32_t)22;
    }

    for (i = 0U; i < CERT_MAX_EVIDENCE; i++)
    {
        if (s_evidences[i].active &&
            cert_streq(s_evidences[i].evidence_id, evidence_id))
        {
            if (!s_evidences[i].collected)
            {
                return -(int32_t)22;
            }
            s_evidences[i].verified = true;
            return KERNEL_OK;
        }
    }

    return -(int32_t)2;
}

/* ========================================================================
 * 代码覆盖率统计框架
 * ======================================================================== */

/**
 * @brief 注册覆盖率记录
 *
 * @param file_path   源文件路径
 * @param module      模块名
 * @param type        覆盖率类型
 * @param total       总项目数
 * @param covered     已覆盖项目数
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t cert_record_coverage(const char *file_path, const char *module,
                                       coverage_type_t type,
                                       uint32_t total, uint32_t covered)
{
    uint32_t i;

    if (!s_initialized)
    {
        return -(int32_t)22;
    }

    if ((file_path == NULL) || (module == NULL))
    {
        return -(int32_t)22;
    }

    if (total == 0U)
    {
        return -(int32_t)22;
    }

    if (s_coverage_count >= CERT_MAX_COVERAGE_ENTRIES)
    {
        return -(int32_t)12;
    }

    for (i = 0U; i < CERT_MAX_COVERAGE_ENTRIES; i++)
    {
        if (!s_coverage[i].active)
        {
            break;
        }
    }

    if (i >= CERT_MAX_COVERAGE_ENTRIES)
    {
        return -(int32_t)12;
    }

    cert_strcpy(s_coverage[i].file_path, file_path, CERT_FILE_PATH_MAX);
    cert_strcpy(s_coverage[i].module, module, 16U);
    s_coverage[i].type = type;
    s_coverage[i].total_items = total;
    s_coverage[i].covered_items = covered;
    s_coverage[i].percentage = (covered * 1000U) / total;
    s_coverage[i].active = true;

    s_coverage_count++;

    /* 更新模块 MC/DC 覆盖率 */
    if (type == COV_TYPE_MCDC)
    {
        int32_t mod_idx = cert_find_or_create_module(module);
        if (mod_idx >= 0)
        {
            s_module_stats[(uint32_t)mod_idx].mcdc_coverage =
                s_coverage[i].percentage;
        }
    }

    return KERNEL_OK;
}

/**
 * @brief 获取覆盖率报告
 *
 * @param type        覆盖率类型
 * @param total_out   总项目数输出
 * @param covered_out 已覆盖项目数输出
 * @param percent_out 百分比输出
 */
void cert_get_coverage_summary(coverage_type_t type,
                                 uint32_t *total_out,
                                 uint32_t *covered_out,
                                 uint32_t *percent_out)
{
    uint32_t i;
    uint32_t total = 0U;
    uint32_t covered = 0U;

    for (i = 0U; i < CERT_MAX_COVERAGE_ENTRIES; i++)
    {
        if (s_coverage[i].active && (s_coverage[i].type == type))
        {
            total += s_coverage[i].total_items;
            covered += s_coverage[i].covered_items;
        }
    }

    if (total_out != NULL)
    {
        *total_out = total;
    }
    if (covered_out != NULL)
    {
        *covered_out = covered;
    }
    if (percent_out != NULL)
    {
        if (total > 0U)
        {
            *percent_out = (covered * 1000U) / total;
        }
        else
        {
            *percent_out = 0U;
        }
    }
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

        if ((s_requirements[i].status >= CERT_REQ_TESTED) &&
            s_requirements[i].has_trace)
        {
            met_count++;
        }
    }

    if (met_count < s_req_count)
    {
        return -(int32_t)1;
    }

    /* 检查所有 ASIL-D 证据是否已验证 */
    for (i = 0U; i < CERT_MAX_EVIDENCE; i++)
    {
        if (s_evidences[i].active &&
            (s_evidences[i].asil_level == CERT_ASIL_D) &&
            !s_evidences[i].verified)
        {
            return -(int32_t)1;
        }
    }

    /* 检查所有 SIL-4 证据是否已验证 */
    for (i = 0U; i < CERT_MAX_EVIDENCE; i++)
    {
        if (s_evidences[i].active &&
            (s_evidences[i].sil_level == CERT_SIL_4) &&
            !s_evidences[i].verified)
        {
            return -(int32_t)1;
        }
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

/**
 * @brief 获取证据收集统计
 *
 * @param total      总证据数输出
 * @param collected  已收集数输出
 * @param verified   已验证数输出
 */
void cert_get_evidence_stats(uint32_t *total, uint32_t *collected,
                               uint32_t *verified)
{
    uint32_t i;
    uint32_t t = 0U;
    uint32_t c = 0U;
    uint32_t v = 0U;

    for (i = 0U; i < CERT_MAX_EVIDENCE; i++)
    {
        if (s_evidences[i].active)
        {
            t++;
            if (s_evidences[i].collected)
            {
                c++;
            }
            if (s_evidences[i].verified)
            {
                v++;
            }
        }
    }

    if (total != NULL)
    {
        *total = t;
    }
    if (collected != NULL)
    {
        *collected = c;
    }
    if (verified != NULL)
    {
        *verified = v;
    }
}
