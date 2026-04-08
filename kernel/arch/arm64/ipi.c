/**
 * @file    ipi.c
 * @brief   核心间中断（IPI）实现
 * @author  AISafe64 Team
 * @date    2026-04-08
 * @version 3.0
 *
 * @details 实现核心间中断的发送、接收和分发处理。
 *          - IPI 批处理（Coalescing）：合并多个 IPI 为一次 SGI
 *          - IPI 延迟统计：每 CPU 记录 min/max/avg 延迟
 *          - HAL 接口迁移：TLB/ASID 操作通过 HAL 层
 *          - 能力撤销和 ASID 刷新广播支持
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: MP-004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/ipi.h>
#include <kernel/smp.h>
#include <kernel/gic.h>
#include <kernel/config.h>
#include <kernel/barrier.h>
#include <kernel/spinlock.h>
#include <kernel/errno.h>
#include <stdint.h>
#include "../../sched/scheduler.h"
#include "hal.h"

/* ========================================================================
 * IPI 全局状态
 * ======================================================================== */

/** @brief IPI 处理函数表 */
static void (*s_ipi_handlers[IPI_TYPE_COUNT])(void *arg);

/** @brief IPI 处理参数表 */
static void *s_ipi_args[IPI_TYPE_COUNT];

/** @brief 每 CPU 函数调用数据 */
static ipi_call_func_t s_call_func_data[CONFIG_MAX_CPUS];

/** @brief IPI 统计计数 */
static uint64_t s_ipi_stats[CONFIG_MAX_CPUS];

/** @brief IPI 锁 */
static TicketLock_t s_ipi_lock;

/* ========================================================================
 * IPI 批处理状态
 * ======================================================================== */

/** @brief 每 CPU 待处理 IPI 位图 */
static volatile uint32_t s_ipi_pending[CONFIG_MAX_CPUS];

/** @brief 每 CPU IPI 批处理锁 */
static volatile uint32_t s_ipi_pending_lock[CONFIG_MAX_CPUS];

/* ========================================================================
 * IPI 延迟统计状态
 * ======================================================================== */

/** @brief 每 CPU 发送时间戳（用于延迟计算） */
static uint64_t s_ipi_send_timestamp[CONFIG_MAX_CPUS][IPI_TYPE_COUNT];

/** @brief 每 CPU 延迟统计 */
static ipi_latency_stats_t s_ipi_latency[CONFIG_MAX_CPUS];

/** @brief 延迟统计锁 */
static TicketLock_t s_latency_lock;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 计时器计数转换为纳秒
 *
 * @param ticks 计时器计数值
 *
 * @return 对应的纳秒值
 */
static uint64_t ipi_ticks_to_ns(uint64_t ticks)
{
    uint64_t freq;

    freq = hal_timer_get_freq();
    if (freq == 0ULL)
    {
        return 0ULL;
    }

    /* 纳秒 = ticks * 1,000,000,000 / freq */
    return (ticks * 1000000000ULL) / freq;
}

/**
 * @brief 更新延迟统计
 *
 * @param cpu_id CPU 编号
 * @param latency_ns 延迟（纳秒）
 */
static void ipi_update_latency(uint32_t cpu_id, uint64_t latency_ns)
{
    ipi_latency_stats_t *stats;

    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return;
    }

    if (latency_ns == 0ULL)
    {
        return;
    }

    stats = &s_ipi_latency[cpu_id];

    ticket_lock_acquire(&s_latency_lock);

    if ((stats->count == 0ULL) || (latency_ns < stats->min_ns))
    {
        stats->min_ns = latency_ns;
    }

    if (latency_ns > stats->max_ns)
    {
        stats->max_ns = latency_ns;
    }

    /* 增量平均：(old_avg * count + new_value) / (count + 1) */
    stats->avg_ns = ((stats->avg_ns * stats->count) + latency_ns)
                    / (stats->count + 1ULL);
    stats->count++;

    ticket_lock_release(&s_latency_lock);
}

/* ========================================================================
 * IPI 初始化
 * ======================================================================== */

