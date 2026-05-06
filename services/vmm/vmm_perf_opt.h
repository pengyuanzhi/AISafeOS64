/**
 * @file    vmm_perf_opt.h
 * @brief   VMM 性能优化模块
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 2.0
 *
 * @details 本文件定义了 VMM 性能优化模块的数据结构和接口：
 *          - TLB 自适应刷新策略优化
 *          - 中断批量注入与优先级队列优化
 *          - MMIO LRU 缓存访问优化
 *          - vCPU 动态负载均衡与 CPU 亲和性调度优化
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_VMM_VMM_PERF_OPT_H
#define SERVICES_VMM_VMM_PERF_OPT_H

#include <kernel/types.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大 VM 数量 */
#define VMM_MAX_VMS                4U

/** @brief 最大 vCPU 数量 */
#define VMM_MAX_VCPUS_PER_VM      4U

/** @brief TLB 刷新延迟（毫秒） */
#define VMM_TLB_FLUSH_DELAY_MS     5U

/** @brief 中断注入超时（微秒） */
#define VMM_IRQ_INJECT_TIMEOUT_US 100U

/** @brief MMIO 访问缓存大小（可动态调整，默认值） */
#define VMM_MMIO_CACHE_SIZE        64U

/** @brief MMIO 缓存最大容量 */
#define VMM_MMIO_CACHE_MAX_SIZE    256U

/** @brief MMIO 缓存最小容量 */
#define VMM_MMIO_CACHE_MIN_SIZE    8U

/** @brief vCPU 调度时间片（毫秒） */
#define VMM_VCPU_TIMESLICE_MS      10U

/** @brief 最大 CPU 亲和性掩码位数 */
#define VMM_MAX_CPU_AFFINITY       8U

/** @brief 最大批量中断注入数量 */
#define VMM_IRQ_BATCH_MAX_SIZE     16U

/** @brief 中断优先级级别数 */
#define VMM_IRQ_PRIORITY_LEVELS    8U

/** @brief TLB 访问频率低阈值（每秒刷新次数） */
#define VMM_TLB_FREQ_LOW           10U

/** @brief TLB 访问频率高阈值（每秒刷新次数） */
#define VMM_TLB_FREQ_HIGH          100U

/** @brief TLB 内存压力低阈值（0-100） */
#define VMM_TLB_PRESSURE_LOW       30U

/** @brief TLB 内存压力高阈值（0-100） */
#define VMM_TLB_PRESSURE_HIGH      80U

/* ========================================================================
 * TLB 自适应刷新策略
 * ======================================================================== */

/**
 * @brief TLB 刷新策略
 */
typedef enum
{
    VMM_TLB_FLUSH_IMMEDIATE = 0U,      /**< @brief 立即刷新 */
    VMM_TLB_FLUSH_DEFERRED,            /**< @brief 延迟刷新 */
    VMM_TLB_FLUSH_BATCH,               /**< @brief 批量刷新 */
    VMM_TLB_FLUSH_ADAPTIVE,            /**< @brief 自适应刷新（根据频率/压力动态选择） */
    VMM_TLB_FLUSH_COUNT                /**< @brief 策略计数 */
} vmm_tlb_flush_strategy_t;

/**
 * @brief TLB 刷新请求
 */
typedef struct
{
    uint32_t vm_id;                     /**< @brief VM ID */
    uint32_t asid;                      /**< @brief ASID */
    uint64_t timestamp;                 /**< @brief 请求时间戳 */
    bool pending;                       /**< @brief 待处理标志 */
} vmm_tlb_flush_req_t;

/**
 * @brief TLB 刷新统计信息
 */
typedef struct
{
    uint64_t flush_count;               /**< @brief 总刷新次数 */
    uint64_t deferred_count;            /**< @brief 延迟刷新次数 */
    uint64_t batched_count;             /**< @brief 批量刷新次数 */
    uint64_t skipped_count;             /**< @brief 跳过刷新次数（自适应优化） */
    uint64_t total_latency_ns;          /**< @brief 总刷新延迟（纳秒） */
    uint64_t max_latency_ns;            /**< @brief 最大刷新延迟（纳秒） */
} vmm_tlb_flush_stats_t;

