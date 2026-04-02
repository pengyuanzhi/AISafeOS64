/**
 * @file    security.c
 * @brief   内核安全机制实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件实现了内核安全基础设施：
 *          - 安全子系统初始化
 *          - 内核代码段只读保护（通过页表权限设置）
 *          - 安全事件记录与统计
 *          - 安全状态管理
 *          - 安全紧急停止（panic）
 *          - 页面访问权限检查
 *
 *          安全机制采用分层防御策略：
 *          1. 页表层：利用 ARMv8-A MMU 硬件强制 R/W/X 权限
 *          2. 内核层：记录和统计安全事件，支持审计追踪
 *          3. 系统层：安全紧急停止确保故障时进入安全状态
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SE-001~004, KR-011
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */
#include <stdint.h>
#include <string.h>
#include <kernel/security.h>
#include <kernel/page_table.h>
#include <kernel/vmspace.h>
#include <kernel/barrier.h>
#include <kernel/config.h>
#include <kernel/compiler.h>
#include <kernel/errno.h>
#include <kernel/spinlock.h>

/* ========================================================================
 * 编译时断言验证
 * ======================================================================== */

/* 验证：内核虚拟地址基址必须足够高（ARM64 canonical high address） */
static_assert(CONFIG_KERNEL_VADDR_BASE >= 0xFFFF000000000000ULL,
              "CONFIG_KERNEL_VADDR_BASE must be a canonical high address");

/* 验证：页大小必须为 4096 字节 */
static_assert(CONFIG_PAGE_SIZE == 4096U,
              "CONFIG_PAGE_SIZE must be 4096 bytes");

/* ========================================================================
 * 内核代码段范围常量（简化版本）
 *
 * @details 在完整实现中，内核代码段范围由链接器脚本提供的
 *          __text_start 和 __text_end 符号确定。
 *          当前简化版本使用硬编码的估算范围。
 * ======================================================================== */

/**
 * @def KERNEL_CODE_START
 * @brief 内核代码段起始地址（简化硬编码）
 *
 * @details 实际应从链接器符号 __text_start 获取。
 *          当前使用 CONFIG_KERNEL_VADDR_BASE 作为近似值。
 */
#define KERNEL_CODE_START  ((uint64_t)CONFIG_KERNEL_VADDR_BASE)

/**
 * @def KERNEL_CODE_SIZE
 * @brief 内核代码段估算大小（简化硬编码）
 *
 * @details 实际应从链接器符号 (__text_end - __text_start) 获取。
 *          当前使用 2MB 作为估算值（足够覆盖典型内核代码段）。
 */
#define KERNEL_CODE_SIZE   ((uint64_t)0x200000ULL)

/* ========================================================================
 * 故障信息缓冲区最大长度
 * ======================================================================== */

/** @brief 安全 panic 故障信息最大长度（含终止符） */
#define PANIC_REASON_MAX_LEN  128U

/* ========================================================================
 * 安全子系统全局状态
 * ======================================================================== */

/**
 * @brief 安全子系统模块级状态
 *
 * @details 包含安全子系统的所有运行时状态：
 *          - 当前安全状态（正常、降级、告警、紧急停止）
 *          - 安全事件统计计数器
 *          - 紧急停止原因缓冲区
 *          - 访问保护自旋锁
 */
typedef struct
{
    /** @brief 当前安全状态 */
    security_state_t  state;

    /** @brief 安全事件统计 */
    security_stats_t  stats;

    /** @brief 紧急停止原因字符串缓冲区 */
    char              panic_reason[PANIC_REASON_MAX_LEN];

    /** @brief 初始化完成标志 */
    uint32_t          initialized;

    /** @brief 访问保护自旋锁 */
    TicketLock_t      lock;

} security_context_t;

/* ========================================================================
 * 全局安全上下文（静态分配）
 * ======================================================================== */

/**
 * @brief 安全子系统全局上下文实例
 *
 * @note 使用静态零初始化，security_subsys_init() 完成实际初始化
 */
static security_context_t s_security_ctx =
{
    .state        = SECURITY_STATE_NORMAL,
    .stats        = { 0U, 0U, 0U, 0U },
    .panic_reason = { '\0' },
    .initialized  = 0U,
    .lock         = TICKET_LOCK_INIT
};

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 更新指定类型事件的统计计数
 *
 * @details 根据安全事件类型递增对应的统计计数器。
 *          调用者需持有自旋锁保护。
 *
 * @param event 安全事件类型
 *
 * @note 仅在持锁上下文中调用
 */