kernel_status_t ipi_init(void)
{
    uint32_t i;
    uint32_t j;

    /* 逐字段清零（避免编译器生成 memset 调用） */
    for (i = 0U; i < IPI_TYPE_COUNT; i++)
    {
        s_ipi_handlers[i] = NULL;
        s_ipi_args[i] = NULL;
    }

    for (i = 0U; i < CONFIG_MAX_CPUS; i++)
    {
        s_call_func_data[i].func = NULL;
        s_call_func_data[i].arg = NULL;
        s_call_func_data[i].done = 0U;
        s_ipi_stats[i] = 0ULL;
        s_ipi_pending[i] = 0U;
        s_ipi_pending_lock[i] = 0U;

        /* 初始化延迟统计 */
        s_ipi_latency[i].min_ns = 0ULL;
        s_ipi_latency[i].max_ns = 0ULL;
        s_ipi_latency[i].avg_ns = 0ULL;
        s_ipi_latency[i].count = 0ULL;

        for (j = 0U; j < IPI_TYPE_COUNT; j++)
        {
            s_ipi_send_timestamp[i][j] = 0ULL;
        }
    }

    ticket_lock_init(&s_ipi_lock);
    ticket_lock_init(&s_latency_lock);

    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * IPI 发送（批处理模式）
 * ======================================================================== */

kernel_status_t ipi_send(uint32_t target_cpu, uint32_t ipi_type)
{
    uint32_t old_lock;

    if (target_cpu >= CONFIG_MAX_CPUS)
    {
        return -(int32_t)EINVAL;
    }

    if (ipi_type >= IPI_TYPE_COUNT)
    {
        return -(int32_t)EINVAL;
    }

    if (!smp_cpu_online(target_cpu))
    {
        return -(int32_t)EINVAL;
    }

    /* 记录发送时间戳（用于延迟统计） */
    s_ipi_send_timestamp[target_cpu][ipi_type] = hal_timer_get_count();

    /* 自旋等待获取目标 CPU 的 pending 锁 */
    do
    {
        old_lock = atomic_cas_u32(&s_ipi_pending_lock[target_cpu], 0U, 1U);
        if (old_lock != 0U)
        {
            cpu_relax();
        }
    } while (old_lock != 0U);

    /* 设置待处理位图 */
    s_ipi_pending[target_cpu] |= (1U << ipi_type);

    /* 释放锁 */
    barrier_store();
    s_ipi_pending_lock[target_cpu] = 0U;

    /* 立即触发 SGI 通知目标 CPU */
    return gic_send_sgi(0U, (uint8_t)(1U << target_cpu));
}

kernel_status_t ipi_broadcast(uint32_t ipi_type, bool exclude_self)
{
    uint32_t cpu_id;
    uint8_t target_mask = 0U;
    uint32_t self = smp_get_cpu_id();

    if (ipi_type >= IPI_TYPE_COUNT)
    {
        return -(int32_t)EINVAL;
    }

    for (cpu_id = 0U; cpu_id < CONFIG_MAX_CPUS; cpu_id++)
    {
        if (smp_cpu_online(cpu_id))
        {
            if (exclude_self && (cpu_id == self))
            {
                continue;
            }

            /* 记录发送时间戳 */
            s_ipi_send_timestamp[cpu_id][ipi_type] = hal_timer_get_count();

            /* 设置待处理位图 */
            s_ipi_pending[cpu_id] |= (1U << ipi_type);

            target_mask |= (uint8_t)(1U << cpu_id);
        }
    }

    if (target_mask != 0U)
    {
        return gic_send_sgi(0U, target_mask);
    }

    return KERNEL_OK;
}

/* ========================================================================
 * IPI 批处理刷新
 * ======================================================================== */

kernel_status_t ipi_flush_pending(uint32_t cpu_id)
{
    uint32_t pending;
    uint32_t bit;
    uint32_t type;
    uint32_t old_lock;

    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return -(int32_t)EINVAL;
    }

    /* 获取 pending 锁 */
    do
    {
        old_lock = atomic_cas_u32(&s_ipi_pending_lock[cpu_id], 0U, 1U);
        if (old_lock != 0U)
        {
            cpu_relax();
        }
    } while (old_lock != 0U);

    pending = s_ipi_pending[cpu_id];

    /* 清除待处理位图 */
    s_ipi_pending[cpu_id] = 0U;

    barrier_store();
    s_ipi_pending_lock[cpu_id] = 0U;

    /* 处理所有待处理的 IPI */
    bit = pending;
    while (bit != 0U)
    {
        type = 0U;
        while ((bit & 1U) == 0U)
        {
            bit >>= 1U;
            type++;
        }

        ipi_handler(type);

        bit >>= 1U;
    }

    return KERNEL_OK;
}

void ipi_flush_pending_self(void)
{
    uint32_t cpu_id = smp_get_cpu_id();
    (void)ipi_flush_pending(cpu_id);
}

/* ========================================================================
 * IPI 函数调用
 * ======================================================================== */