/**
 * @brief TLB 刷新队列
 */
typedef struct
{
    vmm_tlb_flush_req_t reqs[VMM_MAX_VCPUS_PER_VM * VMM_MAX_VMS]; /**< @brief 刷新请求数组 */
    uint32_t head;                      /**< @brief 队列头 */
    uint32_t tail;                      /**< @brief 队列尾 */
    uint32_t count;                     /**< @brief 队列计数 */
    vmm_tlb_flush_strategy_t strategy;  /**< @brief 刷新策略 */
    vmm_tlb_flush_stats_t stats;        /**< @brief 刷新统计 */
    uint64_t adaptive_interval_ns;      /**< @brief 自适应刷新间隔（纳秒） */
    uint32_t access_frequency;          /**< @brief 访问频率（每秒刷新次数） */
    uint32_t memory_pressure;           /**< @brief 内存压力（0-100） */
} vmm_tlb_flush_queue_t;

/* ========================================================================
 * 中断批量注入与优先级队列优化
 * ======================================================================== */

/**
 * @brief 中断注入方法
 */
typedef enum
{
    VMM_IRQ_INJECT_SVC = 0U,            /**< @brief SVC 系统调用 */
    VMM_IRQ_INJECT_ICC_SGI1R,           /**< @brief 直接写 ICC_SGI1R_EL1 */
    VMM_IRQ_INJECT_COUNT
} vmm_irq_inject_method_t;

/**
 * @brief 中断注入状态
 */
typedef struct
{
    uint32_t vm_id;                     /**< @brief VM ID */
    uint32_t vcpu_id;                   /**< @brief vCPU ID */
    uint32_t irq;                       /**< @brief 中断号 */
    uint32_t priority;                  /**< @brief 中断优先级（0最高，7最低） */
    uint64_t timestamp;                 /**< @brief 请求时间戳 */
    bool pending;                       /**< @brief 待处理标志 */
    bool completed;                     /**< @brief 完成标志 */
} vmm_irq_inject_state_t;

/**
 * @brief 批量中断注入请求
 */
typedef struct
{
    uint32_t count;                     /**< @brief 批量注入数量 */
    uint32_t vm_ids[VMM_IRQ_BATCH_MAX_SIZE];    /**< @brief VM ID 列表 */
    uint32_t vcpu_ids[VMM_IRQ_BATCH_MAX_SIZE];  /**< @brief vCPU ID 列表 */
    uint32_t irqs[VMM_IRQ_BATCH_MAX_SIZE];      /**< @brief 中断号列表 */
    uint32_t priorities[VMM_IRQ_BATCH_MAX_SIZE]; /**< @brief 优先级列表 */
} vmm_irq_batch_req_t;

/**
 * @brief 中断优先级队列
 */
typedef struct
{
    vmm_irq_inject_state_t states[VMM_IRQ_BATCH_MAX_SIZE * VMM_IRQ_PRIORITY_LEVELS]; /**< @brief 优先级队列条目 */
    uint32_t head;                      /**< @brief 队列头 */
    uint32_t tail;                      /**< @brief 队列尾 */
    uint32_t count;                     /**< @brief 队列计数 */
    vmm_irq_inject_method_t method;     /**< @brief 注入方法 */
    uint32_t priority_counts[VMM_IRQ_PRIORITY_LEVELS]; /**< @brief 各优先级待处理计数 */
} vmm_irq_inject_queue_t;

/* ========================================================================
 * MMIO LRU 缓存访问优化
 * ======================================================================== */

/**
 * @brief MMIO 缓存条目（带 LRU 计数器）
 */
typedef struct
{
    uint32_t vm_id;                     /**< @brief VM ID */
    uint32_t vcpu_id;                   /**< @brief vCPU ID */
    uint64_t addr;                      /**< @brief MMIO 地址 */
    uint64_t value;                     /**< @brief 缓存值 */
    uint64_t timestamp;                 /**< @brief 缓存时间戳 */
    uint64_t lru_counter;               /**< @brief LRU 计数器（越大越最近使用） */
    bool valid;                         /**< @brief 有效标志 */
    bool read;                          /**< @brief 读/写标志 */
} vmm_mmio_cache_entry_t;

/**
 * @brief MMIO 缓存统计信息
 */
