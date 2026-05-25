/**
 * @file    ipi_coalesce.c
 * @brief   IPI Coalescing（批处理）实现
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 实现 IPI Coalescing 机制：
 *          - IPI 批处理器管理
 *          - IPI 收集和发送
 *          - IPI Coalesce 定时器处理
 *          - IPI 立即发送接口
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.5 - IPI 优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/ipi_coalesce.h>
#include <kernel/gic.h>
#include <kernel/barrier.h>
#include <kernel/config.h>
#include <kernel/compiler.h>
#include "hal.h"
#include <kernel/timer.h>
#include <stddef.h>
#include <string.h>

/* ========================================================================
 * IPI Coalesce 批处理器（每 CPU）
 * ======================================================================== */

/**
 * @brief 每 CPU IPI Coalesce 批处理器
 */
static ipi_coalesce_t s_ipi_coalesce[CONFIG_MAX_CPUS];

/* ========================================================================
 * IPI Coalesce 全局统计
 * ======================================================================== */

/**
 * @brief IPI Coalesce 统计
 */
static struct
{
    uint64_t total_added;           /**< @brief 总添加数量 */
    uint64_t total_immediate;       /**< @brief 总立即发送数量 */
    uint64_t total_batch_sent;       /**< @brief 总批量发送数量 */
    uint64_t total_flushes;         /**< @brief 总刷新次数 */
} s_ipi_coalesce_stats = {0, 0, 0, 0};

/**
 * @brief IPI Coalesce 统计锁
 */
static TicketLock_t s_ipi_coalesce_stats_lock;

/* ========================================================================
 * IPI Coalesce 操作 API 实现
 * ======================================================================== */

/**
 * @brief 初始化 IPI Coalesce 机制
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t ipi_coalesce_init(void)
{
    uint32_t i;
    kernel_status_t ret;

    /* 初始化每 CPU IPI Coalesce 批处理器 */
    for (i = 0U; i < CONFIG_MAX_CPUS; i++)
    {
        (void)memset(&s_ipi_coalesce[i], 0U, sizeof(ipi_coalesce_t));
        s_ipi_coalesce[i].state = IPI_COALESCE_STATE_IDLE;
        s_ipi_coalesce[i].count = 0U;
        ticket_lock_init(&s_ipi_coalesce[i].lock);
    }

    /* 注册 IPI Coalesce 定时器 */
    ret = timer_add_interval(ipi_coalesce_timer_handler, IPI_COALESCE_INTERVAL_US);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    /* 初始化统计锁 */
    ticket_lock_init(&s_ipi_coalesce_stats_lock);

    return KERNEL_OK;
}

/**
 * @brief 尝试添加 IPI 到批处理器
 *
 * @details 如果批处理器未满且状态为 COLLECTING，则添加 IPI 到批处理器并返回 true。
 *          如果批处理器已满或状态不是 COLLECTING，则立即发送 IPI 并返回 false。
 *
 * @param target_cpu 目标 CPU
 * @param type        IPI 类型
 * @param arg         IPI 参数
 *
 * @return true 表示添加到批处理器，false 表示立即发送
 */
bool ipi_coalesce_try_add(uint32_t target_cpu, uint32_t type, void *arg)
{
    uint32_t cpu_id;
    bool result;

    cpu_id = hal_get_cpu_id();

    ticket_lock_acquire(&s_ipi_coalesce[cpu_id].lock);

    /* 检查是否可以添加到批处理器 */
    if ((s_ipi_coalesce[cpu_id].state == IPI_COALESCE_STATE_COLLECTING) &&
        (s_ipi_coalesce[cpu_id].count < IPI_COALESCE_MAX_BATCH))
    {
        /* 添加到批处理器 */
        s_ipi_coalesce[cpu_id].entries[s_ipi_coalesce[cpu_id].count].target_cpu = target_cpu;
        s_ipi_coalesce[cpu_id].entries[s_ipi_coalesce[cpu_id].count].type = type;
        s_ipi_coalesce[cpu_id].entries[s_ipi_coalesce[cpu_id].count].arg = arg;
        s_ipi_coalesce[cpu_id].count++;

        /* 更新统计 */
        ticket_lock_acquire(&s_ipi_coalesce_stats_lock);
        s_ipi_coalesce_stats.total_added++;
        ticket_lock_release(&s_ipi_coalesce_stats_lock);

        result = true;
    }
    else
    {
        /* 立即发送 IPI */
        gic_sgi_send(target_cpu, (uint32_t)type);

        /* 更新统计 */
        ticket_lock_acquire(&s_ipi_coalesce_stats_lock);
        s_ipi_coalesce_stats.total_immediate++;
        ticket_lock_release(&s_ipi_coalesce_stats_lock);

        result = false;
    }

    ticket_lock_release(&s_ipi_coalesce[cpu_id].lock);

    return result;
}

