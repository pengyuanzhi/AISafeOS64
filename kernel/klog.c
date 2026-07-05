/**
 * @file    klog.c
 * @brief   内核标准日志接口实现（异步环形缓冲）
 * @author  AISafe64 Team
 * @date    2026-07-05
 * @version 2.1
 *
 * @details 实现设计：
 *
 *          **异步环形缓冲**：
 *          - 每个 CPU 一个独立环形缓冲（per-CPU，无跨核锁竞争）
 *          - klog_error/warn/info/debug 写入本核缓冲（关中断保护，<1μs）
 *          - klog_flush 在 idle 线程中异步输出到控制台
 *          - 缓冲满时丢弃最旧数据（不阻塞调用者）
 *
 *          **为什么 per-CPU 缓冲不需要自旋锁**：
 *          每个 CPU 只写自己的缓冲（head），只读自己的缓冲（tail），
 *          不存在跨核竞争。写入端用关调度（preempt_disable）保护，
 *          防止本核线程被抢占后另一个线程重入同一缓冲。
 *          中断仍然可以响应（不关中断），保证实时性。
 *          由于中断中禁止调用 klog（AGENTS.md 规则），不存在中断重入。
 *
 *          **中断中禁止 klog**：
 *          AGENTS.md 日志规范明确禁止在中断/调度快路径中调用日志函数。
 *          中断处理应只做最短路径工作，日志延迟到线程上下文输出。
 *          klog_panic 是唯一例外（panic 路径直接同步输出，绕过缓冲）。
 *
 *          **多核 flush 输出顺序**：
 *          多个 CPU 的 idle 线程可能同时调 klog_flush，
 *          它们向同一个物理 UART 输出会导致字符交织乱码。
 *          因此用一个全局自旋锁（s_flush_lock）串行化输出：
 *          同一时刻只有一个 CPU 在执行 UART 输出。
 *
 *          **flush 全局锁不影响实时性**：
 *          klog_flush 只在 idle 线程中调用（最低优先级）。
 *          CPU0 idle 持锁输出 → CPU1 idle 等锁 → 但 CPU1 上的实时任务
 *          优先级高于 idle，随时可以抢占等锁的 idle 线程。
 *          因此 flush 锁竞争只在 idle 上下文，不影响任何实时任务延迟。
 *
 *          **平台无关**：
 *          - 通过 hal_console_putc/puts 输出（HAL 层绑定具体硬件）
 *          - klog 代码不包含任何 UART 基地址/寄存器定义
 *
 * @note    MISRA-C:2012 合规
 *
 * @revision history
 * v1.0 2026-07-04 初版（同步阻塞，硬编码 UART base）
 * v2.0 2026-07-05 异步环形缓冲 + 平台无关
 * v2.1 2026-07-05 去掉 per-CPU 自旋锁（改为仅关中断）+ flush 全局锁
 * v3.0 2026-07-05 关调度替代关中断（preempt_disable/enable）（当前版本）
 */

#include <kernel/klog.h>
#include <kernel/config.h>
#include <kernel/spinlock.h>
#include <kernel/barrier.h>
#include <kernel/smp.h>
#include "hal.h"
#include <stdbool.h>

/* ========================================================================
 * 内部数据结构
 * ======================================================================== */

/**
 * @brief per-CPU 日志环形缓冲
 *
 * @details 每个 CPU 独立缓冲，无跨核竞争。
 *          写入端（klog_write）仅关中断保护，不需要自旋锁（本核独占）。
 *          读取端（klog_flush）通过全局 flush 锁串行化多核输出。
 *
 *          head：写入位置（生产者，本核独占写入）
 *          tail：读取位置（消费者，flush 时推进）
 *          buf[]：环形数据区
 */
typedef struct
{
    uint32_t head;                          /**< @brief 下一个写入位置（本核独占） */
    uint32_t tail;                          /**< @brief 下一个读取位置（flush 推进） */
    uint32_t dropped;                       /**< @brief 丢弃的字节数（诊断统计） */
    char buf[KLOG_BUF_SIZE];                /**< @brief 环形数据区 */
} klog_buf_t;

/** @brief 最大 CPU 数 */
#ifndef CONFIG_MAX_CPUS
#define CONFIG_MAX_CPUS 8U
#endif

/** @brief per-CPU 日志缓冲实例（不需要 CACHE_ALIGN，因为各 CPU 只访问自己的） */
static klog_buf_t s_klog_bufs[CONFIG_MAX_CPUS];

/** @brief 当前日志级别（运行时可调，全局只读访问安全） */
static klog_level_t s_current_level = KLOG_DEFAULT_LEVEL;

/** @brief klog 是否已初始化 */
static bool s_initialized = false;

/** @brief flush 全局锁（串行化多核 UART 输出，防止字符交织乱码） */
static TicketLock_t s_flush_lock;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 获取当前 CPU 的日志缓冲
 *
 * @details 通过 hal_get_cpu_id() 索引 per-CPU 数组。
 *
 * @return 当前 CPU 的 klog_buf_t 指针
 */