typedef struct
{
    uint64_t hit_count;                 /**< @brief 缓存命中次数 */
    uint64_t miss_count;                /**< @brief 缓存未命中次数 */
    uint64_t eviction_count;            /**< @brief 驱逐次数 */
    uint64_t read_count;                /**< @brief 读访问次数 */
    uint64_t write_count;               /**< @brief 写访问次数 */
    uint64_t preheat_count;             /**< @brief 预热次数 */
} vmm_mmio_cache_stats_t;

/**
 * @brief MMIO 访问缓存（LRU 策略）
 */
typedef struct
{
    vmm_mmio_cache_entry_t entries[VMM_MMIO_CACHE_MAX_SIZE]; /**< @brief 缓存条目数组 */
    uint32_t capacity;                  /**< @brief 当前缓存容量 */
    uint32_t count;                     /**< @brief 缓存计数 */
    bool enabled;                       /**< @brief 使能标志 */
    uint64_t timeout_us;                /**< @brief 缓存超时（微秒） */
    uint64_t global_lru_counter;        /**< @brief 全局 LRU 计数器 */
    uint32_t lru_threshold;             /**< @brief LRU 驱逐阈值（访问次数低于此值可被驱逐） */
    vmm_mmio_cache_stats_t stats;       /**< @brief 缓存统计 */
} vmm_mmio_cache_t;

/* ========================================================================
 * vCPU 动态负载均衡与 CPU 亲和性调度优化
 * ======================================================================== */

/** @brief 实时调度策略 */
typedef enum
{
    VMM_SCHED_POLICY_NORMAL = 0U,       /**< @brief 普通调度 */
    VMM_SCHED_POLICY_FIFO,              /**< @brief 先进先出实时调度 */
    VMM_SCHED_POLICY_RR,                /**< @brief 轮转实时调度 */
    VMM_SCHED_POLICY_COUNT
} vmm_sched_policy_t;

/**
 * @brief vCPU 调度策略
 */
typedef enum
{
    VMM_VCPU_SCHED_ROUND_ROBIN = 0U,    /**< @brief 轮转调度 */
    VMM_VCPU_SCHED_PRIORITY,            /**< @brief 优先级调度 */
    VMM_VCPU_SCHED_LOAD_BALANCE,        /**< @brief 负载均衡 */
    VMM_VCPU_SCHED_DYNAMIC,             /**< @brief 动态负载均衡（实时监控+动态调整） */
    VMM_VCPU_SCHED_COUNT
} vmm_vcpu_sched_strategy_t;

/**
 * @brief vCPU 调度状态
 */
typedef struct
{
    uint32_t vm_id;                     /**< @brief VM ID */
    uint32_t vcpu_id;                   /**< @brief vCPU ID */
    uint64_t runtime_ns;                /**< @brief 运行时间（纳秒） */
    uint64_t yield_count;               /**< @brief 让出次数 */
    uint64_t load_score;                /**< @brief 负载分数 */
    uint8_t cpu_affinity;               /**< @brief CPU 亲和性掩码（每位对应一个物理 CPU） */
    uint8_t priority;                   /**< @brief 调度优先级 */
    vmm_sched_policy_t rt_policy;       /**< @brief 实时调度策略 */
    uint32_t rt_priority;               /**< @brief 实时调度优先级 */
    bool runnable;                      /**< @brief 可运行标志 */
} vmm_vcpu_sched_state_t;

/**
 * @brief vCPU 调度统计信息
 */
typedef struct
{
    uint64_t schedule_count;            /**< @brief 调度次数 */
    uint64_t context_switches;          /**< @brief 上下文切换次数 */
    uint64_t migration_count;           /**< @brief vCPU 迁移次数 */
    uint64_t steal_count;               /**< @brief 空间窃取次数 */
    uint64_t total_latency_ns;          /**< @brief 总调度延迟 */
    uint64_t max_latency_ns;            /**< @brief 最大调度延迟 */
} vmm_vcpu_sched_stats_t;

/**
 * @brief vCPU 调度器
 */