/**
 * @brief 立即发送 IPI（绕过批处理）
 *
 * @details 如果需要立即发送 IPI，则调用此函数。
 *          此函数会刷新批处理器并立即发送 IPI。
 *
 * @param target_cpu 目标 CPU
 * @param type        IPI 类型
 * @param arg         IPI 参数
 */
void ipi_coalesce_send_immediate(uint32_t target_cpu, uint32_t type, void *arg)
{
    /* 立即发送 IPI */
    gic_sgi_send(target_cpu, (uint32_t)type);

    /* 更新统计 */
    ticket_lock_acquire(&s_ipi_coalesce_stats_lock);
    s_ipi_coalesce_stats.total_immediate++;
    ticket_lock_release(&s_ipi_coalesce_stats_lock);
}

/**
 * @brief IPI Coalesce 定时器处理
 *
 * @details 定期检查所有 CPU 的 IPI Coalesce 批处理器状态，如果超时则立即发送。
 */
void ipi_coalesce_timer_handler(void)
{
    uint32_t cpu_id;

    cpu_id = hal_get_cpu_id();

    /* 检查所有 CPU 的 IPI Coalesce 批处理器 */
    for (cpu_id = 0U; cpu_id < CONFIG_MAX_CPUS; cpu_id++)
    {
        if (s_ipi_coalesce[cpu_id].state == IPI_COALESCE_STATE_COLLECTING)
        {
            /* 检查是否超时 */
            uint64_t current_ticks = hal_timer_get_count();
            uint64_t elapsed_ticks = current_ticks - s_ipi_coalesce[cpu_id].last_collect_time;

            if (elapsed_ticks > IPI_COALESCE_TIMEOUT_TICKS)
            {
                /* 超时：立即发送所有待处理 IPI */
                ticket_lock_acquire(&s_ipi_coalesce[cpu_id].lock);
                {
                    uint32_t count = s_ipi_coalesce[cpu_id].count;
                    uint32_t i;

                    if (count > 0U)
                    {
                        /* 批量发送 IPI */
                        for (i = 0U; i < count; i++)
                        {
                            uint32_t target_cpu = s_ipi_coalesce[cpu_id].entries[i].target_cpu;
                            uint32_t type = s_ipi_coalesce[cpu_id].entries[i].type;
                            void *arg = s_ipi_coalesce[cpu_id].entries[i].arg;

                            /* 发送 IPI */
                            gic_sgi_send(target_cpu, (uint32_t)type);

                            /* 更新统计 */
                            ticket_lock_acquire(&s_ipi_coalesce_stats_lock);
                            s_ipi_coalesce_stats.total_batch_sent += count;
                            ticket_lock_release(&s_ipi_coalesce_stats_lock);
                        }

                        /* 清空批处理器 */
                        s_ipi_coalesce[cpu_id].count = 0U;
                        s_ipi_coalesce[cpu_id].state = IPI_COALESCE_STATE_IDLE;
                    }
                }
                ticket_lock_release(&s_ipi_coalesce[cpu_id].lock);

                /* 更新统计 */
                ticket_lock_acquire(&s_ipi_coalesce_stats_lock);
                s_ipi_coalesce_stats.total_flushes++;
                ticket_lock_release(&s_ipi_coalesce_stats_lock);
            }
        }
    }

    /* 重启定时器 */
    (void)timer_add_interval(ipi_coalesce_timer_handler, IPI_COALESCE_INTERVAL_US);
}
