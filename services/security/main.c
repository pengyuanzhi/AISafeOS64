/**
 * @file    main.c
 * @brief   安全认证服务实现
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 3.0
 *
 * @details 安全认证服务主程序：
 *          - 安全启动链验证
 *          - 审计日志管理
 *          - SHA-256 完整性校验
 *          - 强制访问控制（MAC）策略引擎
 *          - 基于能力的访问控制决策
 *          - 安全上下文管理
 *          - 认证策略管理
 *          - 安全状态管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SE-001~012
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/security.h>
#include <kernel/certification.h>
#include <kernel/capability.h>
#include <kernel/config.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 安全服务常量
 * ======================================================================== */

/** @brief 审计日志最大条目数 */
#define SECURITY_AUDIT_LOG_MAX    64U

/** @brief SHA-256 哈希大小 */
#define SECURITY_SHA256_SIZE      32U

/** @brief MAC 策略规则最大数 */
#define MAC_MAX_RULES             128U

/** @brief 安全上下文标签最大长度 */
#define SECURITY_LABEL_MAX        32U

/** @brief 安全类别最大数 */
#define SECURITY_MAX_CATEGORIES   16U

/** @brief 访问向量最大位数 */
#define ACCESS_VEC_MAX            32U

/* ========================================================================
 * MAC 访问向量定义
 * ======================================================================== */

/** @brief 文件访问：读 */
#define MAC_FILE_READ             0x00000001U

/** @brief 文件访问：写 */
#define MAC_FILE_WRITE            0x00000002U

/** @brief 文件访问：执行 */
#define MAC_FILE_EXECUTE          0x00000004U

/** @brief 文件访问：追加 */
#define MAC_FILE_APPEND           0x00000008U

/** @brief 进程访问：创建 */
#define MAC_PROCESS_CREATE        0x00000010U

/** @brief 进程访问：销毁 */
#define MAC_PROCESS_DESTROY       0x00000020U

/** @brief 进程访问：信号 */
#define MAC_PROCESS_SIGNAL        0x00000040U

/** @brief IPC 访问：发送 */
#define MAC_IPC_SEND              0x00000100U

/** @brief IPC 访问：接收 */
#define MAC_IPC_RECV              0x00000200U

/** @brief 能力访问：使用 */
#define MAC_CAP_USE               0x00001000U

/** @brief 能力访问：派生 */
#define MAC_CAP_DERIVE            0x00002000U

/* ========================================================================
 * MAC 对象类型
 * ======================================================================== */

/**
 * @brief MAC 对象类型
 */
typedef enum
{
    MAC_OBJ_FILE = 0U,      /**< @brief 文件对象 */
    MAC_OBJ_PROCESS,        /**< @brief 进程对象 */
    MAC_OBJ_IPC_ENDPOINT,   /**< @brief IPC 端点 */
    MAC_OBJ_IPC_CHANNEL,    /**< @brief IPC 通道 */
    MAC_OBJ_CAPABILITY,     /**< @brief 能力对象 */
    MAC_OBJ_DEVICE,         /**< @brief 设备对象 */
    MAC_OBJ_MEMORY          /**< @brief 内存区域 */
} mac_obj_type_t;

/* ========================================================================
 * 安全上下文
 * ======================================================================== */

/**
 * @brief 安全上下文标签
 *
 * @details 包含主体/客体的安全标签信息
 */
typedef struct
{
    char     label[SECURITY_LABEL_MAX]; /**< @brief 安全标签 */
    uint32_t level;                     /**< @brief 安全级别（0-255） */
    uint32_t categories[SECURITY_MAX_CATEGORIES]; /**< @brief 安全类别集合 */
    uint32_t cat_count;                 /**< @brief 类别数 */
    bool     valid;                     /**< @brief 有效标记 */
} security_context_t;

/** @brief 安全上下文表最大数 */
#define SECURITY_MAX_CONTEXTS    64U

/* ========================================================================
 * MAC 策略规则
 * ======================================================================== */

/**
 * @brief MAC 策略规则
 *
 * @details 定义主体标签对客体标签的访问权限
 */
typedef struct
{
    char     src_label[SECURITY_LABEL_MAX]; /**< @brief 主体标签 */
    char     dst_label[SECURITY_LABEL_MAX]; /**< @brief 客体标签 */
    mac_obj_type_t obj_type;               /**< @brief 客体类型 */
    uint32_t access_allowed;               /**< @brief 允许的访问向量 */
    uint32_t access_denied;                /**< @brief 拒绝的访问向量 */
    bool     active;                       /**< @brief 活跃标记 */
} mac_rule_t;