typedef struct
{
    vmm_vcpu_sched_state_t states[VMM_MAX_VCPUS_PER_VM * VMM_MAX_VMS]; /**< @brief 调度状态数组 */
    uint32_t current_idx;               /**< @brief 当前索引 */
    uint32_t count;                     /**< @brief 计数 */
    vmm_vcpu_sched_strategy_t strategy; /**< @brief 调度策略 */
    uint64_t timeslice_ns;              /**< @brief 时间片（纳秒） */
    bool dynamic_enabled;               /**< @brief 动态负载均衡使能 */
    uint64_t last_balance_ns;           /**< @brief 上次负载均衡时间 */
    uint64_t balance_interval_ns;       /**< @brief 负载均衡间隔（纳秒） */
    vmm_vcpu_sched_stats_t stats;       /**< @brief 调度统计 */
} vmm_vcpu_scheduler_t;

/* ========================================================================
 * 性能统计
 * ======================================================================== */

/**
 * @brief VMM 性能统计
 */
typedef struct
{
    /** @brief TLB 刷新统计 */
    uint64_t tlb_flush_count;           /**< @brief TLB 刷新次数 */
    uint64_t tlb_flush_deferred;        /**< @brief 延迟刷新次数 */
    uint64_t tlb_flush_batched;         /**< @brief 批量刷新次数 */
    uint64_t tlb_flush_skipped;         /**< @brief 自适应跳过刷新次数 */

    /** @brief 中断注入统计 */
    uint64_t irq_inject_count;          /**< @brief 中断注入次数 */
    uint64_t irq_inject_svc;            /**< @brief SVC 注入次数 */
    uint64_t irq_inject_icc_sgi1r;      /**< @brief ICC_SGI1R 注入次数 */
    uint64_t irq_inject_timeout;        /**< @brief 注入超时次数 */
    uint64_t irq_inject_batch;          /**< @brief 批量注入次数 */

    /** @brief MMIO 访问统计 */
    uint64_t mmio_read_count;           /**< @brief MMIO 读次数 */
    uint64_t mmio_write_count;          /**< @brief MMIO 写次数 */
    uint64_t mmio_cache_hits;           /**< @brief 缓存命中次数 */
    uint64_t mmio_cache_misses;         /**< @brief 缓存未命中次数 */
    uint64_t mmio_cache_evictions;      /**< @brief 缓存驱逐次数 */

    /** @brief vCPU 调度统计 */
    uint64_t vcpu_schedule_count;       /**< @brief vCPU 调度次数 */
    uint64_t vcpu_context_switches;     /**< @brief 上下文切换次数 */
    uint64_t vcpu_yield_count;          /**< @brief vCPU 让出次数 */
    uint64_t vcpu_migrations;           /**< @brief vCPU 迁移次数 */
} vmm_perf_stats_t;

/* ========================================================================
 * VMM 性能优化模块 API 接口
 * ======================================================================== */

/**
 * @brief 初始化 VMM 性能优化模块
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_perf_opt_global_init(void);

/**
 * @brief 销毁 VMM 性能优化模块
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_perf_opt_global_destroy(void);

/* ========================================================================
 * TLB 自适应刷新优化 API
 * ======================================================================== */

/**
 * @brief 设置 TLB 刷新策略
 *
 * @param strategy   刷新策略
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 策略无效
 */
kernel_status_t vmm_tlb_flush_set_strategy(vmm_tlb_flush_strategy_t strategy);

/**
 * @brief 请求 TLB 刷新（优化版本）
 *
 * @param vm_id      VM ID
 * @param asid       ASID (可选，0 表示刷新所有）
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL VM ID 无效
 */
kernel_status_t vmm_tlb_flush_opt(uint32_t vm_id, uint32_t asid);

/**
 * @brief 处理 TLB 刷新队列（批量刷新）
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_tlb_flush_process_queue(void);

/**
 * @brief 自适应 TLB 刷新
 *
 * @details 根据访问频率和内存压力自动选择最优刷新策略：
 *          - 低频率 + 低压力：延迟刷新（间隔更长）
 *          - 高频率 + 低压力：批量刷新
 *          - 低频率 + 高压力：立即刷新
 *          - 高频率 + 高压力：立即刷新（不跳过）
 *
 * @param opt_id    优化选项 ID（0=默认）
 * @param asid      ASID
 * @param frequency 每秒刷新请求频率
 * @param pressure  内存压力（0-100）
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 参数无效
 */