static klog_buf_t *klog_get_buf(void)
{
    uint32_t cpu = hal_get_cpu_id();
    if (cpu >= CONFIG_MAX_CPUS)
    {
        cpu = 0U;
    }
    return &s_klog_bufs[cpu];
}

/**
 * @brief 向环形缓冲写入一个字节
 *
 * @details 调用者必须已关调度（防止同核线程被抢占后重入破坏 head）。
 *          缓冲满时丢弃最旧数据（推进 tail），保证写入者永不阻塞。
 *
 * @param buf 目标缓冲
 * @param c   要写入的字节
 */
static void klog_buf_putc_preemptoff(klog_buf_t *buf, char c)
{
    uint32_t next_head = (buf->head + 1U) % KLOG_BUF_SIZE;

    if (next_head == buf->tail)
    {
        /* 缓冲满：丢弃最旧数据（推进 tail） */
        buf->tail = (buf->tail + 1U) % KLOG_BUF_SIZE;
        buf->dropped++;
    }

    buf->buf[buf->head] = c;
    buf->head = next_head;
}

/**
 * @brief 向环形缓冲写入字符串
 *
 * @details 逐字节调用 klog_buf_putc_preemptoff。调用者必须已关调度。
 *
 * @param buf 目标缓冲
 * @param str 以 NULL 结尾的字符串（可为 NULL，NULL 时静默返回）
 */
static void klog_buf_puts_preemptoff(klog_buf_t *buf, const char *str)
{
    if (str == NULL)
    {
        return;
    }

    while (*str != '\0')
    {
        klog_buf_putc_preemptoff(buf, *str);
        str++;
    }
}

/**
 * @brief 向当前 CPU 缓冲写入带前缀的消息
 *
 * @details 实时安全写入入口：
 *          1. 关调度（preempt_disable，防止本核线程被抢占后重入缓冲）
 *          2. 写入 per-CPU 缓冲（本核独占，无自旋锁）
 *          3. 开调度（preempt_enable，归零时检查 need_resched）
 *
 *          中断仍然可以响应（不关中断），保证实时性。
 *          整个操作确定性 < 1μs（无 UART 阻塞，无锁竞争）。
 *
 * @param prefix 级别前缀（如 "E: "），可为 NULL
 * @param str    消息字符串
 */
static void klog_write(const char *prefix, const char *str)
{
    if (!s_initialized || (str == NULL))
    {
        return;
    }

    klog_buf_t *buf = klog_get_buf();

    preempt_disable();

    if (prefix != NULL)
    {
        klog_buf_puts_preemptoff(buf, prefix);
    }
    klog_buf_puts_preemptoff(buf, str);

    preempt_enable();
}

/* ========================================================================
 * 公共接口：初始化与配置
 * ======================================================================== */

/**
 * @brief 初始化内核日志子系统
 *
 * @details 初始化 per-CPU 环形缓冲和 flush 全局锁。
 *          内部调用 hal_console_init() 初始化控制台硬件。
 *          必须在内核启动早期（调度器启动前）调用。
 */
void klog_init(void)
{
    uint32_t i;

    s_current_level = KLOG_DEFAULT_LEVEL;

    /* 初始化所有 CPU 的日志缓冲 */
    for (i = 0U; i < CONFIG_MAX_CPUS; i++)
    {
        s_klog_bufs[i].head = 0U;
        s_klog_bufs[i].tail = 0U;
        s_klog_bufs[i].dropped = 0U;
    }

    /* 初始化 flush 全局锁 */
    ticket_lock_init(&s_flush_lock);

    /* 初始化控制台硬件（HAL 层内部绑定具体 UART） */
    hal_console_init();

    s_initialized = true;
}

/**
 * @brief 设置运行时日志级别
 *
 * @param level 允许输出的最高级别
 */
void klog_set_level(klog_level_t level)
{
    s_current_level = level;
}

/* ========================================================================
 * 公共接口：日志输出（写入环形缓冲，异步输出）
 * ======================================================================== */

/**
 * @brief 输出错误级别消息
 *
 * @details 写入 per-CPU 环形缓冲（关中断 < 1μs，无 UART 阻塞）。
 *          消息前缀 "E: "。适用于：panic 诊断、硬件故障、安全违规。
 *
 * @param str 消息字符串
 */
void klog_error(const char *str)
{
    if (((uint32_t)s_current_level) >= ((uint32_t)KLOG_LEVEL_ERROR))
    {
        klog_write("E: ", str);
    }
}

/**
 * @brief 输出警告级别消息
 *
 * @details 写入 per-CPU 环形缓冲。消息前缀 "W: "。
 *          适用于：栈溢出检测、资源耗尽、配置异常。
 *
 * @param str 消息字符串
 */
void klog_warn(const char *str)
{
    if (((uint32_t)s_current_level) >= ((uint32_t)KLOG_LEVEL_WARN))
    {
        klog_write("W: ", str);
    }
}

#if CONFIG_DEBUG
/**
 * @brief 输出信息级别消息（仅 CONFIG_DEBUG=1 时编译）
 *
 * @details 写入 per-CPU 环形缓冲。消息前缀 "I: "。
 *
 * @param str 消息字符串
 */
