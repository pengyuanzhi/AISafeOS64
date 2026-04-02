/**
 * @file    formal_verify.h
 * @brief   AISafe64 RTOS - 形式化验证接口桩
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 1.0
 *
 * @details 形式化验证框架接口：
 *          - 契约式验证（前置/后置/不变式条件）
 *          - 模型检查（状态机可达性、死锁自由）
 *          - 边界值分析（数组越界、整数溢出）
 *          - 时序约束验证
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SE-007, SE-008
 */

#ifndef FORMAL_VERIFY_H
#define FORMAL_VERIFY_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 验证条件类型
 * ======================================================================== */

/** @brief 验证条件类型 */
typedef enum
{
    FV_COND_PRECONDITION  = 0U,  /**< @brief 前置条件 */
    FV_COND_POSTCONDITION = 1U,  /**< @brief 后置条件 */
    FV_COND_INVARIANT   = 2U   /**< @brief 不变式条件 */
    FV_COND_ASSERTION   = 3U   /**< @brief 断言条件 */
    FV_COND_BOUNDARY   = 4U   /**< @brief 边界条件 */
    FV_COND_TIMING     = 5U   /**< @brief 时序约束 */
    FV_COND_LIVENESS   = 6U   /**< @brief 活性条件 */
    FV_COND_SAFETY    = 7U   /**< @brief 安全条件 */
    FV_COND_TYPE_MAX  = 8U
} fv_cond_type_t;

/** @brief 验证结果 */
typedef enum
{
    FV_RESULT_UNKNOWN  = 0U,
    FV_RESULT_PASS      = 1U,
    FV_RESULT_FAIL      = 2U,
    FV_RESULT_TIMEOUT   = 3U,
    FV_RESULT_ERROR     = 4U,
    FV_RESULT_SKIPPED   = 5U
} fv_result_t;

/** @brief 验证严重级别 */
typedef enum
{
    FV_SEVERITY_INFO     = 0U,
    FV_SEVERITY_WARNING  = 1U,
    FV_SEVERITY_ERROR   = 2U,
    FV_SEVERITY_FATAL   = 3U
} fv_severity_t;

/* ========================================================================
 * 验证条件描述
 * ======================================================================== */

#define FV_DESC_MAX  128U
#define FV_FUNC_NAME_MAX 64U
#define FV_MODULE_MAX  32U

/** @brief 验证条件 */
typedef struct
{
    uint32_t        cond_id;       /**< @brief 条件 ID */
    char            desc[FV_DESC_MAX]; /**< @brief 条件描述 */
    fv_cond_type_t  type;          /**< @brief 条件类型 */
    char            function_name[FV_FUNC_NAME_MAX]; /**< @brief 关联函数 */
    char            module[FV_MODULE_MAX]; /**< @brief 所属模块 */
    fv_severity_t   severity;      /**< @brief 严重级别 */
    fv_result_t     result;        /**< @brief 验证结果 */
    bool            enabled;       /**< @brief 是否启用 */
    bool            verified;      /**< @brief 是否已验证 */
} fv_condition_t;

/* ========================================================================
 * 验证统计
 * ======================================================================== */

/** @brief 验证统计 */
typedef struct
{
    uint32_t total_conditions;   /**< @brief 总条件数 */
    uint32_t verified_pass;      /**< @brief 验证通过数 */
    uint32_t verified_fail;      /**< @brief 验证失败数 */
    uint32_t skipped;            /**< @brief 跳过数 */
    uint32_t not_verified;       /**< @brief 未验证数 */
    uint32_t safety_conditions;  /**< @brief 安全条件数 */
    uint32_t safety_passed;      /**< @brief 安全条件通过数 */
} fv_stats_t;

/* ========================================================================
 * 形式化验证 API
 * ======================================================================== */

/**
 * @brief 初始化形式化验证框架
 *
 * @return 0 成功
 */
int32_t fv_init(void);

/**
 * @brief 注册验证条件
 *
 * @param desc         条件描述
 * @param type         条件类型
 * @param function_name 关联函数名
 * @param module       所属模块
 * @param severity     严重级别
 *
 * @return 条件 ID，负数表示错误
 */
int32_t fv_register_condition(const char *desc, fv_cond_type_t type,
                                const char *function_name, const char *module,
                                fv_severity_t severity);

/**
 * @brief 验证单个条件
 *
 * @param cond_id 条件 ID
 *
 * @return 验证结果
 */
fv_result_t fv_verify_condition(uint32_t cond_id);

/**
 * @brief 验证指定模块的所有条件
 *
 * @param module 模块名
 *
 * @return 通过的条件数，负数表示错误
 */
int32_t fv_verify_module(const char *module);

/**
 * @brief 验证所有条件
 *
 * @return 通过的条件数
 */
int32_t fv_verify_all(void);

/**
 * @brief 获取验证统计
 *
 * @param stats 输出统计
 */
void fv_get_stats(fv_stats_t *stats);

/**
 * @brief 获取条件信息
 *
 * @param cond_id 条件 ID
 * @param cond    输出条件
 *
 * @return 0 成功
 */
int32_t fv_get_condition(uint32_t cond_id, fv_condition_t *cond);

/**
 * @brief 启用/禁用条件
 *
 * @param cond_id 条件 ID
 * @param enable 启用标志
 *
 * @return 0 成功
 */
int32_t fv_set_enabled(uint32_t cond_id, bool enable);

/**
 * @brief 生成验证报告
 *
 * @param buf   输出缓冲区
 * @param size  缓冲区大小
 *
 * @return 实际写入字节数
 */
int32_t fv_generate_report(char *buf, uint32_t size);

#endif /* FORMAL_VERIFY_H */
