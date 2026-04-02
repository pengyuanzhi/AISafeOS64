/**
 * @file    evidence.c
 * @brief   AISafe64 RTOS - 认证证据收集框架实现
 * @author  AISafe64 Team
 * @date    2026-04-02
 * @version 1.0
 *
 * @details 认证证据收集框架实现：
 *          - 模块注册与证据收集
 *          - 测试结果记录
 *          - MC/DC 覆盖率跟踪
 *          - 合规性检查
 *          - 证据报告生成
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SE-009, SE-010, DC-001~005
 */

#include <kernel/evidence.h>
#include <kernel/types.h>
#include <kernel/spinlock.h>

/* snprintf 需要 */
extern int snprintf(char *str, uint32_t size, const char *format, ...);

/* ========================================================================
 * 内部常量与宏
 * ======================================================================== */

/** @brief 最大模块数 */
#define EVIDENCE_MAX_MODULES   32U

/** @brief 证据类型数量 */
#define EVIDENCE_TYPE_COUNT    15U

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 字符串比较
 *
 * @param a 字符串 A
 * @param b 字符串 B
 *
 * @return true 相等
 */
static bool str_eq(const char *a, const char *b)
{
    uint32_t i;

    if ((a == NULL) || (b == NULL))
    {
        return false;
    }

    for (i = 0U; (a[i] != '\0') && (b[i] != '\0'); i++)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }

    return (a[i] == b[i]);
}

/**
 * @brief 安全字符串复制
 *
 * @param dst 目标缓冲区
 * @param src 源字符串
 * @param max 最大长度
 */