kernel_status_t vmm_tlb_flush_adaptive(uint32_t opt_id, uint32_t asid,
                                        uint32_t frequency, uint32_t pressure);

/**
 * @brief 获取 TLB 刷新统计信息
 *
 * @param stats     输出统计信息（不能为 NULL）
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL stats 为 NULL
 */
kernel_status_t vmm_tlb_flush_get_stats(vmm_tlb_flush_stats_t *stats);

/* ========================================================================
 * 中断批量注入与优先级队列优化 API
 * ======================================================================== */

/**
 * @brief 设置中断注入方法
 *
 * @param method     注入方法
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 方法无效
 */
kernel_status_t vmm_irq_inject_set_method(vmm_irq_inject_method_t method);

/**
 * @brief 中断注入（优化版本）
 *
 * @param vm_id      VM ID
 * @param vcpu_id    vCPU ID
 * @param irq        中断号
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 参数无效
 */
kernel_status_t vmm_irq_inject_opt(uint32_t vm_id, uint32_t vcpu_id,
                                     uint32_t irq);

/**
 * @brief 处理中断注入队列
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_irq_inject_process_queue(void);

/**
 * @brief 批量中断注入
 *
 * @details 一次性注入多个中断到多个 vCPU，减少逐个注入的开销。
 *          中断按优先级排序后批量写入 ICC_SGI1R_EL1。
 *
 * @param count         注入数量（不超过 VMM_IRQ_BATCH_MAX_SIZE）
 * @param vm_id_list    VM ID 列表（不能为 NULL）
 * @param vcpu_id_list  vCPU ID 列表（不能为 NULL）
 * @param irq_list      中断号列表（不能为 NULL）
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 参数无效或数量为 0
 */
kernel_status_t vmm_irq_inject_batch(uint32_t count,
                                      const uint32_t *vm_id_list,
                                      const uint32_t *vcpu_id_list,
                                      const uint32_t *irq_list);

/**
 * @brief 设置中断优先级
 *
 * @param irq       中断号
 * @param priority  优先级（0最高，7最低）
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 参数无效
 */
kernel_status_t vmm_irq_inject_set_priority(uint32_t irq, uint32_t priority);

/* ========================================================================
 * MMIO LRU 缓存访问优化 API
 * ======================================================================== */

/**
 * @brief 使能 MMIO 缓存
 *
 * @param enabled    使能标志
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_mmio_cache_enable(bool enabled);

/**
 * @brief MMIO 读（优化版本）
 *
 * @param vm_id      VM ID
 * @param vcpu_id    vCPU ID
 * @param addr       MMIO 地址
 * @param value      输出值
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL value 为 NULL
 */
kernel_status_t vmm_mmio_read_opt(uint32_t vm_id, uint32_t vcpu_id,
                                   uint64_t addr, uint64_t *value);

/**
 * @brief MMIO 写（优化版本）
 *
 * @param vm_id      VM ID
 * @param vcpu_id    vCPU ID
 * @param addr       MMIO 地址
 * @param value      写入值
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_mmio_write_opt(uint32_t vm_id, uint32_t vcpu_id,
                                    uint64_t addr, uint64_t value);

/**
 * @brief 清空 MMIO 缓存
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_mmio_cache_flush(void);

/**
 * @brief 设置 LRU 驱逐阈值
 *
 * @details 访问计数低于此阈值的条目优先被驱逐。
 *          默认值为 2。增大此值使 LRU 更激进。
 *
 * @param threshold 驱逐阈值（1-100）
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 阈值无效
 */
kernel_status_t vmm_mmio_cache_set_lru_threshold(uint32_t threshold);

/**
 * @brief 预热 MMIO 缓存
 *
 * @details 预先加载指定地址列表到缓存中，提升后续首次访问的命中率。
 *
 * @param address_list 地址列表（不能为 NULL）
 * @param count        地址数量
 * @param vm_id        VM ID
 * @param vcpu_id      vCPU ID
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 参数无效
 */
kernel_status_t vmm_mmio_cache_preheat(const uint64_t *address_list,
                                        uint32_t count,
                                        uint32_t vm_id, uint32_t vcpu_id);