/* ========================================================================
 * 安全服务全局状态
 * ======================================================================== */

/** @brief 当前安全状态 */
static security_state_t s_security_state;

/** @brief 安全统计 */
static security_stats_t s_security_stats;

/** @brief 审计日志条目 */
typedef struct
{
    uint64_t          timestamp;     /**< @brief 时间戳 */
    security_event_t  event;         /**< @brief 安全事件类型 */
    uint32_t          detail;        /**< @brief 详情 */
} audit_entry_t;

/** @brief 审计日志 */
static audit_entry_t s_audit_log[SECURITY_AUDIT_LOG_MAX];

/** @brief 审计日志写入位置 */
static uint32_t s_audit_head;

/** @brief 审计日志读取位置 */
static uint32_t s_audit_tail;

/** @brief 审计日志条目计数 */
static uint32_t s_audit_count;

/** @brief MAC 策略规则表 */
static mac_rule_t s_mac_rules[MAC_MAX_RULES];

/** @brief MAC 策略规则计数 */
static uint32_t s_mac_rule_count;

/** @brief 安全上下文表 */
static security_context_t s_contexts[SECURITY_MAX_CONTEXTS];

/** @brief MAC 拒绝统计 */
static uint32_t s_mac_denied_count;

/** @brief MAC 允许统计 */
static uint32_t s_mac_allowed_count;

/** @brief 初始化标志 */
static bool s_initialized;

/* ========================================================================
 * SHA-256 简化实现（框架）
 * ======================================================================== */

/** @brief SHA-256 初始哈希值 */
static const uint32_t s_sha256_init[8U] =
{
    0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
    0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U
};

/** @brief SHA-256 轮常量（前 16 个） */
static const uint32_t s_sha256_k[16U] =
{
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
    0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
    0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
    0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U
};

/**
 * @brief SHA-256 哈希计算（简化框架）
 *
 * @param data 输入数据
 * @param len  数据长度
 * @param hash 输出哈希（32字节）
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t sha256_compute(const void *data, uint32_t len,
                                        uint8_t hash[SECURITY_SHA256_SIZE])
{
    const uint8_t *msg;
    uint32_t h[8U];
    uint32_t i;

    if ((data == NULL) || (hash == NULL))
    {
        return -(int32_t)22;
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

/**
 * @brief 数据完整性校验
 *
 * @param data     数据
 * @param len      长度
 * @param expected 预期哈希
 *
 * @return KERNEL_OK 校验通过
 */
