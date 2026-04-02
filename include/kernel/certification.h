/**
 * @file    certification.h
 * @brief   安全认证框架接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 功能安全认证框架：
 *          - IEC 61508 (SIL 1~4) 合规管理
 *          - ISO 26262 (ASIL A~D) 合规管理
 *          - 认证证据收集与追踪
 *          - 安全需求到测试的追溯矩阵
 *          - MC/DC 覆盖率统计
 *          - 安全案例管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SE-006~009
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_CERTIFICATION_H
#define KERNEL_CERTIFICATION_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 认证常量
 * ======================================================================== */

/** @brief 最大需求数 */
#define CERT_MAX_REQUIREMENTS        256U

/** @brief 最大测试用例数 */
#define CERT_MAX_TEST_CASES          512U

/** @brief 最大追溯链接数 */
#define CERT_MAX_TRACES             1024U

/** @brief 最大模块数 */
#define CERT_MAX_MODULES             32U

/** @brief 需求 ID 最大长度 */
#define CERT_REQ_ID_MAX              16U

/* ========================================================================
 * 认证标准枚举
 * ======================================================================== */

/**
 * @brief 安全完整性等级（IEC 61508）
 */
typedef enum
{
    CERT_SIL_NONE = 0U,     /**< @brief 无 SIL 要求 */
    CERT_SIL_1,              /**< @brief SIL 1 (最低) */
    CERT_SIL_2,              /**< @brief SIL 2 */
    CERT_SIL_3,              /**< @brief SIL 3 */
    CERT_SIL_4               /**< @brief SIL 4 (最高) */
} cert_sil_t;

/**
 * @brief 汽车安全完整性等级（ISO 26262）
 */
typedef enum
{
    CERT_ASIL_NONE = 0U,   /**< @brief 无 ASIL 要求 */
    CERT_ASIL_A,            /**< @brief ASIL A (最低) */
    CERT_ASIL_B,            /**< @brief ASIL B */
    CERT_ASIL_C,            /**< @brief ASIL C */
    CERT_ASIL_D             /**< @brief ASIL D (最高) */
} cert_asil_t;

/**
 * @brief 认证需求状态
 */
typedef enum
{
    CERT_REQ_DRAFT = 0U,     /**< @brief 草稿 */
    CERT_REQ_APPROVED,        /**< @brief 已批准 */
    CERT_REQ_IMPLEMENTED,     /**< @brief 已实现 */
    CERT_REQ_TESTED,          /**< @brief 已测试 */
    CERT_REQ_VERIFIED,        /**< @brief 已验证 */
    CERT_REQ_CERTIFIED        /**< @brief 已认证 */
} cert_req_status_t;

/**
 * @brief 测试结果
 */
typedef enum
{
    CERT_TEST_PENDING = 0U,   /**< @brief 待执行 */
    CERT_TEST_PASSED,          /**< @brief 通过 */
    CERT_TEST_FAILED,          /**< @brief 失败 */
    CERT_TEST_BLOCKED,         /**< @brief 阻塞 */
    CERT_TEST_SKIPPED          /**< @brief 跳过 */
} cert_test_result_t;

/* ========================================================================
 * 认证数据结构
 * ======================================================================== */

/**
 * @brief 安全需求条目
 */
typedef struct
{
    char              req_id[CERT_REQ_ID_MAX]; /**< @brief 需求 ID（如 KR-001） */
    char              module[16];              /**< @brief 所属模块 */
    cert_sil_t        target_sil;            /**< @brief 目标 SIL */
    cert_asil_t       target_asil;           /**< @brief 目标 ASIL */
    cert_req_status_t status;                /**< @brief 状态 */
    uint32_t          test_count;            /**< @brief 关联测试数 */
    uint32_t          test_passed;           /**< @brief 通过数 */
    bool              has_trace;             /**< @brief 是否有追溯链 */
} cert_requirement_t;

/**
 * @brief 测试用例条目
 */
typedef struct
{
    uint32_t           test_id;             /**< @brief 测试 ID */
    char               req_id[CERT_REQ_ID_MAX]; /**< @brief 关联需求 ID */
    char               name[32];             /**< @brief 测试名称 */
    cert_test_result_t result;              /**< @brief 测试结果 */
    uint32_t           coverage_percent;     /**< @brief MC/DC 覆盖率（% * 10） */
    uint64_t           executed_at;          /**< @brief 执行时间戳 */
    bool               active;              /**< @brief 活跃标记 */
} cert_test_case_t;

