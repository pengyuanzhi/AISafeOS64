/**
 * @file    ipi.c
 * @brief   核心间中断（IPI）实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 实现核心间中断的发送、接收和分发处理。
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
#include <string.h>

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
 * IPI 初始化
 * ======================================================================== */

kernel_status_t ipi_init(void)
{
    uint32_t i;

    (void)memset(s_ipi_handlers, 0, sizeof(s_ipi_handlers));
    (void)memset(s_ipi_args, 0, sizeof(s_ipi_args));
    (void)memset(s_call_func_data, 0, sizeof(s_call_func_data));
    (void)memset(s_ipi_stats, 0, sizeof(s_ipi_stats));

    ticket_lock_init(&s_ipi_lock);

    /* 注册 SGI 中断处理（SGI 0 用于 IPI） */
    for (i = 0U; i < IPI_TYPE_COUNT; i++)
    {
        s_ipi_handlers[i] = NULL;
        s_ipi_args[i] = NULL;
    }

    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * IPI 发送
 * ======================================================================== */

kernel_status_t ipi_send(uint32_t target_cpu, uint32_t ipi_type)
{
    uint64_t sgi_val;

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

    /* 构造 ICC_SGI1R_EL1 值 */
    sgi_val = ((uint64_t)ipi_type << 24U) |
              ((uint64_t)1U << (16U + target_cpu));

    __asm__ volatile(
        "msr ICC_SGI1R_EL1, %0\n"
        "dsb ish\n"
        :: "r"(sgi_val)
        : "memory"
    );

    return KERNEL_OK;
}

kernel_status_t ipi_broadcast(uint32_t ipi_type, bool exclude_self)
{
    uint32_t cpu_id;
    uint64_t target_mask = 0U;
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
            target_mask |= (1U << (16U + cpu_id));
        }
    }

    if (target_mask != 0U)
    {
        uint64_t sgi_val = ((uint64_t)ipi_type << 24U) | target_mask;

        __asm__ volatile(
            "msr ICC_SGI1R_EL1, %0\n"
            "dsb ish\n"
            :: "r"(sgi_val)
            : "memory"
        );
    }

    return KERNEL_OK;
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
 * IPI 中断处理
 * ======================================================================== */

void ipi_handler(uint32_t ipi_type)
{
    uint32_t cpu_id = smp_get_cpu_id();

    if (cpu_id < CONFIG_MAX_CPUS)
    {
        s_ipi_stats[cpu_id]++;
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
            /* 实际实现中设置 need_reschedule 标志 */
            break;
        }

        case IPI_TYPE_STOP:
        {
            /* 停止当前 CPU */
            (void)smp_cpu_stop(cpu_id);
            for (;;)
            {
                __asm__ volatile("wfe");
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
            __asm__ volatile(
                "tlbi vmalle1is\n"
                "dsb ish\n"
                "isb\n"
                ::: "memory"
            );
            break;
        }

        case IPI_TYPE_CACHE_MAINT:
        {
            /* 缓存维护操作 */
            break;
        }

        default:
        {
            /* 未知 IPI 类型，调用注册的处理函数 */
            if (s_ipi_handlers[ipi_type] != NULL)
            {
                s_ipi_handlers[ipi_type](s_ipi_args[ipi_type]);
            }
            break;
        }
    }
}