kernel_status_t ipi_call_func(uint32_t target_cpu,
                                void (*func)(void *arg),
                                void *arg,
                                bool wait)
{
    ipi_call_func_t *call_data;

    if (target_cpu >= CONFIG_MAX_CPUS)
    {
        return -(int32_t)EINVAL;
    }

    if (func == NULL)
    {
        return -(int32_t)EINVAL;
    }

    call_data = &s_call_func_data[target_cpu];
    call_data->func = func;
    call_data->arg = arg;
    call_data->done = 0U;

    barrier();

    /* 发送 CALL_FUNC IPI */
    (void)ipi_send(target_cpu, IPI_TYPE_CALL_FUNC);

    if (wait)
    {
        uint32_t timeout;
        for (timeout = 0U; timeout < 1000000U; timeout++)
        {
            barrier();
            if (call_data->done != 0U)
            {
                break;
            }
        }
    }

    return KERNEL_OK;
}

/* ========================================================================
 * IPI 延迟统计查询
 * ======================================================================== */

kernel_status_t ipi_get_latency_stats(uint32_t cpu_id, ipi_latency_stats_t *stats)
{
    if (cpu_id >= CONFIG_MAX_CPUS)
    {
        return -(int32_t)EINVAL;
    }

    if (stats == NULL)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&s_latency_lock);

    stats->min_ns = s_ipi_latency[cpu_id].min_ns;
    stats->max_ns = s_ipi_latency[cpu_id].max_ns;
    stats->avg_ns = s_ipi_latency[cpu_id].avg_ns;
    stats->count = s_ipi_latency[cpu_id].count;

    ticket_lock_release(&s_latency_lock);

    return KERNEL_OK;
}

/* ========================================================================
 * IPI 中断处理
 * ======================================================================== */

void ipi_handler(uint32_t ipi_type)
{
    uint32_t cpu_id = smp_get_cpu_id();
    uint64_t recv_ts;
    uint64_t send_ts;
    uint64_t latency_ticks;
    uint64_t latency_ns;

    if (cpu_id < CONFIG_MAX_CPUS)
    {
        s_ipi_stats[cpu_id]++;
    }

    /* 记录接收时间戳并计算延迟 */
    recv_ts = hal_timer_get_count();
    if ((cpu_id < CONFIG_MAX_CPUS) && (ipi_type < IPI_TYPE_COUNT))
    {
        send_ts = s_ipi_send_timestamp[cpu_id][ipi_type];
        if ((send_ts != 0ULL) && (recv_ts > send_ts))
        {
            latency_ticks = recv_ts - send_ts;
            latency_ns = ipi_ticks_to_ns(latency_ticks);
            ipi_update_latency(cpu_id, latency_ns);
        }
    }

    if (ipi_type >= IPI_TYPE_COUNT)
    {
        return;
    }

    switch (ipi_type)
    {
        case IPI_TYPE_RESCHEDULE:
        {
            /* 触发重新调度 */
            schedule();
            break;
        }

        case IPI_TYPE_STOP:
        {
            /* 停止当前 CPU */
            (void)smp_cpu_stop(cpu_id);
            for (;;)
            {
                hal_wfe();
            }
            break;
        }

        case IPI_TYPE_CALL_FUNC:
        {
            ipi_call_func_t *call_data = &s_call_func_data[cpu_id];
            if (call_data->func != NULL)
            {
                call_data->func(call_data->arg);
                call_data->done = 1U;
                barrier();
            }
            break;
        }

        case IPI_TYPE_TLB_FLUSH:
        {
            /* 通过 HAL 接口执行 TLB 刷新（避免内联汇编） */
            hal_tlb_invalidate_all();
            break;
        }

        case IPI_TYPE_CACHE_MAINT:
        {
            /* 缓存维护操作 */
            break;
        }

        case IPI_TYPE_CAP_REVOKE:
        {
            /* 能力撤销广播：使本地能力缓存失效 */
            if (s_ipi_handlers[IPI_TYPE_CAP_REVOKE] != NULL)
            {
                s_ipi_handlers[IPI_TYPE_CAP_REVOKE](
                    s_ipi_args[IPI_TYPE_CAP_REVOKE]);
            }
            break;
        }

        case IPI_TYPE_ASID_FLUSH:
        {
            /* ASID 刷新广播：通过 HAL 执行 */
            hal_tlb_invalidate_all();
            break;
        }

        default:
        {
            /* 未知 IPI 类型，调用注册的处理函数 */
            if (ipi_type < IPI_TYPE_COUNT)
            {
                if (s_ipi_handlers[ipi_type] != NULL)
                {
                    s_ipi_handlers[ipi_type](s_ipi_args[ipi_type]);
                }
            }
            break;
        }
    }
}
