/**
 * @file    arinc653.h
 * @brief   ARINC 653 分区调度器接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了 ARINC 653 分区调度策略的接口：
 *          - 分区（Partition）概念：时间+空间隔离
 *          - 固定时间窗口（Major Frame / Minor Frame）
 *          - 分区内使用优先级调度
 *          - 符合 ARINC 653 标准的时间分区
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SC-003, SC-007~008
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_SCHED_ARINC653_H
#define KERNEL_SCHED_ARINC653_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/list.h>
#include <stdint.h>
#include <stdbool.h>
#include "thread.h"

/* ========================================================================
 * ARINC 653 常量
 * ======================================================================== */

/** @brief 最大分区数 */
#define ARINC_MAX_PARTITIONS       8U

/** @brief 分区名最大长度 */
#define ARINC_PARTITION_NAME_MAX   16U

/** @brief 每分区最大进程数 */
#define ARINC_MAX_PROCS_PER_PART   32U

/** @brief 主时间框架默认长度（ticks） */
#define ARINC_DEFAULT_MAJOR_FRAME  1000U

/* ========================================================================
 * 分区状态
 * ======================================================================== */

/**
 * @brief 分区状态枚举
 */
typedef enum
{
    PARTITION_STATE_IDLE = 0U,    /**< @brief 空闲（未激活） */
    PARTITION_STATE_COLD_START,   /**< @brief 冷启动 */
    PARTITION_STATE_WARM_START,   /**< @brief 热启动 */
    PARTITION_STATE_NORMAL,       /**< @brief 正常运行 */
    PARTITION_STATE_STOPPED,      /**< @brief 已停止 */
    PARTITION_STATE_FAILED        /**< @brief 故障 */
} partition_state_t;

/* ========================================================================
 * 分区时间窗口描述符
 * ======================================================================== */

/**
 * @brief 分区时间窗口
 *
 * @details 定义主时间框架（Major Frame）中各分区的执行时间片分配
 */
typedef struct
{
    uint32_t partition_id;   /**< @brief 分区 ID */
    tick_t   duration;       /**< @brief 时间窗口长度（ticks） */
    tick_t   start_offset;   /**< @brief 在主框架中的起始偏移 */
} partition_window_t;

/* ========================================================================
 * 分区描述符
 * ======================================================================== */

/**
 * @brief ARINC 653 分区描述符
 *
 * @details 包含分区标识、状态、调度信息和进程列表
 */
typedef struct
{
    uint32_t    partition_id;                     /**< @brief 分区 ID */
    char        name[ARINC_PARTITION_NAME_MAX];   /**< @brief 分区名称 */
    partition_state_t state;                      /**< @brief 分区状态 */
    priority_t  criticality;                      /**< @brief 关键性等级 */
    uint32_t    period;                           /**< @brief 分区周期（ticks） */
    uint32_t    duration;                         /**< @brief 分配的执行时间（ticks） */
    tick_t      time_used;                        /**< @brief 本周期已用时间 */
    tick_t      time_remaining;                   /**< @brief 本周期剩余时间 */
    struct list_head proc_list;                   /**< @brief 分区内进程链表 */
    uint32_t    proc_count;                       /**< @brief 进程数量 */
    struct list_head rq_list;                     /**< @brief 调度器链表节点 */
} partition_desc_t;

/* ========================================================================
 * 主时间框架（Major Frame）
 * ======================================================================== */

/**
 * @brief 主时间框架
 *
 * @details ARINC 653 调度的核心数据结构：
 *          - 定义一个循环的时间窗口序列
 *          - 每个窗口分配给特定分区
 *          - 主框架循环执行
 */
typedef struct
{
    partition_window_t windows[ARINC_MAX_PARTITIONS]; /**< @brief 时间窗口数组 */
    uint32_t window_count;                            /**< @brief 窗口总数 */
    tick_t   major_frame_length;                      /**< @brief 主框架总长度（ticks） */
    tick_t   current_tick;                            /**< @brief 当前在主框架中的位置 */
    uint32_t current_window_idx;                      /**< @brief 当前窗口索引 */
    uint32_t lock;                                    /**< @brief 框架锁 */
    bool     initialized;                             /**< @brief 初始化标志 */
} major_frame_t;

/* ========================================================================
 * ARINC 653 调度器 API
 * ======================================================================== */

/**
 * @brief 初始化 ARINC 653 分区调度器
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t arinc653_init(void);

/**
 * @brief 创建分区
 *
 * @param name       分区名称
 * @param period     分区周期（ticks）
 * @param duration   分配的执行时间（ticks）
 * @param criticality 关键性等级（0-255）
 *
 * @return 成功返回分区 ID，失败返回负错误码
 */
int32_t arinc653_create_partition(const char *name,
                                  uint32_t period,
                                  uint32_t duration,
                                  priority_t criticality);

/**
 * @brief 设置主时间框架
 *
 * @param windows      时间窗口数组
 * @param window_count 窗口数量
 * @param frame_length 主框架总长度
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t arinc653_set_schedule(const partition_window_t *windows,
                                       uint32_t window_count,
                                       tick_t frame_length);

/**
 * @brief 获取当前活跃分区
 *
 * @return 当前活跃分区描述符指针，无活跃分区返回 NULL
 */
partition_desc_t *arinc653_get_current_partition(void);

/**
 * @brief ARINC 653 时钟滴答处理
 *
 * @details 推进时间框架，切换分区，
 *          处理分区内优先级调度
 */
void arinc653_tick(void);

/**
 * @brief ARINC 653 选下一个任务
 *
 * @details 先确定当前分区，再在分区内选最高优先级进程
 *
 * @return 下一个要运行的线程
 */
KThread_t *arinc653_pick_next(void);

/**
 * @brief 将线程绑定到分区
 *
 * @param tid          线程 ID
 * @param partition_id 分区 ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t arinc653_bind_thread(thread_id_t tid, uint32_t partition_id);

/**
 * @brief 获取分区描述符
 *
 * @param partition_id 分区 ID
 *
 * @return 分区描述符指针，不存在返回 NULL
 */
partition_desc_t *arinc653_get_partition(uint32_t partition_id);

/**
 * @brief 获取主时间框架信息
 *
 * @return 主时间框架指针
 */
const major_frame_t *arinc653_get_major_frame(void);

#endif /* KERNEL_SCHED_ARINC653_H */
