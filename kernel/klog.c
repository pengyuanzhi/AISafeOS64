/**
 * @file    klog.c
 * @brief   内核标准日志接口实现（异步环形缓冲）
 * @author  AISafe64 Team
 * @date    2026-07-05
 * @version 2.0
 *
 * @details 实现设计：
 *
 *          **异步环形缓冲**：
 *          - 每个 CPU 一个独立环形缓冲（消除跨核锁竞争）
 *          - klog_error/warn/info/debug 写入缓冲（关中断 < 1μs，无 UART 阻塞）
 *          - klog_flush 在 idle 线程中异步输出到控制台
 *          - 缓冲满时丢弃最旧数据（不阻塞调用者）
 *
 *          **平台无关**：
 *          - 通过 hal_console_putc/puts 输出（HAL 层绑定具体硬件）
 *          - klog 代码不包含任何 UART 基地址/寄存器定义
 *
 *          **实时安全**：
 *          - 写入路径：关中断 + memcpy 到缓冲（确定性 < 1μs）
 *          - 输出路径：idle 线程上下文（可被抢占）
 *          - panic 路径：klog_panic 绕过缓冲直接输出（异常诊断专用）
 *
 * @note    MISRA-C:2012 合规
 *
 * @revision history
 * v1.0 2026-07-04 初版（同步阻塞，硬编码 UART base）
 * v2.0 2026-07-05 异步环形缓冲 + 平台无关（当前版本）
 */

#include <kernel/klog.h>
#include <kernel/config.h>
#include <kernel/spinlock.h>
#include <kernel/barrier.h>
#include "hal.h"
#include <stdbool.h>

/* ========================================================================
 * 内部数据结构
 * ======================================================================== */

/**
 * @brief per-CPU 日志环形缓冲
 *
 * @details 每个 CPU 独立缓冲，消除跨核锁竞争。
 *          head：写入位置（生产者）
 *          tail：读取位置（消费者，flush 时推进）
 *          buf[]：环形数据区
 */
typedef struct
{
    uint32_t head;                          /**< @brief 下一个写入位置 */
    uint32_t tail;                          /**< @brief 下一个读取位置 */
    uint32_t dropped;                       /**< @brief 丢弃的字节数（诊断统计） */
    TicketLock_t lock;                      /**< @brief 保护 head/tail 的自旋锁 */
    char buf[KLOG_BUF_SIZE];                /**< @brief 环形数据区 */
} klog_buf_t;

/** @brief 最大 CPU 数 */
#ifndef CONFIG_MAX_CPUS
#define CONFIG_MAX_CPUS 8U
#endif

/** @brief per-CPU 日志缓冲实例 */
static klog_buf_t s_klog_bufs[CONFIG_MAX_CPUS];

/** @brief 当前日志级别（运行时可调） */
static klog_level_t s_current_level = KLOG_DEFAULT_LEVEL;

/** @brief klog 是否已初始化 */
static bool s_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 获取当前 CPU 的日志缓冲
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
 * @brief 向环形缓冲写入一个字节（调用者持锁）
 *
 * @param buf 目标缓冲
 * @param c   要写入的字节
 */
static void klog_buf_putc_locked(klog_buf_t *buf, char c)
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
 * @brief 向环形缓冲写入字符串（调用者持锁）
 *
 * @param buf 目标缓冲
 * @param str 以 NULL 结尾的字符串
 */
static void klog_buf_puts_locked(klog_buf_t *buf, const char *str)
{
    if (str == NULL)
    {
        return;
    }

    while (*str != '\0')
    {
        klog_buf_putc_locked(buf, *str);
        str++;
    }
}

/**
 * @brief 向当前 CPU 缓冲写入消息（关中断 + 持锁）
 *
 * @details 实时安全入口：关中断 → 取锁 → 写入 → 释锁 → 恢复中断。
 *          整个操作确定性 < 1μs（无 UART 阻塞）。
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

    {
        klog_buf_t *buf = klog_get_buf();
        uint32_t irq_state = ticket_lock_acquire_irqsave(&buf->lock);

        if (prefix != NULL)
        {
            klog_buf_puts_locked(buf, prefix);
        }
        klog_buf_puts_locked(buf, str);

        ticket_lock_release_irqrestore(&buf->lock, irq_state);
    }
}

/* ========================================================================
 * 公共接口：初始化与配置
 * ======================================================================== */

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
        ticket_lock_init(&s_klog_bufs[i].lock);
    }

    /* 初始化控制台硬件（HAL 层内部绑定具体 UART） */
    hal_console_init();

    s_initialized = true;
}

void klog_set_level(klog_level_t level)
{
    s_current_level = level;
}

/* ========================================================================
 * 公共接口：日志输出（写入环形缓冲，异步输出）
 * ======================================================================== */

void klog_error(const char *str)
{
    if (((uint32_t)s_current_level) >= ((uint32_t)KLOG_LEVEL_ERROR))
    {
        klog_write("E: ", str);
    }
}

void klog_warn(const char *str)
{
    if (((uint32_t)s_current_level) >= ((uint32_t)KLOG_LEVEL_WARN))
    {
        klog_write("W: ", str);
    }
}

#if CONFIG_DEBUG
void klog_info(const char *str)
{
    if (((uint32_t)s_current_level) >= ((uint32_t)KLOG_LEVEL_INFO))
    {
        klog_write("I: ", str);
    }
}

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

void klog_putc(char c)
{
    if (!s_initialized)
    {
        return;
    }

    {
        klog_buf_t *buf = klog_get_buf();
        uint32_t irq_state = ticket_lock_acquire_irqsave(&buf->lock);
        klog_buf_putc_locked(buf, c);
        ticket_lock_release_irqrestore(&buf->lock, irq_state);
    }
}

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

void klog_flush(void)
{
    if (!s_initialized)
    {
        return;
    }

    {
        klog_buf_t *buf = klog_get_buf();
        uint32_t irq_state = ticket_lock_acquire_irqsave(&buf->lock);

        while (buf->tail != buf->head)
        {
            char c = buf->buf[buf->tail];
            buf->tail = (buf->tail + 1U) % KLOG_BUF_SIZE;

            /* 释锁后输出单字符（避免持锁时阻塞 UART） */
            ticket_lock_release_irqrestore(&buf->lock, irq_state);
            hal_console_putc(c);
            irq_state = ticket_lock_acquire_irqsave(&buf->lock);
        }

        ticket_lock_release_irqrestore(&buf->lock, irq_state);
    }
}

void klog_panic(const char *str)
{
    /* panic 路径：直接同步输出到控制台，绕过环形缓冲 */
    if (str != NULL)
    {
        hal_console_puts(str);
    }
}