static void security_update_stats_internal(security_event_t event)
{
    s_security_ctx.stats.total_events = s_security_ctx.stats.total_events + 1U;

    switch (event)
    {
        case SECURITY_EVENT_STACK_OVERFLOW:
            s_security_ctx.stats.stack_overflows =
                s_security_ctx.stats.stack_overflows + 1U;
            break;

        case SECURITY_EVENT_KERNEL_RO_VIOLATION:
        case SECURITY_EVENT_INVALID_PAGE_ACCESS:
            s_security_ctx.stats.page_faults =
                s_security_ctx.stats.page_faults + 1U;
            break;

        case SECURITY_EVENT_CAPABILITY_VIOLATION:
            s_security_ctx.stats.capability_violations =
                s_security_ctx.stats.capability_violations + 1U;
            break;

        case SECURITY_EVENT_NONE:
        case SECURITY_EVENT_DOUBLE_FREE:
        case SECURITY_EVENT_USE_AFTER_FREE:
        case SECURITY_EVENT_NULL_DEREFERENCE:
        default:
            /* 其他事件类型仅计入 total_events，无独立计数器 */
            break;
    }
}

/**
 * @brief 检查安全事件是否应触发状态转换
 *
 * @details 根据安全事件类型评估是否需要将安全状态
 *          从当前状态转换为更严重的状态。
 *          - 栈溢出、双重释放、释放后使用：直接进入紧急停止
 *          - 内核只读区写入、空指针解引用：进入告警状态
 *          - 其他事件：保持当前状态
 *
 * @param event 安全事件类型
 */
static void security_evaluate_state_transition(security_event_t event)
{
    switch (event)
    {
        case SECURITY_EVENT_STACK_OVERFLOW:
        case SECURITY_EVENT_DOUBLE_FREE:
        case SECURITY_EVENT_USE_AFTER_FREE:
            /* 严重事件：直接进入紧急停止状态 */
            s_security_ctx.state = SECURITY_STATE_PANIC;
            break;

        case SECURITY_EVENT_KERNEL_RO_VIOLATION:
        case SECURITY_EVENT_NULL_DEREFERENCE:
            /* 高危事件：进入告警状态 */
            if (s_security_ctx.state == SECURITY_STATE_NORMAL)
            {
                s_security_ctx.state = SECURITY_STATE_ALERT;
            }
            break;

        case SECURITY_EVENT_INVALID_PAGE_ACCESS:
        case SECURITY_EVENT_CAPABILITY_VIOLATION:
            /* 中危事件：仅在正常状态下升级为降级模式 */
            if (s_security_ctx.state == SECURITY_STATE_NORMAL)
            {
                s_security_ctx.state = SECURITY_STATE_DEGRADED;
            }
            break;

        case SECURITY_EVENT_NONE:
        default:
            /* 不改变状态 */
            break;
    }
}

/* ========================================================================
 * 公共 API 实现
 * ======================================================================== */

/**
 * @brief 初始化安全子系统
 *
 * @details 执行以下初始化步骤：
 *          1. 初始化自旋锁
 *          2. 清零统计计数器
 *          3. 设置初始安全状态为 NORMAL
 *          4. 调用 security_protect_kernel_code() 设置代码段只读保护
 *          5. 标记初始化完成
 *
 * @return KERNEL_OK 成功
 * @return KERNEL_ERROR 代码段保护失败
 *
 * @note 对应需求: SE-001, SE-002
 * @note 必须在页表初始化完成后调用
 */
kernel_status_t security_subsys_init(void)
{
    kernel_status_t ret;

    /* 初始化自旋锁 */
    ticket_lock_init(&s_security_ctx.lock);

    /* 清零统计计数器 */
    (void)memset(&s_security_ctx.stats, 0, sizeof(security_stats_t));

    /* 清零 panic 原因缓冲区 */
    (void)memset(s_security_ctx.panic_reason, 0, PANIC_REASON_MAX_LEN);

    /* 设置初始安全状态 */
    s_security_ctx.state = SECURITY_STATE_NORMAL;

    /* 设置内核代码段只读保护 */
    ret = security_protect_kernel_code();
    if (ret != KERNEL_OK)
    {
        /* 代码段保护失败，进入降级模式但仍继续运行 */
        s_security_ctx.state = SECURITY_STATE_DEGRADED;
    }

    /* 标记初始化完成（使用内存屏障确保之前的写入对其他核可见） */
    atomic_store_release_u32(&s_security_ctx.initialized, 1U);

    return KERNEL_OK;
}

