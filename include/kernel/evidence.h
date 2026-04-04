/**
 * @file    evidence.h
 * @brief   认证证据收集接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 认证证据收集接口：
 *          - 验证证据记录
 *          - 追溯矩阵管理
 *          - MC/DC 覆盖率数据
 *          - 合规性报告生成
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SE-009~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_EVIDENCE_H
#define KERNEL_EVIDENCE_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 证据类型
 * ======================================================================== */

/**
 * @brief 证据类型枚举
 */
typedef enum
{
    EVIDENCE_UNIT_TEST       = 0U,   /**< @brief 单元测试通过 */
    EVIDENCE_INTEGRATION     = 1U,   /**< @brief 集成测试通过 */
    EVIDENCE_COVERAGE        = 2U,   /**< @brief 覆盖率达标 */
    EVIDENCE_TRACEABILITY    = 3U,   /**< @brief 追溯矩阵完整 */
    EVIDENCE_CODE_REVIEW     = 4U,   /**< @brief 代码审查完成 */
    EVIDENCE_STATIC_ANALYSIS = 5U,   /**< @brief 静态分析通过 */
    EVIDENCE_FORMAL_VERIFY   = 6U,   /**< @brief 形式化验证通过 */
    EVIDENCE_LOAD_TEST       = 7U,   /**< @brief 负载测试通过 */
    EVIDENCE_DEMO_TEST       = 8U,   /**< @brief 演示测试通过 */
    EVIDENCE_WCET_ANALYSIS   = 9U,   /**< @brief WCET 分析完成 */
    EVIDENCE_FMEA_REPORT     = 10U,  /**< @brief FMEA 报告完成 */
    EVIDENCE_SIL_ASSESSMENT  = 11U,  /**< @brief SIL 评估完成 */
    EVIDENCE_ASIL_ASSESSMENT = 12U,  /**< @brief ASIL 评估完成 */
    EVIDENCE_COMPLIANCE      = 13U,  /**< @brief 合规性检查通过 */
    EVIDENCE_CERTIFICATION   = 14U   /**< @brief 认证就绪 */
} evidence_type_t;

/* ========================================================================
 * 模块证据状态
 * ======================================================================== */

/**
 * @brief 模块证据状态
 */
typedef struct
{
    char            module[16];              /**< @brief 模块名 */
    bool            evidence_collected[16];  /**< @brief 各类证据收集状态 */
    bool            verified[16];            /**< @brief 各类证据验证状态 */
    uint32_t        coverage_percent;        /**< @brief MC/DC 覆盖率百分比 */
    uint32_t        tests_total;             /**< @brief 测试总数 */
    uint32_t        tests_passed;            /**< @brief 通过测试数 */
    uint32_t        tests_failed;            /**< @brief 失败测试数 */
} module_evidence_t;

/* ========================================================================
 * 全局证据状态
 * ======================================================================== */

/**
 * @brief 全局证据状态
 */
typedef struct
{
    module_evidence_t modules[32];   /**< @brief 模块证据 */
    uint32_t          module_count;  /**< @brief 已注册模块数 */
    uint32_t          total_tests;   /**< @brief 总测试数 */
    uint32_t          total_passed;  /**< @brief 总通过数 */
    uint32_t          total_failed;  /**< @brief 总失败数 */
    uint32_t          avg_coverage;  /**< @brief 平均 MC/DC 覆盖率 */
    uint32_t          requirements_met;   /**< @brief 已满足需求数 */
    uint32_t          requirements_total; /**< @brief 总需求数 */
    bool              certified;          /**< @brief 认证就绪 */
} evidence_state_t;

/* ========================================================================
 * 证据收集 API
 * ======================================================================== */

/**
 * @brief 初始化证据收集框架
 */
void evidence_init(void);

/**
 * @brief 注册模块
 *
 * @param module 模块名
 *
 * @return 0 成功，负数表示错误
 */
int32_t evidence_register_module(const char *module);

/**
 * @brief 记录测试结果
 *
 * @param module   模块名
 * @param total    测试总数
 * @param passed   通过数
 * @param failed   失败数
 * @param coverage MC/DC 覆盖率百分比
 */
void evidence_record_test(const char *module,
                           uint32_t total, uint32_t passed,
                           uint32_t failed, uint32_t coverage);

/**
 * @brief 标记证据项
 *
 * @param module   模块名
 * @param type     证据类型
 * @param verified 是否已验证
 */
void evidence_set_item(const char *module, evidence_type_t type,
                        bool verified);

/**
 * @brief 查询证据状态
 *
 * @param module 模块名
 * @param type   证据类型
 *
 * @return 是否已收集并验证
 */
bool evidence_check(const char *module, evidence_type_t type);

/**
 * @brief 获取模块证据
 *
 * @param module 模块名
 * @param ev     输出模块证据结构
 *
 * @return 0 成功，负数表示错误
 */
int32_t evidence_get_module(const char *module, module_evidence_t *ev);

/**
 * @brief 获取全局证据状态
 *
 * @param state 输出全局证据
 */
void evidence_get_global(evidence_state_t *state);

/**
 * @brief 检查合规性
 *
 * @return KERNEL_OK 合规
 */
int32_t evidence_check_compliance(void);

/**
 * @brief 生成证据报告
 *
 * @param buf      输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 实际写入字节数
 */
uint32_t evidence_generate_report(char *buf, uint32_t buf_size);

#endif /* KERNEL_EVIDENCE_H */