/**
 * @brief 动态调整 MMIO 缓存大小
 *
 * @details 在运行时动态调整缓存容量，不影响已缓存的有效条目。
 *
 * @param size 新的缓存容量（VMM_MMIO_CACHE_MIN_SIZE ~ VMM_MMIO_CACHE_MAX_SIZE）
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 大小超出范围
 */
kernel_status_t vmm_mmio_cache_set_size(uint32_t size);

/**
 * @brief 获取 MMIO 缓存统计信息
 *
 * @param stats     输出统计信息（不能为 NULL）
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL stats 为 NULL
 */
kernel_status_t vmm_mmio_cache_get_stats(vmm_mmio_cache_stats_t *stats);

/* ========================================================================
 * vCPU 动态负载均衡与 CPU 亲和性调度优化 API
 * ======================================================================== */

/**
 * @brief 设置 vCPU 调度策略
 *
 * @param strategy   调度策略
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 策略无效
 */
kernel_status_t vmm_vcpu_sched_set_strategy(vmm_vcpu_sched_strategy_t strategy);

/**
 * @brief vCPU 调度（优化版本）
 *
 * @param vm_id      VM ID
 * @param vcpu_id    输出选中的 vCPU ID
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL vcpu_id 为 NULL
 * @retval -ENOENT VM 不存在
 * @retval -ENODATA 没有可运行的 vCPU
 */
kernel_status_t vmm_vcpu_schedule_opt(uint32_t vm_id, uint32_t *vcpu_id);

/**
 * @brief 更新 vCPU 调度状态
 *
 * @param vm_id      VM ID
 * @param vcpu_id    vCPU ID
 * @param runtime_ns 运行时间（纳秒）
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 参数无效
 */
kernel_status_t vmm_vcpu_sched_update(uint32_t vm_id, uint32_t vcpu_id,
                                      uint64_t runtime_ns);

/**
 * @brief 启用/禁用动态负载均衡
 *
 * @details 启用后，调度器会实时监控各 vCPU 负载，
 *          并在负载不均衡时自动迁移 vCPU。
 *
 * @param enable    true=启用，false=禁用
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_vcpu_sched_enable_dynamic(bool enable);

/**
 * @brief 设置 vCPU 的 CPU 亲和性
 *
 * @details 限制 vCPU 只在指定的物理 CPU 上运行，
 *          减少缓存失效和跨核迁移开销。
 *
 * @param vcpu_id   vCPU ID
 * @param cpu_mask  CPU 亲和性掩码（每位对应一个物理 CPU）
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 参数无效
 */
kernel_status_t vmm_vcpu_sched_set_affinity(uint32_t vcpu_id, uint8_t cpu_mask);

/**
 * @brief 设置实时调度策略和优先级
 *
 * @details 为 vCPU 设置实时调度策略（SCHED_FIFO 或 SCHED_RR），
 *          确保高优先级任务获得确定的 CPU 时间。
 *
 * @param policy    实时调度策略
 * @param priority  实时优先级（0-99）
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 参数无效
 */
kernel_status_t vmm_vcpu_sched_set_realtime_policy(vmm_sched_policy_t policy,
                                                     uint32_t priority);

/**
 * @brief 设置 vCPU 调度时间片
 *
 * @param ms 时间片（毫秒，1-1000）
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 时间片无效
 */
kernel_status_t vmm_vcpu_sched_set_timeslice(uint32_t ms);

/**
 * @brief 获取 vCPU 调度统计信息
 *
 * @param vm_id     VM ID
 * @param stats     输出统计信息（不能为 NULL）
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL stats 为 NULL
 */
kernel_status_t vmm_vcpu_sched_get_stats(uint32_t vm_id,
                                           vmm_vcpu_sched_stats_t *stats);

/* ========================================================================
 * 性能统计 API
 * ======================================================================== */

/**
 * @brief 获取性能统计信息
 *
 * @param stats      输出统计信息
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL stats 为 NULL
 */
kernel_status_t vmm_perf_get_stats(vmm_perf_stats_t *stats);

/**
 * @brief 重置性能统计信息
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_perf_reset_stats(void);

#endif /* SERVICES_VMM_VMM_PERF_OPT_H */