/**
 * @brief 追溯矩阵条目
 */
typedef struct
{
    char    req_id[CERT_REQ_ID_MAX];  /**< @brief 需求 ID */
    uint32_t test_id;                    /**< @brief 测试 ID */
    uint32_t source_file_hash;          /**< @brief 源文件哈希（简化） */
    uint32_t line_start;                /**< @brief 起始行号 */
    uint32_t line_end;                  /**< @brief 结束行号 */
    bool    verified;                    /**< @brief 验证通过 */
} cert_trace_entry_t;

/**
 * @brief 模块覆盖率统计
 */
typedef struct
{
    char        module[16];              /**< @brief 模块名 */
    uint32_t    total_requirements;      /**< @brief 总需求数 */
    uint32_t    requirements_met;        /**< @brief 已满足需求数 */
    uint32_t    total_tests;             /**< @brief 总测试数 */
    uint32_t    tests_passed;            /**< @brief 通过测试数 */
    uint32_t    mcdc_coverage;           /**< @brief MC/DC 覆盖率（% * 10） */
    cert_sil_t  achieved_sil;           /**< @brief 已达 SIL */
    cert_asil_t achieved_asil;          /**< @brief 已达 ASIL */
} cert_module_stats_t;

/* ========================================================================
 * 认证框架 API
 * ======================================================================== */

/**
 * @brief 初始化认证框架
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: SE-006
 */
kernel_status_t cert_init(void);

/**
 * @brief 注册安全需求
 *
 * @param req_id   需求 ID（如 "KR-001"）
 * @param module   所属模块
 * @param sil      目标 SIL
 * @param asil     目标 ASIL
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: SE-006
 */
kernel_status_t cert_register_requirement(const char *req_id, const char *module,
                                            cert_sil_t sil, cert_asil_t asil);

/**
 * @brief 更新需求状态
 *
 * @param req_id  需求 ID
 * @param status  新状态
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t cert_update_req_status(const char *req_id, cert_req_status_t status);

/**
 * @brief 注册测试用例
 *
 * @param req_id 关联需求 ID
 * @param name   测试名称
 *
 * @return 成功返回测试 ID，失败返回负错误码
 *
 * @note 对应需求: SE-007
 */
int32_t cert_register_test(const char *req_id, const char *name);

/**
 * @brief 记录测试结果
 *
 * @param test_id 测试 ID
 * @param result  测试结果
 * @param coverage MC/DC 覆盖率
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: SE-007
 */
kernel_status_t cert_record_test_result(uint32_t test_id,
                                          cert_test_result_t result,
                                          uint32_t coverage);

/**
 * @brief 添加追溯链
 *
 * @param req_id      需求 ID
 * @param test_id     测试 ID
 * @param file_hash   源文件哈希
 * @param line_start  起始行
 * @param line_end    结束行
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: SE-008
 */
kernel_status_t cert_add_trace(const char *req_id, uint32_t test_id,
                                 uint32_t file_hash, uint32_t line_start,
                                 uint32_t line_end);

/**
 * @brief 获取模块认证统计
 *
 * @param module 模块名
 * @param[out] stats 输出统计
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t cert_get_module_stats(const char *module,
                                        cert_module_stats_t *stats);

/**
 * @brief 检查认证合规性
 *
 * @details 检查所有需求是否达到目标 SIL/ASIL 等级
 *
 * @return KERNEL_OK 全部合规
 *
 * @note 对应需求: SE-009
 */
kernel_status_t cert_check_compliance(void);

/**
 * @brief 获取全局认证报告
 *
 * @param[out] total_reqs      总需求数
 * @param[out] reqs_met        已满足需求数
 * @param[out] total_tests     总测试数
 * @param[out] tests_passed    通过测试数
 * @param[out] avg_mcdc        平均 MC/DC 覆盖率
 */
void cert_get_global_report(uint32_t *total_reqs, uint32_t *reqs_met,
                              uint32_t *total_tests, uint32_t *tests_passed,
                              uint32_t *avg_mcdc);

#endif /* KERNEL_CERTIFICATION_H */