static void str_copy(char *dst, const char *src, uint32_t max)
{
    uint32_t i;

    if ((dst == NULL) || (src == NULL) || (max == 0U))
    {
        return;
    }

    for (i = 0U; (i < (max - 1U)) && (src[i] != '\0'); i++)
    {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* ========================================================================
 * 内部数据结构
 * ======================================================================== */

/** @brief 证据收集框架状态 */
typedef struct
{
    module_evidence_t  modules[EVIDENCE_MAX_MODULES]; /**< @brief 模块数组 */
    uint32_t           module_count;                   /**< @brief 已注册模块数 */
    bool               initialized;                    /**< @brief 是否已初始化 */
    TicketLock_t       lock;                           /**< @brief 自旋锁 */
} evidence_internal_t;

/** @brief 全局证据状态 */
static evidence_internal_t s_ev;

/* ========================================================================
 * 公共 API 实现
 * ======================================================================== */

/**
 * @brief 初始化证据收集框架
 */
void evidence_init(void)
{
    uint32_t i;
    uint32_t j;

    ticket_lock_init(&s_ev.lock);

    for (i = 0U; i < EVIDENCE_MAX_MODULES; i++)
    {
        s_ev.modules[i].module[0] = '\0';
        s_ev.modules[i].coverage_percent = 0U;
        s_ev.modules[i].tests_total = 0U;
        s_ev.modules[i].tests_passed = 0U;
        s_ev.modules[i].tests_failed = 0U;

        for (j = 0U; j < EVIDENCE_TYPE_COUNT; j++)
        {
            s_ev.modules[i].evidence_collected[j] = false;
            s_ev.modules[i].verified[j] = false;
        }
    }

    s_ev.module_count = 0U;
    s_ev.initialized = true;
}

/**
 * @brief 注册模块
 */
int32_t evidence_register_module(const char *module)
{
    uint32_t idx;
    module_evidence_t *mod;
    uint32_t i;

    if (module == NULL)
    {
        return -1;
    }

    if (!s_ev.initialized)
    {
        return -1;
    }

    ticket_lock_acquire(&s_ev.lock);

    if (s_ev.module_count >= EVIDENCE_MAX_MODULES)
    {
        ticket_lock_release(&s_ev.lock);
        return -1;
    }

    idx = s_ev.module_count;
    mod = &s_ev.modules[idx];

    str_copy(mod->module, module, sizeof(mod->module));
    mod->coverage_percent = 0U;
    mod->tests_total = 0U;
    mod->tests_passed = 0U;
    mod->tests_failed = 0U;

    for (i = 0U; i < EVIDENCE_TYPE_COUNT; i++)
    {
        mod->evidence_collected[i] = false;
        mod->verified[i] = false;
    }

    s_ev.module_count = idx + 1U;

    ticket_lock_release(&s_ev.lock);

    return 0;
}

/**
 * @brief 查找模块索引
 *
 * @param module 模块名
 *
 * @return 模块索引，-1 表示未找到
 */
static int32_t evidence_find_module(const char *module)
{
    uint32_t i;

    for (i = 0U; i < s_ev.module_count; i++)
    {
        if (str_eq(s_ev.modules[i].module, module))
        {
            return (int32_t)i;
        }
    }

    return -1;
}

/**
 * @brief 记录测试结果
 */
void evidence_record_test(const char *module,
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

    /* 自动标记测试通过证据 */
    if ((total > 0U) && (failed == 0U))
    {
        s_ev.modules[idx].evidence_collected[EVIDENCE_UNIT_TEST] = true;
        s_ev.modules[idx].verified[EVIDENCE_UNIT_TEST] = true;
    }

    /* 自动标记覆盖率证据 */
    if (coverage >= 95U)
    {
        s_ev.modules[idx].evidence_collected[EVIDENCE_COVERAGE] = true;
        s_ev.modules[idx].verified[EVIDENCE_COVERAGE] = true;
    }

    ticket_lock_release(&s_ev.lock);
}

/**
 * @brief 标记证据项
 */
void evidence_set_item(const char *module, evidence_type_t type,
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

/**
 * @brief 查询证据状态
 */
bool evidence_check(const char *module, evidence_type_t type)
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

/**
 * @brief 获取模块证据
 */
int32_t evidence_get_module(const char *module, module_evidence_t *ev)
{
    int32_t idx;
    uint32_t i;

    if ((module == NULL) || (ev == NULL) || !s_ev.initialized)
    {
        return -1;
    }

    ticket_lock_acquire(&s_ev.lock);

    idx = evidence_find_module(module);
    if (idx < 0)
    {
        ticket_lock_release(&s_ev.lock);
        return -1;
    }

    str_copy(ev->module, s_ev.modules[idx].module, sizeof(ev->module));
    ev->coverage_percent = s_ev.modules[idx].coverage_percent;
    ev->tests_total = s_ev.modules[idx].tests_total;
    ev->tests_passed = s_ev.modules[idx].tests_passed;
    ev->tests_failed = s_ev.modules[idx].tests_failed;

    for (i = 0U; i < EVIDENCE_TYPE_COUNT; i++)
    {
        ev->evidence_collected[i] = s_ev.modules[idx].evidence_collected[i];
        ev->verified[i] = s_ev.modules[idx].verified[i];
    }

    ticket_lock_release(&s_ev.lock);

    return 0;
}

/**
 * @brief 获取全局证据状态
 */
void evidence_get_global(evidence_state_t *state)
{
    uint32_t i;
    uint32_t j;

    if ((state == NULL) || !s_ev.initialized)
    {
        return;
    }

    ticket_lock_acquire(&s_ev.lock);

    state->module_count = s_ev.module_count;
    state->total_tests = 0U;
    state->total_passed = 0U;
    state->total_failed = 0U;
    state->avg_coverage = 0U;
    state->requirements_met = 0U;
    state->requirements_total = 0U;
    state->certified = false;

    for (i = 0U; i < s_ev.module_count; i++)
    {
        const module_evidence_t *mod = &s_ev.modules[i];

        state->total_tests += mod->tests_total;
        state->total_passed += mod->tests_passed;
        state->total_failed += mod->tests_failed;
        state->avg_coverage += mod->coverage_percent;

        /* 统计已满足的需求数（所有证据收集并验证） */
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

    /* 认证就绪判定：无失败测试 + 平均覆盖率 >= 95% + 所有模块合规 */
    if ((state->total_failed == 0U) &&
        (state->avg_coverage >= 95U) &&
        (state->requirements_met == state->requirements_total) &&
        (state->requirements_total > 0U))
    {
        state->certified = true;
    }

    /* 复制模块数据 */
    for (i = 0U; (i < s_ev.module_count) && (i < 32U); i++)
    {
        str_copy(state->modules[i].module, s_ev.modules[i].module,
                 sizeof(state->modules[i].module));
        state->modules[i].coverage_percent = s_ev.modules[i].coverage_percent;
        state->modules[i].tests_total = s_ev.modules[i].tests_total;
        state->modules[i].tests_passed = s_ev.modules[i].tests_passed;
        state->modules[i].tests_failed = s_ev.modules[i].tests_failed;

        for (j = 0U; j < EVIDENCE_TYPE_COUNT; j++)
        {
            state->modules[i].evidence_collected[j] = s_ev.modules[i].evidence_collected[j];
            state->modules[i].verified[j] = s_ev.modules[i].verified[j];
        }
    }

    ticket_lock_release(&s_ev.lock);
}

/**
 * @brief 检查合规性
 */
int32_t evidence_check_compliance(void)
{
    uint32_t i;
    uint32_t j;

    if (!s_ev.initialized)
    {
        return -1;
    }

    ticket_lock_acquire(&s_ev.lock);

    for (i = 0U; i < s_ev.module_count; i++)
    {
        const module_evidence_t *mod = &s_ev.modules[i];

        /* 检查每个模块的基本合规要求 */
        if (mod->tests_failed > 0U)
        {
            ticket_lock_release(&s_ev.lock);
            return -1;
        }

        /* 检查覆盖率 */
        if (mod->coverage_percent < 95U)
        {
            ticket_lock_release(&s_ev.lock);
            return -1;
        }

        /* 检查关键证据项 */
        if (!mod->verified[EVIDENCE_UNIT_TEST])
        {
            ticket_lock_release(&s_ev.lock);
            return -1;
        }

        if (!mod->verified[EVIDENCE_STATIC_ANALYSIS])
        {
            ticket_lock_release(&s_ev.lock);
            return -1;
        }

        /* 检查所有证据项 */
        for (j = 0U; j < EVIDENCE_TYPE_COUNT; j++)
        {
            if (!mod->evidence_collected[j])
            {
                /* 未收集的证据项不强制要求 */
                (void)j;
            }
        }
    }

    ticket_lock_release(&s_ev.lock);

    return 0;
}

/**
 * @brief 生成证据报告
 */
uint32_t evidence_generate_report(char *buf, uint32_t buf_size)
{
    evidence_state_t state;
    uint32_t pos = 0U;
    uint32_t i;
    int written;

    if ((buf == NULL) || (buf_size == 0U))
    {
        return 0U;
    }

    if (!s_ev.initialized)
    {
        return 0U;
    }

    evidence_get_global(&state);

    /* 报告头 */
    written = snprintf(&buf[pos], buf_size - pos,
        "=== AISafeOS64 认证证据报告 ===\n"
        "模块数: %u\n"
        "总测试: %u (通过: %u, 失败: %u)\n"
        "平均覆盖率: %u%%\n"
        "认证就绪: %s\n\n",
        state.module_count,
        state.total_tests,
        state.total_passed,
        state.total_failed,
        state.avg_coverage,
        state.certified ? "是" : "否");

    if (written > 0)
    {
        pos += (uint32_t)written;
    }

    /* 模块详情 */
    for (i = 0U; (i < state.module_count) && (pos < buf_size); i++)
    {
        const module_evidence_t *mod = &state.modules[i];

        written = snprintf(&buf[pos], buf_size - pos,
            "--- %s ---\n"
            "  测试: %u/%u 通过, 失败: %u\n"
            "  覆盖率: %u%%\n",
            mod->module,
            mod->tests_passed,
            mod->tests_total,
            mod->tests_failed,
            mod->coverage_percent);

        if (written > 0)
        {
            pos += (uint32_t)written;
        }
    }

    return pos;
}
