/**
 * @file    arinc653.c
 * @brief   ARINC 653 分区调度器实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 实现 ARINC 653 时间分区调度：
 *          - 主时间框架（Major Frame）循环调度
 *          - 分区时间窗口管理
 *          - 分区内优先级调度
 *          - 分区状态机管理
 *          - 空间隔离：线程绑定到分区
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SC-003, SC-007~008
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "arinc653.h"
#include "scheduler.h"
#include "thread.h"

#include <kernel/barrier.h>
#include <kernel/errno.h>
#include <kernel/config.h>
#include <stdint.h>
#include <string.h>
#include "hal.h"

/* HAL 接口 */
extern tick_t hal_get_tick_count(void);

/* ========================================================================
 * 分区表
 * ======================================================================== */

/** @brief 分区描述符静态池 */
static partition_desc_t s_partitions[ARINC_MAX_PARTITIONS];

/** @brief 分区使用标记 */
static bool s_partition_used[ARINC_MAX_PARTITIONS];

/** @brief 线程到分区的绑定表 */
static uint32_t s_thread_partition[CONFIG_MAX_THREADS];

/** @brief 主时间框架实例 */
static major_frame_t s_major_frame;

/** @brief 当前活跃分区指针 */
static partition_desc_t *s_current_partition;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 字符串复制（安全，最多 n-1 字符）
 *
 * @param dst   目标缓冲区
 * @param src   源字符串
 * @param n     缓冲区大小
 */
static void safe_strcpy(char *dst, const char *src, uint32_t n)
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
 * 初始化
 * ======================================================================== */