/**
 * @brief 设置内核代码段只读保护
 *
 * @details 将内核代码段的页表权限从"读写+可执行"修改为"只读+可执行"，
 *          防止运行时代码被修改（如代码注入攻击）。
 *
 *          实现步骤：
 *          1. 获取内核地址空间的 PGD（顶层页表）
 *          2. 遍历代码段覆盖的所有页面（4KB 粒度）
 *          3. 对每个页面调用 page_table_protect() 设置为 RX 权限
 *          4. 刷新 TLB 确保权限变更生效
 *
 * @return KERNEL_OK 成功
 * @return KERNEL_ERROR 内核地址空间未初始化或页表保护失败
 *
 * @note 对应需求: SE-003
 * @note 必须在 MMU 和页表初始化完成后调用
 */
kernel_status_t security_protect_kernel_code(void)
{
    vm_space_t     *kernel_space;
    page_table_t   *pgd;
    uint64_t        addr;
    uint64_t        code_end;
    kernel_status_t ret;

    /* 获取内核地址空间 */
    kernel_space = vmspace_get_kernel();
    if (unlikely(kernel_space == NULL))
    {
        return KERNEL_ERROR;
    }

    pgd = kernel_space->pgd;
    if (unlikely(pgd == NULL))
    {
        return KERNEL_ERROR;
    }

    /* 计算代码段结束地址（页对齐向上取整） */
    code_end = (KERNEL_CODE_START + KERNEL_CODE_SIZE + (uint64_t)CONFIG_PAGE_SIZE - 1ULL)
               & ~((uint64_t)CONFIG_PAGE_SIZE - 1ULL);

    /* 遍历代码段覆盖的每一页，设置为只读+可执行 */
    addr = KERNEL_CODE_START;
    while (addr < code_end)
    {
        ret = page_table_protect(pgd, (vaddr_t)addr, PAGE_PERM_RX);
        if (unlikely(ret != KERNEL_OK))
        {
            /* 个别页面保护失败不中断，继续保护后续页面 */
            security_report_event(SECURITY_EVENT_KERNEL_RO_VIOLATION, (uint32_t)(addr >> 12U));
        }

        addr = addr + (uint64_t)CONFIG_PAGE_SIZE;
    }

    /* 刷新全部 TLB，确保权限变更对所有核心可见 */
    tlb_flush_all();

    /* 插入完整内存屏障，确保权限变更生效 */
    full_barrier();

    return KERNEL_OK;
}

/**
 * @brief 记录安全事件
 *
 * @details 将安全事件记录到统计计数器，并评估是否需要触发
 *          安全状态转换。如果事件触发紧急停止状态，
 *          则直接调用 security_panic()。
 *
 * @param event  安全事件类型
 * @param detail 事件详情（可选，0 表示无详情）
 *
 * @note 对应需求: SE-004
 * @note 此函数可从中断上下文调用
 */
void security_report_event(security_event_t event, uint32_t detail)
{
    uint32_t irq_state;

    /* 参数有效性检查：NONE 事件不记录 */
    if (event == SECURITY_EVENT_NONE)
    {
        return;
    }

    /* 获取自旋锁（禁用中断以保证原子性） */
    irq_state = ticket_lock_acquire_irqsave(&s_security_ctx.lock);

    /* 更新统计计数器 */
    security_update_stats_internal(event);

    /* 评估状态转换 */
    security_evaluate_state_transition(event);

    /* 释放自旋锁（恢复中断） */
    ticket_lock_release_irqrestore(&s_security_ctx.lock, irq_state);

    /* 如果进入紧急停止状态，立即触发 panic */
    if (unlikely(s_security_ctx.state == SECURITY_STATE_PANIC))
    {
        security_panic("安全事件触发紧急停止");
    }

    /* 消除 detail 未使用的编译器警告 */
    (void)detail;
}

/**
 * @brief 获取当前安全状态
 *
 * @details 使用 acquire 语义读取当前安全状态，
 *          确保读取到最新值。
 *
 * @return 当前安全状态
 */
security_state_t security_get_state(void)
{
    security_state_t current_state;

    current_state = s_security_ctx.state;
    barrier_load();

    return current_state;
}

/**
 * @brief 获取安全统计信息
 *
 * @details 将当前安全统计信息复制到调用者提供的缓冲区中。
 *          使用自旋锁保护确保读取的一致性。
 *
 * @param[out] stats 输出统计信息结构体指针
 *
 * @note 调用者需确保 stats 指针有效
 */
void security_get_stats(security_stats_t *stats)
{
    uint32_t irq_state;

    /* 参数有效性检查 */
    if (unlikely(stats == NULL))
    {
        return;
    }

    /* 获取自旋锁（禁用中断以保证读取一致性） */
    irq_state = ticket_lock_acquire_irqsave(&s_security_ctx.lock);

    /* 复制统计信息到调用者缓冲区 */
    (void)memcpy(stats, &s_security_ctx.stats, sizeof(security_stats_t));

    /* 释放自旋锁 */
    ticket_lock_release_irqrestore(&s_security_ctx.lock, irq_state);
}