void klog_info(const char *str)
{
    if (((uint32_t)s_current_level) >= ((uint32_t)KLOG_LEVEL_INFO))
    {
        klog_write("I: ", str);
    }
}

/**
 * @brief 输出调试级别消息（仅 CONFIG_DEBUG=1 时编译）
 *
 * @details 写入 per-CPU 环形缓冲。消息前缀 "D: "。
 *
 * @param str 消息字符串
 */
void klog_debug(const char *str)
{
    if (((uint32_t)s_current_level) >= ((uint32_t)KLOG_LEVEL_DEBUG))
    {
        klog_write("D: ", str);
    }
}
#endif /* CONFIG_DEBUG */

/* ========================================================================
 * 公共接口：低级输出（不受级别过滤）
 * ======================================================================== */

/**
 * @brief 输出单个字符到日志缓冲
 *
 * @details 不受级别过滤，用于 hex/dec 辅助函数组装诊断行。
 *          通过关调度（preempt_disable）保护写入（per-CPU 无锁竞争）。
 *
 * @param c 要输出的字符
 */
void klog_putc(char c)
{
    if (!s_initialized)
    {
        return;
    }

    klog_buf_t *buf = klog_get_buf();

    preempt_disable();
    klog_buf_putc_preemptoff(buf, c);
    preempt_enable();
}

/**
 * @brief 输出 32 位无符号十进制值到日志缓冲
 *
 * @details 将 val 转换为十进制 ASCII 字符串，逐字符写入缓冲。
 *
 * @param val 要输出的值
 */
void klog_dec(uint32_t val)
{
    char tmp[10];
    int32_t i = 9;

    tmp[i] = '\0';
    if (val == 0U)
    {
        klog_putc('0');
        return;
    }

    while ((val > 0U) && (i > 0))
    {
        i--;
        tmp[i] = (char)('0' + (val % 10U));
        val /= 10U;
    }

    while (i < 9)
    {
        klog_putc(tmp[i]);
        i++;
    }
}

/**
 * @brief 输出 32 位十六进制值到日志缓冲
 *
 * @details 输出 8 位十六进制数字（大写），无 "0x" 前缀。
 *          用于寄存器值、物理地址低 32 位等诊断输出。
 *
 * @param val 要输出的值
 */
void klog_hex32(uint32_t val)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    int32_t i;

    for (i = 28; i >= 0; i -= 4)
    {
        uint8_t nibble = (uint8_t)((val >> (uint32_t)i) & 0xFU);
        klog_putc(hex_chars[nibble]);
    }
}

/**
 * @brief 输出 64 位十六进制值到日志缓冲
 *
 * @details 输出 16 位十六进制数字（大写），无 "0x" 前缀。
 *          用于 64 位虚拟地址、物理地址等诊断输出。
 *
 * @param val 要输出的值
 */
void klog_hex64(uint64_t val)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    int32_t i;

    for (i = 60; i >= 0; i -= 4)
    {
        uint8_t nibble = (uint8_t)((val >> (uint32_t)i) & 0xFU);
        klog_putc(hex_chars[nibble]);
    }
}

/* ========================================================================
 * 公共接口：异步刷新与紧急输出
 * ======================================================================== */

/**
 * @brief 将环形缓冲中的待输出消息刷新到控制台
 *
 * @details 从当前 CPU 的环形缓冲取出消息并输出到 UART。
 *
 *          **多核输出顺序保证**：
 *          多个 CPU 的 idle 线程可能同时调用此函数。
 *          通过全局自旋锁（s_flush_lock）串行化输出，
 *          同一时刻只有一个 CPU 在执行 UART 输出，
 *          防止字符交织乱码。
 *
 *          **实时性**：
 *          此函数会阻塞（等待 UART TX FIFO），因此**只能在非实时上下文调用**：
 *          - idle 线程循环中周期调用
 *          - panic 路径使用 klog_panic 替代
 *
 * @note 禁止在中断处理或调度路径中调用此函数
 */
void klog_flush(void)
{
    if (!s_initialized)
    {
        return;
    }

    klog_buf_t *buf = klog_get_buf();

    /* 获取全局 flush 锁（串行化多核 UART 输出） */
    ticket_lock_acquire(&s_flush_lock);

    /* 逐字符取出并输出（UART 输出是慢操作，但持 flush 锁期间可接受） */
    while (buf->tail != buf->head)
    {
        char c = buf->buf[buf->tail];
        buf->tail = (buf->tail + 1U) % KLOG_BUF_SIZE;
        hal_console_putc(c);
    }

    /* 释放全局 flush 锁 */
    ticket_lock_release(&s_flush_lock);
}

/**
 * @brief 紧急同步输出（绕过环形缓冲，直接写控制台）
 *
 * @details 用于 panic/异常处理路径，此时环形缓冲机制可能不可靠。
 *          直接调用 HAL 控制台输出，不经过环形缓冲，不获取 flush 锁。
 *
 * @param str 要输出的字符串
 */
void klog_panic(const char *str)
{
    if (str != NULL)
    {
        hal_console_puts(str);
    }
}