kernel_status_t arinc653_init(void)
{
    uint32_t i;

    (void)memset(s_partitions, 0, sizeof(s_partitions));
    (void)memset(s_partition_used, 0, sizeof(s_partition_used));
    (void)memset(s_thread_partition, 0xFF, sizeof(s_thread_partition));
    (void)memset(&s_major_frame, 0, sizeof(major_frame_t));
    s_current_partition = NULL;

    /* 初始化分区描述符 */
    for (i = 0U; i < ARINC_MAX_PARTITIONS; i++)
    {
        s_partitions[i].partition_id = i;
        s_partitions[i].state = PARTITION_STATE_IDLE;
        s_partitions[i].proc_count = 0U;
        INIT_LIST_HEAD(&s_partitions[i].proc_list);
        INIT_LIST_HEAD(&s_partitions[i].rq_list);
    }

    /* 初始化主时间框架 */
    s_major_frame.window_count = 0U;
    s_major_frame.major_frame_length = ARINC_DEFAULT_MAJOR_FRAME;
    s_major_frame.current_tick = 0ULL;
    s_major_frame.current_window_idx = 0U;
    s_major_frame.lock = 0U;
    s_major_frame.initialized = false;

    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 创建分区
 * ======================================================================== */

int32_t arinc653_create_partition(const char *name,
                                   uint32_t period,
                                   uint32_t duration,
                                   priority_t criticality)
{
    uint32_t i;
    partition_desc_t *part;

    if (name == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if ((period == 0U) || (duration == 0U) || (duration > period))
    {
        return -(int32_t)EINVAL;
    }

    /* 查找空闲分区槽 */
    for (i = 0U; i < ARINC_MAX_PARTITIONS; i++)
    {
        if (!s_partition_used[i])
        {
            break;
        }
    }

    if (i >= ARINC_MAX_PARTITIONS)
    {
        return -(int32_t)ENOMEM;
    }

    part = &s_partitions[i];

    safe_strcpy(part->name, name, ARINC_PARTITION_NAME_MAX);
    part->partition_id = i;
    part->state = PARTITION_STATE_COLD_START;
    part->criticality = criticality;
    part->period = period;
    part->duration = duration;
    part->time_used = 0ULL;
    part->time_remaining = (tick_t)duration;
    part->proc_count = 0U;
    INIT_LIST_HEAD(&part->proc_list);

    s_partition_used[i] = true;

    barrier();

    /* 将分区转为正常状态 */
    part->state = PARTITION_STATE_NORMAL;

    return (int32_t)i;
}

/* ========================================================================
 * 设置主时间框架
 * ======================================================================== */

kernel_status_t arinc653_set_schedule(const partition_window_t *windows,
                                       uint32_t window_count,
                                       tick_t frame_length)
{
    uint32_t i;
    tick_t offset = 0ULL;

    if (windows == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if ((window_count == 0U) || (window_count > ARINC_MAX_PARTITIONS))
    {
        return -(int32_t)EINVAL;
    }

    if (frame_length == 0ULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 验证窗口参数 */
    for (i = 0U; i < window_count; i++)
    {
        if (windows[i].partition_id >= ARINC_MAX_PARTITIONS)
        {
            return -(int32_t)EINVAL;
        }

        if (windows[i].duration == 0ULL)
        {
            return -(int32_t)EINVAL;
        }
    }

    /* 设置窗口 */
    for (i = 0U; i < window_count; i++)
    {
        s_major_frame.windows[i].partition_id = windows[i].partition_id;
        s_major_frame.windows[i].duration = windows[i].duration;
        s_major_frame.windows[i].start_offset = offset;
        offset += windows[i].duration;
    }

    s_major_frame.window_count = window_count;
    s_major_frame.major_frame_length = frame_length;
    s_major_frame.current_tick = 0ULL;
    s_major_frame.current_window_idx = 0U;
    s_major_frame.initialized = true;

    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 获取当前活跃分区
 * ======================================================================== */

partition_desc_t *arinc653_get_current_partition(void)
{
    return s_current_partition;
}

/* ========================================================================
 * 时钟滴答处理
 * ======================================================================== */

void arinc653_tick(void)
{
    uint32_t window_idx;
    partition_window_t *window;
    partition_desc_t *prev_part;
    partition_desc_t *next_part;

    if (!s_major_frame.initialized)
    {
        return;
    }

    /* 推进主框架时间 */
    s_major_frame.current_tick++;

    /* 检查是否需要切换窗口 */
    window_idx = s_major_frame.current_window_idx;
    window = &s_major_frame.windows[window_idx];

    if (s_major_frame.current_tick >= (window->start_offset + window->duration))
    {
        /* 当前窗口结束，冻结旧分区 */
        prev_part = s_current_partition;
        if (prev_part != NULL)
        {
            prev_part->time_remaining = 0ULL;
            prev_part->time_used += window->duration;
        }

        /* 切换到下一个窗口 */
        window_idx++;
        if (window_idx >= s_major_frame.window_count)
        {
            /* 主框架结束，重新开始 */
            window_idx = 0U;
            s_major_frame.current_tick = 0ULL;

            /* 重置所有分区的时间计数器 */
            uint32_t p;
            for (p = 0U; p < ARINC_MAX_PARTITIONS; p++)
            {
                if (s_partition_used[p])
                {
                    s_partitions[p].time_used = 0ULL;
                    s_partitions[p].time_remaining = (tick_t)s_partitions[p].duration;
                }
            }
        }

        s_major_frame.current_window_idx = window_idx;
        window = &s_major_frame.windows[window_idx];

        /* 激活新分区 */
        next_part = &s_partitions[window->partition_id];
        if (s_partition_used[window->partition_id] &&
            (next_part->state == PARTITION_STATE_NORMAL))
        {
            s_current_partition = next_part;
            next_part->time_remaining = (tick_t)window->duration;
        }
        else
        {
            s_current_partition = NULL;
        }

        /* 触发调度 */
        schedule();
    }
    else
    {
        /* 更新当前分区剩余时间 */
        if (s_current_partition != NULL)
        {
            if (s_current_partition->time_remaining > 0ULL)
            {
                s_current_partition->time_remaining--;
            }
            s_current_partition->time_used++;
        }
    }
}

/* ========================================================================
 * 选下一个任务
 * ======================================================================== */

KThread_t *arinc653_pick_next(void)
{
    partition_desc_t *part;
    struct list_head *first;
    KThread_t *next;

    part = s_current_partition;
    if (part == NULL)
    {
        return NULL;
    }

    if (list_empty(&part->proc_list) != 0)
    {
        return NULL;
    }

    /* 在分区内选择最高优先级线程 */
    first = part->proc_list.next;
    next = container_of(first, KThread_t, rq_list);

    return next;
}

/* ========================================================================
 * 绑定线程到分区
 * ======================================================================== */

kernel_status_t arinc653_bind_thread(thread_id_t tid, uint32_t partition_id)
{
    KThread_t *thread;
    partition_desc_t *part;

    if (tid >= (thread_id_t)CONFIG_MAX_THREADS)
    {
        return -(int32_t)ESRCH;
    }

    if (partition_id >= ARINC_MAX_PARTITIONS)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_partition_used[partition_id])
    {
        return -(int32_t)EINVAL;
    }

    thread = &g_scheduler.thread_table[tid];
    if (thread->state == KTHREAD_STATE_DEAD)
    {
        return -(int32_t)ESRCH;
    }

    part = &s_partitions[partition_id];

    /* 绑定 */
    s_thread_partition[tid] = partition_id;
    list_add_tail(&thread->rq_list, &part->proc_list);
    part->proc_count++;

    barrier();

    return KERNEL_OK;
}

/* ========================================================================
 * 获取分区描述符
 * ======================================================================== */

partition_desc_t *arinc653_get_partition(uint32_t partition_id)
{
    if (partition_id >= ARINC_MAX_PARTITIONS)
    {
        return NULL;
    }

    if (!s_partition_used[partition_id])
    {
        return NULL;
    }

    return &s_partitions[partition_id];
}

/* ========================================================================
 * 获取主时间框架
 * ======================================================================== */

const major_frame_t *arinc653_get_major_frame(void)
{
    if (!s_major_frame.initialized)
    {
        return NULL;
    }

    return &s_major_frame;
}