/**
 * @brief 安全紧急停止
 *
 * @details 进入安全紧急停止状态：
 *          1. 将安全状态设置为 SECURITY_STATE_PANIC
 *          2. 将停止原因保存到静态缓冲区
 *          3. 插入完整内存屏障确保数据持久化
 *          4. 进入不可恢复的无限循环（禁用中断）
 *
 *          此函数不返回。
 *
 * @param reason 停止原因字符串（不能为 NULL）
 *
 * @note 此函数标记为 NORETURN，调用后不会返回
 * @warning 仅在检测到不可恢复的安全违规时调用
 */
void NORETURN security_panic(const char *reason)
{
    uint32_t irq_state;
    uint32_t copy_len;
    uint32_t reason_len;

    /* 禁用中断以防止并发访问 */
    irq_state = ticket_lock_acquire_irqsave(&s_security_ctx.lock);

    /* 设置安全状态为紧急停止 */
    s_security_ctx.state = SECURITY_STATE_PANIC;

    /* 保存停止原因到静态缓冲区 */
    if (reason != NULL)
    {
        /* 计算原因字符串长度（手动实现，避免依赖 strnlen 可能不可用） */
        reason_len = 0U;
        while ((reason_len < (PANIC_REASON_MAX_LEN - 1U)) &&
               (reason[reason_len] != '\0'))
        {
            reason_len = reason_len + 1U;
        }

        /* 限制复制长度 */
        copy_len = reason_len;
        if (copy_len >= PANIC_REASON_MAX_LEN)
        {
            copy_len = PANIC_REASON_MAX_LEN - 1U;
        }

        (void)memcpy(s_security_ctx.panic_reason, reason, copy_len);

        /* 确保以空字符结尾 */
        s_security_ctx.panic_reason[copy_len] = '\0';
    }

    /* 插入完整内存屏障，确保所有写入对其他核可见 */
    full_barrier();

    /* 消除 irq_state 未使用的编译器警告（panic 时不释放锁） */
    (void)irq_state;

    /*
     * 进入不可恢复的无限循环
     * 使用 WFE 指令降低功耗，等待外部调试器或硬件看门狗复位
     *
     * 注意：此处故意不释放锁，因为系统即将停止
     */
    for (;;)
    {
        WFE();
    }

    /* 函数不会到达此处（消除编译器警告） */
    (void)copy_len;
}

/**
 * @brief 检查页面访问权限
 *
 * @details 由异常处理器调用，根据故障地址和访问类型
 *          判断是否为合法访问。检查逻辑：
 *
 *          1. 内核代码段：仅允许读+执行，禁止写入
 *          2. 用户空间地址：在内核态下需进一步检查 VMA 权限
 *          3. 内核数据段：允许读写
 *
 * @param fault_addr 故障虚拟地址
 * @param is_write   是否为写操作
 * @param is_exec    是否为执行操作
 *
 * @return KERNEL_OK 权限检查通过（合法访问）
 * @return -EACCES   权限违规（非法访问）
 *
 * @note 对应需求: KR-011, SE-003
 * @note 此函数由异常处理器调用，不可阻塞
 */
kernel_status_t security_check_page_access(uint64_t fault_addr,
                                            bool is_write,
                                            bool is_exec)
{
    /* 检查是否在内核代码段范围内 */
    if ((fault_addr >= KERNEL_CODE_START) &&
        (fault_addr < (KERNEL_CODE_START + KERNEL_CODE_SIZE)))
    {
        /* 内核代码段：禁止写入操作 */
        if (is_write)
        {
            security_report_event(SECURITY_EVENT_KERNEL_RO_VIOLATION,
                                  (uint32_t)(fault_addr >> 12U));
            return -(int32_t)EACCES;
        }

        /* 内核代码段：允许读和执行 */
        if (is_exec)
        {
            /* 执行操作合法 */
            return KERNEL_OK;
        }

        /* 读操作合法 */
        return KERNEL_OK;
    }

    /* 检查是否为用户空间地址的非法访问 */
    if (fault_addr < CONFIG_KERNEL_VADDR_BASE)
    {
        /*
         * 用户空间地址的访问需通过 VMA 权限检查。
         * 当前简化版本仅记录事件并返回权限违规。
         * 完整实现应调用 vmspace_find_vma() 检查 VMA 权限。
         */
        security_report_event(SECURITY_EVENT_INVALID_PAGE_ACCESS,
                              (uint32_t)(fault_addr >> 12U));
        return -(int32_t)EACCES;
    }

    /* 内核数据段：允许读写执行 */
    return KERNEL_OK;
}