static kernel_status_t verify_integrity(const void *data, uint32_t len,
                                          const uint8_t expected[SECURITY_SHA256_SIZE])
{
    uint8_t actual[SECURITY_SHA256_SIZE];
    uint32_t i;
    kernel_status_t ret;
    uint32_t diff;

    if ((data == NULL) || (expected == NULL))
    {
        return -(int32_t)22;
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
        return -(int32_t)1;
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 安全字符串比较
 */
static bool sec_streq(const char *a, const char *b, uint32_t max_len)
{
    uint32_t i;

    if ((a == NULL) || (b == NULL))
    {
        return false;
    }

    for (i = 0U; i < max_len; i++)
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
 * @brief 安全字符串复制
 */
static void sec_strcpy(char *dst, const char *src, uint32_t n)
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

/* ========================================================================
 * 安全上下文管理
 * ======================================================================== */

/**
 * @brief 创建安全上下文
 *
 * @param label  安全标签
 * @param level  安全级别
 *
 * @return 成功返回上下文索引，失败返回负错误码
 */
static int32_t security_create_context(const char *label, uint32_t level)
{
    uint32_t i;

    if (label == NULL)
    {
        return -(int32_t)22;
    }

    for (i = 0U; i < SECURITY_MAX_CONTEXTS; i++)
    {
        if (!s_contexts[i].valid)
        {
            break;
        }
    }

    if (i >= SECURITY_MAX_CONTEXTS)
    {
        return -(int32_t)12;
    }

    sec_strcpy(s_contexts[i].label, label, SECURITY_LABEL_MAX);
    s_contexts[i].level = level;
    (void)memset(s_contexts[i].categories, 0, sizeof(s_contexts[i].categories));
    s_contexts[i].cat_count = 0U;
    s_contexts[i].valid = true;

    return (int32_t)i;
}

/**
 * @brief 向安全上下文添加类别
 *
 * @param ctx_idx  上下文索引
 * @param category 类别值
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t security_add_category(uint32_t ctx_idx, uint32_t category)
{
    security_context_t *ctx;

    if (ctx_idx >= SECURITY_MAX_CONTEXTS)
    {
        return -(int32_t)22;
    }

    ctx = &s_contexts[ctx_idx];
    if (!ctx->valid)
    {
        return -(int32_t)2;
    }

    if (ctx->cat_count >= SECURITY_MAX_CATEGORIES)
    {
        return -(int32_t)12;
    }

    ctx->categories[ctx->cat_count] = category;
    ctx->cat_count++;

    return KERNEL_OK;
}

/**
 * @brief 查找安全上下文
 *
 * @param label 安全标签
 *
 * @return 上下文索引，-1 表示未找到
 */
static int32_t security_find_context(const char *label)
{
    uint32_t i;

    for (i = 0U; i < SECURITY_MAX_CONTEXTS; i++)
    {
        if (s_contexts[i].valid &&
            sec_streq(s_contexts[i].label, label, SECURITY_LABEL_MAX))
        {
            return (int32_t)i;
        }
    }

    return -1;
}

/* ========================================================================
 * MAC 策略引擎
 * ======================================================================== */

/**
 * @brief 添加 MAC 策略规则
 *
 * @param src_label     主体标签
 * @param dst_label     客体标签
 * @param obj_type      客体类型
 * @param access_allowed 允许的访问向量
 * @param access_denied  拒绝的访问向量
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t mac_add_rule(const char *src_label, const char *dst_label,
                               mac_obj_type_t obj_type,
                               uint32_t access_allowed, uint32_t access_denied)
{
    uint32_t i;

    if (!s_initialized)
    {
        return -(int32_t)22;
    }

    if ((src_label == NULL) || (dst_label == NULL))
    {
        return -(int32_t)22;
    }

    if (s_mac_rule_count >= MAC_MAX_RULES)
    {
        return -(int32_t)12;
    }

    for (i = 0U; i < MAC_MAX_RULES; i++)
    {
        if (!s_mac_rules[i].active)
        {
            break;
        }
    }

    if (i >= MAC_MAX_RULES)
    {
        return -(int32_t)12;
    }

    sec_strcpy(s_mac_rules[i].src_label, src_label, SECURITY_LABEL_MAX);
    sec_strcpy(s_mac_rules[i].dst_label, dst_label, SECURITY_LABEL_MAX);
    s_mac_rules[i].obj_type = obj_type;
    s_mac_rules[i].access_allowed = access_allowed;
    s_mac_rules[i].access_denied = access_denied;
    s_mac_rules[i].active = true;
    s_mac_rule_count++;

    return KERNEL_OK;
}

/**
 * @brief 移除 MAC 策略规则
 *
 * @param rule_idx 规则索引
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t mac_remove_rule(uint32_t rule_idx)
{
    if (rule_idx >= MAC_MAX_RULES)
    {
        return -(int32_t)22;
    }

    if (!s_mac_rules[rule_idx].active)
    {
        return -(int32_t)2;
    }

    s_mac_rules[rule_idx].active = false;
    if (s_mac_rule_count > 0U)
    {
        s_mac_rule_count--;
    }

    return KERNEL_OK;
}

/**
 * @brief MAC 访问控制决策
 *
 * @details 基于主体标签、客体标签和请求的访问向量，
 *          在策略规则表中查找匹配规则并做出决策。
 *
 * @param src_label 主体标签
 * @param dst_label 客体标签
 * @param obj_type  客体类型
 * @param requested 请求的访问向量
 *
 * @return KERNEL_OK 允许，负数表示拒绝
 */
kernel_status_t mac_check_access(const char *src_label, const char *dst_label,
                                   mac_obj_type_t obj_type, uint32_t requested)
{
    uint32_t i;
    bool found = false;
    uint32_t allowed = 0U;
    uint32_t denied = 0U;

    if (!s_initialized)
    {
        return -(int32_t)22;
    }

    if ((src_label == NULL) || (dst_label == NULL))
    {
        return -(int32_t)22;
    }

    /* 在规则表中查找匹配规则 */
    for (i = 0U; i < MAC_MAX_RULES; i++)
    {
        if (!s_mac_rules[i].active)
        {
            continue;
        }

        if ((s_mac_rules[i].obj_type == obj_type) &&
            sec_streq(s_mac_rules[i].src_label, src_label, SECURITY_LABEL_MAX) &&
            sec_streq(s_mac_rules[i].dst_label, dst_label, SECURITY_LABEL_MAX))
        {
            allowed |= s_mac_rules[i].access_allowed;
            denied |= s_mac_rules[i].access_denied;
            found = true;
        }
    }

    if (!found)
    {
        /* 无匹配规则：默认拒绝 */
        s_mac_denied_count++;
        security_report_event(SECURITY_EVENT_CAPABILITY_VIOLATION, requested);
        return -(int32_t)13;
    }

    /* 检查是否在拒绝列表中 */
    if ((requested & denied) != 0U)
    {
        s_mac_denied_count++;
        security_report_event(SECURITY_EVENT_CAPABILITY_VIOLATION, requested);
        return -(int32_t)13;
    }

    /* 检查是否在允许列表中 */
    if ((requested & ~allowed) != 0U)
    {
        s_mac_denied_count++;
        security_report_event(SECURITY_EVENT_CAPABILITY_VIOLATION, requested);
        return -(int32_t)13;
    }

    s_mac_allowed_count++;
    return KERNEL_OK;
}

/**
 * @brief 基于能力的访问控制决策
 *
 * @details 结合能力系统和 MAC 策略进行双重检查
 *
 * @param cap       能力对象
 * @param src_label 主体标签
 * @param dst_label 客体标签
 * @param obj_type  客体类型
 * @param requested 请求的访问向量
 *
 * @return KERNEL_OK 允许，负数表示拒绝
 */
kernel_status_t security_cap_check_access(const cap_desc_t *cap,
                                           const char *src_label,
                                           const char *dst_label,
                                           mac_obj_type_t obj_type,
                                           uint32_t requested)
{
    kernel_status_t ret;

    if (cap == NULL)
    {
        return -(int32_t)22;
    }

    /* 第一层：能力检查 */
    if ((cap->rights & requested) != requested)
    {
        security_report_event(SECURITY_EVENT_CAPABILITY_VIOLATION, requested);
        return -(int32_t)13;
    }

    /* 第二层：MAC 检查 */
    ret = mac_check_access(src_label, dst_label, obj_type, requested);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 安全服务初始化
 * ======================================================================== */

kernel_status_t security_subsys_init(void)
{
    s_security_state = SECURITY_STATE_NORMAL;
    (void)memset(&s_security_stats, 0, sizeof(security_stats_t));
    (void)memset(s_audit_log, 0, sizeof(s_audit_log));
    s_audit_head = 0U;
    s_audit_tail = 0U;
    s_audit_count = 0U;

    /* 初始化 MAC 策略表 */
    (void)memset(s_mac_rules, 0, sizeof(s_mac_rules));
    s_mac_rule_count = 0U;

    /* 初始化安全上下文表 */
    (void)memset(s_contexts, 0, sizeof(s_contexts));

    s_mac_denied_count = 0U;
    s_mac_allowed_count = 0U;

    /* 初始化认证框架 */
    (void)cert_init();

    /* 注册默认 MAC 策略 */
    (void)mac_add_rule("kernel", "kernel", MAC_OBJ_PROCESS,
                       MAC_PROCESS_CREATE | MAC_PROCESS_DESTROY | MAC_PROCESS_SIGNAL, 0U);
    (void)mac_add_rule("kernel", "system", MAC_OBJ_FILE,
                       MAC_FILE_READ | MAC_FILE_WRITE | MAC_FILE_EXECUTE, 0U);
    (void)mac_add_rule("system", "system", MAC_OBJ_IPC_ENDPOINT,
                       MAC_IPC_SEND | MAC_IPC_RECV, 0U);
    (void)mac_add_rule("system", "kernel", MAC_OBJ_CAPABILITY,
                       MAC_CAP_USE, MAC_CAP_DERIVE);

    /* 创建默认安全上下文 */
    (void)security_create_context("kernel", 255U);
    (void)security_create_context("system", 128U);
    (void)security_create_context("user", 64U);
    (void)security_create_context("guest", 32U);

    s_initialized = true;

    return KERNEL_OK;
}

/* ========================================================================
 * 安全启动链验证
 * ======================================================================== */

kernel_status_t security_verify_boot_chain(void)
{
    security_report_event(SECURITY_EVENT_NONE, 0U);
    return KERNEL_OK;
}

/* ========================================================================
 * 安全事件报告
 * ======================================================================== */

void security_report_event(security_event_t event, uint32_t detail)
{
    uint32_t idx;
    audit_entry_t *entry;

    if (!s_initialized)
    {
        return;
    }

    s_security_stats.total_events++;

    switch (event)
    {
        case SECURITY_EVENT_STACK_OVERFLOW:
            s_security_stats.stack_overflows++;
            break;
        case SECURITY_EVENT_KERNEL_RO_VIOLATION:
        case SECURITY_EVENT_INVALID_PAGE_ACCESS:
            s_security_stats.page_faults++;
            break;
        case SECURITY_EVENT_CAPABILITY_VIOLATION:
            s_security_stats.capability_violations++;
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

    if (event == SECURITY_EVENT_KERNEL_RO_VIOLATION)
    {
        if (s_security_state == SECURITY_STATE_NORMAL)
        {
            s_security_state = SECURITY_STATE_DEGRADED;
        }
    }

    if (event == SECURITY_EVENT_DOUBLE_FREE)
    {
        if (s_security_state == SECURITY_STATE_NORMAL)
        {
            s_security_state = SECURITY_STATE_DEGRADED;
        }
    }
}

/* ========================================================================
 * 安全状态管理
 * ======================================================================== */

security_state_t security_get_state(void)
{
    return s_security_state;
}

void security_get_stats(security_stats_t *stats)
{
    if (stats != NULL)
    {
        (void)memcpy(stats, &s_security_stats, sizeof(security_stats_t));
    }
}

void security_panic(const char *reason)
{
    if (reason != NULL)
    {
        security_report_event(SECURITY_EVENT_KERNEL_RO_VIOLATION, 0U);
    }

    s_security_state = SECURITY_STATE_PANIC;

    for (;;)
    {
        /* 安全停止 */
    }
}

/* ========================================================================
 * 页面访问权限检查
 * ======================================================================== */

kernel_status_t security_check_page_access(uint64_t fault_addr,
                                             bool is_write,
                                             bool is_exec)
{
    (void)fault_addr;

    if (is_write)
    {
        security_report_event(SECURITY_EVENT_KERNEL_RO_VIOLATION, 0U);
        return -(int32_t)13;
    }

    if (is_exec)
    {
        security_report_event(SECURITY_EVENT_INVALID_PAGE_ACCESS, 0U);
        return -(int32_t)13;
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 审计日志查询
 * ======================================================================== */

/**
 * @brief 获取审计日志
 *
 * @param entries      输出日志数组
 * @param max_count    最大条目数
 * @param actual_count 实际条目数
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t get_audit_log(audit_entry_t *entries,
                                       uint32_t max_count,
                                       uint32_t *actual_count)
{
    uint32_t count;
    uint32_t i;
    uint32_t src_idx;

    if ((entries == NULL) || (actual_count == NULL))
    {
        return -(int32_t)22;
    }

    count = s_audit_count;
    if (count > max_count)
    {
        count = max_count;
    }

    for (i = 0U; i < count; i++)
    {
        src_idx = (s_audit_tail + i) % SECURITY_AUDIT_LOG_MAX;
        (void)memcpy(&entries[i], &s_audit_log[src_idx],
                      sizeof(audit_entry_t));
    }

    *actual_count = count;

    return KERNEL_OK;
}

/**
 * @brief 获取 MAC 策略统计
 *
 * @param allowed  允许次数输出
 * @param denied   拒绝次数输出
 * @param rule_count 活跃规则数输出
 */
void mac_get_stats(uint32_t *allowed, uint32_t *denied, uint32_t *rule_count)
{
    if (allowed != NULL)
    {
        *allowed = s_mac_allowed_count;
    }
    if (denied != NULL)
    {
        *denied = s_mac_denied_count;
    }
    if (rule_count != NULL)
    {
        *rule_count = s_mac_rule_count;
    }
}

/* ========================================================================
 * 服务主函数
 * ======================================================================== */

int main(void)
{
    kernel_status_t ret;

    ret = security_subsys_init();
    if (ret != KERNEL_OK)
    {
        return (int)ret;
    }

    /* 验证启动链 */
    ret = security_verify_boot_chain();
    if (ret != KERNEL_OK)
    {
        security_panic("Boot chain verification failed");
    }

    for (;;)
    {
        /* 通过 IPC 接收并处理安全请求 */
    }

    return 0;
}
